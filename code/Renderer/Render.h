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
    LightCull,
    SunShadowMap
};

struct RGeometrySubChunk
{
    uint32 startIndex;
    uint32 numIndices;
    GfxImageHandle baseColorImg;
    bool hasAlphaMask;
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

DEFINE_HANDLE(RViewHandle);

struct RView
{
    RViewHandle mHandle;
    uint32 mThreadId;

    void SetCamera(const Camera& cam, Float2 viewSize);
    void SetLocalLights(uint32 numLights, const RLightBounds* bounds, const RLightProps* props);
    void SetAmbientLight(Float4 skyAmbientColor, Float4 groundAmbientColor);
    void SetSunLight(Float3 direction, Float4 color, GfxImageHandle shadowMapImage, const Mat4& sunlightWorldToClipMat);

    RGeometryChunk* NewGeometryChunk();

    Mat4 GetWorldToClipMat() const;
};

enum class RViewType
{
    FwdLight,
    ShadowMap
};

namespace R 
{
    bool Initialize();
    void Release();

    void GetCompatibleLayout(uint32 maxAttributes, GfxVertexInputAttributeDesc* outAtts, uint32 maxStrides, uint32* outStrides);

    RView CreateView(RViewType viewType);
    void DestroyView(RView& view);

    void NewFrame();

    namespace FwdLight
    {
        void Update(RView& view, GfxCommandBuffer& cmd);
        void Render(RView& view, GfxCommandBuffer& cmd, GfxImageHandle finalColorImage, GfxImageHandle finalDepthImage, 
                    RDebugMode debugMode = RDebugMode::None);
    }

    namespace ShadowMap
    {
        void Update(RView& view, GfxCommandBuffer& cmd);
        void Render(RView& view, GfxCommandBuffer& cmd, GfxImageHandle shadowMapDepthImage);
    }
} // R
