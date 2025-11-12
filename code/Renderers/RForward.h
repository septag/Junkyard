#pragma once

#include "../Core/Base.h"
#include "../Core/MathTypes.h"

#include "../Common/CommonTypes.h"

struct GfxCommandBuffer;
struct Camera;
struct GfxVertexInputAttributeDesc;
struct ShaderLoadParams;

struct RLightBounds
{
    Float3 position;
    float radius;
};

struct RLightProps
{
    Float4 color;  
};

enum class RDebugMode
{
    None = 0,
    LightCull
};

struct RGeometrySubChunk
{
    uint32 startIndex;
    uint32 numIndices;
    GfxImageHandle baseColorImg;
};

struct RGeometryChunk
{
    Mat4 localToWorldMat;

    GfxBufferHandle posVertexBuffer;
    uint64 posVertexBufferOffset;

    GfxBufferHandle lightingVertexBuffer;
    uint64 lightingVertexBufferOffset;

    GfxBufferHandle indexBuffer;
    uint64 indexBufferOffset;

    RGeometrySubChunk* subChunks;
    uint32 numSubChunks;

    RGeometryChunk* nextChunk;

    void AddSubChunk(const RGeometrySubChunk& subChunk);
    void AddSubChunks(uint32 numSubChunks, const RGeometrySubChunk* subChunks);
};

namespace R 
{
    bool Initialize();
    void Release();

    void GetCompatibleLayout(uint32 maxAttributes, GfxVertexInputAttributeDesc* outAtts, uint32 maxStrides, uint32* outStrides);

    void SetLocalLights(uint32 numLights, const RLightBounds* bounds, const RLightProps* props);
    void SetAmbientLight(Float4 skyAmbientColor, Float4 groundAmbientColor);
    void SetSunLight(Float3 direction, Float4 color);

    void NewFrame();
    void Update(GfxCommandBuffer& cmd, const Camera& cam);
    void Render(GfxCommandBuffer& cmd, GfxImageHandle finalColorImage, GfxImageHandle finalDepthImage, 
                RDebugMode debugMode = RDebugMode::None);

    RGeometryChunk* NewGeometryChunk();
} // R
