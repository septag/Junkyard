#pragma once

#include "../Core/Base.h"
#include "../Core/MathTypes.h"
#include "../Common/Application.h"
#include "../Graphics/GfxBackend.h"

struct RenderViewportDesc
{
    const char* name = "Viewport";
    bool useImGuiViewport = false;
    GfxFormat colorFormat = GfxFormat::Undefined;
    GfxFormat depthFormat = GfxFormat::Undefined;
    GfxMultiSampleCount msaa = GfxMultiSampleCount::SampleCount1;
    bool sampleDepth = false;
};

struct RenderViewportContext
{
    const char* name = "Viewport";
    bool useImGuiViewport = false;

    uint16 width = 1;
    uint16 height = 1;
    uint16 requestedWidth = 1;
    uint16 requestedHeight = 1;

    bool hovered = false;
    bool focused = false;
    bool visible = true;
    bool inputCaptured = false;

    Float2 imguiPos;
    Float2 imguiSize;

    GfxFormat colorFormat = GfxFormat::Undefined;
    GfxFormat depthFormat = GfxFormat::Undefined;
    GfxMultiSampleCount msaa = GfxMultiSampleCount::SampleCount1;
    bool sampleDepth = false;

    GfxImageHandle colorImage;
    GfxImageHandle depthImage;
};

namespace RenderViewport
{
    void Initialize(RenderViewportContext* viewport, const RenderViewportDesc& desc);
    void Release(RenderViewportContext* viewport);

    void OnFramebufferResized(RenderViewportContext* viewport, uint16 width, uint16 height);
    void PrepareRenderTargets(RenderViewportContext* viewport);
    RectInt GetViewportRect(const RenderViewportContext& viewport);

    GfxBackendRenderPass MakeRenderPass(const RenderViewportContext& viewport, Color4u clearColor, float clearDepth = 1.0f);
    void SetViewportAndScissor(GfxCommandBuffer& cmd, const RenderViewportContext& viewport);
    void TransitionToRenderTarget(GfxCommandBuffer& cmd, const RenderViewportContext& viewport,
                                  GfxImageTransitionFlags depthFlags = GfxImageTransitionFlags::DepthWrite);
    void TransitionToShaderRead(GfxCommandBuffer& cmd, const RenderViewportContext& viewport);

    Mat4 GetClipTransform(const RenderViewportContext& viewport);
    bool IsFullscreen(const RenderViewportContext& viewport);
    bool IsImGuiPanel(const RenderViewportContext& viewport);
}
