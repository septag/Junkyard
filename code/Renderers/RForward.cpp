#include "RForward.h"

#include "../Core/MathAll.h"

#include "../Common/Application.h"
#include "../Common/Camera.h"

#include "../Graphics/GfxBackend.h"

#include "../Assets/AssetManager.h"
#include "../Assets/Shader.h"
#include "../Assets/Image.h"

#include "../Engine.h"

static inline constexpr uint32 R_LIGHT_CULL_TILE_SIZE = 16;
static inline constexpr uint32 R_LIGHT_CULL_MAX_LIGHTS_PER_TILE = 64;
static inline constexpr uint32 R_LIGHT_CULL_MAX_LIGHTS_PER_FRAME = 1024;
static inline constexpr uint32 R_MSAA = 4;

struct RVertexStreamPosition
{
    Float3 position;
};

struct RVertexStreamLighting
{
    Float3 normal;
    Float2 uv;
};

struct RLightCullShaderFrameData
{
    Mat4 worldToViewMat;
    Mat4 clipToViewMat;
    float cameraNear;
    float cameraFar;
    float _reserved1[2];
    uint32 numLights;
    uint32 windowWidth;
    uint32 windowHeight;
    uint32 _reserved2;
};

struct RLightCullDebugShaderFrameData
{
    uint32 tilesCountX;
    uint32 tilesCountY;
    uint32 _reserved[2];
};

struct RLightShaderFrameData
{
    Mat4 worldToClipMat;
    Float3 sunLightDir;
    float _reserved1;
    Float4 sunLightColor;
    Float4 skyAmbientColor;
    Float4 groundAmbientColor;
    uint32 tilesCountX;
    uint32 tilesCountY;
    uint32 _reserved2[2];
};

static const GfxVertexInputAttributeDesc R_VERTEX_ATTRIBUTES[] = {
    {"POSITION", 0, 0, GfxFormat::R32G32B32_SFLOAT, offsetof(RVertexStreamPosition, position)},
    {"NORMAL", 0, 1, GfxFormat::R32G32B32_SFLOAT, offsetof(RVertexStreamLighting, normal)},
    {"TEXCOORD", 0, 1, GfxFormat::R32G32_SFLOAT, offsetof(RVertexStreamLighting, uv)}
};

static const uint32 R_VERTEXBUFFER_STRIDES[] = {
    sizeof(RVertexStreamPosition),
    sizeof(RVertexStreamLighting)
};

struct RFwdContext
{
    MemBumpAllocatorVM frameAlloc;

    RGeometryChunk* chunkList;
    RGeometryChunk* lastChunk;
    uint32 numGeometryChunks;

    GfxImageHandle msaaColorRenderImage;
    GfxImageHandle msaaDepthRenderImage;

    AssetHandleShader sZPrepass;
    GfxPipelineHandle pZPrepass;
    GfxPipelineLayoutHandle pZPrepassLayout;
    GfxBufferHandle ubZPrepass;

    RLightBounds* lightBounds;
    RLightProps* lightProps;
    uint32 numLights;

    GfxBufferHandle bVisibleLightIndices;
    GfxBufferHandle bLightBounds; 
    GfxBufferHandle bLightProps;

    AssetHandleShader sLightCull;
    GfxPipelineHandle pLightCull;
    GfxPipelineLayoutHandle pLightCullLayout;
    GfxBufferHandle ubLightCull;

    AssetHandleShader sLightCullDebug;
    GfxPipelineHandle pLightCullDebug;
    GfxPipelineLayoutHandle pLightCullDebugLayout;

    AssetHandleShader sLight;
    GfxPipelineHandle pLight;
    GfxPipelineLayoutHandle pLightLayout;
    GfxBufferHandle ubLight;

    RLightShaderFrameData lightPerFrameData;
};

RFwdContext gFwd;

namespace R
{
    static void _CreateFramebufferDependentResources(uint16 width, uint16 height)
    {
        GfxBackend::DestroyImage(gFwd.msaaDepthRenderImage);
        GfxBackend::DestroyImage(gFwd.msaaColorRenderImage);
        GfxBackend::DestroyBuffer(gFwd.bVisibleLightIndices);

        //--------------------------------------------------------------------------------------------------------------
        // Render Images
        if constexpr (R_MSAA > 1) {
            GfxImageDesc desc {
                .width = width,
                .height = height,
                .multisampleFlags = (GfxMultiSampleCount)R_MSAA,
                .format = GfxBackend::GetValidDepthStencilFormat(), // TODO:
                .usageFlags = GfxImageUsageFlags::DepthStencilAttachment|GfxImageUsageFlags::Sampled,
            };

            // Note: this won't probably work with tiled GPUs because it's incompatible with Sampled flag
            //       So we probably need to copy the contents of the zbuffer to another one
            #if PLATFORM_MOBILE
            desc.usageFlags |= GfxImageUsageFlags::TransientAttachment;
            #endif

            gFwd.msaaDepthRenderImage = GfxBackend::CreateImage(desc);
        }

        {
            GfxImageDesc desc {
                .width = width,
                .height = height,
                .multisampleFlags = (GfxMultiSampleCount)R_MSAA,
                .format = GfxBackend::GetSwapchainFormat(), // TODO: 
                .usageFlags = GfxImageUsageFlags::ColorAttachment,
            };

            // Note: this won't probably work with tiled GPUs because it's incompatible with Sampled flag
            //       So we probably need to copy the contents of the zbuffer to another one
            #if PLATFORM_MOBILE
            desc.usageFlags |= GfxImageUsageFlags::TransientAttachment;
            #endif

            gFwd.msaaColorRenderImage = GfxBackend::CreateImage(desc);
        }

        // Buffers
        {
            uint32 numTilesX = M::CeilDiv(uint32(width), R_LIGHT_CULL_TILE_SIZE);
            uint32 numTilesY = M::CeilDiv(uint32(height), R_LIGHT_CULL_TILE_SIZE);
            GfxBufferDesc bufferDesc = {
                .sizeBytes = sizeof(uint32)*numTilesX*numTilesY*R_LIGHT_CULL_MAX_LIGHTS_PER_TILE,
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Storage
            };
            gFwd.bVisibleLightIndices = GfxBackend::CreateBuffer(bufferDesc);

        }
    }

    static void _CreatePipelines()
    {
        //--------------------------------------------------------------------------------------------------------------
        // ZPrepass
        {
            // Layout
            ASSERT(gFwd.sZPrepass.IsValid());
            AssetObjPtrScope<GfxShader> shader(gFwd.sZPrepass);
            GfxPipelineLayoutDesc::Binding pBindings[] = {
                {
                    .name = "PerFrameData",
                    .type = GfxDescriptorType::UniformBuffer,
                    .stagesUsed = GfxShaderStage::Vertex
                }
            };

            GfxPipelineLayoutDesc::PushConstant pPushConstants[] = {
                {
                    .name = "PerObjectData",
                    .stagesUsed = GfxShaderStage::Vertex,
                    .size = sizeof(Mat4)
                }
            };

            GfxPipelineLayoutDesc pLayoutDesc {
                .numBindings = CountOf(pBindings),
                .bindings = pBindings,
                .numPushConstants = CountOf(pPushConstants),
                .pushConstants = pPushConstants
            };

            gFwd.pZPrepassLayout = GfxBackend::CreatePipelineLayout(*shader, pLayoutDesc);

            // Pipeline
            GfxVertexInputAttributeDesc vertexInputAttDescs[] = {
                {
                    .semantic = "POSITION",
                    .binding = 0,
                    .format = GfxFormat::R32G32B32_SFLOAT,
                    .offset = 0 
                }
            };

            GfxVertexBufferBindingDesc vertexBufferBindingDescs[] = {
                {
                    .binding = 0,
                    .stride = sizeof(RVertexStreamPosition),
                }
            };

            GfxGraphicsPipelineDesc pDesc {
                .numVertexInputAttributes = CountOf(vertexInputAttDescs),
                .vertexInputAttributes = vertexInputAttDescs,
                .numVertexBufferBindings = CountOf(vertexBufferBindingDescs),
                .vertexBufferBindings = vertexBufferBindingDescs,
                .rasterizer = {
                    .cullMode = GfxCullMode::Back
                },
                .blend = {
                    .numAttachments = 1,
                    .attachments = GfxBlendAttachmentDesc::GetDefault()
                },
                .depthStencil = {
                    .depthTestEnable = true,
                    .depthWriteEnable = true,
                    .depthCompareOp = GfxCompareOp::Less
                },
                .msaa = {
                    .sampleCount = (GfxMultiSampleCount)R_MSAA
                },
                .numColorAttachments = 0,
                .depthAttachmentFormat = GfxBackend::GetValidDepthStencilFormat(),
                .stencilAttachmentFormat = GfxBackend::GetValidDepthStencilFormat()
            };

            gFwd.pZPrepass = GfxBackend::CreateGraphicsPipeline(*shader, gFwd.pZPrepassLayout, pDesc);

            // Buffers
            GfxBufferDesc bufferDesc {
                .sizeBytes = sizeof(Mat4),
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Uniform,
                .arena = GfxMemoryArena::PersistentGPU
            };
            gFwd.ubZPrepass = GfxBackend::CreateBuffer(bufferDesc);
        }

        //--------------------------------------------------------------------------------------------------------------
        // LightCull
        {
            AssetObjPtrScope<GfxShader> shader(gFwd.sLightCull);
            // Layout
            GfxPipelineLayoutDesc::Binding bindings[] = {
                {
                    .name = "PerFrameData",
                    .type = GfxDescriptorType::UniformBuffer,
                    .stagesUsed = GfxShaderStage::Compute
                },
                {
                    .name = "Lights",
                    .type = GfxDescriptorType::StorageBuffer,
                    .stagesUsed = GfxShaderStage::Compute
                },
                {
                    .name = "VisibleLightIndices",
                    .type = GfxDescriptorType::StorageBuffer,
                    .stagesUsed = GfxShaderStage::Compute
                },
                {
                    .name = "DepthTexture",
                    .type = GfxDescriptorType::SampledImage,
                    .stagesUsed = GfxShaderStage::Compute

                }
            };

            GfxPipelineLayoutDesc layoutDesc { 
                .numBindings = CountOf(bindings),
                .bindings = bindings
            };

            gFwd.pLightCullLayout = GfxBackend::CreatePipelineLayout(*shader, layoutDesc);

            // Pipeline
            gFwd.pLightCull = GfxBackend::CreateComputePipeline(*shader, gFwd.pLightCullLayout);

            // Buffers
            GfxBufferDesc bufferDesc {
                .sizeBytes = sizeof(RLightCullShaderFrameData),
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Uniform
            };
            gFwd.ubLightCull = GfxBackend::CreateBuffer(bufferDesc);
        }

        //--------------------------------------------------------------------------------------------------------------
        // Lighting
        {
            AssetObjPtrScope<GfxShader> shader(gFwd.sLight);

            // Layout
            GfxPipelineLayoutDesc::Binding bindings[] = {
                {
                    .name = "PerFrameData",
                    .type = GfxDescriptorType::UniformBuffer,
                    .stagesUsed = GfxShaderStage::Fragment|GfxShaderStage::Vertex
                },
                {
                    .name = "BaseColorTexture",
                    .type = GfxDescriptorType::CombinedImageSampler,
                    .stagesUsed = GfxShaderStage::Fragment
                },
                {
                    .name = "VisibleLightIndices",
                    .type = GfxDescriptorType::StorageBuffer,
                    .stagesUsed = GfxShaderStage::Fragment
                },
                {
                    .name = "LocalLights",
                    .type = GfxDescriptorType::StorageBuffer,
                    .stagesUsed = GfxShaderStage::Fragment
                },
                {
                    .name = "LocalLightBounds",
                    .type = GfxDescriptorType::StorageBuffer,
                    .stagesUsed = GfxShaderStage::Fragment
                }
            };

            GfxPipelineLayoutDesc::PushConstant pushConstants[] = {
                {
                    .name = "PerObjectData",
                    .stagesUsed = GfxShaderStage::Vertex,
                    .size = sizeof(Mat4)
                }
            };

            GfxPipelineLayoutDesc layoutDesc {
                .numBindings = CountOf(bindings),
                .bindings = bindings,
                .numPushConstants = CountOf(pushConstants),
                .pushConstants = pushConstants
            };

            gFwd.pLightLayout = GfxBackend::CreatePipelineLayout(*shader, layoutDesc);

            // Pipeline
            GfxVertexBufferBindingDesc vertexBufferBindingDescs[] = {
                {
                    .binding = 0,
                    .stride = sizeof(RVertexStreamPosition),   // TODO: use renderer specific vertex 
                },
                {
                    .binding = 1,
                    .stride = sizeof(RVertexStreamLighting),
                }
            };

            GfxVertexInputAttributeDesc vertexInputAttDescs[] = {
                {
                    .semantic = "POSITION",
                    .binding = 0,
                    .format = GfxFormat::R32G32B32_SFLOAT,
                    .offset = offsetof(RVertexStreamPosition, position)
                },
                {
                    .semantic = "NORMAL",
                    .binding = 1,
                    .format = GfxFormat::R32G32B32_SFLOAT,
                    .offset = offsetof(RVertexStreamLighting, normal)
                },
                {
                    .semantic = "TEXCOORD",
                    .binding = 1,
                    .format = GfxFormat::R32G32_SFLOAT,
                    .offset = offsetof(RVertexStreamLighting, uv)
                }
            };

            GfxGraphicsPipelineDesc pDesc {
                .numVertexInputAttributes = CountOf(vertexInputAttDescs),
                .vertexInputAttributes = vertexInputAttDescs,
                .numVertexBufferBindings = CountOf(vertexBufferBindingDescs),
                .vertexBufferBindings = vertexBufferBindingDescs,
                .rasterizer = {
                    .cullMode = GfxCullMode::Back
                },
                .blend = {
                    .numAttachments = 1,
                    .attachments = GfxBlendAttachmentDesc::GetDefault()
                },
                .depthStencil = {
                    .depthTestEnable = true,
                    .depthWriteEnable = false,
                    .depthCompareOp = GfxCompareOp::Equal
                },
                .msaa = {
                    .sampleCount = (GfxMultiSampleCount)R_MSAA
                },
                .numColorAttachments = 1,
                .colorAttachmentFormats = {GfxBackend::GetSwapchainFormat()},
                .depthAttachmentFormat = GfxBackend::GetValidDepthStencilFormat(),
                .stencilAttachmentFormat = GfxBackend::GetValidDepthStencilFormat()
            };

            gFwd.pLight = GfxBackend::CreateGraphicsPipeline(*shader, gFwd.pLightLayout, pDesc);
        }

        //--------------------------------------------------------------------------------------------------------------
        // LightCull Debug
        {
            AssetObjPtrScope<GfxShader> shader(gFwd.sLightCullDebug);

            // Layout
            GfxPipelineLayoutDesc::Binding bindings[] = {
                {
                    .name = "VisibleLightIndices",
                    .type = GfxDescriptorType::StorageBuffer,
                    .stagesUsed = GfxShaderStage::Fragment
                }
            };

            GfxPipelineLayoutDesc::PushConstant pushConstants[] = {
                {
                    .name = "PerFrameData",
                    .stagesUsed = GfxShaderStage::Fragment,
                    .size = sizeof(RLightCullDebugShaderFrameData)
                }
            };

            GfxPipelineLayoutDesc layoutDesc { 
                .numBindings = CountOf(bindings),
                .bindings = bindings,
                .numPushConstants = CountOf(pushConstants),
                .pushConstants = pushConstants
            };

            gFwd.pLightCullDebugLayout = GfxBackend::CreatePipelineLayout(*shader, layoutDesc);

            GfxGraphicsPipelineDesc pDesc {
                .rasterizer = {
                    .cullMode = GfxCullMode::Back
                },
                .blend = {
                    .numAttachments = 1,
                    .attachments = GfxBlendAttachmentDesc::GetDefault()
                },
                .numColorAttachments = 1,
                .colorAttachmentFormats = {GfxBackend::GetSwapchainFormat()},
                .depthAttachmentFormat = GfxBackend::GetValidDepthStencilFormat(),
                .stencilAttachmentFormat = GfxBackend::GetValidDepthStencilFormat()
            };

            gFwd.pLightCullDebug = GfxBackend::CreateGraphicsPipeline(*shader, gFwd.pLightCullDebugLayout, pDesc);

            // Uniform Buffers
            GfxBufferDesc ubDesc {
                .sizeBytes = sizeof(RLightShaderFrameData),
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Uniform,
            };

            gFwd.ubLight = GfxBackend::CreateBuffer(ubDesc);
        }
    }
} // R

void R::GetCompatibleLayout(uint32 maxAttributes, GfxVertexInputAttributeDesc* outAtts, uint32 maxStrides, uint32* outStrides)
{
    ASSERT(maxAttributes);
    ASSERT(maxStrides);

    uint32 numAttributes = Min(maxAttributes, CountOf(R_VERTEX_ATTRIBUTES));
    uint32 numStrides = Min(maxStrides, CountOf(R_VERTEXBUFFER_STRIDES));

    memcpy(outAtts, R_VERTEX_ATTRIBUTES, sizeof(GfxVertexInputAttributeDesc)*numAttributes);
    memcpy(outStrides, R_VERTEXBUFFER_STRIDES, sizeof(uint32)*numStrides);
}

bool R::Initialize()
{
    gFwd.frameAlloc.Initialize(SIZE_MB, SIZE_KB*128);

    App::RegisterEventsCallback([](const AppEvent& ev, void*)
                                {
                                    if (ev.type == AppEventType::Resized)
                                        _CreateFramebufferDependentResources(ev.framebufferWidth, ev.framebufferHeight);
                                });

    _CreateFramebufferDependentResources(App::GetFramebufferWidth(), App::GetFramebufferHeight());

    //--------------------------------------------------------------------------------------------------------------
    // Common buffers
    {
        GfxBufferDesc bufferDesc = {
            .sizeBytes = sizeof(RLightBounds)*R_LIGHT_CULL_MAX_LIGHTS_PER_FRAME,
            .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Storage
        };
        gFwd.bLightBounds = GfxBackend::CreateBuffer(bufferDesc);

        bufferDesc = {
            .sizeBytes = sizeof(RLightProps)*R_LIGHT_CULL_MAX_LIGHTS_PER_FRAME,
            .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Storage
        };
        gFwd.bLightProps = GfxBackend::CreateBuffer(bufferDesc);
    }

    //------------------------------------------------------------------------------------------------------------------
    // Load shaders and initialize pipelines afterwards
    const AssetGroup& assetGroup = Engine::RegisterInitializeResources([](void*) {
        _CreatePipelines();
    }, nullptr);

    gFwd.sZPrepass = Shader::Load("/shaders/ZPrepass.hlsl", ShaderLoadParams{}, assetGroup);

    {
        ShaderLoadParams loadParams {
            .compileDesc = {
                .numDefines = 3,
                .defines = {
                    {
                        .define = "TILE_SIZE",
                        .value = String32::Format("%d", R_LIGHT_CULL_TILE_SIZE)
                    },
                    {
                        .define = "MAX_LIGHTS_PER_TILE",
                        .value = String32::Format("%d", R_LIGHT_CULL_MAX_LIGHTS_PER_TILE)
                    },
                    {
                        .define = "MSAA",
                        .value = String32::Format("%d", R_MSAA)
                    }
                }
            }
        };
        gFwd.sLightCull = Shader::Load("/shaders/LightCull.hlsl", loadParams, assetGroup);
        gFwd.sLight = Shader::Load("/shaders/FwdPlusLight.hlsl", loadParams, assetGroup);
        gFwd.sLightCullDebug = Shader::Load("/shaders/LightCullDebug.hlsl", loadParams, assetGroup);
    }

    return true;
}

void R::Release()
{
    GfxBackend::DestroyPipeline(gFwd.pZPrepass);
    GfxBackend::DestroyPipelineLayout(gFwd.pZPrepassLayout);
    GfxBackend::DestroyBuffer(gFwd.ubZPrepass);

    GfxBackend::DestroyPipeline(gFwd.pLightCull);
    GfxBackend::DestroyPipelineLayout(gFwd.pLightCullLayout);
    GfxBackend::DestroyBuffer(gFwd.ubLightCull);

    GfxBackend::DestroyPipeline(gFwd.pLightCullDebug);
    GfxBackend::DestroyPipelineLayout(gFwd.pLightCullDebugLayout);

    GfxBackend::DestroyPipeline(gFwd.pLight);
    GfxBackend::DestroyPipelineLayout(gFwd.pLightLayout);
    GfxBackend::DestroyBuffer(gFwd.ubLight);

    GfxBackend::DestroyBuffer(gFwd.bLightBounds);
    GfxBackend::DestroyBuffer(gFwd.bVisibleLightIndices);
    GfxBackend::DestroyBuffer(gFwd.bLightProps);

    GfxBackend::DestroyImage(gFwd.msaaColorRenderImage);
    GfxBackend::DestroyImage(gFwd.msaaDepthRenderImage);

    Mem::Free(gFwd.lightBounds);
    Mem::Free(gFwd.lightProps);
    gFwd.lightBounds = nullptr;
    gFwd.lightProps = nullptr;

    gFwd.frameAlloc.Release();
}

void R::Update(GfxCommandBuffer& cmd, const Camera& cam)
{
    float vwidth = (float)App::GetFramebufferWidth();
    float vheight = (float)App::GetFramebufferHeight();

    Mat4 worldToClipMat = cam.GetPerspectiveMat(vwidth, vheight) * cam.GetViewMat();
    if (cmd.mDrawsToSwapchain) // TODO: this is not gonna detect swapchain properly
        worldToClipMat = GfxBackend::GetSwapchainTransformMat() * worldToClipMat;

    gFwd.lightPerFrameData.worldToClipMat = worldToClipMat;
    gFwd.lightPerFrameData.tilesCountX = M::CeilDiv((uint32)App::GetFramebufferWidth(), R_LIGHT_CULL_TILE_SIZE);
    gFwd.lightPerFrameData.tilesCountY = M::CeilDiv((uint32)App::GetFramebufferHeight(), R_LIGHT_CULL_TILE_SIZE);
    uint32 numTiles = gFwd.lightPerFrameData.tilesCountX * gFwd.lightPerFrameData.tilesCountY;

    {
        GfxHelperBufferUpdateScope updater(cmd, gFwd.ubZPrepass, uint32(-1), GfxShaderStage::Vertex|GfxShaderStage::Fragment);
        memcpy(updater.mData, &worldToClipMat, sizeof(Mat4));
    }

    // Per-frame light culling data
    {
        GfxHelperBufferUpdateScope updater(cmd, gFwd.ubLightCull, uint32(-1), GfxShaderStage::Compute);
        RLightCullShaderFrameData* buffer = (RLightCullShaderFrameData*)updater.mData;
        buffer->worldToViewMat = cam.GetViewMat();
        buffer->clipToViewMat = Mat4::Inverse(cam.GetPerspectiveMat(vwidth, vheight));
        buffer->cameraNear = cam.Near();
        buffer->cameraFar = cam.Far();
        buffer->numLights = gFwd.numLights;
        buffer->windowWidth = App::GetFramebufferWidth();
        buffer->windowHeight = App::GetFramebufferHeight();
    }

    // Per-frame lighting data
    {
        GfxHelperBufferUpdateScope updater(cmd, gFwd.ubLight, uint32(-1), GfxShaderStage::Fragment);
        memcpy(updater.mData, &gFwd.lightPerFrameData, sizeof(RLightShaderFrameData));
    }

    if (gFwd.numLights) {
        GfxHelperBufferUpdateScope lightBoundsUpdater(cmd, gFwd.bLightBounds, sizeof(RLightBounds)*gFwd.numLights,
                                                      GfxShaderStage::Compute);
        RLightBounds* bounds = (RLightBounds*)lightBoundsUpdater.mData;
        memcpy(bounds, gFwd.lightBounds, sizeof(RLightBounds)*gFwd.numLights);

        GfxHelperBufferUpdateScope lightPropsUpdater(cmd, gFwd.bLightProps, sizeof(RLightProps)*gFwd.numLights,
                                                     GfxShaderStage::Fragment);
        RLightProps* props = (RLightProps*)lightPropsUpdater.mData;
        memcpy(props, gFwd.lightProps, sizeof(RLightProps)*gFwd.numLights);
    }
    else {
        // Fill visible light indices buffer with sentinels (empty state)
        GfxHelperBufferUpdateScope updater(cmd, gFwd.bVisibleLightIndices, uint32(-1), GfxShaderStage::Fragment);
        uint32* indices = (uint32*)updater.mData;
        for (uint32 i = 0; i < numTiles; i++)
            indices[i*R_LIGHT_CULL_MAX_LIGHTS_PER_TILE] = 0xffffffff;
    }
}

void R::Render(GfxCommandBuffer& cmd, GfxImageHandle finalColorImage, GfxImageHandle finalDepthImage, RDebugMode debugMode)
{
    GfxImageHandle renderDepthImage = R_MSAA > 1 ? gFwd.msaaDepthRenderImage : finalDepthImage;
    ASSERT(renderDepthImage.IsValid());

    // Render blank screen if we have nothing to render
    if (gFwd.numGeometryChunks == 0) {
        GfxBackendRenderPass pass { 
            .colorAttachments = {{ 
                .clear = true,
                .clearValue = {
                    .color = Color4u::ToFloat4(COLOR4U_BLACK)
                }
            }},
            .swapchain = true,
            .hasDepth = false
        };

        cmd.BeginRenderPass(pass);
        cmd.EndRenderPass();

        return;
    }

    // Z-Prepass
    {
        cmd.TransitionImage(renderDepthImage, GfxImageTransition::RenderTarget, GfxImageTransitionFlags::DepthWrite);

        GfxBackendRenderPass zprepass { 
            .depthAttachment = {
                .image = renderDepthImage,
                .clear = true,
                .clearValue = {
                    .depth = 1.0f
                }
            },
            .hasDepth = true
        };
        cmd.BeginRenderPass(zprepass);

        cmd.BindPipeline(gFwd.pZPrepass);
        cmd.HelperSetFullscreenViewportAndScissor();

        RGeometryChunk* chunk = gFwd.chunkList;
        while (chunk) {
            cmd.PushConstants(gFwd.pZPrepassLayout, "PerObjectData", &chunk->localToWorldMat, sizeof(Mat4));

            cmd.BindVertexBuffers(0, 1, &chunk->posVertexBuffer, &chunk->posVertexBufferOffset);
            cmd.BindIndexBuffer(chunk->indexBuffer, chunk->indexBufferOffset, GfxIndexType::Uint32);

            GfxBindingDesc bindings[] = {
                {
                    .name = "PerFrameData",
                    .buffer = gFwd.ubZPrepass
                }
            };
            cmd.PushBindings(gFwd.pZPrepassLayout, CountOf(bindings), bindings);
            
            uint32 numIndices = 0;
            for (uint32 sc = 0; sc < chunk->numSubChunks; sc++)
                numIndices += chunk->subChunks[sc].numIndices;

            cmd.DrawIndexed(numIndices, 1, 0, 0, 0);

            chunk = chunk->nextChunk;
        }

        cmd.EndRenderPass();
    }

    // Light culling
    if (gFwd.numLights) {
        cmd.TransitionImage(renderDepthImage, GfxImageTransition::ShaderRead);
        cmd.TransitionBuffer(gFwd.bVisibleLightIndices, GfxBufferTransition::ComputeWrite);

        GfxBindingDesc bindings[] = {
            {
                .name = "PerFrameData", 
                .buffer = gFwd.ubLightCull
            },
            {
                .name = "Lights",
                .buffer = gFwd.bLightBounds
            },
            {
                .name = "VisibleLightIndices",
                .buffer = gFwd.bVisibleLightIndices
            },
            {
                .name = "DepthTexture",
                .image = renderDepthImage
            }
        };

        cmd.BindPipeline(gFwd.pLightCull);
        cmd.PushBindings(gFwd.pLightCullLayout, CountOf(bindings), bindings);
        cmd.Dispatch(gFwd.lightPerFrameData.tilesCountX, gFwd.lightPerFrameData.tilesCountY, 1);

        cmd.TransitionBuffer(gFwd.bVisibleLightIndices, GfxBufferTransition::FragmentRead);
    }

    cmd.TransitionImage(renderDepthImage, GfxImageTransition::RenderTarget, GfxImageTransitionFlags::DepthRead);

    // Light pass
    if (debugMode == RDebugMode::None) {
        if (R_MSAA > 1 && finalDepthImage.IsValid()) {
            cmd.TransitionImage(finalDepthImage, GfxImageTransition::RenderTarget, 
                                GfxImageTransitionFlags::DepthWrite|GfxImageTransitionFlags::DepthResolve);
        }

        // If finalColorImage is not provided, we render to Swapchain
        GfxImageHandle renderColorImage = R_MSAA > 1 ? gFwd.msaaColorRenderImage : finalColorImage;

        // Render to swapchain if we don't have MSAA, otherwise, resolve to Swapchain and provided depth buffer
        GfxBackendRenderPass pass { 
            .numAttachments = 1,
            .colorAttachments = {{ 
                .image = renderColorImage,
                .resolveImage = finalColorImage,
                .clear = true,
                .resolveToSwapchain = R_MSAA > 1 && !finalColorImage.IsValid(),
                .clearValue = {
                    .color = gFwd.lightPerFrameData.skyAmbientColor
                }
            }},
            .depthAttachment = {
                .image = renderDepthImage,
                .resolveImage = R_MSAA > 1 ? finalDepthImage : GfxImageHandle(),
                .load = true,
                .clear = false,
            },
            .swapchain = !renderColorImage.IsValid(), 
            .hasDepth = true
        };

        cmd.BeginRenderPass(pass);
        cmd.BindPipeline(gFwd.pLight);
        
        cmd.HelperSetFullscreenViewportAndScissor();

        RGeometryChunk* chunk = gFwd.chunkList;
        while (chunk) {
            cmd.PushConstants<Mat4>(gFwd.pLightLayout, "PerObjectData", chunk->localToWorldMat);

            const GfxBufferHandle vertexBuffers[] = {
                chunk->posVertexBuffer,
                chunk->lightingVertexBuffer
            };

            const uint64 vertexBufferOffsets[] = {
                chunk->posVertexBufferOffset,
                chunk->lightingVertexBufferOffset
            };

            cmd.BindVertexBuffers(0, 2, vertexBuffers, vertexBufferOffsets);
            cmd.BindIndexBuffer(chunk->indexBuffer, chunk->indexBufferOffset, GfxIndexType::Uint32);

            for (uint32 sc = 0; sc < chunk->numSubChunks; sc++) {
                const RGeometrySubChunk& subChunk = chunk->subChunks[sc];
                GfxBindingDesc bindings[] = {
                    {
                        .name = "PerFrameData",
                        .buffer = gFwd.ubLight
                    },
                    {
                        .name = "BaseColorTexture",
                        .image = subChunk.baseColorImg.IsValid() ? subChunk.baseColorImg : Image::GetWhite1x1()
                    },
                    {
                        .name = "VisibleLightIndices",
                        .buffer = gFwd.bVisibleLightIndices
                    },
                    {
                        .name = "LocalLights",
                        .buffer = gFwd.bLightProps
                    },
                    {
                        .name = "LocalLightBounds",
                        .buffer = gFwd.bLightBounds
                    }
                };
                cmd.PushBindings(gFwd.pLightLayout, CountOf(bindings), bindings);

                cmd.DrawIndexed(subChunk.numIndices, 1, subChunk.startIndex, 0, 0);
            }

            chunk = chunk->nextChunk;
        }

        cmd.EndRenderPass();
    }
    else if (debugMode == RDebugMode::LightCull) {
        GfxBackendRenderPass pass { 
            .swapchain = true,
        };

        cmd.BeginRenderPass(pass);
        cmd.BindPipeline(gFwd.pLightCullDebug);
        cmd.HelperSetFullscreenViewportAndScissor();

        GfxBindingDesc bindings[] = {
            {
                .name = "VisibleLightIndices",
                .buffer = gFwd.bVisibleLightIndices
            }
        };

        cmd.PushBindings(gFwd.pLightCullDebugLayout, CountOf(bindings), bindings);

        RLightCullDebugShaderFrameData perFrameData {
            .tilesCountX = gFwd.lightPerFrameData.tilesCountX,
            .tilesCountY = gFwd.lightPerFrameData.tilesCountY
        };
        cmd.PushConstants<RLightCullDebugShaderFrameData>(gFwd.pLightCullDebugLayout, "PerFrameData", perFrameData);

        cmd.Draw(3, 1, 0, 0);

        cmd.EndRenderPass();
    }
}

void R::SetLocalLights(uint32 numLights, const RLightBounds* bounds, const RLightProps* props)
{
    gFwd.numLights = numLights;

    if (numLights) {
        gFwd.lightBounds = Mem::ReallocTyped<RLightBounds>(gFwd.lightBounds, numLights);   // TODO: alloc
        gFwd.lightProps = Mem::ReallocTyped<RLightProps>(gFwd.lightProps, numLights);
        memcpy(gFwd.lightBounds, bounds, sizeof(RLightBounds)*numLights);
        memcpy(gFwd.lightProps, props, sizeof(RLightProps)*numLights);
    }
}

void R::SetAmbientLight(Float4 skyAmbientColor, Float4 groundAmbientColor)
{
    gFwd.lightPerFrameData.skyAmbientColor = Color4u::ToFloat4Linear(skyAmbientColor);
    gFwd.lightPerFrameData.groundAmbientColor = Color4u::ToFloat4Linear(groundAmbientColor);
}

void R::SetSunLight(Float3 direction, Float4 color)
{
    gFwd.lightPerFrameData.sunLightDir = Float3::Norm(direction);
    gFwd.lightPerFrameData.sunLightColor = Color4u::ToFloat4Linear(color);
}

RGeometryChunk* R::NewGeometryChunk()
{
    RGeometryChunk* chunk = Mem::AllocZeroTyped<RGeometryChunk>(1, &gFwd.frameAlloc);
    chunk->localToWorldMat = MAT4_IDENT;

    if (gFwd.lastChunk)
        gFwd.lastChunk->nextChunk = chunk;
    else
        gFwd.chunkList = chunk;
    gFwd.lastChunk = chunk;

    ++gFwd.numGeometryChunks;

    return chunk;        
}

void R::NewFrame()
{
    gFwd.frameAlloc.Reset();
    gFwd.chunkList = nullptr;
    gFwd.lastChunk = nullptr;
    gFwd.numGeometryChunks = 0;
}


   
//  ██████╗ ███╗   ███╗███████╗███╗   ███╗ ██████╗ ██████╗ ██╗   ██╗ ██████╗██╗  ██╗██╗   ██╗███╗   ██╗██╗  ██╗
//  ██╔══██╗████╗ ████║██╔════╝████╗ ████║██╔═══██╗██╔══██╗╚██╗ ██╔╝██╔════╝██║  ██║██║   ██║████╗  ██║██║ ██╔╝
//  ██████╔╝██╔████╔██║█████╗  ██╔████╔██║██║   ██║██████╔╝ ╚████╔╝ ██║     ███████║██║   ██║██╔██╗ ██║█████╔╝ 
//  ██╔══██╗██║╚██╔╝██║██╔══╝  ██║╚██╔╝██║██║   ██║██╔══██╗  ╚██╔╝  ██║     ██╔══██║██║   ██║██║╚██╗██║██╔═██╗ 
//  ██║  ██║██║ ╚═╝ ██║███████╗██║ ╚═╝ ██║╚██████╔╝██║  ██║   ██║   ╚██████╗██║  ██║╚██████╔╝██║ ╚████║██║  ██╗
//  ╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝╚═╝     ╚═╝ ╚═════╝ ╚═╝  ╚═╝   ╚═╝    ╚═════╝╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═══╝╚═╝  ╚═╝
                                                                                                                   
void RGeometryChunk::AddSubChunk(const RGeometrySubChunk& subChunk)
{
    subChunks = Mem::ReallocTyped<RGeometrySubChunk>(subChunks, numSubChunks+1, &gFwd.frameAlloc);
    subChunks[numSubChunks] = subChunk;
    ++numSubChunks;
}

void RGeometryChunk::AddSubChunks(uint32 _numSubChunks, const RGeometrySubChunk* _subChunks)
{
    ASSERT(_numSubChunks);
    ASSERT(_subChunks);

    this->subChunks = Mem::ReallocTyped<RGeometrySubChunk>(this->subChunks, this->numSubChunks+_numSubChunks, &gFwd.frameAlloc);
    memcpy(this->subChunks + this->numSubChunks, _subChunks, sizeof(RGeometrySubChunk)*_numSubChunks);
    this->numSubChunks += _numSubChunks;
}

