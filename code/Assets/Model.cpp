#include "Model.h"
#include "AssetManager.h"

#define CGLTF_IMPLEMENTATION
#define CGLTF_MALLOC(size) Mem::Alloc(size);
#define CGLTF_FREE(ptr) Mem::Free(ptr)
#if CONFIG_ENABLE_ASSERT
    #define CGLTF_VALIDATE_ENABLE_ASSERTS 1
#endif
#include "../External/cgltf/cgltf.h"

#include "../Core/Allocators.h"
#include "../Core/System.h"
#include "../Core/Jobs.h"
#include "../Core/Log.h"
#include "../Core/TracyHelper.h"
#include "../Core/Hash.h"
#include "../Core/MathAll.h"

#include "../Common/VirtualFS.h"
#include "../Common/RemoteServices.h"

#include "../Tool/MeshOptimizer.h"

constexpr uint32 MODEL_ASSET_TYPE = MakeFourCC('M', 'O', 'D', 'L');
constexpr uint32 RCMD_LOAD_MODEL = MakeFourCC('M', 'O', 'D', 'L');

struct ModelVertexAttribute
{
    const char* semantic;
    uint32 index;
};

struct ModelLoadRequest 
{
    AssetHandle handle;
    AssetLoaderAsyncCallback loadCallback;
    void* loadCallbackUserData;
    ModelLoadParams loadParams;
    AssetLoadParams params;
};

struct ModelLoader final : AssetCallbacks 
{
    AssetResult Load(AssetHandle handle, const AssetLoadParams& params, uint32 cacheHash, MemAllocator* dependsAlloc) override;
    void LoadRemote(AssetHandle handle, const AssetLoadParams& params, uint32 cacheHash, void* userData, AssetLoaderAsyncCallback loadCallback) override;
    bool InitializeSystemResources(void* obj, const AssetLoadParams& params) override;
    void Release(void* data, MemAllocator* alloc) override;
    bool ReloadSync(AssetHandle handle, void* prevData) override;
};

struct ModelContext 
{
    Mutex requestsMutex;
    ModelGeometryLayout defaultLayout;
    Array<ModelLoadRequest> requests;
};

static ModelContext gModelCtx;
static ModelLoader gModelLoader;

enum GLTF_Filter 
{
    GLTF_FILTER_NEAREST = 9728,
    GLTF_FILTER_LINEAR = 9729,
    GLTF_FILTER_NEAREST_MIPMAP_NEAREST = 9984,
    GLTF_FILTER_LINEAR_MIPMAP_NEAREST = 9985,
    GLTF_FILTER_NEAREST_MIPMAP_LINEAR = 9986,
    GLTF_FILTER_LINEAR_MIPMAP_LINEAR = 9987
};

enum GLTF_Wrap
{
    GLTF_WRAP_CLAMP_TO_EDGE = 33071,
    GLTF_WRAP_MIRRORED_REPEAT = 33648,
    GLTF_WRAP_REPEAT = 10497
};

INLINE GfxSamplerFilterMode modelGltfGetFilter(GLTF_Filter filter)
{
    switch (filter) {
    case GLTF_FILTER_NEAREST:                   return GfxSamplerFilterMode::Nearest;
    case GLTF_FILTER_LINEAR:                    return GfxSamplerFilterMode::Linear;
    case GLTF_FILTER_NEAREST_MIPMAP_NEAREST:    return GfxSamplerFilterMode::NearestMipmapNearest;
    case GLTF_FILTER_LINEAR_MIPMAP_NEAREST:     return GfxSamplerFilterMode::LinearMipmapNearest;
    case GLTF_FILTER_NEAREST_MIPMAP_LINEAR:     return GfxSamplerFilterMode::NearestMipmapLinear;
    case GLTF_FILTER_LINEAR_MIPMAP_LINEAR:      return GfxSamplerFilterMode::LinearMipmapLinear;
    default:                                    return GfxSamplerFilterMode::Default;
    }
}

INLINE GfxSamplerWrapMode modelGltfGetWrap(GLTF_Wrap wrap)
{
    switch (wrap) {
    case GLTF_WRAP_CLAMP_TO_EDGE:   return GfxSamplerWrapMode::ClampToEdge;
    case GLTF_WRAP_MIRRORED_REPEAT: return GfxSamplerWrapMode::MirroredRepeat;
    case GLTF_WRAP_REPEAT:          return GfxSamplerWrapMode::Repeat;
    default:                        return GfxSamplerWrapMode::Default;
    }
}

// Returns the hash of the material data
static ModelMaterial* modelCreateMaterial(uint32* outNumTextures, uint32* outHash, cgltf_material* gltfMtl, const char* fileDir, MemAllocator* alloc)
{
    ASSERT(gltfMtl);

    auto LoadTextureFromGltf = [alloc](cgltf_texture* gltfTexture, ModelMaterialTexture* tex, const char* fileDir, HashMurmur32Incremental& hasher)
    {
        ASSERT(gltfTexture);
        char texturePath[PATH_CHARS_MAX];
        {
            char* dir = strCopy(texturePath, sizeof(texturePath), fileDir);
            if (*(dir - 1) != '/') {
                dir[0] = '/';
                dir[1] = '\0';
                ++dir;
            }
            strConcat(dir, sizeof(texturePath), gltfTexture->image->uri);
        }


        ImageLoadParams tparams;
        if (gltfTexture->sampler) {
            ASSERT(gltfTexture->sampler->wrap_s == gltfTexture->sampler->wrap_t);
            tparams.samplerFilter = modelGltfGetFilter((GLTF_Filter)gltfTexture->sampler->min_filter);
            tparams.samplerWrap = modelGltfGetWrap((GLTF_Wrap)gltfTexture->sampler->wrap_s);
        }

        uint32 texturePathLen = strLen(texturePath);
        tex->texturePath = Mem::AllocCopy<char>(texturePath, texturePathLen+1, alloc);
        tex->params = tparams;

        hasher.Add(texturePath, texturePathLen);
        hasher.Add<ImageLoadParams>(&tparams);
    };

    ModelMaterialAlphaMode alphaMode;
    switch (gltfMtl->alpha_mode) {
    case cgltf_alpha_mode_opaque:      alphaMode = ModelMaterialAlphaMode::Opaque;              break;
    case cgltf_alpha_mode_mask:        alphaMode = ModelMaterialAlphaMode::Mask;                break;
    case cgltf_alpha_mode_blend:       alphaMode = ModelMaterialAlphaMode::Blend;               break;
    default:                           ASSERT(0); alphaMode = ModelMaterialAlphaMode::Opaque;   break;
    }

    ModelMaterial* mtl = Mem::AllocTyped<ModelMaterial>(1, alloc);
    *mtl = ModelMaterial {
        .hasMetalRoughness = (bool)gltfMtl->has_pbr_metallic_roughness,
        .hasSpecularGlossiness = (bool)gltfMtl->has_pbr_specular_glossiness,
        .hasClearcoat = (bool)gltfMtl->has_clearcoat,
        .pbrMetallicRoughness = {
            .baseColorFactor = Float4 {
                gltfMtl->pbr_metallic_roughness.base_color_factor[0],
                gltfMtl->pbr_metallic_roughness.base_color_factor[1],
                gltfMtl->pbr_metallic_roughness.base_color_factor[2],
                gltfMtl->pbr_metallic_roughness.base_color_factor[3]
            },
            .metallicFactor = gltfMtl->pbr_metallic_roughness.metallic_factor,
            .roughnessFactor = gltfMtl->pbr_metallic_roughness.roughness_factor
        },
        .pbrSpecularGlossiness = {
            .diffuseFactor = Float4 {
                gltfMtl->pbr_specular_glossiness.diffuse_factor[0],
                gltfMtl->pbr_specular_glossiness.diffuse_factor[1],
                gltfMtl->pbr_specular_glossiness.diffuse_factor[2],
                gltfMtl->pbr_specular_glossiness.diffuse_factor[3]
            },
            .specularFactor = Float3 { 
                gltfMtl->pbr_specular_glossiness.specular_factor[0],
                gltfMtl->pbr_specular_glossiness.specular_factor[1],
                gltfMtl->pbr_specular_glossiness.specular_factor[2]
            },
            .glossinessFactor = gltfMtl->pbr_specular_glossiness.glossiness_factor,
        },
        .clearcoat = {
            .clearcoatFactor = gltfMtl->clearcoat.clearcoat_factor,
            .clearcoatRoughnessFactor = gltfMtl->clearcoat.clearcoat_roughness_factor
        },
        .emissiveFactor = Float3 {
            gltfMtl->emissive_factor[0],
            gltfMtl->emissive_factor[1],
            gltfMtl->emissive_factor[2]
        },
        .alphaMode = alphaMode,
        .alphaCutoff = gltfMtl->alpha_cutoff,
        .doubleSided = (bool)gltfMtl->double_sided,
        .unlit = (bool)gltfMtl->unlit
    };

    HashMurmur32Incremental hasher(0x669);
    hasher.Add<ModelMaterial>(mtl);

    uint32 numTextures = 0;
    if (gltfMtl->has_pbr_metallic_roughness) {
        cgltf_texture* tex = gltfMtl->pbr_metallic_roughness.base_color_texture.texture;
        if (tex) {
            LoadTextureFromGltf(tex, &mtl->pbrMetallicRoughness.baseColorTex, fileDir, hasher);
            ++numTextures;
        }

        tex = gltfMtl->pbr_metallic_roughness.metallic_roughness_texture.texture;
        if (tex) {
            LoadTextureFromGltf(tex, &mtl->pbrMetallicRoughness.metallicRoughnessTex, fileDir, hasher);
            ++numTextures;
        }

        tex = gltfMtl->normal_texture.texture;
        if (tex) {
            LoadTextureFromGltf(tex, &mtl->normalTexture, fileDir, hasher);
            ++numTextures;
        }

        tex = gltfMtl->occlusion_texture.texture;
        if (tex) {
            LoadTextureFromGltf(tex, &mtl->occlusionTexture, fileDir, hasher);
            ++numTextures;
        }
    }

    *outNumTextures = numTextures;
    *outHash = hasher.Hash();
    return mtl;
}   


static ModelVertexAttribute modelConvertVertexAttribute(cgltf_attribute_type type, uint32 index)
{
    if (type == cgltf_attribute_type_position && index == 0)  {
        return ModelVertexAttribute { .semantic = "POSITION", .index = 0 };
    } 
    else if (type == cgltf_attribute_type_normal && index == 0) {
        return ModelVertexAttribute { .semantic = "NORMAL", .index = 0 };
    } 
    else if (type == cgltf_attribute_type_tangent && index == 0) {
        return ModelVertexAttribute { .semantic = "TANGENT", .index = 0 };
    } 
    else if (type == cgltf_attribute_type_texcoord) {
        switch (index) {
        case 0:     return ModelVertexAttribute { .semantic = "TEXCOORD", .index = 0 };
        case 1:     return ModelVertexAttribute { .semantic = "TEXCOORD", .index = 1 };
        case 2:     return ModelVertexAttribute { .semantic = "TEXCOORD", .index = 2 };
        case 3:     return ModelVertexAttribute { .semantic = "TEXCOORD", .index = 3 };
        default:    return ModelVertexAttribute {};
        }
    } 
    else if (type == cgltf_attribute_type_color) {
        switch (index) {
        case 0:     return ModelVertexAttribute { .semantic = "COLOR", .index = 0 };
        case 1:     return ModelVertexAttribute { .semantic = "COLOR", .index = 0 };
        case 2:     return ModelVertexAttribute { .semantic = "COLOR", .index = 0 };
        case 3:     return ModelVertexAttribute { .semantic = "COLOR", .index = 0 };
        default:    return ModelVertexAttribute {};
        }
    } 
    else if (type == cgltf_attribute_type_joints && index == 0) {
        return ModelVertexAttribute { .semantic = "BLENDINDICES", .index = 0 };
    } 
    else if (type == cgltf_attribute_type_weights && index == 0) {
        return ModelVertexAttribute { .semantic = "BLENDWEIGHT", .index = 0 };
    } 
    else {
        return ModelVertexAttribute {};
    }
}

static uint32 modelGetVertexStride(GfxFormat fmt)
{
    switch (fmt) {
    case GfxFormat::R32_SFLOAT:
        return sizeof(float);
    case GfxFormat::R32G32_SFLOAT:
        return sizeof(float)*2;
    case GfxFormat::R32G32B32_SFLOAT:
        return sizeof(float)*3;
    case GfxFormat::R32G32B32A32_SFLOAT:
        return sizeof(float)*4;
    case GfxFormat::R8G8B8A8_SINT:
    case GfxFormat::R8G8B8A8_SNORM:
    case GfxFormat::R8G8B8A8_UINT:
    case GfxFormat::R8G8B8A8_UNORM:
        return sizeof(uint8)*4;
    case GfxFormat::R16G16_SINT: 
    case GfxFormat::R16G16_UNORM:
    case GfxFormat::R16G16_SNORM:
    case GfxFormat::R16G16_UINT:
        return sizeof(uint16)*2;
    case GfxFormat::R16G16B16A16_SNORM:
    case GfxFormat::R16G16B16A16_UNORM:
    case GfxFormat::R16G16B16A16_SINT:
    case GfxFormat::R16G16B16A16_UINT:
          return sizeof(uint16)*4;
    default:
        return 0;
    }
}

static bool modelMapVertexAttributesToBuffer(ModelMesh* mesh, const ModelGeometryLayout& vertexLayout, 
                                            cgltf_attribute* srcAttribute, uint32 startVertex)
{
    cgltf_accessor* access = srcAttribute->data;
    const GfxVertexInputAttributeDesc* attr = &vertexLayout.vertexAttributes[0];
    ModelVertexAttribute mappedAttribute = modelConvertVertexAttribute(srcAttribute->type, srcAttribute->index);
    while (!attr->semantic.IsEmpty()) {
        if (attr->semantic == mappedAttribute.semantic && attr->semanticIdx == mappedAttribute.index) {
            uint32 vertexStride = vertexLayout.vertexBufferStrides[attr->binding];
            uint8* srcBuffer = (uint8*)access->buffer_view->buffer->data;
            uint8* dstBuffer = mesh->cpuBuffers.vertexBuffers[attr->binding].Get();
            uint32 dstOffset =  startVertex * vertexStride + attr->offset;
            uint32 srcOffset = static_cast<uint32>(access->offset + access->buffer_view->offset);

            uint32 count = static_cast<uint32>(access->count);
            uint32 srcDataSize = static_cast<uint32>(access->stride); 
            uint32 dstDataSize = modelGetVertexStride(attr->format);
            ASSERT_MSG(dstDataSize != 0, "you must explicitly declare formats for vertex_layout attributes");
            uint32 stride = Min<uint32>(dstDataSize, srcDataSize);
            for (uint32 i = 0; i < count; i++)
                memcpy(dstBuffer + dstOffset + vertexStride*i, srcBuffer + srcOffset + srcDataSize*i, stride);

            return true;
        }
        ++attr;
    }

    return false;
}

static bool modelLayoutHasTangents(const ModelGeometryLayout& vertexLayout)
{
    const GfxVertexInputAttributeDesc* attr = &vertexLayout.vertexAttributes[0];
    while (!attr->semantic.IsEmpty()) {
        if (attr->semantic == "TANGENT")
            return true;
        ++attr;
    }
    return false;
}

static bool modelHasTangents(const cgltf_primitive* prim)
{
    for (cgltf_size i = 0; i < prim->attributes_count; i++) {
        if (prim->attributes[i].type == cgltf_attribute_type_tangent)
            return true;
    }

    return false;
}

static uint8* modelGetVertexAttributePointer(ModelMesh* mesh, const ModelGeometryLayout& vertexLayout, const char* semantic, 
                                             uint32 semanticIdx, uint32* outVertexStride)
{
    const GfxVertexInputAttributeDesc* attr = &vertexLayout.vertexAttributes[0];
    
    while (!attr->semantic.IsEmpty()) {
        if (attr->semantic == semantic && attr->semanticIdx == semanticIdx) {
            *outVertexStride = vertexLayout.vertexBufferStrides[attr->binding];
            uint8* dstBuff = mesh->cpuBuffers.vertexBuffers[attr->binding].Get();
            return dstBuff + attr->offset;
        }
        ++attr;
    }

    return nullptr;
}

static void modelCalculateTangents(ModelMesh* mesh, const ModelGeometryLayout& vertexLayout)
{
    uint32* indexBuffer = mesh->cpuBuffers.indexBuffer.Get();

    MemTempAllocator tmpAlloc;
    
    Float3* tan1 = tmpAlloc.MallocZeroTyped<Float3>(mesh->numVertices);
    Float3* tan2 = tmpAlloc.MallocZeroTyped<Float3>(mesh->numVertices);

    for (uint32 i = 0; i < mesh->numIndices; i+=3) {
        uint32 i1 = indexBuffer[i];
        uint32 i2 = indexBuffer[i+1];
        uint32 i3 = indexBuffer[i+2];

        uint32 posStride = 0, uvStride = 0;
        uint8* posPtr = modelGetVertexAttributePointer(mesh, vertexLayout, "POSITION", 0, &posStride);
        uint8* uvPtr = modelGetVertexAttributePointer(mesh, vertexLayout, "TEXCOORD", 0, &uvStride);

        Float3 v1 = *((Float3*)(posPtr + posStride*i1));
        Float3 v2 = *((Float3*)(posPtr + posStride*i2));
        Float3 v3 = *((Float3*)(posPtr + posStride*i3));

        Float2 w1 = *((Float2*)(uvPtr + uvStride*i1));
        Float2 w2 = *((Float2*)(uvPtr + uvStride*i2));
        Float2 w3 = *((Float2*)(uvPtr + uvStride*i3));

        float x1 = v2.x - v1.x;
        float x2 = v3.x - v1.x;
        float y1 = v2.y - v1.y;
        float y2 = v3.y - v1.y;
        float z1 = v2.z - v1.z;
        float z2 = v3.z - v1.z;

        float s1 = w2.x - w1.x;
        float s2 = w3.x - w1.x;
        float t1 = w2.y - w1.y;
        float t2 = w3.y - w1.y;

        float r = 1.0f / (s1 * t2 - s2 * t1);
        if (!mathIsINF(r)) {
            Float3 sdir = Float3((t2 * x1 - t1 * x2)*r, (t2 * y1 - t1 * y2)*r, (t2 * z1 - t1 * z2)*r);
            Float3 tdir = Float3((s1 * x2 - s2 * x1)*r, (s1 * y2 - s2 * y1)*r, (s1 * z2 - s2 * z1)*r);

            tan1[i1] = tan1[i1] + sdir;
            tan1[i2] = tan1[i2] + sdir;
            tan1[i3] = tan1[i3] + sdir;
            tan2[i1] = tan2[i1] + tdir;
            tan2[i2] = tan2[i2] + tdir;
            tan2[i3] = tan2[i3] + tdir;
        }
    }

    for (uint32 i = 0; i < mesh->numVertices; i++) {
        uint32 normalStride = 0, tangentStride = 0, bitangentStride = 0;
        uint8* normalPtr = modelGetVertexAttributePointer(mesh, vertexLayout, "NORMAL", 0, &normalStride);
        uint8* tangentPtr = modelGetVertexAttributePointer(mesh, vertexLayout, "TANGENT", 0, &tangentStride);
        uint8* bitangentPtr = modelGetVertexAttributePointer(mesh, vertexLayout, "BINORMAL", 0, &bitangentStride);

        Float3 n = *((Float3*)(normalPtr + normalStride*i));
        Float3 t = tan1[i];
    
        if (float3Dot(t, t) != 0) {
            Float3 tangent = float3Norm(t - n*float3Dot(n, t));
            *((Float3*)(tangentPtr + tangentStride*i)) = tangent;
    
            // (Dot(Cross(n, t), tan2[a]) < 0.0F) ? -1.0F : 1.0F;
            float handedness = (float3Dot(float3Cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;

            *((Float3*)(bitangentPtr + bitangentStride*i)) = float3Cross(n, tangent) * -handedness;
        }
    }
}

static void modelSetupBuffers(ModelMesh* mesh, const ModelGeometryLayout& vertexLayout, cgltf_mesh* srcMesh)
{
    // create buffers based on input vertexLayout

    // map source vertex buffer to our data
    // map source index buffer to our data
    uint32 startIndex = 0;
    uint32 startVertex = 0;
    bool calcTangents = false;
    bool layoutHasTangents = modelLayoutHasTangents(vertexLayout);

    for (uint32 i = 0; i < (uint32)srcMesh->primitives_count; i++) {
        cgltf_primitive* srcPrim = &srcMesh->primitives[i];

        // vertices
        // go through gltf vertex attributes and find them in the vertex layout, then we can map the data to the buffers
        uint32 count = 0;
        for (cgltf_size k = 0; k < srcPrim->attributes_count; k++) {
            cgltf_attribute* srcAtt = &srcPrim->attributes[k];
            modelMapVertexAttributesToBuffer(mesh, vertexLayout, srcAtt, startVertex);
            if (count == 0) {
                count = (uint32)srcAtt->data->count;
            }
            ASSERT(count == (uint32)srcAtt->data->count);
        }

        // in some instances, we may need tangents in the layout, but they wouldn't be present in
        // the gltf data so in that case, we have to calculate them manually
        if (layoutHasTangents && !modelHasTangents(srcPrim))
            calcTangents = true;

        // indices
        cgltf_accessor* srcIndices = srcPrim->indices;
        if (srcIndices->component_type == cgltf_component_type_r_16u) {
            uint32* indices = mesh->cpuBuffers.indexBuffer.Get() + startIndex;
            uint16* srcIndicesOffseted = reinterpret_cast<uint16*>((uint8*)srcIndices->buffer_view->buffer->data + srcIndices->buffer_view->offset);
            for (cgltf_size k = 0; k < srcIndices->count; k++)
                indices[k] = uint32(srcIndicesOffseted[k] + startVertex);

            // flip the winding
            #if 0
            for (uint32 k = 0, numTris = (uint32)srcIndices->count/3; k < numTris; k++) {
                uint32 ii = k*3;
                Swap<uint32>(indices[ii], indices[ii+2]);
            }
            #endif
        } 
        else if (srcIndices->component_type == cgltf_component_type_r_32u) {
            uint32* indices = mesh->cpuBuffers.indexBuffer.Get() + startIndex;
            uint32* srcIndicesOffseted = reinterpret_cast<uint32*>((uint8*)srcIndices->buffer_view->buffer->data + srcIndices->buffer_view->offset);
            for (cgltf_size k = 0; k < srcIndices->count; k++)
                indices[k] = srcIndicesOffseted[k] + (uint32)startVertex;

            // flip the winding
            #if 0
            for (uint32 k = 0, numTris = (uint32)srcIndices->count/3; k < numTris; k++) {
                int ii = k*3;
                Swap<uint32>(indices[ii], indices[ii+2]);
            }
            #endif
        }

        ModelSubmesh* submesh = &mesh->submeshes[i];
        submesh->startIndex = startIndex;
        submesh->numIndices = (uint32)srcPrim->indices->count;
        startIndex += (uint32)srcPrim->indices->count;
        startVertex += count;
    }

    if (calcTangents)
        modelCalculateTangents(mesh, vertexLayout);
}

static bool modelSetupGpuBuffers(Model* model, GfxBufferUsage vbuffUsage, GfxBufferUsage ibuffUsage) 
{
    ModelGeometryLayout* layout = &model->layout;
    for (uint32 i = 0; i < model->numMeshes; i++) {
        ModelMesh* mesh = &model->meshes[i];

        if (vbuffUsage != GfxBufferUsage::Default) {
            int bufferIndex = 0;
            while (layout->vertexBufferStrides[bufferIndex]) {
                mesh->gpuBuffers.vertexBuffers[bufferIndex] = gfxCreateBuffer(GfxBufferDesc {
                    .size = layout->vertexBufferStrides[bufferIndex]*mesh->numVertices,
                    .type = GfxBufferType::Vertex,
                    .usage = vbuffUsage,
                    .content = mesh->cpuBuffers.vertexBuffers[bufferIndex].Get()
                });

                if (!mesh->gpuBuffers.vertexBuffers[bufferIndex].IsValid())
                    return false;
                bufferIndex++;
            }
        }

        if (ibuffUsage != GfxBufferUsage::Default) {
            mesh->gpuBuffers.indexBuffer = gfxCreateBuffer(GfxBufferDesc {
                .size = uint32(sizeof(uint32)*mesh->numIndices),
                .type = GfxBufferType::Index,
                .usage = ibuffUsage,
                .content = mesh->cpuBuffers.indexBuffer.Get(),
            });

            if (!mesh->gpuBuffers.indexBuffer.IsValid())
                return false;
        }

    }

    return true;
}

static void modelLoadTextures(Model* model, AssetBarrier barrier)
{
    // TODO: notice that asserts that we commented. 
    //       These should be uncommented later after we solved the problem of separating resources from asset data
    for (uint32 i = 0; i < model->numMeshes; i++) {
        const ModelMesh& mesh = model->meshes[i];
        for (uint32 smi = 0; smi < mesh.numSubmeshes; smi++) {
            const ModelSubmesh& submesh = mesh.submeshes[smi];
            if (submesh.materialId == 0)
                continue;
            ModelMaterial* mtl = model->materials[IdToIndex(submesh.materialId)].Get();
            if (!mtl->pbrMetallicRoughness.baseColorTex.texturePath.IsNull()) {
                ASSERT(!mtl->pbrMetallicRoughness.baseColorTex.texture.IsValid());
                mtl->pbrMetallicRoughness.baseColorTex.texture =
                    assetLoadImage(mtl->pbrMetallicRoughness.baseColorTex.texturePath.Get(), 
                                   mtl->pbrMetallicRoughness.baseColorTex.params, barrier);
            }
            if (!mtl->pbrMetallicRoughness.metallicRoughnessTex.texturePath.IsNull()) {
                ASSERT(!mtl->pbrMetallicRoughness.metallicRoughnessTex.texture.IsValid());
                mtl->pbrMetallicRoughness.metallicRoughnessTex.texture = 
                    assetLoadImage(mtl->pbrMetallicRoughness.metallicRoughnessTex.texturePath.Get(), 
                                   mtl->pbrMetallicRoughness.metallicRoughnessTex.params, barrier);
            }
            if (!mtl->normalTexture.texturePath.IsNull()) {
                ASSERT(!mtl->normalTexture.texture.IsValid());
                mtl->normalTexture.texture = 
                    assetLoadImage(mtl->normalTexture.texturePath.Get(), mtl->normalTexture.params, barrier);
            }
            if (!mtl->occlusionTexture.texturePath.IsNull()) {
                ASSERT(!mtl->occlusionTexture.texture.IsValid());
                mtl->occlusionTexture.texture = 
                    assetLoadImage(mtl->occlusionTexture.texturePath.Get(), mtl->occlusionTexture.params, barrier);
            }
        }
    }
}

static void modelUnloadTextures(Model* model)
{
    for (uint32 i = 0; i < model->numMeshes; i++) {
        const ModelMesh& mesh = model->meshes[i];
        for (uint32 smi = 0; smi < mesh.numSubmeshes; smi++) {
            const ModelSubmesh& submesh = mesh.submeshes[smi];
            if (submesh.materialId == 0)
                continue;
            ModelMaterial* mtl = model->materials[IdToIndex(submesh.materialId)].Get();
            if (mtl->pbrMetallicRoughness.baseColorTex.texture.IsValid()) {
                assetUnload(mtl->pbrMetallicRoughness.baseColorTex.texture);
                mtl->pbrMetallicRoughness.baseColorTex.texture = AssetHandleImage();
            }

            if (mtl->pbrMetallicRoughness.metallicRoughnessTex.texture.IsValid()) {
                assetUnload(mtl->pbrMetallicRoughness.metallicRoughnessTex.texture);
                mtl->pbrMetallicRoughness.metallicRoughnessTex.texture = AssetHandleImage();
            }

            if (mtl->normalTexture.texture.IsValid()) {
                assetUnload(mtl->normalTexture.texture);
                mtl->normalTexture.texture = AssetHandleImage();
            }

            if (mtl->occlusionTexture.texture.IsValid()) {
                assetUnload(mtl->occlusionTexture.texture);
                mtl->occlusionTexture.texture = AssetHandleImage();
            }
        }
    }
}    

static const GfxVertexInputAttributeDesc* modelFindAttribute(const ModelGeometryLayout& layout, const char* semantic, uint32 semanticIdx)
{
    const GfxVertexInputAttributeDesc* attr = &layout.vertexAttributes[0];
    while (!attr->semantic.IsEmpty()) {
        if (attr->semantic == semantic && attr->semanticIdx == semanticIdx)
            return attr;
        ++attr;
    }
    return nullptr;
}

// Note: `alloc` shouldn't be temp allocator
Span<Model> modelLoadGltf(const char* filepath, MemAllocator* alloc, const ModelLoadParams& params, char* errorDesc, uint32 errorDescSize)
{
    PROFILE_ZONE(true);

    const ModelGeometryLayout& layout = params.layout.vertexBufferStrides[0] ? params.layout : gModelCtx.defaultLayout;

    Path fileDir = Path(filepath).GetDirectory_CStr();

    MemTempAllocator tmpAlloc;
    Blob blob = Vfs::ReadFile(filepath, VfsFlags::None, &tmpAlloc);
    if (!blob.IsValid()) {
        strPrintFmt(errorDesc, errorDescSize, "Opening model failed: %s", filepath);
        return {};
    }

    cgltf_options options {
        .type = cgltf_file_type_invalid,
        .memory = {
            .alloc_func = [](void* user, cgltf_size size)->void* { 
                return reinterpret_cast<MemTempAllocator*>(user)->Malloc(size); 
            },
            .free_func = [](void* user, void* ptr) { 
                reinterpret_cast<MemTempAllocator*>(user)->Free(ptr);
            },
            .user_data = &tmpAlloc
        },
        .file = {
            .read = [](const cgltf_memory_options*, const cgltf_file_options* fileOpts, 
                       const char*, cgltf_size* size, void** data)->cgltf_result
            {
                Blob* blob = reinterpret_cast<Blob*>(fileOpts->user_data);
                size_t readBytes = blob->Read(*data, *size);
                return readBytes == *size ? cgltf_result_success : cgltf_result_data_too_short;
            },
            .release = [](const cgltf_memory_options*, const cgltf_file_options*, void*)
            {
            },
            .user_data = &blob
        }
    };

    cgltf_data* data;
    cgltf_result result = cgltf_parse(&options, blob.Data(), blob.Size(), &data);
    if (result != cgltf_result_success) {
        strPrintFmt(errorDesc, errorDescSize, "Parsing model failed: %s", filepath);
        return {};
    }

    // Load Data buffers
    ASSERT_ALWAYS(data->buffers_count, "Model '%s' does not contain any data buffers", filepath);
    for (uint32 i = 0; i < (uint32)data->buffers_count; i++) {
        Path bufferFilepath = Path::JoinUnix(fileDir, data->buffers[i].uri);
        Blob bufferBlob = Vfs::ReadFile(bufferFilepath.CStr(), VfsFlags::None, &tmpAlloc);
        if (!bufferBlob.IsValid()) {
            strPrintFmt(errorDesc, errorDescSize, "Load model buffer failed: %s", bufferFilepath.CStr());
            return {};
        }
        bufferBlob.Detach(&data->buffers[i].data, &data->buffers[i].size);
        data->buffers[i].data_free_method = cgltf_data_free_method_memory_free;
    }

    // Gather materials and remove duplicates by looking up data hash
    struct MaterialData
    {
        ModelMaterial* mtl;
        uint32 size;
        uint32 id;
        uint32 hash;
    };

    uint32 numTotalTextures = 0;
    Array<MaterialData> materials(&tmpAlloc);
    Array<uint32> materialsMap(&tmpAlloc);     // count = NumMeshes*NumSubmeshPerMesh: maps each gltf material index to materials array

    for (uint32 i = 0; i < uint32(data->meshes_count); i++) {
        cgltf_mesh* mesh = &data->meshes[i];
        for (uint32 pi = 0; pi < uint32(mesh->primitives_count); pi++) {
            cgltf_primitive* prim = &mesh->primitives[pi];

            if (prim->material) {
                uint32 hash;
                uint32 numTextures;
                ModelMaterial* mtl = modelCreateMaterial(&numTextures, &hash, prim->material, fileDir.CStr(), &tmpAlloc);

                numTotalTextures += numTextures;

                uint32 index = materials.FindIf([hash](const MaterialData& m)->bool { return m.hash == hash; });
                if (index == UINT32_MAX) {
                    index = materials.Count();
                    materials.Push(MaterialData { 
                        .mtl = mtl, 
                        .size = uint32(tmpAlloc.GetOffset() - tmpAlloc.GetPointerOffset(mtl)), 
                        .id = IndexToId(index),
                        .hash = hash });
                }

                materialsMap.Push(index);
            }
        }
    }

    // Start creating the model. This is where the blob data starts
    Model* model = tmpAlloc.MallocZeroTyped<Model>();
    model->rootTransform = TRANSFORM3D_IDENT;
    model->layout = layout;

    // Meshes
    model->meshes = tmpAlloc.MallocZeroTyped<ModelMesh>((uint32)data->meshes_count);
    model->numMeshes = (uint32)data->meshes_count;
    uint32 mtlIndex = 0;

    for (uint32 i = 0; i < (uint32)data->meshes_count; i++) {
        cgltf_mesh* mesh = &data->meshes[i];
        ModelMesh* dstMesh = &model->meshes[i];

        if (mesh->name == nullptr) {
            char name[32];
            strPrintFmt(name, sizeof(name), "Mesh_%u", i);
            mesh->name = Mem::AllocCopy<char>(name, sizeof(name), &tmpAlloc);
        }

        dstMesh->name = mesh->name;
        dstMesh->submeshes = tmpAlloc.MallocZeroTyped<ModelSubmesh>((uint32)mesh->primitives_count);
        dstMesh->numSubmeshes = (uint32)mesh->primitives_count;

        // NumVertices/Indices/MaterialsIds
        uint32 numVertices = 0;
        uint32 numIndices = 0;
        for (uint32 pi = 0; pi < (uint32)mesh->primitives_count; pi++) {
            cgltf_primitive* prim = &mesh->primitives[pi];
            uint32 count = 0;

            for (cgltf_size ai = 0; ai < prim->attributes_count; ai++) {
                cgltf_attribute* srcAtt = &prim->attributes[ai];
                if (count == 0) 
                    count = (uint32)srcAtt->data->count;
                ASSERT_ALWAYS(count == (uint32)srcAtt->data->count, 
                              "Model %s, mesh %s: all primitives of the mesh should have the same vertex attributes", 
                              filepath, mesh->name);
            }

            numVertices += count;
            numIndices += (uint32)mesh->primitives[pi].indices->count;

            if (prim->material)
                dstMesh->submeshes[pi].materialId = materials[materialsMap[mtlIndex++]].id;
        } // foreach (mesh-primitive)
        ASSERT_ALWAYS(numVertices && numIndices, "Model %s Mesh %s: doesn't have any vertices", filepath, mesh->name);
        dstMesh->numVertices = numVertices;
        dstMesh->numIndices = numIndices;

        // Buffers
        uint32 bufferIdx = 0;
        while (layout.vertexBufferStrides[bufferIdx]) {
            uint32 vertexSize = layout.vertexBufferStrides[bufferIdx];
            dstMesh->cpuBuffers.vertexBuffers[bufferIdx] = tmpAlloc.MallocTyped<uint8>(vertexSize*numVertices);
            bufferIdx++;
        }
        dstMesh->numVertexBuffers = bufferIdx;

        dstMesh->cpuBuffers.indexBuffer = tmpAlloc.MallocTyped<uint32>(numIndices);
        modelSetupBuffers(dstMesh, layout, mesh);
    } // foreach (mesh)


    // Construct materials (from previously created array)
    if (materials.Count()) {
        model->numMaterials = materials.Count();
        model->materials = tmpAlloc.MallocZeroTyped<RelativePtr<ModelMaterial>>(materials.Count());
        for (uint32 i = 0; i < materials.Count(); i++) {
            const MaterialData& m = materials[i];
            model->materials[i] = Mem::AllocCopyRawBytes<ModelMaterial>(m.mtl, m.size, &tmpAlloc);
        }
    }

    // Nodes
    model->nodes = tmpAlloc.MallocZeroTyped<ModelNode>((uint32)data->nodes_count);
    model->numNodes = (uint32)data->nodes_count;

    for (uint32 i = 0; i < (uint32)data->nodes_count; i++) {
        cgltf_node* srcNode = &data->nodes[i];
        ModelNode* dstNode = &model->nodes[i];

        // Auto-generate name if it's not set
        if (srcNode->name == nullptr) {
            char name[32];
            strPrintFmt(name, sizeof(name), "Node_%u", i);
            srcNode->name = Mem::AllocCopy<char>(name, sizeof(name), &tmpAlloc);
        }

        dstNode->localTransform = TRANSFORM3D_IDENT;
        dstNode->name = srcNode->name;
        if (dstNode->name.Length() != strLen(srcNode->name)) {
            LOG_WARNING("Model %s, Node: %s: name is too long (more than standard 31 characters), "
                       "Node setup will likely have errors", filepath, srcNode->name);
        }

        ASSERT_ALWAYS(!srcNode->has_scale, "Model %s, Node: %s: Node scaling not supported yet", filepath, srcNode->name);

        if (srcNode->has_rotation) 
            dstNode->localTransform.rot = quatToMat3(Quat(srcNode->rotation));
        if (srcNode->has_translation)
            dstNode->localTransform.pos = Float3(srcNode->translation);

        for (cgltf_size mi = 0; mi < data->meshes_count; mi++) {
            if (&data->meshes[mi] == srcNode->mesh) {
                dstNode->meshId = IndexToId((uint32)mi);
                break;
            }
        }

        // Bounds
        AABB bounds = AABB_EMPTY;
        if (dstNode->meshId) {
            const ModelMesh& mesh = model->meshes[IdToIndex(dstNode->meshId)];
            const GfxVertexInputAttributeDesc* attr = modelFindAttribute(layout, "POSITION", 0);
            uint32 vertexStride = layout.vertexBufferStrides[attr->binding];
            uint8* vbuffu8 = mesh.cpuBuffers.vertexBuffers[attr->binding].Get();
            for (uint32 v = 0; v < mesh.numVertices; v++) {
                Float3 pos = *((Float3*)(vbuffu8 + v*vertexStride + attr->offset));
                AABBAddPoint(&bounds, pos);
            }
        }
        dstNode->bounds = bounds;
    }

    // Build Node Hierarchy
    auto FindNodeByName = [model](const char* name)->uint32
    {
        for (uint32 ni = 0; ni < model->numNodes; ni++) {
            if (model->nodes[ni].name == name)
                return IndexToId(ni);
        }
        return 0;
    };

    for (uint32 i = 0; i < model->numNodes; i++) {
        ModelNode* dstNode = &model->nodes[i];
        cgltf_node* srcNode = &data->nodes[i];

        if (srcNode->parent) 
            dstNode->parentId = FindNodeByName(srcNode->parent->name);

        if (srcNode->children_count) {
            dstNode->numChilds = (uint32)srcNode->children_count;
            dstNode->childIds = tmpAlloc.MallocZeroTyped<uint32>((uint32)srcNode->children_count);
            for (uint32 ci = 0; ci < (uint32)srcNode->children_count; ci++)
                dstNode->childIds[ci] = FindNodeByName(srcNode->children[ci]->name);
        }
    }

    // Allocate one big chunk and copy the temp data over to it
    uint32 modelBufferSize = uint32(tmpAlloc.GetOffset() - tmpAlloc.GetPointerOffset(model));
    return Span<Model>(Mem::AllocCopyRawBytes<Model>(model, modelBufferSize, alloc), modelBufferSize);
}

static void modelDestroy(Model* model, MemAllocator* alloc)
{
    ASSERT(model);

    // Release all graphics resources 
    for (uint32 i = 0; i < model->numMeshes; i++) {
        ModelMesh* mesh = &model->meshes[i];

        for (uint32 vi = 0; vi < mesh->numVertexBuffers; vi++)
            gfxDestroyBuffer(mesh->gpuBuffers.vertexBuffers[vi]);

        gfxDestroyBuffer(mesh->gpuBuffers.indexBuffer);
    }

    modelUnloadTextures(model);

    Mem::Free(model, alloc);
}

static Pair<AssetDependency*, uint32> modelGatherDependencies(const Model* model, const AssetLoadParams& params, MemTempAllocator* alloc,
                                                              uint32* outBufferSize)
{
    auto AddDependencyTextureStruct = [alloc](Array<AssetDependency>* depends, const AssetLoadParams& params, 
        const ModelMaterialTexture& tex) 
    {
        AssetDependency* dep = depends->Push();
        *dep = AssetDependency {
            .path = tex.texturePath.Get(),
            .params = AssetLoadParams {
                .alloc = params.alloc,
                .typeId = kImageAssetType,
                .tags = params.tags,
                .platform = params.platform,
            }
        };
        dep->params.next = (uint8*)Mem::AllocCopy<ImageLoadParams>(&tex.params, 1, alloc);
    };

    Array<AssetDependency> depends(alloc);
    depends.Reserve(model->numMaterialTextures);

    for (uint32 i = 0; i < model->numMeshes; i++) {
        const ModelMesh& mesh = model->meshes[i];
        for (uint32 smi = 0; smi < mesh.numSubmeshes; smi++) {
            const ModelSubmesh& submesh = mesh.submeshes[smi];
            const ModelMaterial* mtl = model->materials[IdToIndex(submesh.materialId)].Get();
            if (!mtl->pbrMetallicRoughness.baseColorTex.texturePath.IsNull())
                AddDependencyTextureStruct(&depends, params, mtl->pbrMetallicRoughness.baseColorTex);
            if (!mtl->pbrMetallicRoughness.metallicRoughnessTex.texturePath.IsNull()) 
                AddDependencyTextureStruct(&depends, params, mtl->pbrMetallicRoughness.metallicRoughnessTex);
            if (!mtl->normalTexture.texturePath.IsNull())
                AddDependencyTextureStruct(&depends, params, mtl->normalTexture);
            if (!mtl->occlusionTexture.texturePath.IsNull())
                AddDependencyTextureStruct(&depends, params, mtl->occlusionTexture);
        }
    }

    uint32 dependsBufferSize = uint32(alloc->GetOffset() - alloc->GetPointerOffset((void*)depends.Ptr()));

    *outBufferSize = dependsBufferSize;
    Pair<AssetDependency*, uint32> result;
    depends.Detach(&result.first, &result.second);
    return result;
}

#if CONFIG_TOOLMODE
static void modelOptimizeModel(Model* model, const ModelLoadParams& modelParams)
{
    MemTempAllocator tmpAlloc;
    MeshOptModel bakeModel;
    bakeModel.meshes = tmpAlloc.MallocTyped<MeshOptMesh*>(model->numMeshes);
    bakeModel.numMeshes = model->numMeshes;

    for (uint32 i = 0; i < model->numMeshes; i++) {
        ModelMesh& srcMesh = model->meshes[i];
        MemSingleShotMalloc<MeshOptMesh> mallocMesh;
        mallocMesh.AddMemberField<void*>(offsetof(MeshOptMesh, vertexBuffers), srcMesh.numVertexBuffers);
        mallocMesh.AddMemberField<uint32>(offsetof(MeshOptMesh, indexBuffer), srcMesh.numIndices);
        mallocMesh.AddMemberField<uint32>(offsetof(MeshOptMesh, vertexStrides), srcMesh.numVertexBuffers);
        MeshOptMesh* bakeMesh = mallocMesh.Calloc(&tmpAlloc);

        for (uint32 k = 0; k < srcMesh.numVertexBuffers; k++) {
            bakeMesh->vertexBuffers[k] = srcMesh.cpuBuffers.vertexBuffers[k].Get();
            bakeMesh->vertexStrides[k] = modelParams.layout.vertexBufferStrides[k];
        }

        bakeMesh->indexBuffer = srcMesh.cpuBuffers.indexBuffer.Get();
        bakeMesh->numVertexBuffers = srcMesh.numVertexBuffers;
        bakeMesh->numVertices = srcMesh.numVertices;
        bakeMesh->numIndices = srcMesh.numIndices;

        const GfxVertexInputAttributeDesc* attr = &modelParams.layout.vertexAttributes[0];    
        bool foundPos = false;
        while (!attr->semantic.IsEmpty()) {
            if (attr->semantic == "POSITION" && attr->semanticIdx == 0) {
                bakeMesh->posStride = modelParams.layout.vertexBufferStrides[attr->binding];
                bakeMesh->posBufferIndex = attr->binding;
                bakeMesh->posOffset = attr->offset;
                foundPos = true;
                break;
            }
            ++attr;
        }

        ASSERT_ALWAYS(foundPos, "Model should at least have positions for MeshOptimizer");

        bakeModel.meshes[i] = bakeMesh;
    }

    MeshOpt::Optimize(&bakeModel);
}
#endif // CONFIG_TOOLMODE

AssetResult ModelLoader::Load(AssetHandle handle, const AssetLoadParams& params, uint32 cacheHash, MemAllocator* dependsAlloc)
{
    ASSERT(params.next);
    const ModelLoadParams& modelParams = *reinterpret_cast<ModelLoadParams*>(params.next.Get());
    MemTempAllocator tmpAlloc;

    AssetMetaKeyValue* metaData;
    uint32 numMeta;
    assetLoadMetaData(handle, &tmpAlloc, &metaData, &numMeta);
    uint32 newCacheHash = assetMakeCacheHash(AssetCacheDesc {
        .filepath = params.path,
        .loadParams = params.next.Get(), 
        .loadParamsSize = sizeof(ModelLoadParams),
        .metaData = metaData,
        .numMeta = numMeta,
        .lastModified = Vfs::GetLastModified(params.path)
    });

    if (newCacheHash != cacheHash) {
        char errorDesc[512];
        Span<Model> modelBuffer = modelLoadGltf(params.path, params.alloc, modelParams, errorDesc, sizeof(errorDesc));
        Model* model = modelBuffer.Ptr();
        uint32 modelBufferSize = modelBuffer.Count();
        if (!model) {
            LOG_ERROR(errorDesc);
            return AssetResult {};
        }
    
        #if CONFIG_TOOLMODE
        modelOptimizeModel(model, modelParams);
        #endif // CONFIG_TOOLMODE
    
        if (model->numMaterialTextures) {
            uint32 dependsBufferSize;
            Pair<AssetDependency*, uint32> depends = modelGatherDependencies(model, params, &tmpAlloc, &dependsBufferSize);
            return AssetResult { 
                .obj = model,  
                .depends = Mem::AllocCopyRawBytes<AssetDependency>(depends.first, dependsBufferSize, dependsAlloc),
                .numDepends = depends.second,
                .dependsBufferSize = dependsBufferSize,
                .objBufferSize = modelBufferSize,
                .cacheHash = newCacheHash
            };
        }
        else {
            return AssetResult { .obj = model, .objBufferSize = modelBufferSize, .cacheHash = newCacheHash };
        }
    }
    else {
        return AssetResult { .cacheHash = newCacheHash };
    }
}

static void modelLoadTask(uint32 groupIndex, void* userData)
{
    UNUSED(groupIndex);

    MemTempAllocator tmpAlloc;
    Blob* blob = reinterpret_cast<Blob*>(userData);
    Blob outgoingBlob(&tmpAlloc);
    outgoingBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);
    
    char filepath[PATH_CHARS_MAX];
    char errorMsg[kRemoteErrorDescSize];
    AssetPlatform platform;
    ModelLoadParams loadModelParams;

    uint32 handle;
    uint32 oldCacheHash;
    blob->Read<uint32>(&handle);
    blob->Read<uint32>(&oldCacheHash);
    blob->ReadStringBinary(filepath, sizeof(filepath));
    blob->Read<uint32>(reinterpret_cast<uint32*>(&platform));
    blob->Read(&loadModelParams, sizeof(loadModelParams));

    outgoingBlob.Write<uint32>(handle);

    AssetMetaKeyValue* metaData;
    uint32 numMeta;
    assetLoadMetaData(filepath, platform, &tmpAlloc, &metaData, &numMeta);

    uint32 cacheHash = assetMakeCacheHash(AssetCacheDesc {
        .filepath = filepath,
        .loadParams = &loadModelParams,
        .loadParamsSize = sizeof(loadModelParams),
        .metaData = metaData,
        .numMeta = numMeta,
        .lastModified = Vfs::GetLastModified(filepath)
    });

    if (cacheHash != oldCacheHash) {
        TimerStopWatch timer;
        Span<Model> result = modelLoadGltf(filepath, Mem::GetDefaultAlloc(), loadModelParams, errorMsg, sizeof(errorMsg));
        Model* model = result.Ptr();
        uint32 modelBufferSize = result.Count();
    
        if (model) {
            #if CONFIG_TOOLMODE
            modelOptimizeModel(model, loadModelParams);
            #endif
        
            outgoingBlob.Write<uint32>(cacheHash);
            outgoingBlob.Write<uint32>(modelBufferSize);
            outgoingBlob.Write(model, modelBufferSize);
            Remote::SendResponse(RCMD_LOAD_MODEL, outgoingBlob, false, nullptr);
            LOG_VERBOSE("Model loaded: %s (%.1f ms)", filepath, timer.ElapsedMS());
            Mem::Free(model);
        }
        else {
            Remote::SendResponse(RCMD_LOAD_MODEL, outgoingBlob, true, errorMsg);
            LOG_VERBOSE(errorMsg);
        }
    }
    else {
        outgoingBlob.Write<uint32>(cacheHash);
        outgoingBlob.Write<uint32>(0);  // nothing has loaded. it's safe to load from client's local cache
        Remote::SendResponse(RCMD_LOAD_MODEL, outgoingBlob, false, nullptr);
        LOG_VERBOSE("Model: %s [cached]", filepath);
    }

    blob->Free();
    Mem::Free(blob);
}

static bool modelHandlerServerFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, Blob*, 
                                 void*, char outgoingErrorDesc[kRemoteErrorDescSize])
{
    ASSERT(cmd == RCMD_LOAD_MODEL);
    UNUSED(outgoingErrorDesc);
    
    // spawn and pass the copy it over to a task
    Blob* taskDataBlob = NEW(Mem::GetDefaultAlloc(), Blob)();
    incomingData.CopyTo(taskDataBlob);
    Jobs::DispatchAndForget(JobsType::LongTask, modelLoadTask, taskDataBlob, 1, JobsPriority::Low);

    return true;
}

static void modelHandlerClientFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, void* userData, bool error, const char* errorDesc)
{
    ASSERT(cmd == RCMD_LOAD_MODEL);
    UNUSED(userData);

    AssetHandle handle;
    incomingData.Read<uint32>(&handle.mId);
    ASSERT(handle.IsValid());

    ModelLoadRequest request {};
    {   
        MutexScope mtx(gModelCtx.requestsMutex);
        if (uint32 reqIndex = gModelCtx.requests.FindIf([handle](const ModelLoadRequest& req) { return req.handle == handle; });
            reqIndex != UINT32_MAX)
        {
            request = gModelCtx.requests[reqIndex];
            gModelCtx.requests.RemoveAndSwap(reqIndex);
        }
        else {
            ASSERT(0);
        }
    }

    if (!error) {    
        uint32 modelBufferSize= 0;
        uint32 cacheHash = 0;
        incomingData.Read<uint32>(&cacheHash);
        incomingData.Read<uint32>(&modelBufferSize);
        
        if (modelBufferSize) {
            void* modelData = Mem::Alloc(modelBufferSize, request.params.alloc);
            incomingData.Read(modelData, modelBufferSize);

            Model* model = reinterpret_cast<Model*>(modelData);
            if (model->numMaterialTextures) {
                MemTempAllocator tmpAlloc;
                uint32 dependsBufferSize;
                Pair<AssetDependency*, uint32> depends = modelGatherDependencies(model, request.params, &tmpAlloc, &dependsBufferSize);
                AssetResult result { 
                    .obj = modelData,  
                    .depends =  depends.first,
                    .numDepends = depends.second,
                    .dependsBufferSize = dependsBufferSize,
                    .objBufferSize = modelBufferSize,
                    .cacheHash = cacheHash
                };
                if (request.loadCallback)
                    request.loadCallback(handle, result, request.loadCallbackUserData);
            }
            else {
                if (request.loadCallback) {
                    request.loadCallback(handle, AssetResult { .obj = modelData, .objBufferSize = modelBufferSize, .cacheHash = cacheHash }, 
                                         request.loadCallbackUserData);
                }
            }
        }
        else {
            if (request.loadCallback) 
                request.loadCallback(handle, AssetResult { .cacheHash = cacheHash }, request.loadCallbackUserData);
        }
    }
    else {
        LOG_ERROR(errorDesc);
        if (request.loadCallback)
            request.loadCallback(handle, AssetResult {}, request.loadCallbackUserData);
    }
}

void ModelLoader::LoadRemote(AssetHandle handle, const AssetLoadParams& params, uint32 cacheHash, void* userData, 
                             AssetLoaderAsyncCallback loadCallback)
{
    ASSERT(params.next);
    ASSERT(loadCallback);
    ASSERT(Remote::IsConnected());

    const ModelLoadParams* modelParams = reinterpret_cast<ModelLoadParams*>(params.next.Get());

    // Gotta copy the damn strings in vertex attributes

    {   
        MutexScope mtx(gModelCtx.requestsMutex);
        gModelCtx.requests.Push(ModelLoadRequest {
            .handle = handle,
            .loadCallback = loadCallback,
            .loadCallbackUserData = userData,
            .loadParams = *modelParams,
            .params = params
        });
    }

    MemTempAllocator tmpAlloc;
    Blob outgoingBlob(&tmpAlloc);
    outgoingBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);

    outgoingBlob.Write<uint32>(uint32(handle));
    outgoingBlob.Write<uint32>(cacheHash);
    outgoingBlob.WriteStringBinary(params.path, strLen(params.path));
    outgoingBlob.Write<uint32>(uint32(params.platform));
    outgoingBlob.Write<ModelLoadParams>(*modelParams);

    Remote::ExecuteCommand(RCMD_LOAD_MODEL, outgoingBlob);
    outgoingBlob.Free();
}

bool ModelLoader::InitializeSystemResources(void* obj, const AssetLoadParams& params)
{
    Model* model = reinterpret_cast<Model*>(obj);
    const ModelLoadParams& modelParams = *reinterpret_cast<const ModelLoadParams*>(params.next.Get());
    if (modelParams.vertexBufferUsage != GfxBufferUsage::Default || modelParams.indexBufferUsage != GfxBufferUsage::Default) {
        if (!modelSetupGpuBuffers(model, modelParams.vertexBufferUsage, modelParams.indexBufferUsage))
            return false;
    }

    modelLoadTextures(model, params.barrier);
    return true;
}

void ModelLoader::Release(void* data, MemAllocator* alloc)
{
    modelDestroy(reinterpret_cast<Model*>(data), alloc);
}

bool ModelLoader::ReloadSync(AssetHandle handle, void* prevData)
{
    UNUSED(handle);
    UNUSED(prevData);
    return false;
}

AssetHandleModel assetLoadModel(const char* path, const ModelLoadParams& params, AssetBarrier barrier)
{
    AssetLoadParams assetParams {
        .path = path,
        .alloc = Mem::GetDefaultAlloc(),     // TODO: replace with a custom allocator
        .typeId = MODEL_ASSET_TYPE,
        .barrier = barrier
    };

    return AssetHandleModel { assetLoad(assetParams, &params) };
}

Model* assetGetModel(AssetHandleModel modelHandle)
{
    return reinterpret_cast<Model*>(_private::assetGetData(modelHandle));
}

bool _private::assetInitializeModelManager()
{
    assetRegisterType(AssetTypeDesc {
        .fourcc = MODEL_ASSET_TYPE,
        .name = "Model",
        .callbacks = &gModelLoader,
        .extraParamTypeName = "ModelLoadParams",
        .extraParamTypeSize = sizeof(ModelLoadParams),
        .failedObj = nullptr,
        .asyncObj = nullptr
    });

    gModelCtx.requestsMutex.Initialize();
    gModelCtx.requests.SetAllocator(Mem::GetDefaultAlloc());
    Remote::RegisterCommand(RemoteCommandDesc {
        .cmdFourCC = RCMD_LOAD_MODEL,
        .serverFn = modelHandlerServerFn,
        .clientFn = modelHandlerClientFn,
        .async = true
    });

    #if CONFIG_TOOLMODE
    MeshOpt::Initialize();
    #endif

    LOG_INFO("(init) Model asset manager");

    return true;
}

void _private::assetReleaseModelManager()
{
    assetUnregisterType(MODEL_ASSET_TYPE);
}
