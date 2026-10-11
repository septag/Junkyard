#pragma once

// Every view owns the resources of its render path, so any number of views can be rendered in the same frame
// FwdLight views create their own Color and Depth framebuffers based on MSAA setting, sized by RView::SetCamera viewSize.
// They are (re)created in FwdLight::Update when the view size changes
// So for FwdLight::Render function, passing finalColorImage and finalDepthImage means that we want to resolve MSAA to those buffers
// If finalColorImage is empty, then it renders/resolves to swapchain

#include "../Core/Base.h"
#include "../Core/MathTypes.h"

#include "../Common/CommonTypes.h"

struct GfxCommandBuffer;
struct Camera;
struct GfxVertexInputAttributeDesc;
struct ShaderLoadParams;
struct GeometryVertexLayout;

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
    Color4u tintColor = COLOR4U_WHITE;
    bool hasAlphaMask;
    uint32 _drawItemIndex;  // Internal use only
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

// Each type is a separate render path with its own resources. Variations of a path should not be new types
enum class RViewType
{
    FwdLight,       // Forward+ lighting. Owns MSAA targets, light culling and per-frame buffers
    ShadowMap       // Depth only. Depth target is provided in ShadowMap::Render
};

namespace R 
{
    bool Initialize();
    void Release();

    void GetCompatibleLayout(GeometryVertexLayout& outLayout);
    RView CreateView(RViewType viewType);
    void DestroyView(RView& view);

    void NewFrame();
    bool IsEnabled();

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
