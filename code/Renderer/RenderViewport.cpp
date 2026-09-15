#include "RenderViewport.h"

#include "../Common/Application.h"
#include "../ImGui/ImGuiMain.h"
#include "../ImGui/ImGuizmo.h"

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

        GfxImageDesc depthDesc {
            .width = viewport->width,
            .height = viewport->height,
            .multisampleFlags = viewport->msaa,
            .format = viewport->depthFormat != GfxFormat::Undefined ? viewport->depthFormat : GfxBackend::GetValidDepthStencilFormat(),
            .usageFlags = GfxImageUsageFlags::DepthStencilAttachment | 
                          (viewport->sampleDepth ? GfxImageUsageFlags::Sampled : GfxImageUsageFlags::TransientAttachment),
            .arena = GfxMemoryArena::DynamicImageGPU
        };
        viewport->depthImage = GfxBackend::CreateImage(depthDesc);
    }

    void Initialize(RenderViewportContext* viewport, const RenderViewportDesc& desc)
    {
        ASSERT(viewport);
        *viewport = {};
        viewport->name = desc.name ? desc.name : "Viewport";
        viewport->useImGuiViewport = desc.useImGuiViewport && ImGui::IsEnabled();
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

    void DrawImGui(RenderViewportContext* viewport)
    {
        ASSERT(viewport);
        if (!viewport->useImGuiViewport)
            return;

        ImGui::SetNextWindowDockID(ImGui::GetID("MainDockSpace"), ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(viewport->name);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        viewport->requestedWidth = Max<uint16>(uint16(avail.x), 1);
        viewport->requestedHeight = Max<uint16>(uint16(avail.y), 1);
        viewport->focused = ImGui::IsWindowFocused();
        viewport->visible = !ImGui::IsWindowCollapsed();
        viewport->imguiPos = Float2(pos.x, pos.y);
        viewport->imguiSize = Float2(avail.x, avail.y);

        ImGui::Image(ImTextureID(uint64(viewport->colorImage.mId)), avail);
        viewport->hovered = ImGui::IsItemHovered();

        ImGui::End();
        ImGui::PopStyleVar();
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
            .hasDepth = true
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

    bool CanReceiveMouseInput(const RenderViewportContext& viewport)
    {
        if (!viewport.useImGuiViewport)
            return true;

        return viewport.hovered && !ImGuizmo::IsOver() && !ImGui::IsAnyItemActive();
    }

    bool CanReceiveMouseInput(RenderViewportContext* viewport, const AppEvent& ev)
    {
        ASSERT(viewport);
        if (!viewport->useImGuiViewport)
            return true;

        InputMouseButton activeButton = InputMouseButton::Right;
        if constexpr (PLATFORM_ANDROID)
            activeButton = InputMouseButton::Left;

        const bool canStartInput = viewport->hovered && !ImGuizmo::IsOver() && !ImGui::IsAnyItemActive();

        switch (ev.type) {
        case AppEventType::MouseDown:
            if (ev.mouseButton != activeButton)
                return false;
            if (canStartInput)
                viewport->inputCaptured = true;
            return canStartInput;

        case AppEventType::MouseMove:
            return viewport->inputCaptured || canStartInput;

        case AppEventType::MouseScroll:
            return canStartInput;

        case AppEventType::MouseUp: {
                if (ev.mouseButton != activeButton)
                    return false;
                bool wasCaptured = viewport->inputCaptured;
                viewport->inputCaptured = false;
                return wasCaptured || canStartInput;
            }

        case AppEventType::MouseLeave:
            viewport->inputCaptured = false;
            return false;

        default:
            return canStartInput;
        }
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
