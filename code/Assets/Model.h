#pragma once

//------------------------------------------------------------------------
// Model
// mesh objects contains the main geometry for each node of the model
// it includes multiple "submeshes". a submesh is part of the geometry with differnt material
// so, a mesh can contain multiple sub-materials and sub-meshes within itself
//
// vertex buffers are another topic that needs to be discussed here:
//      when loading a model, you should define the whole vertex-layout that your render pipeline uses
//      so for example, a renderer may need to layout it's vertex data in multiple buffers like this:
//          - buffer #1: position
//          - buffer #2: normal/tangent
//          - buffer #3: texcoord
//          - buffer #4: joints/weights
//      4 buffers will be reserved (numVertexBuffers=4) for every model loaded with this pipeline setup
//        - if the source model doesn't have joints AND weights. then the buffer #4 slot for the model will be NULL
//        - if the source model does have normal but does not have tangents, then buffer #2 will be created and tangents will be undefined
//      When rendering, you can customize which set of buffers you'll need based on the shader's input layout
//        - shadow map shader: can only fetch the buffer #1 (position)
//      The catch is when you setup your pipeline, all shaders should comply for one or more vertex-buffer formats
//      So in our example, every shader must take one of the 4 buffer formats or several of them
//
// vertexAttributes is all vertex attributes of the source model and is not related for vertex buffer formats
// GpuBuffers struct is filled only for models without STREAM buffer flag
//

#include "../Core/StringUtil.h"
#include "../Core/MathTypes.h"
#include "../Common/CommonTypes.h"

#include "Image.h"

inline constexpr uint32 kModelMaxVertexAttributes = 16;
inline constexpr uint32 kModelMaxVertexBuffersPerShader = 8;

//------------------------------------------------------------------------
// Material
struct ModelMaterialTexture 
{
    RelativePtr<char> texturePath;
    ImageLoadParams params;
    AssetHandleImage texture;
    uint32 arrayIndex;
};

struct ModelMaterialMetallicRoughness 
{
    ModelMaterialTexture baseColorTex;
    ModelMaterialTexture metallicRoughnessTex;
    
    Float4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
};

struct ModelMaterialSpecularGlossiness 
{
    ModelMaterialTexture diffuseTexture;
    ModelMaterialTexture specularGlossinessTexture;

    Float4 diffuseFactor;
    Float3 specularFactor;
    float glossinessFactor;
};

struct ModelMaterialClearcoat 
{
    AssetHandleImage clearcoatTex;
    AssetHandleImage clearcoatRoughnessTexture;
    AssetHandleImage clearcoatNormalTexture;

    float clearcoatFactor;
    float clearcoatRoughnessFactor;
};

enum ModelMaterialAlphaMode 
{
    Opaque = 0,
    Mask,
    Blend
};

struct ModelMaterial 
{
    bool hasMetalRoughness;
    bool hasSpecularGlossiness;
    bool hasClearcoat;
    bool reserved1;
    ModelMaterialMetallicRoughness pbrMetallicRoughness;
    ModelMaterialSpecularGlossiness pbrSpecularGlossiness;
    ModelMaterialClearcoat clearcoat;
    ModelMaterialTexture normalTexture;
    ModelMaterialTexture occlusionTexture;
    ModelMaterialTexture emissiveTexture;
    Float3 emissiveFactor;
    ModelMaterialAlphaMode alphaMode;
    float alphaCutoff;
    bool doubleSided;
    bool unlit;
};

//------------------------------------------------------------------------
struct ModelGeometryLayout 
{
    GfxVertexInputAttributeDesc vertexAttributes[kModelMaxVertexAttributes];
    uint32 vertexBufferStrides[kModelMaxVertexBuffersPerShader];
};

struct ModelSubmesh 
{
    uint32 startIndex;
    uint32 numIndices;
    uint32 materialId;
};

struct ModelMesh 
{
    struct CpuBuffers
    {
        RelativePtr<uint8>  vertexBuffers[kModelMaxVertexBuffersPerShader];    // void cast struct for each vbuff (count=numVertexBuffers)
        RelativePtr<uint32> indexBuffer;
    };

    struct GpuBuffers
    {
        GfxBufferHandle vertexBuffers[kModelMaxVertexBuffersPerShader];
        GfxBufferHandle indexBuffer;
    };

    String32 name;
    uint32 numSubmeshes;
    uint32 numVertices;
    uint32 numIndices;
    uint32 numVertexBuffers;
    RelativePtr<ModelSubmesh> submeshes;
    CpuBuffers cpuBuffers;
    GpuBuffers gpuBuffers;
};

struct ModelNode 
{
    String32 name;
    uint32 meshId;       // =0 if it's not renderable
    uint32 parentId;     // index to GfxModelData::nodes
    uint32 numChilds;
    Transform3D localTransform;
    AABB bounds;
    RelativePtr<uint32> childIds;     // indices to GfxModelData::nodes
};

struct Model 
{
    uint32 numMeshes;
    uint32 numNodes;
    uint32 numMaterials;
    uint32 numMaterialTextures;

    Transform3D rootTransform;

    RelativePtr<ModelNode> nodes;
    RelativePtr<ModelMesh> meshes;
    RelativePtr<RelativePtr<ModelMaterial>> materials;
    ModelGeometryLayout layout;
};

// provide this for loading "model" asset
// if layout is zero initialized, default layout will be used:
//  buffer #1: position/normal/uv/color
struct ModelLoadParams 
{
    ModelGeometryLayout layout;
    GfxBufferUsage vertexBufferUsage = GfxBufferUsage::Default;
    GfxBufferUsage indexBufferUsage = GfxBufferUsage::Default;
};

API AssetHandleModel assetLoadModel(const char* path, const ModelLoadParams& params, AssetBarrier barrier = AssetBarrier());
API Model* assetGetModel(AssetHandleModel modelHandle);  

namespace _private
{
    bool assetInitializeModelManager();
    void assetReleaseModelManager();
} // _private
