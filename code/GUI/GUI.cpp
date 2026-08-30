#include "../Core/MathAll.h"
#include "../Core/Log.h"
#include "../Core/TracyHelper.h"

PRAGMA_DIAGNOSTIC_PUSH()
    PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4244)   // type conversion, possible loss of data warning
    PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4305)   // truncation warning
    PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4201)   // non-standard extension
    PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4100)   // unreferenced parameter
    PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4189)   // local var initialized but not referenced
    PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4389)   // signed/unisnged mismatch
    #define CLAY_IMPLEMENTATION
    #include "../External/clay/clay.h"
PRAGMA_DIAGNOSTIC_POP()

#include "GUI.h"

#include "../Common/Application.h"

#include "../Assets/AssetManager.h"
#include "../Assets/Shader.h"
#include "../Assets/Image.h"
#include "../Assets/Font.h"
#include "../Graphics/TextBuilder.h"

#include "../Engine.h"

static constexpr uint32 GUI_VERTICES_POOL_SIZE = 16*1000;
static constexpr uint32 GUI_INDICES_POOL_SIZE =  GUI_VERTICES_POOL_SIZE*3;
static constexpr uint32 GUI_MAX_SCISSOR_DEPTH = 8;

struct GUIVertex
{
    Float2 pos;
    Float2 uv;
    Color4u color;
};

struct GUIDrawCommand
{
    uint32 startIndex;
    uint32 numIndices;
    uint32 vertexOffset;
    GfxPipelineHandle pipeline;
    GfxImageHandle image;
    RectInt scissor;
};

struct GUIContext
{
    MemProxyAllocator alloc;

    AssetHandleShader shapeShader;
    AssetHandleShader textShader;
    AssetHandleFont font;

    GfxPipelineHandle shapePipeline;
    GfxPipelineLayoutHandle shapePipelineLayout;
    GfxPipelineHandle textPipeline;
    GfxPipelineLayoutHandle textPipelineLayout;
    GfxSamplerHandle sampler;
    GfxBufferHandle vertexBuffer;
    GfxBufferHandle indexBuffer;
    GfxMultiSampleCount msaa = GfxMultiSampleCount::SampleCount1;

    uint32 maxVertices;
    uint32 maxIndices;

    RectFloat mainViewport;
    Clay_Vector2 mousePos;
    Clay_Vector2 mouseScroll;
    float frameBufferScale;
    bool mouseDown;

    void* clayMemory;
    bool inFlight;
};

static GUIContext gGUI;

namespace GUI
{
    static void _ClayHandleErrors(Clay_ErrorData errorData)
    {
        LOG_ERROR("GUI: %s", errorData.errorText.chars);
    }

    // Clay calls this to measure every text run (and each word/space when wrapping)
    static Clay_Dimensions _MeasureText(Clay_StringSlice text, Clay_TextElementConfig* config, [[maybe_unused]] void* userData)
    {
        // Font is still a placeholder until the group finishes loading, so there is nothing to measure yet
        if (text.length == 0)
            return {};

        AssetObjPtrScope<FontData> font(gGUI.font);
        if (font.IsNull())
            return {};

        float fontSize = config->fontSize ? float(config->fontSize) : float(font->size);
        Float2 size = TextBuilder::CalculateTextSize(*font, fontSize/float(font->size), text.chars, uint32(text.length));

        // TextBuilder doesn't know about clay's extra letter spacing
        if (config->letterSpacing && text.length > 1)
            size.x += float(config->letterSpacing) * float(text.length - 1);

        if (config->lineHeight)
            size.y = float(config->lineHeight);
        return { size.x, size.y };
    }

    static void _OnEventCallback(const AppEvent& ev, [[maybe_unused]] void* userData)
    {
        switch (ev.type) {
            case AppEventType::MouseDown:
                if (ev.mouseButton == InputMouseButton::Left)
                    gGUI.mouseDown = true;
                break;

            case AppEventType::MouseUp:
                if (ev.mouseButton == InputMouseButton::Left)
                    gGUI.mouseDown = false;
                break;

            case AppEventType::MouseScroll:
                gGUI.mouseScroll = { ev.scrollX, ev.scrollY };
                break;

            case AppEventType::MouseMove:
                gGUI.mousePos = { ev.mouseX * gGUI.frameBufferScale, ev.mouseY * gGUI.frameBufferScale };
                break;

            case AppEventType::Resized:
                gGUI.mainViewport = RectFloat(0, 0, App::GetFramebufferWidth(), App::GetFramebufferHeight());
                break;
            default:
                break;
        }
    }

    static void _GrowGeometryBuffers(uint32 numVertices, uint32 numIndices)
    {
        if (numVertices > gGUI.maxVertices) {
            gGUI.maxVertices = AlignValue(numVertices, GUI_VERTICES_POOL_SIZE);
            GfxBackend::DestroyBuffer(gGUI.vertexBuffer);
            GfxBufferDesc vertexBufferDesc {
                .sizeBytes = gGUI.maxVertices*sizeof(GUIVertex),
                .usageFlags = GfxBufferUsageFlags::TransferDst|GfxBufferUsageFlags::Vertex,
                .perFrameUpdates = true
            };
            gGUI.vertexBuffer = GfxBackend::CreateBuffer(vertexBufferDesc);

            LOG_VERBOSE("GUI vertex capacity increased to maximum %u vertices", gGUI.maxVertices);
        }

        if (numIndices > gGUI.maxIndices) {
            gGUI.maxIndices = AlignValue(numIndices, GUI_INDICES_POOL_SIZE);
            GfxBackend::DestroyBuffer(gGUI.indexBuffer);            
            GfxBufferDesc indexBufferDesc {
                .sizeBytes = gGUI.maxIndices*sizeof(uint32),
                .usageFlags = GfxBufferUsageFlags::TransferDst|GfxBufferUsageFlags::Index,
                .perFrameUpdates = true
            };
            gGUI.indexBuffer = GfxBackend::CreateBuffer(indexBufferDesc);

            LOG_VERBOSE("GUI index capacity increased to maximum %u indices", gGUI.maxIndices);
        }
    }

    static void _InitializeGraphicsResources(void*)
    {
        {
            AssetObjPtrScope<GfxShader> shader(gGUI.textShader);
            if (shader.IsNull()) {
                ASSERT_MSG(0, "GUI: Text shader not loaded");
                return;
            }

            // No depth attachment: GUI is drawn into the swapchain with a color-only render pass
            TextDrawGraphicsObjects textObjects = TextBuilder::HelperCreateGraphicsObjects(*shader, TextEffect::None,
                                                                                           GfxBackend::GetSwapchainFormat(),
                                                                                           GfxFormat::Undefined);
            gGUI.textPipeline = textObjects.pipeline;
            gGUI.textPipelineLayout = textObjects.pipelineLayout;
            gGUI.sampler = textObjects.sampler;
        }


        {
            AssetObjPtrScope<GfxShader> shader(gGUI.shapeShader);
            if (shader.IsNull()) {
                ASSERT_MSG(0, "GUI: Shape shader not loaded");
                return;
            }

            GfxVertexBufferBindingDesc vertexBufferBindingDesc {
                .binding = 0,
                .stride = sizeof(GUIVertex),
                .inputRate = GfxVertexInputRate::Vertex
            };

            GfxVertexInputAttributeDesc vertexInputAttDescs[] = {
                {
                    .semantic = "POSITION",
                    .binding = 0,
                    .format = GfxFormat::R32G32_SFLOAT,
                    .offset = offsetof(GUIVertex, pos)
                },
                {
                    .semantic = "TEXCOORD",
                    .binding = 0,
                    .format = GfxFormat::R32G32_SFLOAT,
                    .offset = offsetof(GUIVertex, uv)
                },
                {
                    .semantic = "COLOR",
                    .binding = 0,
                    .format = GfxFormat::R8G8B8A8_UNORM,
                    .offset = offsetof(GUIVertex, color)
                }
            };

            const GfxPipelineLayoutDesc::PushConstant pushConstant {
                .name = "PerFrameData",
                .stagesUsed = GfxShaderStage::Vertex,
                .size = sizeof(Mat4)
            };

            const GfxPipelineLayoutDesc::Binding layoutBindings[] {
                {
                    .name = "ShapeTexture",
                    .type = GfxDescriptorType::SampledImage,
                    .stagesUsed = GfxShaderStage::Fragment
                },
                {
                    .name = "ShapeSampler",
                    .type = GfxDescriptorType::Sampler,
                    .stagesUsed = GfxShaderStage::Fragment
                }
            };

            GfxPipelineLayoutDesc layoutDesc {
                .type = GfxPipelineLayoutType::PushDescriptor,
                .numBindings = CountOf(layoutBindings),
                .bindings = layoutBindings,
                .numPushConstants = 1,
                .pushConstants = &pushConstant
            };
            gGUI.shapePipelineLayout = GfxBackend::CreatePipelineLayout(*shader, layoutDesc);

            GfxGraphicsPipelineDesc pipelineDesc {
                .numVertexInputAttributes = CountOf(vertexInputAttDescs),
                .vertexInputAttributes = vertexInputAttDescs,
                .numVertexBufferBindings = 1,
                .vertexBufferBindings = &vertexBufferBindingDesc,
                .rasterizer = {
                    .frontFace = GfxFrontFace::Clockwise
                },
                .blend {
                    .numAttachments = 1,
                    .attachments = GfxBlendAttachmentDesc::GetAlphaBlending()
                },
                .msaa = {
                    .sampleCount = gGUI.msaa,
                },
                .numColorAttachments = 1,
                .colorAttachmentFormats = {GfxBackend::GetSwapchainFormat()}
            };

            gGUI.shapePipeline = GfxBackend::CreateGraphicsPipeline(*shader, gGUI.shapePipelineLayout, pipelineDesc);
        }

        // Buffers
        {
            gGUI.maxVertices = GUI_VERTICES_POOL_SIZE;
            gGUI.maxIndices = GUI_INDICES_POOL_SIZE;

            GfxBufferDesc vertexBufferDesc {
                .sizeBytes = GUI_VERTICES_POOL_SIZE*sizeof(GUIVertex),
                .usageFlags = GfxBufferUsageFlags::TransferDst|GfxBufferUsageFlags::Vertex,
                .perFrameUpdates = true
            };
            gGUI.vertexBuffer = GfxBackend::CreateBuffer(vertexBufferDesc);

            GfxBufferDesc indexBufferDesc {
                .sizeBytes = GUI_VERTICES_POOL_SIZE*sizeof(uint32),
                .usageFlags = GfxBufferUsageFlags::TransferDst|GfxBufferUsageFlags::Index,
                .perFrameUpdates = true
            };
            gGUI.indexBuffer = GfxBackend::CreateBuffer(indexBufferDesc);
        }

    }

    // RECTANGLE and IMAGE commands share the same pipeline, so consecutive batches often don't need a re-bind
    static void _SwitchPipeline(GfxCommandBuffer cmd, GfxPipelineHandle pipeline, GfxPipelineHandle& inoutCurrentPipeline)
    {
        if (pipeline != inoutCurrentPipeline) {
            cmd.BindPipeline(pipeline);
            inoutCurrentPipeline = pipeline;
        }
    }

    INLINE Color4u _ToColor(Clay_Color c)
    {
        return Color4u(uint8(c.r), uint8(c.g), uint8(c.b), uint8(c.a));
    }

    INLINE bool _IsSameRect(const RectInt& a, const RectInt& b)
    {
        return a.xmin == b.xmin && a.ymin == b.ymin && a.xmax == b.xmax && a.ymax == b.ymax;
    }

    // Our tessellation uses a single radius for the whole shape, so per-corner radii collapse into one
    INLINE float _CornerRadius(const Clay_CornerRadius& radius)
    {
        return Max(Max(radius.topLeft, radius.topRight), Max(radius.bottomLeft, radius.bottomRight));
    }

    // Quad vertices/indices are generated with the same layout and winding as TextBuilder::CreateText
    static void _PushQuad(Array<GUIVertex>& vertices, Array<uint32>& indices,
                          const Clay_BoundingBox& box, Color4u color, Float2 uvMin, Float2 uvMax)
    {
        uint32 vertexIndex = vertices.Count();
        GUIVertex* v = vertices.PushBatch(4);
        v[0] = { .pos = Float2(box.x, box.y),                        .uv = Float2(uvMin.x, uvMin.y), .color = color };
        v[1] = { .pos = Float2(box.x, box.y + box.height),           .uv = Float2(uvMin.x, uvMax.y), .color = color };
        v[2] = { .pos = Float2(box.x + box.width, box.y + box.height),.uv = Float2(uvMax.x, uvMax.y), .color = color };
        v[3] = { .pos = Float2(box.x + box.width, box.y),            .uv = Float2(uvMax.x, uvMin.y), .color = color };

        uint32* idx = indices.PushBatch(6);
        idx[0] = vertexIndex;       idx[1] = vertexIndex + 1;   idx[2] = vertexIndex + 2;
        idx[3] = vertexIndex + 2;   idx[4] = vertexIndex + 3;   idx[5] = vertexIndex;
    }

    // A triangle fan around the center, with every corner tessellated into an arc.
    // Corners are walked in the same rotational direction as _PushQuad (TL->BL->BR->TR)
    static void _PushQuadRoundedCorner(Array<GUIVertex>& vertices, Array<uint32>& indices,
                                       const Clay_BoundingBox& box, Color4u color, Float2 uvMin, Float2 uvMax,
                                       float cornerRadius)
    {
        cornerRadius = Min(cornerRadius, Min(box.width, box.height)*0.5f);
        ASSERT(cornerRadius > 0);

        const uint32 numSegments = Clamp(uint32(cornerRadius*0.5f), 2u, 8u);
        const uint32 numRingVertices = 4*(numSegments + 1);

        const Float2 cornerCenters[4] = {
            Float2(box.x + cornerRadius, box.y + cornerRadius),                             // top-left
            Float2(box.x + cornerRadius, box.y + box.height - cornerRadius),                // bottom-left
            Float2(box.x + box.width - cornerRadius, box.y + box.height - cornerRadius),    // bottom-right
            Float2(box.x + box.width - cornerRadius, box.y + cornerRadius)                  // top-right
        };

        auto CalcUV = [&box, uvMin, uvMax](Float2 p)->Float2
        {
            return Float2(uvMin.x + ((p.x - box.x)/box.width)*(uvMax.x - uvMin.x),
                          uvMin.y + ((p.y - box.y)/box.height)*(uvMax.y - uvMin.y));
        };

        uint32 centerIndex = vertices.Count();
        GUIVertex* v = vertices.PushBatch(numRingVertices + 1);

        Float2 center(box.x + box.width*0.5f, box.y + box.height*0.5f);
        v[0] = { .pos = center, .uv = CalcUV(center), .color = color };

        uint32 numVertices = 1;
        for (uint32 corner = 0; corner < 4; corner++) {
            // Each corner sweeps a quarter turn, the top-left one starts at 270 degrees
            float startAngle = 3.0f*M_HALFPI - float(corner)*M_HALFPI;
            for (uint32 s = 0; s <= numSegments; s++) {
                float angle = startAngle - M_HALFPI*(float(s)/float(numSegments));
                Float2 p = cornerCenters[corner] + Float2(M::Cos(angle), M::Sin(angle))*cornerRadius;
                v[numVertices++] = { .pos = p, .uv = CalcUV(p), .color = color };
            }
        }
        ASSERT(numVertices == numRingVertices + 1);

        uint32* idx = indices.PushBatch(numRingVertices*3);
        for (uint32 s = 0; s < numRingVertices; s++) {
            idx[s*3 + 0] = centerIndex;
            idx[s*3 + 1] = centerIndex + 1 + s;
            idx[s*3 + 2] = centerIndex + 1 + ((s + 1) % numRingVertices);
        }
    }
} // UI

bool GUI::Initialize()
{
    Engine::HelperInitializeProxyAllocator(&gGUI.alloc, "GUI");
    Engine::RegisterProxyAllocator(&gGUI.alloc);

    AssetGroup loadAssetGroup = Engine::RegisterInitializeResources(_InitializeGraphicsResources);
    gGUI.shapeShader = Shader::Load("/shaders/GUIShape.hlsl", ShaderLoadParams(), loadAssetGroup);
    gGUI.textShader = Shader::Load("/shaders/DrawText.hlsl", ShaderLoadParams(), loadAssetGroup);
    gGUI.font = Font::Load("/fonts/arial.jfnt", loadAssetGroup);

    App::RegisterEventsCallback(_OnEventCallback);

    // Resized event only fires on actual resizes, so seed the viewport with the current framebuffer size
    gGUI.mainViewport = RectFloat(0, 0, App::GetFramebufferWidth(), App::GetFramebufferHeight());

    size_t memSize = Clay_MinMemorySize();
    gGUI.clayMemory = Mem::Alloc(memSize, &gGUI.alloc);
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(memSize, gGUI.clayMemory);

    Clay_Initialize(arena, {gGUI.mainViewport.Width(), gGUI.mainViewport.Height()}, {_ClayHandleErrors, nullptr});
    Clay_SetMeasureTextFunction(_MeasureText, nullptr);

    gGUI.frameBufferScale = App::GetWindowDPIScale();
    LOG_INFO("(init) GUI initialized");

    return true;
}

void GUI::Release()
{
    GfxBackend::DestroyBuffer(gGUI.vertexBuffer);
    GfxBackend::DestroyBuffer(gGUI.indexBuffer);
    GfxBackend::DestroySampler(gGUI.sampler);
    GfxBackend::DestroyPipeline(gGUI.textPipeline);
    GfxBackend::DestroyPipelineLayout(gGUI.textPipelineLayout);
    GfxBackend::DestroyPipeline(gGUI.shapePipeline);
    GfxBackend::DestroyPipelineLayout(gGUI.shapePipelineLayout);

    Mem::Free(gGUI.clayMemory, &gGUI.alloc);
}

void GUI::Begin()
{
    PROFILE_ZONE("GUI.Begin");

    ASSERT_MSG(!gGUI.inFlight, "End() should be called before Begin()");
    Clay_SetLayoutDimensions({ gGUI.mainViewport.Width(), gGUI.mainViewport.Height() });
    Clay_SetPointerState(gGUI.mousePos, gGUI.mouseDown);
    Clay_UpdateScrollContainers(true, gGUI.mouseScroll, Engine::GetFrameTime());

    // Clay treats the scroll as a per-frame delta, so it has to be consumed here
    gGUI.mouseScroll = {};

    Clay_BeginLayout();
    gGUI.inFlight = true;
}

void GUI::End(GfxCommandBuffer cmd)
{
    PROFILE_ZONE("GUI.End");
    ASSERT_MSG(gGUI.inFlight, "Begin() is not called");
    ASSERT_MSG(cmd.mIsRecording && !cmd.mIsInRenderPass,
               "%s must be called while CommandBuffer is recording and not in the RenderPass", __FUNCTION__);

    Clay_RenderCommandArray renderCommands = Clay_EndLayout(Engine::GetFrameTime());
    gGUI.inFlight = false;

    // Graphics resources are created after the initial asset group is loaded
    if (!gGUI.shapePipeline.IsValid() || !gGUI.textPipeline.IsValid())
        return;

    AssetObjPtrScope<FontData> font(gGUI.font);
    if (font.IsNull())
        return;
    AssetObjPtrScope<GfxImage> fontImage(font->atlas);
    if (fontImage.IsNull())
        return;

    static_assert(sizeof(GUIVertex) == sizeof(TextVertex), "GUIVertex should be layout compatible with TextVertex");

    MemTempAllocator tempAlloc;
    Array<GUIVertex> vertices(&tempAlloc);
    Array<uint32> indices(&tempAlloc);
    Array<GUIDrawCommand> drawCommands(&tempAlloc);
    drawCommands.Reserve(128);

    const GfxImageHandle whiteImage = Image::GetWhite1x1();
    Clay_RenderCommandType prevCommandType = CLAY_RENDER_COMMAND_TYPE_NONE;
    GfxImageHandle prevImage;

    // Clay emits SCISSOR_START/END pairs around clipped elements. They can nest, so keep a stack of
    // intersected rects instead of just resetting back to the full viewport on SCISSOR_END
    const RectInt viewportScissor(int(gGUI.mainViewport.xmin), int(gGUI.mainViewport.ymin),
                                  int(gGUI.mainViewport.xmax), int(gGUI.mainViewport.ymax));
    StaticArray<RectInt, GUI_MAX_SCISSOR_DEPTH> scissorStack;
    RectInt currentScissor = viewportScissor;
    RectInt prevScissor = viewportScissor;

    for (int i = 0; i < renderCommands.length; i++) {
        const Clay_RenderCommand& renderCommand = renderCommands.internalArray[i];
        const Clay_BoundingBox& box = renderCommand.boundingBox;
        const uint32 startIndex = indices.Count();
        GfxPipelineHandle pipeline;
        GfxImageHandle image;

        switch (renderCommand.commandType) {
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                const Clay_TextRenderData& textData = renderCommand.renderData.text;
                uint32 textLen = uint32(textData.stringContents.length);
                if (textLen == 0)
                    continue;

                pipeline = gGUI.textPipeline;
                image = fontImage->handle;

                float fontSize = textData.fontSize ? float(textData.fontSize) : float(font->size);
                // CreateText shifts glyphs down by the descender, so pull the baseline up to put the
                // top of the line on the top of the bounding box
                Float2 pos(box.x, box.y - (font->ascender + font->descender)*fontSize);

                // CreateText can skip characters, so zero the batch first. Unwritten indices become
                // degenerate triangles instead of garbage
                uint32 numVertices = textLen*4;
                uint32 numIndices = textLen*6;
                uint32 vertexIndex = vertices.Count();
                GUIVertex* v = vertices.PushBatch(numVertices);
                memset(v, 0x0, numVertices*sizeof(GUIVertex));
                uint32* idx = indices.PushBatch(numIndices);
                memset(idx, 0x0, numIndices*sizeof(uint32));

                TextBuilder::CreateText((TextVertex*)v, numVertices, idx, numIndices,
                                        textData.stringContents.chars, textLen, *font, pos,
                                        TextAlignment::Left, _ToColor(textData.textColor),
                                        fontSize/float(font->size));

                // CreateText emits indices relative to its own output
                for (uint32 k = 0; k < numIndices; k++)
                    idx[k] += vertexIndex;
                break;
            }

            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                const Clay_RectangleRenderData& rectData = renderCommand.renderData.rectangle;
                pipeline = gGUI.shapePipeline;
                image = whiteImage;

                Color4u rectColor = _ToColor(rectData.backgroundColor);
                float cornerRadius = _CornerRadius(rectData.cornerRadius);
                if (cornerRadius > 0) {
                    _PushQuadRoundedCorner(vertices, indices, box, rectColor,
                                           Float2(0, 0), Float2(1.0f, 1.0f), cornerRadius);
                }
                else {
                    _PushQuad(vertices, indices, box, rectColor, Float2(0, 0), Float2(1.0f, 1.0f));
                }
                break;
            }

            case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
                const Clay_ImageRenderData& imageData = renderCommand.renderData.image;
                pipeline = gGUI.shapePipeline;
                image = imageData.imageData ? *reinterpret_cast<const GfxImageHandle*>(imageData.imageData) : whiteImage;
                if (!image.IsValid())
                    continue;

                // Default backgroundColor is all zeros, which means "untinted"
                Color4u tint = imageData.backgroundColor.a > 0 ? _ToColor(imageData.backgroundColor) : COLOR4U_WHITE;
                float cornerRadius = _CornerRadius(imageData.cornerRadius);
                if (cornerRadius > 0) {
                    _PushQuadRoundedCorner(vertices, indices, box, tint,
                                           Float2(0, 0), Float2(1.0f, 1.0f), cornerRadius);
                }
                else {
                    _PushQuad(vertices, indices, box, tint, Float2(0, 0), Float2(1.0f, 1.0f));
                }
                break;
            }

            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                const Clay_ClipRenderData& clipData = renderCommand.renderData.clip;
                RectInt rc = currentScissor;
                if (clipData.horizontal) {
                    rc.xmin = Max(int(box.x), rc.xmin);
                    rc.xmax = Min(int(box.x + box.width), rc.xmax);
                }
                if (clipData.vertical) {
                    rc.ymin = Max(int(box.y), rc.ymin);
                    rc.ymax = Min(int(box.y + box.height), rc.ymax);
                }

                // An inverted rect means everything is clipped away. Vulkan wants a non-negative extent
                rc.xmax = Max(rc.xmax, rc.xmin);
                rc.ymax = Max(rc.ymax, rc.ymin);

                ASSERT_MSG(!scissorStack.IsFull(), "Clip nesting is too deep");
                scissorStack.Push(currentScissor);
                currentScissor = rc;
                continue;
            }

            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
                ASSERT_MSG(!scissorStack.IsEmpty(), "SCISSOR_END without a matching SCISSOR_START");
                currentScissor = !scissorStack.IsEmpty() ? scissorStack.PopLast() : viewportScissor;
                continue;
            }

            default:
                ASSERT_MSG(0, "Not Implemented");
                continue;
        }

        // Naive batching: break the batch whenever the command type (pipeline), image or scissor changes
        uint32 numIndices = indices.Count() - startIndex;
        if (numIndices == 0)
            continue;

        if (drawCommands.IsEmpty() || renderCommand.commandType != prevCommandType || image != prevImage ||
            !_IsSameRect(currentScissor, prevScissor))
        {
            drawCommands.Push(GUIDrawCommand {
                .startIndex = startIndex,
                .numIndices = numIndices,
                .vertexOffset = 0,
                .pipeline = pipeline,
                .image = image,
                .scissor = currentScissor
            });
        }
        else {
            drawCommands.Last().numIndices += numIndices;
        }

        prevCommandType = renderCommand.commandType;
        prevImage = image;
        prevScissor = currentScissor;
    }

    if (drawCommands.IsEmpty())
        return;

    // Upload geometry. This cannot happen inside a render pass
    _GrowGeometryBuffers(vertices.Count(), indices.Count());
    {
        uint32 vertexSize = vertices.Count()*sizeof(GUIVertex);
        uint32 indexSize = indices.Count()*sizeof(uint32);
        GfxHelperBufferUpdateScope vertexBufferUpdate(cmd, gGUI.vertexBuffer, vertexSize, GfxShaderStage::Vertex);
        GfxHelperBufferUpdateScope indexBufferUpdate(cmd, gGUI.indexBuffer, indexSize, GfxShaderStage::Vertex);
        memcpy(vertexBufferUpdate.mData, vertices.Ptr(), vertexSize);
        memcpy(indexBufferUpdate.mData, indices.Ptr(), indexSize);
    }

    GfxBackendRenderPass pass {
        .colorAttachments = {{ .load = true }},
        .swapchain = true,
        .hasDepth = false
    };
    cmd.BeginRenderPass(pass);

    GfxViewport viewport {
        .x = gGUI.mainViewport.xmin,
        .y = gGUI.mainViewport.ymin,
        .width = gGUI.mainViewport.Width(),
        .height = gGUI.mainViewport.Height()
    };
    cmd.SetViewports(0, 1, &viewport);

    cmd.BindVertexBuffer(gGUI.vertexBuffer, 0);
    cmd.BindIndexBuffer(gGUI.indexBuffer, 0, GfxIndexType::Uint32);

    // 2D projection in framebuffer pixel coords (top-left = 0, 0)
    Mat4 worldToClipMat = GfxBackend::GetSwapchainTransformMat() *
                          Mat4::OrthoOffCenter(0, viewport.height, viewport.width, 0, -1.0f, 1.0f);

    GfxPipelineHandle currentPipeline;
    for (const GUIDrawCommand& drawCommand : drawCommands) {
        _SwitchPipeline(cmd, drawCommand.pipeline, currentPipeline);
        cmd.SetScissors(0, 1, &drawCommand.scissor);

        if (drawCommand.pipeline == gGUI.textPipeline) {
            cmd.PushConstants<Mat4>(gGUI.textPipelineLayout, "PerFrameData", worldToClipMat);

            GfxBindingDesc bindings[] = {
                {
                    .name = "FontTexture",
                    .image = drawCommand.image
                },
                {
                    .name = "FontSampler",
                    .sampler = gGUI.sampler
                }
            };
            cmd.PushBindings(gGUI.textPipelineLayout, CountOf(bindings), bindings);
        }
        else {
            cmd.PushConstants<Mat4>(gGUI.shapePipelineLayout, "PerFrameData", worldToClipMat);

            GfxBindingDesc bindings[] = {
                {
                    .name = "ShapeTexture",
                    .image = drawCommand.image
                },
                {
                    .name = "ShapeSampler",
                    .sampler = gGUI.sampler
                }
            };
            cmd.PushBindings(gGUI.shapePipelineLayout, CountOf(bindings), bindings);
        }

        cmd.DrawIndexed(drawCommand.numIndices, 1, drawCommand.startIndex, drawCommand.vertexOffset, 0);
    }

    cmd.EndRenderPass();
}
