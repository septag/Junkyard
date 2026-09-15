#include "RenderViewport.h"

#include "../Common/Application.h"

namespace RenderViewport
{
    static void _RecreateImages(RenderViewportContext* viewport, uint16 width, uint16 height)
    {
        ASSERT(viewport);

        viewport->width = Max<uint16>(width, 1);
        viewport->height = Max<uint16>(height, 1);
        viewport->requestedWidth = viewport->width;
        viewport->requestedHeight = viewport->height;

        GfxBackend::DestroyImage(viewport->colorImage);
        GfxBackend::DestroyImage(viewport->depthImage);

        if (viewport->useImGuiViewport) {
            GfxImageDesc colorDesc {
                .width = viewport->width,
                .height = viewport->height,
                .multisampleFlags = viewport->msaa,
                .format = viewport->colorFormat != GfxFormat::Undefined ? viewport->colorFormat : GfxBackend::GetSwapchainFormat(),
                .usageFlags = GfxImageUsageFlags::Sampled|GfxImageUsageFlags::ColorAttachment,
                .arena = GfxMemoryArena::DynamicImageGPU
            };
            viewport->colorImage = GfxBackend::CreateImage(colorDesc);
        }

        if (viewport->depthFormat != GfxFormat::Undefined) {
            GfxImageDesc depthDesc {
                .width = viewport->width,
                .height = viewport->height,
                .multisampleFlags = viewport->msaa,
                .format = viewport->depthFormat,
                .usageFlags = GfxImageUsageFlags::DepthStencilAttachment | 
                              (viewport->sampleDepth ? GfxImageUsageFlags::Sampled : GfxImageUsageFlags::TransientAttachment),
                .arena = GfxMemoryArena::DynamicImageGPU
            };
            viewport->depthImage = GfxBackend::CreateImage(depthDesc);
        }
    }

    void Initialize(RenderViewportContext* viewport, const RenderViewportDesc& desc)
    {
        ASSERT(viewport);
        *viewport = {};
        viewport->name = desc.name ? desc.name : "Viewport";
        viewport->useImGuiViewport = desc.useImGuiViewport;
        viewport->colorFormat = desc.colorFormat;
        viewport->depthFormat = desc.depthFormat;
        viewport->msaa = desc.msaa;
        viewport->sampleDepth = desc.sampleDepth;

        _RecreateImages(viewport, App::GetFramebufferWidth(), App::GetFramebufferHeight());
    }

    void Release(RenderViewportContext* viewport)
    {
        ASSERT(viewport);
        GfxBackend::DestroyImage(viewport->colorImage);
        GfxBackend::DestroyImage(viewport->depthImage);
        *viewport = {};
    }

    void OnFramebufferResized(RenderViewportContext* viewport, uint16 width, uint16 height)
    {
        ASSERT(viewport);
        if (!viewport->useImGuiViewport)
            _RecreateImages(viewport, width, height);
    }

    void PrepareRenderTargets(RenderViewportContext* viewport)
    {
        ASSERT(viewport);
        if (viewport->requestedWidth != viewport->width || viewport->requestedHeight != viewport->height)
            _RecreateImages(viewport, viewport->requestedWidth, viewport->requestedHeight);
    }

    RectInt GetViewportRect(const RenderViewportContext& viewport)
    {
        if (viewport.useImGuiViewport) {
            return RectInt(int(viewport.imguiPos.x), int(viewport.imguiPos.y), 
                           int(viewport.imguiPos.x + viewport.width), int(viewport.imguiPos.y + viewport.height));
        }
        else {
            return RectInt(0, 0, viewport.width, viewport.height);
        }
    }

    GfxBackendRenderPass MakeRenderPass(const RenderViewportContext& viewport, Color4u clearColor, float clearDepth)
    {
        GfxBackendRenderPass pass {
            .numAttachments = viewport.useImGuiViewport ? 1u : 0u,
            .colorAttachments = {{
                .image = viewport.colorImage,
                .clear = true,
                .clearValue = {
                    .color = Color4u::ToFloat4(clearColor)
                }
            }},
            .depthAttachment = {
                .image = viewport.depthImage,
                .clear = true,
                .clearValue = {
                    .depth = clearDepth
                }
            },
            .swapchain = !viewport.useImGuiViewport,
            .hasDepth = viewport.depthImage.IsValid()
        };
        return pass;
    }

    void SetViewportAndScissor(GfxCommandBuffer& cmd, const RenderViewportContext& viewport)
    {
        GfxViewport gfxViewport {
            .x = 0,
            .y = 0,
            .width = float(viewport.width),
            .height = float(viewport.height)
        };
        RectInt scissor(0, 0, viewport.width, viewport.height);
        cmd.SetViewports(0, 1, &gfxViewport);
        cmd.SetScissors(0, 1, &scissor);
    }

    void TransitionToRenderTarget(GfxCommandBuffer& cmd, const RenderViewportContext& viewport, GfxImageTransitionFlags depthFlags)
    {
        if (viewport.useImGuiViewport)
            cmd.TransitionImage(viewport.colorImage, GfxImageTransition::RenderTarget);
        if (viewport.depthImage.IsValid())
            cmd.TransitionImage(viewport.depthImage, GfxImageTransition::RenderTarget, depthFlags);
    }

    void TransitionToShaderRead(GfxCommandBuffer& cmd, const RenderViewportContext& viewport)
    {
        if (viewport.useImGuiViewport)
            cmd.TransitionImage(viewport.colorImage, GfxImageTransition::ShaderRead);
    }

    Mat4 GetClipTransform(const RenderViewportContext& viewport)
    {
        return viewport.useImGuiViewport ? MAT4_IDENT : GfxBackend::GetSwapchainTransformMat();
    }

    bool IsFullscreen(const RenderViewportContext& viewport)
    {
        return !viewport.useImGuiViewport;
    }

    bool IsImGuiPanel(const RenderViewportContext& viewport)
    {
        return viewport.useImGuiViewport;
    }
}
