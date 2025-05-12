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
#include "../Core/Log.h"
#include "../Core/Hash.h"
#include "../Core/MathAll.h"

#include "../Common/VirtualFS.h"

#include "../Tool/MeshOptimizer.h"

constexpr uint32 MODEL_ASSET_TYPE = MakeFourCC('M', 'O', 'D', 'L');

struct ModelVertexAttribute
{
    const char* semantic;
    uint32 index;
};

struct ModelCpuBuffers
{
    uint8* vertexBuffers[MODEL_MAX_VERTEX_BUFFERS_PER_SHADER];
    uint8* indexBuffer;
    uint64 vertexBufferSizes[MODEL_MAX_VERTEX_BUFFERS_PER_SHADER];
    uint64 indexBufferSize;
};

struct AssetModelImpl final : AssetTypeImplBase
{
    bool Bake(const AssetParams& params, AssetData* data, const Span<uint8>& srcData, String<256>* outErrorDesc) override;
    bool Reload(void* newData, void* oldData) override;
};

static ModelGeometryLayout gModelDefaultLayout;
static AssetModelImpl gModelImpl;

enum GLTFFilter 
{
    GLTF_FILTER_NEAREST = 9728,
    GLTF_FILTER_LINEAR = 9729,
    GLTF_FILTER_NEAREST_MIPMAP_NEAREST = 9984,
    GLTF_FILTER_LINEAR_MIPMAP_NEAREST = 9985,
    GLTF_FILTER_NEAREST_MIPMAP_LINEAR = 9986,
    GLTF_FILTER_LINEAR_MIPMAP_LINEAR = 9987
};

enum GLTFWrap
{
    GLTF_WRAP_CLAMP_TO_EDGE = 33071,
    GLTF_WRAP_MIRRORED_REPEAT = 33648,
    GLTF_WRAP_REPEAT = 10497
};

namespace ModelUtil
{
    static uint32 _GetVertexStride(GfxFormat fmt)
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

    static bool _LayoutHasTangents(const ModelGeometryLayout& vertexLayout)
    {
        const GfxVertexInputAttributeDesc* attr = &vertexLayout.vertexAttributes[0];
        while (!attr->semantic.IsEmpty()) {
            if (attr->semantic == "TANGENT")
                return true;
            ++attr;
        }
        return false;
    }

    static uint8* _GetVertexAttributePointer(ModelMesh* mesh, ModelCpuBuffers* cpuBuffers, const ModelGeometryLayout& vertexLayout, 
                                             const char* semantic, uint32 semanticIdx, uint32* outVertexStride)
    {
        const GfxVertexInputAttributeDesc* attr = &vertexLayout.vertexAttributes[0];
    
        while (!attr->semantic.IsEmpty()) {
            if (attr->semantic == semantic && attr->semanticIdx == semanticIdx) {
                *outVertexStride = vertexLayout.vertexBufferStrides[attr->binding];
                uint8* dstBuff = cpuBuffers->vertexBuffers[attr->binding] + mesh->vertexBufferOffsets[attr->binding];
                return dstBuff + attr->offset;
            }
            ++attr;
        }

        return nullptr;
    }

    static void _CalculateTangents(ModelMesh* mesh, ModelCpuBuffers* cpuBuffers, const ModelGeometryLayout& vertexLayout)
    {
        using namespace M;

        uint32* indexBuffer = (uint32*)(cpuBuffers->indexBuffer + mesh->indexBufferOffset);

        MemTempAllocator tmpAlloc;
    
        Float3* tan1 = tmpAlloc.MallocZeroTyped<Float3>(mesh->numVertices);
        Float3* tan2 = tmpAlloc.MallocZeroTyped<Float3>(mesh->numVertices);

        for (uint32 i = 0; i < mesh->numIndices; i+=3) {
            uint32 i1 = indexBuffer[i];
            uint32 i2 = indexBuffer[i+1];
            uint32 i3 = indexBuffer[i+2];

            uint32 posStride = 0, uvStride = 0;
            uint8* posPtr = ModelUtil::_GetVertexAttributePointer(mesh, cpuBuffers, vertexLayout, "POSITION", 0, &posStride);
            uint8* uvPtr = ModelUtil::_GetVertexAttributePointer(mesh, cpuBuffers, vertexLayout, "TEXCOORD", 0, &uvStride);

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
            if (!IsINF(r)) {
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
            uint8* normalPtr = ModelUtil::_GetVertexAttributePointer(mesh, cpuBuffers, vertexLayout, "NORMAL", 0, &normalStride);
            uint8* tangentPtr = ModelUtil::_GetVertexAttributePointer(mesh, cpuBuffers, vertexLayout, "TANGENT", 0, &tangentStride);
            uint8* bitangentPtr = ModelUtil::_GetVertexAttributePointer(mesh, cpuBuffers, vertexLayout, "BINORMAL", 0, &bitangentStride);

            Float3 n = *((Float3*)(normalPtr + normalStride*i));
            Float3 t = tan1[i];
    
            if (Float3Dot(t, t) != 0) {
                Float3 tangent = Float3Norm(t - n*Float3Dot(n, t));
                *((Float3*)(tangentPtr + tangentStride*i)) = tangent;
    
                // (Dot(Cross(n, t), tan2[a]) < 0.0F) ? -1.0F : 1.0F;
                float handedness = (Float3Dot(Float3Cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;

                *((Float3*)(bitangentPtr + bitangentStride*i)) = Float3Cross(n, tangent) * -handedness;
            }
        }
    }

    static const GfxVertexInputAttributeDesc* _FindAttribute(const ModelGeometryLayout& layout, const char* semantic, uint32 semanticIdx)
    {
        const GfxVertexInputAttributeDesc* attr = &layout.vertexAttributes[0];
        while (!attr->semantic.IsEmpty()) {
            if (attr->semantic == semantic && attr->semanticIdx == semanticIdx)
                return attr;
            ++attr;
        }
        return nullptr;
    }

    #if CONFIG_TOOLMODE
    static void _Optimize(ModelData* model, ModelCpuBuffers* cpuBuffers, const ModelLoadParams& modelParams)
    {
        MemTempAllocator tmpAlloc;
        MeshOptModel bakeModel;
        bakeModel.meshes = tmpAlloc.MallocTyped<MeshOptMesh*>(model->numMeshes);
        bakeModel.numMeshes = model->numMeshes;

        for (uint32 i = 0; i < model->numMeshes; i++) {
            ModelMesh& srcMesh = model->meshes[i];
            MemSingleShotMalloc<MeshOptMesh> mallocMesh;
            mallocMesh.AddMemberArray<void*>(offsetof(MeshOptMesh, vertexBuffers), model->numVertexBuffers);
            mallocMesh.AddMemberArray<uint32>(offsetof(MeshOptMesh, indexBuffer), srcMesh.numIndices);
            mallocMesh.AddMemberArray<uint32>(offsetof(MeshOptMesh, vertexStrides), model->numVertexBuffers);
            mallocMesh.AddMemberArray<MeshOptSubmesh>(offsetof(MeshOptMesh, submeshes), srcMesh.numSubmeshes);
            MeshOptMesh* bakeMesh = mallocMesh.Calloc(&tmpAlloc);

            for (uint32 k = 0; k < model->numVertexBuffers; k++) {
                bakeMesh->vertexBuffers[k] = cpuBuffers->vertexBuffers[k] + srcMesh.vertexBufferOffsets[k];
                bakeMesh->vertexStrides[k] = modelParams.layout.vertexBufferStrides[k];
            }

            for (uint32 k = 0; k < srcMesh.numSubmeshes; k++) {
                bakeMesh->submeshes[k].startIndex = srcMesh.submeshes[k].startIndex;
                bakeMesh->submeshes[k].numIndices = srcMesh.submeshes[k].numIndices;
            }

            bakeMesh->indexBuffer = (uint32*)(cpuBuffers->indexBuffer + srcMesh.indexBufferOffset);
            bakeMesh->numVertexBuffers = model->numVertexBuffers;
            bakeMesh->numVertices = srcMesh.numVertices;
            bakeMesh->numIndices = srcMesh.numIndices;
            bakeMesh->numSubmeshes = srcMesh.numSubmeshes;

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

        for (uint32 i = 0; i < model->numMeshes; i++) {
            ModelMesh& srcMesh = model->meshes[i];
            srcMesh.numVertices = bakeModel.meshes[i]->numVertices;
        }
    }
    #endif // CONFIG_TOOLMODE

} // ModelUtil

namespace GLTF
{
    INLINE GfxSamplerFilterMode _GetFilter(GLTFFilter filter)
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

    INLINE GfxSamplerWrapMode _GetWrap(GLTFWrap wrap)
    {
        switch (wrap) {
        case GLTF_WRAP_CLAMP_TO_EDGE:   return GfxSamplerWrapMode::ClampToEdge;
        case GLTF_WRAP_MIRRORED_REPEAT: return GfxSamplerWrapMode::MirroredRepeat;
        case GLTF_WRAP_REPEAT:          return GfxSamplerWrapMode::Repeat;
        default:                        return GfxSamplerWrapMode::Default;
        }
    }

    static ModelMaterial* _CreateMaterial(uint32* outNumTextures, uint32* outHash, cgltf_material* gltfMtl, const char* fileDir, MemAllocator* alloc)
    {
        ASSERT(gltfMtl);

        auto LoadTextureFromGltf = [alloc](cgltf_texture* gltfTexture, ModelMaterialTexture* tex, const char* fileDir, HashMurmur32Incremental& hasher)
        {
            ASSERT(gltfTexture);
            char texturePath[PATH_CHARS_MAX];
            {
                char* dir = Str::Copy(texturePath, sizeof(texturePath), fileDir);
                if (*(dir - 1) != '/') {
                    dir[0] = '/';
                    dir[1] = '\0';
                    ++dir;
                }
                Str::Concat(dir, sizeof(texturePath), gltfTexture->image->uri);
            }


            ImageLoadParams tparams;
            if (gltfTexture->sampler) {
                ASSERT(gltfTexture->sampler->wrap_s == gltfTexture->sampler->wrap_t);
                tparams.samplerFilter = GLTF::_GetFilter((GLTFFilter)gltfTexture->sampler->min_filter);
                tparams.samplerWrap = GLTF::_GetWrap((GLTFWrap)gltfTexture->sampler->wrap_s);
            }

            uint32 texturePathLen = Str::Len(texturePath);
            tex->texturePath = Mem::AllocCopy<char>(texturePath, texturePathLen+1, alloc);
            tex->params = tparams;

            hasher.Add(texturePath, texturePathLen);
            hasher.Add<ImageLoadParams>(tparams);
        };

        ModelMaterialAlphaMode alphaMode;
        switch (gltfMtl->alpha_mode) {
        case cgltf_alpha_mode_opaque:      alphaMode = ModelMaterialAlphaMode::Opaque;              break;
        case cgltf_alpha_mode_mask:        alphaMode = ModelMaterialAlphaMode::Mask;                break;
        case cgltf_alpha_mode_blend:       alphaMode = ModelMaterialAlphaMode::Blend;               break;
        default:                           ASSERT(0); alphaMode = ModelMaterialAlphaMode::Opaque;   break;
        }

        ModelMaterial* mtl = Mem::AllocTyped<ModelMaterial>(1, alloc);
        *mtl = {
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
        hasher.Add<ModelMaterial>(*mtl);

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


    static ModelVertexAttribute _ConvertVertexAttribute(cgltf_attribute_type type, uint32 index)
    {
        if (type == cgltf_attribute_type_position && index == 0)  {
            return { .semantic = "POSITION", .index = 0 };
        } 
        else if (type == cgltf_attribute_type_normal && index == 0) {
            return { .semantic = "NORMAL", .index = 0 };
        } 
        else if (type == cgltf_attribute_type_tangent && index == 0) {
            return { .semantic = "TANGENT", .index = 0 };
        } 
        else if (type == cgltf_attribute_type_texcoord) {
            switch (index) {
            case 0:     return { .semantic = "TEXCOORD", .index = 0 };
            case 1:     return { .semantic = "TEXCOORD", .index = 1 };
            case 2:     return { .semantic = "TEXCOORD", .index = 2 };
            case 3:     return { .semantic = "TEXCOORD", .index = 3 };
            default:    return {};
            }
        } 
        else if (type == cgltf_attribute_type_color) {
            switch (index) {
            case 0:     return { .semantic = "COLOR", .index = 0 };
            case 1:     return { .semantic = "COLOR", .index = 0 };
            case 2:     return { .semantic = "COLOR", .index = 0 };
            case 3:     return { .semantic = "COLOR", .index = 0 };
            default:    return {};
            }
        } 
        else if (type == cgltf_attribute_type_joints && index == 0) {
            return { .semantic = "BLENDINDICES", .index = 0 };
        } 
        else if (type == cgltf_attribute_type_weights && index == 0) {
            return { .semantic = "BLENDWEIGHT", .index = 0 };
        } 
        else {
            return {};
        }
    }

    static bool _MapVertexAttributesToBuffer(ModelCpuBuffers* cpuBuffers, const ModelMesh& mesh,
                                             const ModelGeometryLayout& vertexLayout, 
                                             cgltf_attribute* srcAttribute, uint32 startVertex)
    {
        cgltf_accessor* access = srcAttribute->data;
        const GfxVertexInputAttributeDesc* attr = &vertexLayout.vertexAttributes[0];
        ModelVertexAttribute mappedAttribute = _ConvertVertexAttribute(srcAttribute->type, srcAttribute->index);
        while (!attr->semantic.IsEmpty()) {
            if (attr->semantic == mappedAttribute.semantic && attr->semanticIdx == mappedAttribute.index) {
                uint32 vertexStride = vertexLayout.vertexBufferStrides[attr->binding];
                uint8* srcBuffer = (uint8*)access->buffer_view->buffer->data;
                uint8* dstBuffer = cpuBuffers->vertexBuffers[attr->binding] + mesh.vertexBufferOffsets[attr->binding];
                uint32 dstOffset =  startVertex * vertexStride + attr->offset;
                uint32 srcOffset = static_cast<uint32>(access->offset + access->buffer_view->offset);

                uint32 count = static_cast<uint32>(access->count);
                uint32 srcDataSize = static_cast<uint32>(access->stride); 
                uint32 dstDataSize = ModelUtil::_GetVertexStride(attr->format);
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

    static bool _HasTangents(const cgltf_primitive* prim)
    {
        for (cgltf_size i = 0; i < prim->attributes_count; i++) {
            if (prim->attributes[i].type == cgltf_attribute_type_tangent)
                return true;
        }

        return false;
    }

    static void _SetupBuffers(ModelMesh* mesh, ModelCpuBuffers* cpuBuffers, const ModelGeometryLayout& vertexLayout, cgltf_mesh* srcMesh)
    {
        // create buffers based on input vertexLayout

        // map source vertex buffer to our data
        // map source index buffer to our data
        uint32 startIndex = 0;
        uint32 startVertex = 0;
        bool calcTangents = false;
        bool layoutHasTangents = ModelUtil::_LayoutHasTangents(vertexLayout);

        for (uint32 i = 0; i < (uint32)srcMesh->primitives_count; i++) {
            cgltf_primitive* srcPrim = &srcMesh->primitives[i];

            // vertices
            // go through gltf vertex attributes and find them in the vertex layout, then we can map the data to the buffers
            uint32 count = 0;
            for (cgltf_size k = 0; k < srcPrim->attributes_count; k++) {
                cgltf_attribute* srcAtt = &srcPrim->attributes[k];
                _MapVertexAttributesToBuffer(cpuBuffers, *mesh, vertexLayout, srcAtt, startVertex);
                if (count == 0) {
                    count = uint32(srcAtt->data->count);
                }
                ASSERT(count == (uint32)srcAtt->data->count);
            }

            // in some instances, we may need tangents in the layout, but they wouldn't be present in
            // the gltf data so in that case, we have to calculate them manually
            if (layoutHasTangents && !_HasTangents(srcPrim))
                calcTangents = true;

            // indices
            cgltf_accessor* srcIndices = srcPrim->indices;
            if (srcIndices->component_type == cgltf_component_type_r_16u) {
                uint32* indices = (uint32*)(cpuBuffers->indexBuffer + mesh->indexBufferOffset) + startIndex;
                uint16* srcIndicesOffseted = reinterpret_cast<uint16*>((uint8*)srcIndices->buffer_view->buffer->data + srcIndices->buffer_view->offset);
                for (cgltf_size k = 0; k < srcIndices->count; k++)
                    indices[k] = uint32(srcIndicesOffseted[k]) + startVertex;

                // flip the winding
                #if 0
                for (uint32 k = 0, numTris = (uint32)srcIndices->count/3; k < numTris; k++) {
                    uint32 ii = k*3;
                    Swap<uint32>(indices[ii], indices[ii+2]);
                }
                #endif
            } 
            else if (srcIndices->component_type == cgltf_component_type_r_32u) {
                uint32* indices = (uint32*)(cpuBuffers->indexBuffer + mesh->indexBufferOffset) + startIndex;
                uint32* srcIndicesOffseted = reinterpret_cast<uint32*>((uint8*)srcIndices->buffer_view->buffer->data + srcIndices->buffer_view->offset);
                for (cgltf_size k = 0; k < srcIndices->count; k++)
                    indices[k] = srcIndicesOffseted[k] + startVertex;

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
            ModelUtil::_CalculateTangents(mesh, cpuBuffers, vertexLayout);
    }

    static Pair<ModelData*, uint32> _Load(Blob& fileBlob, const Path& fileDir, MemTempAllocator* tmpAlloc, const ModelLoadParams& params, 
                                      String<256>* outErrorDesc, ModelCpuBuffers* outCpuBuffers)
    {
        const ModelGeometryLayout& layout = params.layout.vertexBufferStrides[0] ? params.layout : gModelDefaultLayout;

        cgltf_options options {
            .type = cgltf_file_type_invalid,
            .memory = {
                .alloc_func = [](void* user, cgltf_size size)->void* { 
                    return reinterpret_cast<MemTempAllocator*>(user)->Malloc(size); 
                },
                .free_func = [](void* user, void* ptr) { 
                    reinterpret_cast<MemTempAllocator*>(user)->Free(ptr);
                },
                .user_data = tmpAlloc
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
                .user_data = &fileBlob
            }
        };

        cgltf_data* data;
        cgltf_result result = cgltf_parse(&options, fileBlob.Data(), fileBlob.Size(), &data);
        if (result != cgltf_result_success) {
            *outErrorDesc = "Parsing GLTF model failed";
            return {};
        }

        // Load Data buffers
        ASSERT_ALWAYS(data->buffers_count, "Model does not contain any data buffers");
        for (uint32 i = 0; i < (uint32)data->buffers_count; i++) {
            Path bufferFilepath = Path::JoinUnix(fileDir, data->buffers[i].uri);
            Blob bufferBlob = Vfs::ReadFile(bufferFilepath.CStr(), VfsFlags::None, tmpAlloc);
            if (!bufferBlob.IsValid()) {
                outErrorDesc->FormatSelf("Load model buffer failed: %s", bufferFilepath.CStr());
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
        Array<MaterialData> materials(tmpAlloc);
        Array<uint32> materialsMap(tmpAlloc);     // count = NumMeshes*NumSubmeshPerMesh: maps each gltf material index to materials array

        for (uint32 i = 0; i < uint32(data->meshes_count); i++) {
            cgltf_mesh* mesh = &data->meshes[i];
            for (uint32 pi = 0; pi < uint32(mesh->primitives_count); pi++) {
                cgltf_primitive* prim = &mesh->primitives[pi];

                if (prim->material) {
                    uint32 hash;
                    uint32 numTextures;
                    ModelMaterial* mtl = GLTF::_CreateMaterial(&numTextures, &hash, prim->material, fileDir.CStr(), tmpAlloc);

                    numTotalTextures += numTextures;

                    uint32 index = materials.FindIf([hash](const MaterialData& m)->bool { return m.hash == hash; });
                    if (index == UINT32_MAX) {
                        index = materials.Count();
                        MaterialData mtlData { 
                            .mtl = mtl, 
                            .size = uint32(tmpAlloc->GetOffset() - tmpAlloc->GetPointerOffset(mtl)), 
                            .id = IndexToId(index),
                            .hash = hash
                        };

                        materials.Push(mtlData);
                    }

                    materialsMap.Push(index);
                }
            }
        }

        // Start creating the model. This is where the blob data starts
        ModelData* model = tmpAlloc->MallocZeroTyped<ModelData>();
        model->rootTransform = TRANSFORM3D_IDENT;
        model->layout = layout;
        model->numMaterialTextures = numTotalTextures;

        {
            uint32 bufferIdx = 0;
            while (layout.vertexBufferStrides[bufferIdx])
                bufferIdx++;
            ASSERT_MSG(bufferIdx, "Vertex layout should at least contain one vertex attribute+stride");
            model->numVertexBuffers = bufferIdx;
        }

        // Meshes
        model->meshes = tmpAlloc->MallocZeroTyped<ModelMesh>((uint32)data->meshes_count);
        model->numMeshes = (uint32)data->meshes_count;
        uint32 mtlIndex = 0;

        for (uint32 i = 0; i < (uint32)data->meshes_count; i++) {
            cgltf_mesh* mesh = &data->meshes[i];
            ModelMesh* dstMesh = &model->meshes[i];

            if (mesh->name == nullptr) {
                char name[32];
                Str::PrintFmt(name, sizeof(name), "Mesh_%u", i);
                mesh->name = Mem::AllocCopy<char>(name, Str::Len(name)+1, tmpAlloc);
            }

            dstMesh->name = mesh->name;
            dstMesh->submeshes = tmpAlloc->MallocZeroTyped<ModelSubmesh>(uint32(mesh->primitives_count));
            dstMesh->numSubmeshes = uint32(mesh->primitives_count);

            // NumVertices/Indices/MaterialsIds
            uint32 numVertices = 0;
            uint32 numIndices = 0;
            for (uint32 pi = 0; pi < uint32(mesh->primitives_count); pi++) {
                cgltf_primitive* prim = &mesh->primitives[pi];
                uint32 count = 0;

                for (cgltf_size ai = 0; ai < prim->attributes_count; ai++) {
                    cgltf_attribute* srcAtt = &prim->attributes[ai];
                    if (count == 0) 
                        count = (uint32)srcAtt->data->count;
                    ASSERT_ALWAYS(count == uint32(srcAtt->data->count), "Mesh %s: all primitives of the mesh should have the same vertex attributes", mesh->name);
                }

                numVertices += count;
                numIndices += uint32(mesh->primitives[pi].indices->count);

                if (prim->material)
                    dstMesh->submeshes[pi].materialId = materials[materialsMap[mtlIndex++]].id;
            } // foreach (mesh-primitive)
            ASSERT_ALWAYS(numVertices && numIndices, "Mesh %s: doesn't have any vertices", mesh->name);
            dstMesh->numVertices = numVertices;
            dstMesh->numIndices = numIndices;
        } // foreach (mesh)


        // Construct materials (from previously created array)
        if (materials.Count()) {
            model->numMaterials = materials.Count();
            model->materials = tmpAlloc->MallocZeroTyped<RelativePtr<ModelMaterial>>(materials.Count());
            for (uint32 i = 0; i < materials.Count(); i++) {
                const MaterialData& m = materials[i];
                model->materials[i] = Mem::AllocCopyRawBytes<ModelMaterial>(m.mtl, m.size, tmpAlloc);
            }
        }

        // Nodes
        model->nodes = tmpAlloc->MallocZeroTyped<ModelNode>((uint32)data->nodes_count);
        model->numNodes = (uint32)data->nodes_count;

        for (uint32 i = 0; i < (uint32)data->nodes_count; i++) {
            cgltf_node* srcNode = &data->nodes[i];
            ModelNode* dstNode = &model->nodes[i];

            // Auto-generate name if it's not set
            if (srcNode->name == nullptr) {
                char name[32];
                Str::PrintFmt(name, sizeof(name), "Node_%u", i);
                srcNode->name = Mem::AllocCopy<char>(name, sizeof(name), tmpAlloc);
            }

            dstNode->localTransform = TRANSFORM3D_IDENT;
            dstNode->name = srcNode->name;
            if (dstNode->name.Length() != Str::Len(srcNode->name)) {
                LOG_WARNING("Node: %s: name is too long (more than standard 31 characters), "
                            "Node setup will likely have errors", srcNode->name);
            }

            // ASSERT_ALWAYS(!srcNode->has_scale, "Node: %s: Node scaling not supported yet", srcNode->name);

            if (srcNode->has_rotation) 
                dstNode->localTransform.rot = Mat3::FromQuat(Quat(srcNode->rotation));
            if (srcNode->has_translation)
                dstNode->localTransform.pos = Float3(srcNode->translation);

            for (cgltf_size mi = 0; mi < data->meshes_count; mi++) {
                if (&data->meshes[mi] == srcNode->mesh) {
                    dstNode->meshId = IndexToId(uint32(mi));
                    break;
                }
            }
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
                dstNode->childIds = tmpAlloc->MallocZeroTyped<uint32>((uint32)srcNode->children_count);
                for (uint32 ci = 0; ci < (uint32)srcNode->children_count; ci++)
                    dstNode->childIds[ci] = FindNodeByName(srcNode->children[ci]->name);
            }
        }

        // Allocate one big chunk and copy the temp data over to it
        uint32 modelBufferSize = uint32(tmpAlloc->GetOffset() - tmpAlloc->GetPointerOffset(model));

        // Buffers
        ModelCpuBuffers* cpuBuffers = outCpuBuffers;
        ASSERT(cpuBuffers->indexBufferSize == 0);
        ASSERT(cpuBuffers->vertexBufferSizes[0] == 0);

        for (uint32 i = 0; i < model->numMeshes; i++) {
            ModelMesh& mesh = model->meshes[i];

            for (uint32 vertexBufferIdx = 0; vertexBufferIdx < model->numVertexBuffers; vertexBufferIdx++) {
                mesh.vertexBufferOffsets[vertexBufferIdx] = cpuBuffers->vertexBufferSizes[vertexBufferIdx];
                mesh.vertexBufferSizes[vertexBufferIdx] = layout.vertexBufferStrides[vertexBufferIdx]*mesh.numVertices;
                cpuBuffers->vertexBufferSizes[vertexBufferIdx] += mesh.vertexBufferSizes[vertexBufferIdx];
                cpuBuffers->vertexBufferSizes[vertexBufferIdx] = AlignValue<uint64>(cpuBuffers->vertexBufferSizes[vertexBufferIdx], 16ull);
            }

            {
                mesh.indexBufferOffset = cpuBuffers->indexBufferSize;
                mesh.indexBufferSize = sizeof(uint32)*mesh.numIndices;
                cpuBuffers->indexBufferSize += mesh.indexBufferSize;
                cpuBuffers->indexBufferSize = AlignValue<uint64>(cpuBuffers->indexBufferSize, 16ull);
            }
        }

        for (uint32 vertexBufferIdx = 0; vertexBufferIdx < model->numVertexBuffers; vertexBufferIdx++)
            cpuBuffers->vertexBuffers[vertexBufferIdx] = (uint8*)tmpAlloc->Malloc(cpuBuffers->vertexBufferSizes[vertexBufferIdx]);
        cpuBuffers->indexBuffer = (uint8*)tmpAlloc->Malloc(cpuBuffers->indexBufferSize);

        for (uint32 i = 0; i < (uint32)data->meshes_count; i++) {
            cgltf_mesh* mesh = &data->meshes[i];
            ModelMesh* dstMesh = &model->meshes[i];

            _SetupBuffers(dstMesh, cpuBuffers, layout, mesh);
        }

        // Bounds
        for (uint32 i = 0; i < (uint32)data->nodes_count; i++) {
            ModelNode* dstNode = &model->nodes[i];

            AABB bounds = AABB_EMPTY;
            if (dstNode->meshId) {
                const ModelMesh& mesh = model->meshes[IdToIndex(dstNode->meshId)];
                const GfxVertexInputAttributeDesc* attr = ModelUtil::_FindAttribute(layout, "POSITION", 0);
                uint32 vertexStride = layout.vertexBufferStrides[attr->binding];
                uint8* vbuffu8 = cpuBuffers->vertexBuffers[attr->binding] + mesh.vertexBufferOffsets[attr->binding];
                for (uint32 v = 0; v < mesh.numVertices; v++) {
                    Float3 pos = *((Float3*)(vbuffu8 + v*vertexStride + attr->offset));
                    AABB::AddPoint(&bounds, pos);
                }
            }
            dstNode->bounds = bounds;
        }

        return Pair<ModelData*, uint32>(model, modelBufferSize);
    }

} // GLTF

bool Model::InitializeManager()
{
    AssetTypeDesc desc {
        .fourcc = MODEL_ASSET_TYPE,
        .name = "Model",
        .impl = &gModelImpl,
        .extraParamTypeName = "ModelLoadParams",
        .extraParamTypeSize = sizeof(ModelLoadParams),
        .failedObj = nullptr,
        .asyncObj = nullptr
    };

    Asset::RegisterType(desc);

    #if CONFIG_TOOLMODE
    MeshOpt::Initialize();
    #endif

    return true;
}

void Model::ReleaseManager()
{
    Asset::UnregisterType(MODEL_ASSET_TYPE);
}

bool AssetModelImpl::Bake(const AssetParams& params, AssetData* data, const Span<uint8>& srcData, String<256>* outErrorDesc)
{
    const ModelLoadParams* modelParams = (const ModelLoadParams*)params.extraParams;

    MemTempAllocator tmpAlloc;
    Blob fileBlob(const_cast<uint8*>(srcData.Ptr()), srcData.Count());
    fileBlob.SetSize(srcData.Count());

    Path fileDir = params.path.GetDirectory();
    ModelCpuBuffers cpuBuffers {};
    Pair<ModelData*, uint32> modelResult = GLTF::_Load(fileBlob, fileDir, &tmpAlloc, *modelParams, outErrorDesc, &cpuBuffers);
    ModelData* model = modelResult.first;
    uint32 modelBufferSize = modelResult.second;
    if (!model)
        return false;

    #if CONFIG_TOOLMODE
    ModelUtil::_Optimize(model, &cpuBuffers, *modelParams);
    #endif // CONFIG_TOOLMODE

    ASSERT(modelBufferSize <= UINT32_MAX);
    data->SetObjData(model, modelBufferSize);

    // Dependencies (Textures)
    if (model->numMaterialTextures) {
        AssetParams assetParams {
            .typeId = IMAGE_ASSET_TYPE,
            .platform = params.platform
        };

        for (uint32 i = 0; i < model->numMeshes; i++) {
            const ModelMesh& mesh = model->meshes[i];
            for (uint32 smi = 0; smi < mesh.numSubmeshes; smi++) {
                const ModelSubmesh& submesh = mesh.submeshes[smi];
                ModelMaterial* mtl = model->materials[IdToIndex(submesh.materialId)].Get();

                if (!mtl->pbrMetallicRoughness.baseColorTex.texturePath.IsNull()) {
                    assetParams.path = mtl->pbrMetallicRoughness.baseColorTex.texturePath.Get();
                    assetParams.extraParams = &mtl->pbrMetallicRoughness.baseColorTex.params;
                    data->AddDependency(&mtl->pbrMetallicRoughness.baseColorTex.texture, assetParams);
                }

                if (!mtl->pbrMetallicRoughness.metallicRoughnessTex.texturePath.IsNull()) {
                    assetParams.path = mtl->pbrMetallicRoughness.metallicRoughnessTex.texturePath.Get();
                    assetParams.extraParams = &mtl->pbrMetallicRoughness.metallicRoughnessTex.params;
                    data->AddDependency(&mtl->pbrMetallicRoughness.metallicRoughnessTex.texture, assetParams);
                }

                if (!mtl->normalTexture.texturePath.IsNull()) {
                    assetParams.path = mtl->normalTexture.texturePath.Get();
                    assetParams.extraParams = &mtl->normalTexture.params;
                    data->AddDependency(&mtl->normalTexture.texture, assetParams);
                }

                if (!mtl->occlusionTexture.texturePath.IsNull()) {
                    assetParams.path = mtl->occlusionTexture.texturePath.Get();
                    assetParams.extraParams = &mtl->occlusionTexture.params;
                    data->AddDependency(&mtl->occlusionTexture.texture, assetParams);
                }
            }
        }
    } // we have textures 

    // GPU Buffers
    for (uint32 vertexBufferIdx = 0; vertexBufferIdx < model->numVertexBuffers; vertexBufferIdx++) {
        GfxBufferDesc desc {
            .sizeBytes = cpuBuffers.vertexBufferSizes[vertexBufferIdx],
            .usageFlags = GfxBufferUsageFlags::TransferDst|GfxBufferUsageFlags::Vertex,
            .arena = GfxMemoryArena::DynamicBufferGPU
        };

        data->AddGpuBufferObject(&model->vertexBuffers[vertexBufferIdx], desc, cpuBuffers.vertexBuffers[vertexBufferIdx]);
    }

    {
        GfxBufferDesc desc {
            .sizeBytes = cpuBuffers.indexBufferSize,
            .usageFlags = GfxBufferUsageFlags::TransferDst|GfxBufferUsageFlags::Index,
            .arena = GfxMemoryArena::DynamicBufferGPU
        };
        data->AddGpuBufferObject(&model->indexBuffer, desc, cpuBuffers.indexBuffer);
    }

    return true;
}

bool AssetModelImpl::Reload(void* newData, void* oldData)
{
    UNUSED(newData);
    UNUSED(oldData);

    return false;
}

AssetHandleModel Model::Load(const char* path, const ModelLoadParams& params, const AssetGroup& group)
{
    AssetParams assetParams {
        .typeId = MODEL_ASSET_TYPE,
        .path = path,
        .extraParams = const_cast<ModelLoadParams*>(&params)
    };

    return group.AddToLoadQueue(assetParams);
}
