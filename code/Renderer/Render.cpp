#include "Render.h"

#include "../Core/MathAll.h"
#include "../Core/Log.h"
#include "../Core/Pools.h"
#include "../Core/TracyHelper.h"

#include "../Common/Application.h"
#include "../Common/Camera.h"
#include "../Common/JunkyardSettings.h"

#include "../Graphics/GfxBackend.h"
#include "../Graphics/Geometry.h"

#include "../Assets/AssetManager.h"
#include "../Assets/Shader.h"
#include "../Assets/Image.h"

#include "../Engine.h"

static inline constexpr uint32 R_LIGHT_CULL_TILE_SIZE = 16;
static inline constexpr uint32 R_LIGHT_CULL_MAX_LIGHTS_PER_TILE = 64;
static inline constexpr uint32 R_LIGHT_CULL_MAX_LIGHTS_PER_FRAME = 1024;
static inline constexpr uint32 R_MAX_DRAW_OBJECTS = 5000;

enum class RDescriptorSetIndex : uint8
{
    PerFrame = 0,
    PerObject,
    Samplers
};

//----------------------------------------------------------------------------------------------------------------------
// Global samplers
// See Samplers.h.hlsl
// Add samplers here and keep those two in sync. 
enum class RSamplerType : uint32
{
    BilinearClamp = 0,
    TrilinearWrap,
    ShadowMapCmp,
    _Count
};

static const GfxSamplerDesc R_SAMPLER_DESCS[uint32(RSamplerType::_Count)] = {
    {
        .samplerFilter = GfxSamplerFilterMode::LinearMipmapNearest,
        .samplerWrap = GfxSamplerWrapMode::ClampToEdge,
    },
    {
        .samplerFilter = GfxSamplerFilterMode::LinearMipmapLinear,
        .samplerWrap = GfxSamplerWrapMode::Repeat,
    },
    {
        .samplerFilter = GfxSamplerFilterMode::Linear,
        .samplerWrap = GfxSamplerWrapMode::ClampToEdge,
        .compareOp = GfxCompareOp::LessOrEqual
    }
};

#if !CONFIG_GFX_IMMUTABLE_SAMPLERS
    static const char* R_SAMPLER_NAMES[uint32(RSamplerType::_Count)] = {
        "SamplerBilinear",
        "SamplerTrilinear",
        "SamplerBilinearLess"
    };
    #define R_INSERT_IMMUTABLE_SAMPLER_BINDINGS() \
    { \
        .name = R_SAMPLER_NAMES[0], \
        .type = GfxDescriptorType::Sampler, \
        .stagesUsed = GfxShaderStage::Fragment | GfxShaderStage::Vertex, \
        .setIndex = uint8(RDescriptorSetIndex::Samplers) \
    }, \
    { \
        .name = R_SAMPLER_NAMES[1], \
        .type = GfxDescriptorType::Sampler, \
        .stagesUsed = GfxShaderStage::Fragment | GfxShaderStage::Vertex, \
        .setIndex = uint8(RDescriptorSetIndex::Samplers) \
    }, \
    { \
        .name = R_SAMPLER_NAMES[2], \
        .type = GfxDescriptorType::Sampler, \
        .stagesUsed = GfxShaderStage::Fragment | GfxShaderStage::Vertex, \
        .setIndex = uint8(RDescriptorSetIndex::Samplers) \
    },
#else
    #define R_INSERT_IMMUTABLE_SAMPLER_BINDINGS()
#endif

struct RVertexStreamPosition
{
    Float3 position;
};

struct RVertexStreamLighting
{
    Float3 normal;
    Float2 uv;
};

struct GFX_UNIFORM_BUFFER_ALIGNMENT RLightCullShaderFrameData
{
    Mat4 worldToViewMat;
    Mat4 clipToViewMat;
    float cameraNear;
    float cameraFar;
    uint32 numLights;
    uint32 windowWidth;
    uint32 windowHeight;
    STRUCT_PADDING_12_BYTES();
};

struct GFX_UNIFORM_BUFFER_ALIGNMENT RLightCullDebugShaderFrameData
{
    uint32 tilesCountX;
    uint32 tilesCountY;
    STRUCT_PADDING_8_BYTES();
};

struct GFX_UNIFORM_BUFFER_ALIGNMENT RTextureDebugShaderFrameData
{
    float cameraNear;
    float cameraFar;
    STRUCT_PADDING_8_BYTES();
};

struct GFX_UNIFORM_BUFFER_ALIGNMENT RLightShaderFrameData
{
    Mat4 worldToClipMat;
    Mat4 worldToSunLightClipMat;
    Float3 sunLightDir;
    STRUCT_PADDING_4_BYTES();
    Float4 sunLightColor;
    Float4 skyAmbientColor;
    Float4 groundAmbientColor;
    uint32 tilesCountX;
    STRUCT_PADDING_12_BYTES();
};

struct GFX_UNIFORM_BUFFER_ALIGNMENT RLightShaderObjectData
{
    Mat4 localToWorldMat;
    Float4 materialTintColor;
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

DEFINE_HANDLE(RFwdLightViewHandle);
DEFINE_HANDLE(RShadowMapViewHandle);

// Common data of all view types: "What to draw". Filled by the RView API every frame
// Render path specific resources live in a separate pool per path (see RFwdLightViewData/RShadowMapViewData)
struct RViewData
{
    RViewType type;
    RFwdLightViewHandle fwdLightHandle;     // Valid only for RViewType::FwdLight
    RShadowMapViewHandle shadowMapHandle;   // Valid only for RViewType::ShadowMap

    Mat4 worldToViewMat;
    Mat4 viewToClipMat;
    Mat4 worldToClipMat;
    float nearDist;
    float farDist;
    uint32 viewWidth;
    uint32 viewHeight;

    Mat4 sunLightWorldToClipMat;
    Float3 sunLightDir;
    Float4 sunLightColor;
    Float4 skyAmbientColor;
    Float4 groundAmbientColor;
    GfxImageHandle sunShadowMapImage;

    RLightBounds* lightBounds;
    RLightProps* lightProps;
    uint32 numLights;

    RGeometryChunk* chunkList;
    RGeometryChunk* lastChunk;
    uint32 numGeometryChunks;
};

// FwdLight render path resources, owned by each FwdLight view, so multiple views can be rendered in the same frame
// GPU resources are created lazily in FwdLight::Update: Descriptor buffers are sized by the pipeline layouts, which are
// not ready until the shaders are loaded. Everything is allocated from Dynamic arenas, because views can be
// created and destroyed at any time
struct RFwdLightViewData
{
    // Size dependent resources. Recreated when the view size changes
    uint32 targetWidth;
    uint32 targetHeight;
    uint32 tilesCountX;
    uint32 tilesCountY;
    GfxImageHandle msaaColorImage;          // Always created. Dummy color attachment of the alpha-masked Z-prepass without MSAA
    GfxImageHandle msaaDepthImage;          // Only with MSAA, otherwise the final depth image is rendered to directly
    GfxBufferHandle bVisibleLightIndices;

    // Per-frame data (ring-buffered by the backend for frames in flight)
    GfxBufferHandle ubZPrepass;
    GfxBufferHandle ubLightCull;
    GfxBufferHandle ubLightPerFrameData;
    GfxBufferHandle ubLightPerObjectData;
    GfxBufferHandle dbLightPerFrameResources;
    GfxBufferHandle dbLightPerObjectResources;
    GfxBufferHandle bLightBounds;
    GfxBufferHandle bLightProps;
};

// ShadowMap render path resources. Depth target is provided by the caller (see ShadowMap::Render)
struct RShadowMapViewData
{
    GfxBufferHandle ubShadowMap;            // Created lazily in ShadowMap::Update
};

struct RFwdContext
{
    MemBumpAllocatorVM frameAlloc;

    AssetHandleShader sZPrepass;
    AssetHandleShader sZPrepassAlphaMask;
    GfxPipelineHandle pZPrepass;
    GfxPipelineHandle pZPrepassAlphaMask;
    GfxPipelineLayoutHandle pZPrepassLayout;
    GfxPipelineLayoutHandle pZPrepassLayoutAlphaMask;

    GfxPipelineHandle pShadowMap;

    AssetHandleShader sLightCull;
    GfxPipelineHandle pLightCull;
    GfxPipelineLayoutHandle pLightCullLayout;

    AssetHandleShader sLightCullDebug;
    GfxPipelineHandle pLightCullDebug;
    GfxPipelineLayoutHandle pLightCullDebugLayout;

    AssetHandleShader sLight;
    GfxPipelineHandle pLight;
    GfxPipelineLayoutHandle pLightLayout;
#if !CONFIG_GFX_IMMUTABLE_SAMPLERS
    GfxBufferHandle dbImmutableSamplers;
    bool immutableSamplersUpdated;
#endif

    AssetHandleShader sTextureDebugDepth;
    GfxPipelineHandle pTextureDebugDepth;
    GfxPipelineLayoutHandle pTextureDebugDepthLayout;

    AssetHandleShader sTextureDebugColor;
    GfxPipelineHandle pTextureDebugColor;
    GfxPipelineLayoutHandle pTextureDebugColorLayout;

    GfxSamplerHandle samplers[uint32(RSamplerType::_Count)];

    HandlePool<RViewHandle, RViewData> viewPool;
    HandlePool<RFwdLightViewHandle, RFwdLightViewData> fwdLightViewPool;
    HandlePool<RShadowMapViewHandle, RShadowMapViewData> shadowMapViewPool;

    bool initialized;
};

RFwdContext gFwd;

namespace R
{
    static void _SetViewportAndScissor(GfxCommandBuffer& cmd, uint32 width, uint32 height)
    {
        GfxViewport viewport {
            .x = 0,
            .y = 0,
            .width = float(width),
            .height = float(height)
        };
        RectInt scissor(0, 0, int(width), int(height));
        cmd.SetViewports(0, 1, &viewport);
        cmd.SetScissors(0, 1, &scissor);
    }

    // Debug views render into the final color image of the view (or the swapchain if it's not provided), just like
    // the light pass. So they show up in the same place, ie. inside an ImGui viewport panel
    static void _BeginDebugRenderPass(GfxCommandBuffer& cmd, GfxImageHandle finalColorImage, Float4 clearColor)
    {
        if (finalColorImage.IsValid())
            cmd.TransitionImage(finalColorImage, GfxImageTransition::RenderTarget);

        GfxBackendRenderPass pass {
            .numAttachments = finalColorImage.IsValid() ? 1u : 0u,
            .colorAttachments = {{
                .image = finalColorImage,
                .clear = true,
                .clearValue = {
                    .color = clearColor
                }
            }},
            .swapchain = !finalColorImage.IsValid()
        };
        cmd.BeginRenderPass(pass);
    }

    static void _ResizeFwdLightViewTargets(RFwdLightViewData& data, uint32 width, uint32 height)
    {
        uint32 msaa = SettingsJunkyard::Get().graphics.msaa;

        GfxBackend::DestroyImage(data.msaaDepthImage);
        GfxBackend::DestroyImage(data.msaaColorImage);
        GfxBackend::DestroyBuffer(data.bVisibleLightIndices);

        data.targetWidth = width;
        data.targetHeight = height;
        data.tilesCountX = M::CeilDiv(width, R_LIGHT_CULL_TILE_SIZE);
        data.tilesCountY = M::CeilDiv(height, R_LIGHT_CULL_TILE_SIZE);

        //--------------------------------------------------------------------------------------------------------------
        // Render Images
        if (msaa > 1) {
            GfxImageDesc desc {
                .width = uint16(width),
                .height = uint16(height),
                .multisampleFlags = (GfxMultiSampleCount)msaa,
                .format = GfxBackend::GetValidDepthStencilFormat(), // TODO:
                .usageFlags = GfxImageUsageFlags::DepthStencilAttachment|GfxImageUsageFlags::Sampled,
                .arena = GfxMemoryArena::DynamicImageGPU
            };

            // Note: this won't probably work with tiled GPUs because it's incompatible with Sampled flag
            //       So we probably need to copy the contents of the zbuffer to another one
            #if PLATFORM_MOBILE
            desc.usageFlags |= GfxImageUsageFlags::TransientAttachment;
            #endif

            data.msaaDepthImage = GfxBackend::CreateImage(desc);
        }

        {
            GfxImageDesc desc {
                .width = uint16(width),
                .height = uint16(height),
                .multisampleFlags = (GfxMultiSampleCount)msaa,
                .format = GfxBackend::GetSwapchainFormat(), // TODO:
                .usageFlags = GfxImageUsageFlags::ColorAttachment,
                .arena = GfxMemoryArena::DynamicImageGPU
            };

            // Note: this won't probably work with tiled GPUs because it's incompatible with Sampled flag
            //       So we probably need to copy the contents of the zbuffer to another one
            #if PLATFORM_MOBILE
            desc.usageFlags |= GfxImageUsageFlags::TransientAttachment;
            #endif

            data.msaaColorImage = GfxBackend::CreateImage(desc);
        }

        // Buffers
        {
            GfxBufferDesc bufferDesc = {
                .sizeBytes = sizeof(uint32)*data.tilesCountX*data.tilesCountY*R_LIGHT_CULL_MAX_LIGHTS_PER_TILE,
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Storage | GfxBufferUsageFlags::ShaderDeviceAddress,
                .arena = GfxMemoryArena::DynamicAddressGPU,
                .perFrameUpdates = true
            };
            data.bVisibleLightIndices = GfxBackend::CreateBuffer(bufferDesc);
        }

        LOG_VERBOSE("Renderer FwdLight view buffers recreated: %ux%u", width, height);
    }

    static void _CreateFwdLightViewBuffers(RFwdLightViewData& data)
    {
        ASSERT_MSG(gFwd.pLightLayout.IsValid(), "Renderer pipelines are not created yet");

        // Z-Prepass
        GfxBufferDesc bufferDesc {
            .sizeBytes = sizeof(Mat4),
            .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Uniform,
            .arena = GfxMemoryArena::DynamicBufferGPU,
            .perFrameUpdates = true
        };
        data.ubZPrepass = GfxBackend::CreateBuffer(bufferDesc);

        // Light culling
        bufferDesc = {
            .sizeBytes = sizeof(RLightCullShaderFrameData),
            .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Uniform,
            .arena = GfxMemoryArena::DynamicBufferGPU,
            .perFrameUpdates = true
        };
        data.ubLightCull = GfxBackend::CreateBuffer(bufferDesc);

        bufferDesc = {
            .sizeBytes = sizeof(RLightBounds)*R_LIGHT_CULL_MAX_LIGHTS_PER_FRAME,
            .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Storage | GfxBufferUsageFlags::ShaderDeviceAddress,
            .arena = GfxMemoryArena::DynamicAddressGPU,
            .perFrameUpdates = true
        };
        data.bLightBounds = GfxBackend::CreateBuffer(bufferDesc);

        bufferDesc = {
            .sizeBytes = sizeof(RLightProps)*R_LIGHT_CULL_MAX_LIGHTS_PER_FRAME,
            .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Storage | GfxBufferUsageFlags::ShaderDeviceAddress,
            .arena = GfxMemoryArena::DynamicAddressGPU,
            .perFrameUpdates = true
        };
        data.bLightProps = GfxBackend::CreateBuffer(bufferDesc);

        // Light: Uniform Buffers
        bufferDesc = {
            .sizeBytes = sizeof(RLightShaderFrameData),
            .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Uniform | GfxBufferUsageFlags::ShaderDeviceAddress,
            .arena = GfxMemoryArena::DynamicAddressGPU,
            .perFrameUpdates = true
        };
        data.ubLightPerFrameData = GfxBackend::CreateBuffer(bufferDesc);

        bufferDesc = {
            .sizeBytes = GfxHelperUniformBuffer::GetMemoryRequirement(sizeof(RLightShaderObjectData), R_MAX_DRAW_OBJECTS),
            .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Uniform | GfxBufferUsageFlags::ShaderDeviceAddress,
            .arena = GfxMemoryArena::DynamicAddressGPU,
            .perFrameUpdates = true
        };
        data.ubLightPerObjectData = GfxBackend::CreateBuffer(bufferDesc);

        // Light: Descriptor Buffers
        bufferDesc = {
            .sizeBytes = GfxHelperDescriptorBuffer::GetMemoryRequirement(gFwd.pLightLayout,
                                                                         (uint32)RDescriptorSetIndex::PerFrame, 1),
            .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::ResourceDescriptorBuffer | GfxBufferUsageFlags::ShaderDeviceAddress,
            .arena = GfxMemoryArena::DynamicAddressGPU,
            .perFrameUpdates = true
        };
        data.dbLightPerFrameResources = GfxBackend::CreateBuffer(bufferDesc);

        bufferDesc = {
            .sizeBytes = GfxHelperDescriptorBuffer::GetMemoryRequirement(gFwd.pLightLayout,
                                                                         (uint32)RDescriptorSetIndex::PerObject,
                                                                         R_MAX_DRAW_OBJECTS),
            .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::ResourceDescriptorBuffer | GfxBufferUsageFlags::ShaderDeviceAddress,
            .arena = GfxMemoryArena::DynamicAddressGPU,
            .perFrameUpdates = true
        };
        data.dbLightPerObjectResources = GfxBackend::CreateBuffer(bufferDesc);
    }

    static void _DestroyFwdLightViewResources(RFwdLightViewData& data)
    {
        GfxBackend::DestroyImage(data.msaaColorImage);
        GfxBackend::DestroyImage(data.msaaDepthImage);
        GfxBackend::DestroyBuffer(data.bVisibleLightIndices);

        GfxBackend::DestroyBuffer(data.ubZPrepass);
        GfxBackend::DestroyBuffer(data.ubLightCull);
        GfxBackend::DestroyBuffer(data.ubLightPerFrameData);
        GfxBackend::DestroyBuffer(data.ubLightPerObjectData);
        GfxBackend::DestroyBuffer(data.dbLightPerFrameResources);
        GfxBackend::DestroyBuffer(data.dbLightPerObjectResources);
        GfxBackend::DestroyBuffer(data.bLightBounds);
        GfxBackend::DestroyBuffer(data.bLightProps);

        data = {};
    }

    static void _DestroyShadowMapViewResources(RShadowMapViewData& data)
    {
        GfxBackend::DestroyBuffer(data.ubShadowMap);
        data = {};
    }

    static void _CreatePipelines()
    {
        uint32 msaa = SettingsJunkyard::Get().graphics.msaa;

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
                .type = GfxPipelineLayoutType::PushDescriptor,
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
                    .offset = offsetof(RVertexStreamPosition, position) 
                }
            };

            GfxVertexBufferBindingDesc vertexBufferBindingDescs[] = {
                {
                    .binding = 0,
                    .stride = sizeof(RVertexStreamPosition),
                }
            };

            GfxGraphicsPipelineDesc pZPrepassDesc {
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
                    .sampleCount = (GfxMultiSampleCount)msaa
                },
                .numColorAttachments = 0,
                .depthAttachmentFormat = GfxBackend::GetValidDepthStencilFormat(),
                .stencilAttachmentFormat = GfxBackend::GetValidDepthStencilFormat()
            };

            gFwd.pZPrepass = GfxBackend::CreateGraphicsPipeline(*shader, gFwd.pZPrepassLayout, pZPrepassDesc);

            // ShadowMaps are pretty much as same as ZPrepass with minor differences (CullMode and depthAttachmentFormat)
            // Layout and shader is also the same currently
            GfxGraphicsPipelineDesc pShadowMapDesc {
                .numVertexInputAttributes = CountOf(vertexInputAttDescs),
                .vertexInputAttributes = vertexInputAttDescs,
                .numVertexBufferBindings = CountOf(vertexBufferBindingDescs),
                .vertexBufferBindings = vertexBufferBindingDescs,
                .rasterizer = {
                    .cullMode = GfxCullMode::Front
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
                .numColorAttachments = 0,
                .depthAttachmentFormat = GfxFormat::D16_UNORM,
            };
            gFwd.pShadowMap = GfxBackend::CreateGraphicsPipeline(*shader, gFwd.pZPrepassLayout, pShadowMapDesc);
        }

        //--------------------------------------------------------------------------------------------------------------
        // Z-Prepass Alpha Masked
        {
            // Layout
            ASSERT(gFwd.sZPrepassAlphaMask.IsValid());
            AssetObjPtrScope<GfxShader> shader(gFwd.sZPrepassAlphaMask);
            GfxPipelineLayoutDesc::Binding pBindings[] = {
                {
                    .name = "PerFrameData",
                    .type = GfxDescriptorType::UniformBuffer,
                    .stagesUsed = GfxShaderStage::Vertex
                },
                {
                    .name = "ColorTexture",
                    .type = GfxDescriptorType::CombinedImageSampler,
                    .stagesUsed = GfxShaderStage::Fragment
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
                .type = GfxPipelineLayoutType::PushDescriptor,
                .numBindings = CountOf(pBindings),
                .bindings = pBindings,
                .numPushConstants = CountOf(pPushConstants),
                .pushConstants = pPushConstants
            };

            gFwd.pZPrepassLayoutAlphaMask = GfxBackend::CreatePipelineLayout(*shader, pLayoutDesc);

            // Pipeline
            GfxVertexInputAttributeDesc vertexInputAttDescs[] = {
                {
                    .semantic = "POSITION",
                    .binding = 0,
                    .format = GfxFormat::R32G32B32_SFLOAT,
                    .offset = offsetof(RVertexStreamPosition, position)
                },
                {
                    .semantic = "TEXCOORD",
                    .binding = 1,
                    .format = GfxFormat::R32G32_SFLOAT,
                    .offset = offsetof(RVertexStreamLighting, uv)
                }
            };

            GfxVertexBufferBindingDesc vertexBufferBindingDescs[] = {
                {
                    .binding = 0,
                    .stride = sizeof(RVertexStreamPosition),
                },
                {
                    .binding = 1,
                    .stride = sizeof(RVertexStreamLighting)
                }
            };

            GfxGraphicsPipelineDesc pZPrepassDesc {
                .numVertexInputAttributes = CountOf(vertexInputAttDescs),
                .vertexInputAttributes = vertexInputAttDescs,
                .numVertexBufferBindings = CountOf(vertexBufferBindingDescs),
                .vertexBufferBindings = vertexBufferBindingDescs,
                .rasterizer = {
                    .cullMode = GfxCullMode::Back,
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
                    .sampleCount = (GfxMultiSampleCount)msaa,
                    .alphaToCoverageEnable = true
                },
                .numColorAttachments = 0,
                .depthAttachmentFormat = GfxBackend::GetValidDepthStencilFormat(),
                .stencilAttachmentFormat = GfxBackend::GetValidDepthStencilFormat()
            };

            gFwd.pZPrepassAlphaMask = GfxBackend::CreateGraphicsPipeline(*shader, gFwd.pZPrepassLayoutAlphaMask, pZPrepassDesc);
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
                .type = GfxPipelineLayoutType::PushDescriptor,
                .numBindings = CountOf(bindings),
                .bindings = bindings
            };

            gFwd.pLightCullLayout = GfxBackend::CreatePipelineLayout(*shader, layoutDesc);

            // Pipeline
            gFwd.pLightCull = GfxBackend::CreateComputePipeline(*shader, gFwd.pLightCullLayout);
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
                    .stagesUsed = GfxShaderStage::Fragment|GfxShaderStage::Vertex,
                    .setIndex = (uint8)RDescriptorSetIndex::PerFrame
                },
                {
                    .name = "VisibleLightIndices",
                    .type = GfxDescriptorType::StorageBuffer,
                    .stagesUsed = GfxShaderStage::Fragment,
                    .setIndex = (uint8)RDescriptorSetIndex::PerFrame
                },
                {
                    .name = "LocalLights",
                    .type = GfxDescriptorType::StorageBuffer,
                    .stagesUsed = GfxShaderStage::Fragment,
                    .setIndex = (uint8)RDescriptorSetIndex::PerFrame
                },
                {
                    .name = "LocalLightBounds",
                    .type = GfxDescriptorType::StorageBuffer,
                    .stagesUsed = GfxShaderStage::Fragment,
                    .setIndex = (uint8)RDescriptorSetIndex::PerFrame
                },
                {
                    .name = "ShadowMap",
                    .type = GfxDescriptorType::SampledImage,
                    .stagesUsed = GfxShaderStage::Fragment,
                    .setIndex = (uint8)RDescriptorSetIndex::PerFrame
                },
                {
                    .name = "PerObjectData",
                    .type = GfxDescriptorType::UniformBuffer,
                    .stagesUsed = GfxShaderStage::Vertex | GfxShaderStage::Fragment,
                    .setIndex = (uint8)RDescriptorSetIndex::PerObject
                },
                {
                    .name = "BaseColorTexture",
                    .type = GfxDescriptorType::SampledImage,
                    .stagesUsed = GfxShaderStage::Fragment,
                    .setIndex = (uint8)RDescriptorSetIndex::PerObject
                },
                R_INSERT_IMMUTABLE_SAMPLER_BINDINGS()
            };

            GfxPipelineLayoutDesc layoutDesc {
                .type = GfxPipelineLayoutType::DescriptorBuffer,
                .numBindings = CountOf(bindings),
                .bindings = bindings,
                .useImmutableSamplers = true
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
                    .cullMode = GfxCullMode::Back,
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
                    .sampleCount = (GfxMultiSampleCount)msaa,
                    .alphaToCoverageDynamicSetup = true
                },
                .numColorAttachments = 1,
                .colorAttachmentFormats = {GfxBackend::GetSwapchainFormat()},
                .depthAttachmentFormat = GfxBackend::GetValidDepthStencilFormat(),
                .stencilAttachmentFormat = GfxBackend::GetValidDepthStencilFormat()
            };

            gFwd.pLight = GfxBackend::CreateGraphicsPipeline(*shader, gFwd.pLightLayout, pDesc);

    #if !CONFIG_GFX_IMMUTABLE_SAMPLERS
            GfxBufferDesc dbDesc {
                .sizeBytes = GfxHelperDescriptorBuffer::GetMemoryRequirement(gFwd.pLightLayout,
                                                                             (uint32)RDescriptorSetIndex::Samplers,
                                                                             CountOf(R_SAMPLER_DESCS)),
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::SamplerDescriptorBuffer | GfxBufferUsageFlags::ShaderDeviceAddress,
                .arena = GfxMemoryArena::PersistentAddressGPU
            };
            gFwd.dbImmutableSamplers = GfxBackend::CreateBuffer(dbDesc);
    #endif
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
                    .size = sizeof(RLightCullDebugShaderFrameData),
                }
            };

            GfxPipelineLayoutDesc layoutDesc { 
                .type = GfxPipelineLayoutType::PushDescriptor,
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
                .colorAttachmentFormats = {GfxBackend::GetSwapchainFormat()}
            };

            gFwd.pLightCullDebug = GfxBackend::CreateGraphicsPipeline(*shader, gFwd.pLightCullDebugLayout, pDesc);
        }

        //--------------------------------------------------------------------------------------------------------------
        // TextureDebug
        {
            AssetObjPtrScope<GfxShader> shader(gFwd.sTextureDebugDepth);
            GfxPipelineLayoutDesc::Binding bindings[] = {
                {
                    .name = "DepthTexture",
                    .type = GfxDescriptorType::SampledImage,
                    .stagesUsed = GfxShaderStage::Fragment
                },
                {
                    .name = "TextureSampler",
                    .type = GfxDescriptorType::Sampler,
                    .stagesUsed = GfxShaderStage::Fragment
                }
            };

            GfxPipelineLayoutDesc::PushConstant pushConstants[] = {
                {
                    .name = "PerFrameData",
                    .stagesUsed = GfxShaderStage::Fragment,
                    .size = sizeof(RTextureDebugShaderFrameData)
                }
            };

            GfxPipelineLayoutDesc layoutDesc { 
                .type = GfxPipelineLayoutType::PushDescriptor,
                .numBindings = CountOf(bindings),
                .bindings = bindings,
                .numPushConstants = CountOf(pushConstants),
                .pushConstants = pushConstants
            };

            gFwd.pTextureDebugDepthLayout = GfxBackend::CreatePipelineLayout(*shader, layoutDesc);

            GfxGraphicsPipelineDesc pDesc {
                .rasterizer = {
                    .cullMode = GfxCullMode::Back
                },
                .blend = {
                    .numAttachments = 1,
                    .attachments = GfxBlendAttachmentDesc::GetDefault()
                },
                .numColorAttachments = 1,
                .colorAttachmentFormats = {GfxBackend::GetSwapchainFormat()}
            };

            {
                // Only used for debugging the sun shadow map, which has an ortho projection, so depth is already linear
                GfxShaderPermutationVar permuts[] = {
                    GfxShaderPermutationVar("LinearizeDepthTexture", false)
                };
                gFwd.pTextureDebugDepth = GfxBackend::CreateGraphicsPipeline(*shader, gFwd.pTextureDebugDepthLayout, pDesc, CountOf(permuts), permuts);
            }
        }
    }
} // R

void R::GetCompatibleLayout(GeometryVertexLayout& outLayout)
{
    memset(&outLayout, 0x0, sizeof(outLayout));
    memcpy(outLayout.vertexAttributes, R_VERTEX_ATTRIBUTES, sizeof(GfxVertexInputAttributeDesc)*CountOf(R_VERTEX_ATTRIBUTES));
    memcpy(outLayout.vertexBufferStrides, R_VERTEXBUFFER_STRIDES, sizeof(uint32)*CountOf(R_VERTEXBUFFER_STRIDES));
}

bool R::Initialize()
{
    const SettingsJunkyard& settings = SettingsJunkyard::Get();

    if (settings.graphics.msaa != 1 && 
        settings.graphics.msaa != 2 &&
        settings.graphics.msaa != 4 &&
        settings.graphics.msaa != 8 &&
        settings.graphics.msaa != 16)
    {
        LOG_ERROR("Invalid MSAA value in settings (%u). Should be either 1/2/4/8/16", settings.graphics.msaa);
        return false;
    }

    bool debugAllocs = settings.engine.debugAllocations;
    gFwd.frameAlloc.Initialize(SIZE_MB, SIZE_KB*128, debugAllocs);
    Engine::RegisterVMAllocator(&gFwd.frameAlloc, "Render");

    //------------------------------------------------------------------------------------------------------------------
    // Samplers 
    for (uint32 i = 0; i < CountOf(R_SAMPLER_DESCS); i++)
        gFwd.samplers[i] = GfxBackend::CreateSampler(R_SAMPLER_DESCS[i]);

    {
        uint32 bindings[uint32(RSamplerType::_Count)];
        for (uint32 i = 0; i < uint32(RSamplerType::_Count); i++) 
            bindings[i] = i;
        GfxImmutableSamplersDesc desc {
            .descriptorSetIndex = uint32(RDescriptorSetIndex::Samplers),
            .numSamplers = uint32(RSamplerType::_Count),
            .samplerBindings = bindings,
            .samplers = gFwd.samplers
        };
        GfxBackend::SetupImmutableSamplers(desc);
    }

    //------------------------------------------------------------------------------------------------------------------
    // Load shaders and initialize pipelines afterwards
    const AssetGroup& assetGroup = Engine::RegisterInitializeResources([](void*) {
        _CreatePipelines();
    }, nullptr);

    {
        gFwd.sZPrepass = Shader::Load("/shaders/ZPrepass.hlsl", ShaderLoadParams{}, assetGroup);

        ShaderLoadParams loadParams {
            .compileDesc = {
                .numDefines = 1,
                .defines = {{ .define = "HAS_ALPHA_MASK", .value= "1" }}
            }
        };
        gFwd.sZPrepassAlphaMask = Shader::Load("/shaders/ZPrepass.hlsl", loadParams, assetGroup);
    }

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
                        .value = String32::Format("%d", settings.graphics.msaa)
                    }
                }
            }
        };
        gFwd.sLightCull = Shader::Load("/shaders/LightCull.hlsl", loadParams, assetGroup);
        gFwd.sLight = Shader::Load("/shaders/FwdPlusLight.hlsl", loadParams, assetGroup);
        gFwd.sLightCullDebug = Shader::Load("/shaders/LightCullDebug.hlsl", loadParams, assetGroup);
    }

    {
        ShaderLoadParams loadParams {
            .compileDesc = {
                .numDefines = 2,
                .defines = {
                    {
                        .define = "IS_DEPTH_TEXTURE",
                        .value = "1"
                    },
                    {
                        .define = "MSAA",
                        .value = String32::Format("%d", settings.graphics.msaa)
                    }
                }
            }
        };
        gFwd.sTextureDebugDepth = Shader::Load("/shaders/TextureDebug.hlsl", loadParams, assetGroup);

        loadParams = {
            .compileDesc = {
                .numDefines = 1,
                .defines = {
                    {
                        .define = "MSAA",
                        .value = String32::Format("%d", settings.graphics.msaa)
                    }
                }
            }
        };
        gFwd.sTextureDebugColor = Shader::Load("/shaders/TextureDebug.hlsl", loadParams, assetGroup);
    }

    gFwd.initialized = true;
    LOG_INFO("(init) 3D renderer initialized");
    return true;
}

void R::Release()
{
    Engine::UnregisterVMAllocator(&gFwd.frameAlloc);

    // Views that are not destroyed by the user
    for (RFwdLightViewData& data : gFwd.fwdLightViewPool)
        _DestroyFwdLightViewResources(data);
    for (RShadowMapViewData& data : gFwd.shadowMapViewPool)
        _DestroyShadowMapViewResources(data);
    gFwd.fwdLightViewPool.Free();
    gFwd.shadowMapViewPool.Free();
    gFwd.viewPool.Free();

    GfxBackend::DestroyPipeline(gFwd.pShadowMap);

    GfxBackend::DestroyPipeline(gFwd.pZPrepass);
    GfxBackend::DestroyPipelineLayout(gFwd.pZPrepassLayout);

    GfxBackend::DestroyPipeline(gFwd.pZPrepassAlphaMask);
    GfxBackend::DestroyPipelineLayout(gFwd.pZPrepassLayoutAlphaMask);

    GfxBackend::DestroyPipeline(gFwd.pLightCull);
    GfxBackend::DestroyPipelineLayout(gFwd.pLightCullLayout);

    GfxBackend::DestroyPipeline(gFwd.pLightCullDebug);
    GfxBackend::DestroyPipelineLayout(gFwd.pLightCullDebugLayout);

    GfxBackend::DestroyPipeline(gFwd.pLight);
    GfxBackend::DestroyPipelineLayout(gFwd.pLightLayout);

    GfxBackend::DestroyPipeline(gFwd.pTextureDebugDepth);
    GfxBackend::DestroyPipelineLayout(gFwd.pTextureDebugDepthLayout);

    GfxBackend::DestroyPipeline(gFwd.pTextureDebugColor);
    GfxBackend::DestroyPipelineLayout(gFwd.pTextureDebugColorLayout);

    for (uint32 i = 0; i < uint32(RSamplerType::_Count); i++)
        GfxBackend::DestroySampler(gFwd.samplers[i]);    
#if !CONFIG_GFX_IMMUTABLE_SAMPLERS
    GfxBackend::DestroyBuffer(gFwd.dbImmutableSamplers);
#endif

    gFwd.frameAlloc.Release();
    gFwd.initialized = false;
}

void R::FwdLight::Update(RView& view, GfxCommandBuffer& cmd)
{
    PROFILE_ZONE("R_FwdLightUpdate");
    RViewData& viewData = gFwd.viewPool.Data(view.mHandle);
    ASSERT_MSG(viewData.type == RViewType::FwdLight, "View is not FwdLight type");
    RFwdLightViewData& fwdData = gFwd.fwdLightViewPool.Data(viewData.fwdLightHandle);

    // We need to do Buffer creation here because at init time shaders are still loading and the program might call CreateView before pipelines are created
    if (!fwdData.ubZPrepass.IsValid())
        _CreateFwdLightViewBuffers(fwdData);
    if (fwdData.targetWidth != viewData.viewWidth || fwdData.targetHeight != viewData.viewHeight)
        _ResizeFwdLightViewTargets(fwdData, viewData.viewWidth, viewData.viewHeight);

    Mat4 worldToClipMat = viewData.worldToClipMat;
    if (cmd.mDrawsToSwapchain) // TODO: this is not gonna detect swapchain properly
        worldToClipMat = GfxBackend::GetSwapchainTransformMat() * worldToClipMat;
    uint32 numTiles = fwdData.tilesCountX * fwdData.tilesCountY;

    {
        GfxHelperBufferUpdateScope updater(cmd, fwdData.ubZPrepass, uint32(-1), GfxShaderStage::Vertex|GfxShaderStage::Fragment);
        memcpy(updater.mData, &worldToClipMat, sizeof(Mat4));
    }

    // Per-frame light culling data
    {
        GfxHelperBufferUpdateScope updater(cmd, fwdData.ubLightCull, uint32(-1), GfxShaderStage::Compute);
        RLightCullShaderFrameData* buffer = (RLightCullShaderFrameData*)updater.mData;
        buffer->worldToViewMat = viewData.worldToViewMat;
        buffer->clipToViewMat = Mat4::Inverse(viewData.viewToClipMat);
        buffer->cameraNear = viewData.nearDist;
        buffer->cameraFar = viewData.farDist;
        buffer->numLights = viewData.numLights;
        buffer->windowWidth = viewData.viewWidth;
        buffer->windowHeight = viewData.viewHeight;
    }

    // Per-frame lighting data
    {
        GfxHelperBufferUpdateScope uniformBufferUpdater(cmd, fwdData.ubLightPerFrameData, uint32(-1), GfxShaderStage::Vertex);
        RLightShaderFrameData* frameData = (RLightShaderFrameData*)uniformBufferUpdater.mData;
        frameData->worldToClipMat = worldToClipMat;
        frameData->worldToSunLightClipMat = viewData.sunLightWorldToClipMat;
        frameData->sunLightDir = viewData.sunLightDir;
        frameData->sunLightColor = viewData.sunLightColor;
        frameData->skyAmbientColor = viewData.skyAmbientColor;
        frameData->groundAmbientColor = viewData.groundAmbientColor;
        frameData->tilesCountX = fwdData.tilesCountX;

        GfxHelperBufferUpdateScope descriptorBufferUpdater(cmd, fwdData.dbLightPerFrameResources, uint32(-1), GfxShaderStage::Vertex);
        GfxHelperDescriptorBuffer descriptorBuffer(descriptorBufferUpdater.mData, gFwd.pLightLayout, (uint32)RDescriptorSetIndex::PerFrame);

        GfxBindingDesc perFrameBindings[] = {
            {
                .name = "PerFrameData",
                .buffer = fwdData.ubLightPerFrameData,
            },
            {
                .name = "VisibleLightIndices",
                .buffer = fwdData.bVisibleLightIndices,
            },
            {
                .name = "LocalLights",
                .buffer = fwdData.bLightProps
            },
            {
                .name = "LocalLightBounds",
                .buffer = fwdData.bLightBounds
            },
            {
                .name = "ShadowMap",
                .image = viewData.sunShadowMapImage
            }
        };

        descriptorBuffer.WriteBindings(0, CountOf(perFrameBindings), perFrameBindings);
    }
    cmd.TransitionBuffer(fwdData.dbLightPerFrameResources, GfxBufferTransition::DescriptorBufferRead);

    // Per-Object lighting data
    {
        GfxHelperBufferUpdateScope uniformBufferUpdater(cmd, fwdData.ubLightPerObjectData, uint32(-1), GfxShaderStage::Vertex);
        GfxHelperUniformBuffer uniformBuffer(uniformBufferUpdater.mData, sizeof(RLightShaderObjectData), R_MAX_DRAW_OBJECTS);

        GfxHelperBufferUpdateScope descriptorBufferUpdater(cmd, fwdData.dbLightPerObjectResources, uint32(-1), GfxShaderStage::Vertex);
        GfxHelperDescriptorBuffer descriptorBuffer(descriptorBufferUpdater.mData, gFwd.pLightLayout, (uint32)RDescriptorSetIndex::PerObject);

        uint32 index = 0;
        RGeometryChunk* chunk = viewData.chunkList;
        while (chunk) {
            for (uint32 sc = 0; sc < chunk->numSubChunks; sc++) {
                RGeometrySubChunk& subChunk = chunk->subChunks[sc];
                ASSERT_MSG(index < R_MAX_DRAW_OBJECTS, "Too many objects are being drawn. Increase R_MAX_DRAW_OBJECTS");
                subChunk._drawItemIndex = index;

                RLightShaderObjectData objectData {
                    .localToWorldMat = chunk->localToWorldMat,
                    .materialTintColor = Color4u::ToFloat4(subChunk.tintColor)
                };

                GfxBindingDesc bindings[] = {
                    {
                        .name = "PerObjectData",
                        .buffer = fwdData.ubLightPerObjectData,
                        .bufferRange = {
                            .offset = uniformBuffer.Write<RLightShaderObjectData>(index, objectData),
                            .size = sizeof(RLightShaderObjectData)
                        }
                    },
                    {
                        .name = "BaseColorTexture",
                        .image = subChunk.baseColorImg
                    }
                };

                descriptorBuffer.WriteBindings(index, CountOf(bindings), bindings);     
                ++index;
            }

            chunk = chunk->nextChunk;
        }
    }
    cmd.TransitionBuffer(fwdData.dbLightPerObjectResources, GfxBufferTransition::DescriptorBufferRead);

    if (viewData.numLights) {
        GfxHelperBufferUpdateScope lightBoundsUpdater(cmd, fwdData.bLightBounds, sizeof(RLightBounds)*viewData.numLights,
                                                      GfxShaderStage::Compute);
        RLightBounds* bounds = (RLightBounds*)lightBoundsUpdater.mData;
        memcpy(bounds, viewData.lightBounds, sizeof(RLightBounds)*viewData.numLights);

        GfxHelperBufferUpdateScope lightPropsUpdater(cmd, fwdData.bLightProps, sizeof(RLightProps)*viewData.numLights,
                                                     GfxShaderStage::Fragment);
        RLightProps* props = (RLightProps*)lightPropsUpdater.mData;
        memcpy(props, viewData.lightProps, sizeof(RLightProps)*viewData.numLights);
    }
    else {
        // Fill visible light indices buffer with sentinels (empty state)
        GfxHelperBufferUpdateScope updater(cmd, fwdData.bVisibleLightIndices, uint32(-1), GfxShaderStage::Fragment);
        uint32* indices = (uint32*)updater.mData;
        for (uint32 i = 0; i < numTiles; i++)
            indices[i*R_LIGHT_CULL_MAX_LIGHTS_PER_TILE] = 0xffffffff;
    }

#if !CONFIG_GFX_IMMUTABLE_SAMPLERS
    if (!gFwd.immutableSamplersUpdated) {
        GfxHelperBufferUpdateScope samplersBufferUpdater(cmd, gFwd.dbImmutableSamplers, uint32(-1), GfxShaderStage::Vertex);
        GfxHelperDescriptorBuffer samplersDescriptorBuffer(samplersBufferUpdater.mData, gFwd.pLightLayout, (uint32)RDescriptorSetIndex::Samplers);
        GfxBindingDesc bindings[uint32(RSamplerType::_Count)] = {
            {
                .name = R_SAMPLER_NAMES[0],
                .sampler = gFwd.samplers[0],
            },
            {
                .name = R_SAMPLER_NAMES[1],
                .sampler = gFwd.samplers[1],
            },
            {
                .name = R_SAMPLER_NAMES[2],
                .sampler = gFwd.samplers[2],
            }
        };
        samplersDescriptorBuffer.WriteBindings(0, CountOf(bindings), bindings);
        gFwd.immutableSamplersUpdated = true;
    }
#endif
}

void R::FwdLight::Render(RView& view, GfxCommandBuffer& cmd, GfxImageHandle finalColorImage, GfxImageHandle finalDepthImage, 
                         RDebugMode debugMode)
{
    PROFILE_ZONE("R_FwdLight");

    uint32 msaa = SettingsJunkyard::Get().graphics.msaa;
    RViewData& viewData = gFwd.viewPool.Data(view.mHandle);
    ASSERT_MSG(viewData.type == RViewType::FwdLight, "View is not FwdLight type");
    RFwdLightViewData& fwdData = gFwd.fwdLightViewPool.Data(viewData.fwdLightHandle);
    ASSERT_MSG(fwdData.targetWidth == viewData.viewWidth && fwdData.targetHeight == viewData.viewHeight,
               "View size has changed after FwdLight::Update");

    GfxImageHandle renderDepthImage = msaa > 1 ? fwdData.msaaDepthImage : finalDepthImage;
    ASSERT(renderDepthImage.IsValid());

    // Render blank screen if we have nothing to render
    if (viewData.numGeometryChunks == 0) {
        GfxBackendRenderPass pass { 
            .numAttachments = finalColorImage.IsValid() ? 1u : 0u,
            .colorAttachments = {{ 
                .image = finalColorImage,
                .clear = true,
                .clearValue = {
                    .color = Color4u::ToFloat4(COLOR4U_BLACK)
                }
            }},
            .swapchain = !finalColorImage.IsValid(),
            .hasDepth = false
        };

        if (finalColorImage.IsValid())
            cmd.TransitionImage(finalColorImage, GfxImageTransition::RenderTarget);

        cmd.BeginRenderPass(pass);
        cmd.EndRenderPass();

        return;
    }

    // Z-Prepass (Normal + AlphaMasked)
    {
        MemTempAllocator tempAlloc;
        struct AlphaMaskChunkRef 
        {
            RGeometryChunk* chunk;
            uint32 subChunkIdx;
        };

        Array<AlphaMaskChunkRef> alphaMaskedRefs(&tempAlloc);

        GPU_PROFILE_ZONE(cmd, "Z-Prepass");
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

        {
            cmd.BindPipeline(gFwd.pZPrepass);
            R::_SetViewportAndScissor(cmd, viewData.viewWidth, viewData.viewHeight);

            RGeometryChunk* chunk = viewData.chunkList;
            while (chunk) {
                cmd.PushConstants(gFwd.pZPrepassLayout, "PerObjectData", &chunk->localToWorldMat, sizeof(Mat4));
                cmd.BindVertexBuffers(0, 1, &chunk->posVertexBuffer, &chunk->posVertexBufferOffset);
                cmd.BindIndexBuffer(chunk->indexBuffer, chunk->indexBufferOffset, GfxIndexType::Uint32);

                GfxBindingDesc bindings[] = {
                    {
                        .name = "PerFrameData",
                        .buffer = fwdData.ubZPrepass
                    }
                };
                cmd.PushBindings(gFwd.pZPrepassLayout, CountOf(bindings), bindings);
            
                uint32 numIndices = 0;
                bool hasAlphaMask = false;
                for (uint32 sc = 0; sc < chunk->numSubChunks; sc++) {
                    const RGeometrySubChunk& subChunk = chunk->subChunks[sc];
                    numIndices += subChunk.numIndices;

                    if (subChunk.hasAlphaMask) {
                        AlphaMaskChunkRef ref { 
                            .chunk = chunk,
                            .subChunkIdx = sc
                        };
                        alphaMaskedRefs.Push(ref);
                        hasAlphaMask = true;
                    }
                }

                if (!hasAlphaMask) {
                    cmd.DrawIndexed(numIndices, 1, 0, 0, 0);
                }
                else {
                    for (uint32 sc = 0; sc < chunk->numSubChunks; sc++) {
                        const RGeometrySubChunk& subChunk = chunk->subChunks[sc];
                        if (!subChunk.hasAlphaMask)
                            cmd.DrawIndexed(subChunk.numIndices, 1, subChunk.startIndex, 0, 0);
                    }
                }

                chunk = chunk->nextChunk;
            }
        }
        cmd.EndRenderPass();

        // Z-Prepass (Alpha Masked): The chunks and their subchunks that has AlphaMask has already been gathered in the previous step
        if (!alphaMaskedRefs.IsEmpty()) {
            GfxBackendRenderPass zprepassExtra {
                .numAttachments = 1,
                .colorAttachments = {{
                    .image = fwdData.msaaColorImage,
                    .ignoreStore = true
                }},
                .depthAttachment = {
                    .image = renderDepthImage,
                    .load = true
                },
                .hasDepth = true
            };

            cmd.BeginRenderPass(zprepassExtra);

            cmd.BindPipeline(gFwd.pZPrepassAlphaMask);
            R::_SetViewportAndScissor(cmd, viewData.viewWidth, viewData.viewHeight);

            for (const AlphaMaskChunkRef& ref : alphaMaskedRefs) {
                RGeometryChunk* chunk = ref.chunk;
                const RGeometrySubChunk& subChunk = chunk->subChunks[ref.subChunkIdx];
                const GfxBufferHandle vertexBuffers[] = {
                    chunk->posVertexBuffer,
                    chunk->lightingVertexBuffer
                };

                const uint64 vertexBufferOffsets[] = {
                    chunk->posVertexBufferOffset,
                    chunk->lightingVertexBufferOffset
                };

                cmd.PushConstants(gFwd.pZPrepassLayout, "PerObjectData", &chunk->localToWorldMat, sizeof(Mat4));
                cmd.BindVertexBuffers(0, 2, vertexBuffers, vertexBufferOffsets);
                cmd.BindIndexBuffer(chunk->indexBuffer, chunk->indexBufferOffset, GfxIndexType::Uint32);                

                GfxBindingDesc bindings[] = {
                    {
                        .name = "PerFrameData",
                        .buffer = fwdData.ubZPrepass
                    },
                    {
                        .name = "ColorTexture",
                        .image = subChunk.baseColorImg,
                        .sampler = gFwd.samplers[uint32(RSamplerType::TrilinearWrap)]
                    }
                };
                cmd.PushBindings(gFwd.pZPrepassLayoutAlphaMask, CountOf(bindings), bindings);
                cmd.DrawIndexed(subChunk.numIndices, 1, subChunk.startIndex, 0, 0);
            }

            cmd.EndRenderPass();
        }

    }   // ZPrepass 

    // Light culling
    if (viewData.numLights) {
        GPU_PROFILE_ZONE(cmd, "LightCull");
        cmd.TransitionImage(renderDepthImage, GfxImageTransition::ShaderRead);
        cmd.TransitionBuffer(fwdData.bVisibleLightIndices, GfxBufferTransition::ComputeWrite);

        GfxBindingDesc bindings[] = {
            {
                .name = "PerFrameData", 
                .buffer = fwdData.ubLightCull
            },
            {
                .name = "Lights",
                .buffer = fwdData.bLightBounds
            },
            {
                .name = "VisibleLightIndices",
                .buffer = fwdData.bVisibleLightIndices
            },
            {
                .name = "DepthTexture",
                .image = renderDepthImage
            }
        };

        cmd.BindPipeline(gFwd.pLightCull);
        cmd.PushBindings(gFwd.pLightCullLayout, CountOf(bindings), bindings);
        cmd.Dispatch(fwdData.tilesCountX, fwdData.tilesCountY, 1);

        cmd.TransitionBuffer(fwdData.bVisibleLightIndices, GfxBufferTransition::FragmentRead);
    }

    cmd.TransitionImage(renderDepthImage, GfxImageTransition::RenderTarget, GfxImageTransitionFlags::DepthRead);

    // Light pass
    if (debugMode == RDebugMode::None) {
        GPU_PROFILE_ZONE(cmd, "LightPass");
        if (msaa > 1 && finalDepthImage.IsValid()) {
            cmd.TransitionImage(finalDepthImage, GfxImageTransition::RenderTarget, 
                                GfxImageTransitionFlags::DepthWrite|GfxImageTransitionFlags::DepthResolve);
        }

        if (viewData.sunShadowMapImage.IsValid())
            cmd.TransitionImage(viewData.sunShadowMapImage, GfxImageTransition::ShaderRead);

        // If finalColorImage is not provided, we render to Swapchain
        GfxImageHandle renderColorImage = msaa > 1 ? fwdData.msaaColorImage : finalColorImage;

        // Render to swapchain if we don't have MSAA, otherwise, resolve to Swapchain and provided depth buffer
        if (renderColorImage.IsValid())
            cmd.TransitionImage(renderColorImage, GfxImageTransition::RenderTarget);
        if (msaa > 1 && finalColorImage.IsValid())
            cmd.TransitionImage(finalColorImage, GfxImageTransition::RenderTarget);

        GfxBackendRenderPass pass { 
            .numAttachments = 1,
            .colorAttachments = {{ 
                .image = renderColorImage,
                .resolveImage = msaa > 1 ? finalColorImage : GfxImageHandle(),
                .clear = true,
                .resolveToSwapchain = msaa > 1 && !finalColorImage.IsValid(),
                .clearValue = {
                    .color = viewData.skyAmbientColor
                }
            }},
            .depthAttachment = {
                .image = renderDepthImage,
                .resolveImage = msaa > 1 ? finalDepthImage : GfxImageHandle(),
                .load = true,
                .clear = false,
            },
            .swapchain = !renderColorImage.IsValid(), 
            .hasDepth = true
        };

        cmd.BeginRenderPass(pass);
        cmd.BindPipeline(gFwd.pLight);        
        R::_SetViewportAndScissor(cmd, viewData.viewWidth, viewData.viewHeight);

        GfxBufferHandle descriptorBuffers[] = {
            fwdData.dbLightPerFrameResources,
            fwdData.dbLightPerObjectResources,
#if !CONFIG_GFX_IMMUTABLE_SAMPLERS
            gFwd.dbImmutableSamplers
#endif
        };

        cmd.BindDescriptorBuffers(CountOf(descriptorBuffers), descriptorBuffers);
        cmd.SetDescriptorBufferOffset(gFwd.pLightLayout, (uint32)RDescriptorSetIndex::PerFrame, 0);
#if !CONFIG_GFX_IMMUTABLE_SAMPLERS
        cmd.SetDescriptorBufferOffset(gFwd.pLightLayout, (uint32)RDescriptorSetIndex::Samplers, 0);
#endif
        cmd.EnableAlphaToCoverage(false);

        bool alphaToCoverageEnabled = false;
        RGeometryChunk* chunk = viewData.chunkList;
        while (chunk) {
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

                if (subChunk.hasAlphaMask != alphaToCoverageEnabled) {
                    cmd.EnableAlphaToCoverage(subChunk.hasAlphaMask);
                    alphaToCoverageEnabled = subChunk.hasAlphaMask;
                }

                cmd.SetDescriptorBufferOffset(gFwd.pLightLayout, (uint32)RDescriptorSetIndex::PerObject, subChunk._drawItemIndex);
                cmd.DrawIndexed(subChunk.numIndices, 1, subChunk.startIndex, 0, 0);
            }

            chunk = chunk->nextChunk;
        }

        cmd.EndRenderPass();
    }
    else if (debugMode == RDebugMode::LightCull) {
        _BeginDebugRenderPass(cmd, finalColorImage, FLOAT4_ZERO);
        cmd.BindPipeline(gFwd.pLightCullDebug);
        R::_SetViewportAndScissor(cmd, viewData.viewWidth, viewData.viewHeight);

        GfxBindingDesc bindings[] = {
            {
                .name = "VisibleLightIndices",
                .buffer = fwdData.bVisibleLightIndices
            }
        };

        cmd.PushBindings(gFwd.pLightCullDebugLayout, CountOf(bindings), bindings);

        RLightCullDebugShaderFrameData perFrameData {
            .tilesCountX = fwdData.tilesCountX,
            .tilesCountY = fwdData.tilesCountY
        };
        cmd.PushConstants<RLightCullDebugShaderFrameData>(gFwd.pLightCullDebugLayout, "PerFrameData", perFrameData);

        cmd.Draw(3, 1, 0, 0);

        cmd.EndRenderPass();
    }
    else if (debugMode == RDebugMode::SunShadowMap) {
        cmd.TransitionImage(viewData.sunShadowMapImage, GfxImageTransition::ShaderRead);

        _BeginDebugRenderPass(cmd, finalColorImage, FLOAT4_UNITZ);
        cmd.BindPipeline(gFwd.pTextureDebugDepth);
        R::_SetViewportAndScissor(cmd, viewData.viewWidth, viewData.viewHeight);   // Viewport gets letterboxed below

        // Letterboxing (TODO: decouple this code into a function)
        {
            GfxImageDesc imageDesc = GfxBackend::GetImageDesc(viewData.sunShadowMapImage);
            GfxViewport vp {};

            float fbWidth = float(viewData.viewWidth);
            float fbHeight = float(viewData.viewHeight);
            float imageWidth = float(imageDesc.width);
            float imageHeight = float(imageDesc.height);
            float scaleX = fbWidth /  imageWidth;
            float scaleY = fbHeight / imageHeight;
            float scale = (scaleX < scaleY) ? scaleX : scaleY;
            vp.width = imageWidth * scale;
            vp.height = imageHeight * scale;
            vp.x = (fbWidth - vp.width) * 0.5f;
            vp.y = (fbHeight - vp.height) * 0.5f;

            cmd.SetViewports(0, 1, &vp);
        }

        GfxBindingDesc bindings[] = {
            {
                .name = "DepthTexture",
                .image = viewData.sunShadowMapImage
            },
            {
                .name = "TextureSampler",
                .sampler = gFwd.samplers[uint32(RSamplerType::BilinearClamp)]
            }
        };

        cmd.PushBindings(gFwd.pTextureDebugDepthLayout, CountOf(bindings), bindings);

        RTextureDebugShaderFrameData perFrameData {
            .cameraNear = viewData.nearDist,
            .cameraFar = viewData.farDist
        };
        cmd.PushConstants<RTextureDebugShaderFrameData>(gFwd.pTextureDebugDepthLayout, "PerFrameData", perFrameData);

        cmd.Draw(3, 1, 0, 0);

        cmd.EndRenderPass();
    }
}

void RView::SetCamera(const Camera& cam, Float2 viewSize)
{
    RViewData& viewData = gFwd.viewPool.Data(mHandle);

    viewData.worldToViewMat = cam.GetViewMat();

    if (viewData.type == RViewType::ShadowMap)
        viewData.viewToClipMat = cam.GetOrthoMat(viewSize.x, viewSize.y);
    else 
        viewData.viewToClipMat = cam.GetPerspectiveMat(viewSize.x, viewSize.y);
    viewData.worldToClipMat = viewData.viewToClipMat * viewData.worldToViewMat;

    viewData.nearDist = cam.Near();
    viewData.farDist = cam.Far();
    viewData.viewWidth = Max<uint32>(uint32(viewSize.x), 1);
    viewData.viewHeight = Max<uint32>(uint32(viewSize.y), 1);
}

void RView::SetLocalLights(uint32 numLights, const RLightBounds* bounds, const RLightProps* props)
{
    RViewData& viewData = gFwd.viewPool.Data(mHandle);

    viewData.numLights = numLights;

    if (numLights) {
        viewData.lightBounds = Mem::AllocTyped<RLightBounds>(numLights, &gFwd.frameAlloc);
        viewData.lightProps = Mem::AllocTyped<RLightProps>(numLights, &gFwd.frameAlloc);
        memcpy(viewData.lightBounds, bounds, sizeof(RLightBounds)*numLights);
        memcpy(viewData.lightProps, props, sizeof(RLightProps)*numLights);
    }
}

Mat4 RView::GetWorldToClipMat() const
{
    RViewData& viewData = gFwd.viewPool.Data(mHandle);
    return viewData.worldToClipMat;
}

void RView::SetAmbientLight(Float4 skyAmbientColor, Float4 groundAmbientColor)
{
    RViewData& vdata = gFwd.viewPool.Data(mHandle);
    vdata.skyAmbientColor = Color4u::ToFloat4Linear(skyAmbientColor);
    vdata.groundAmbientColor = Color4u::ToFloat4Linear(groundAmbientColor);
}

void RView::SetSunLight(Float3 direction, Float4 color, GfxImageHandle shadowMapImage, const Mat4& sunlightWorldToClipMat)
{
    RViewData& viewData = gFwd.viewPool.Data(mHandle);
    viewData.sunLightDir = Float3::Norm(direction);
    viewData.sunLightColor = Color4u::ToFloat4Linear(color);
    viewData.sunShadowMapImage = shadowMapImage;
    viewData.sunLightWorldToClipMat = sunlightWorldToClipMat;
}

RGeometryChunk* RView::NewGeometryChunk()
{
    RViewData& viewData = gFwd.viewPool.Data(mHandle);

    RGeometryChunk* chunk = Mem::AllocZeroTyped<RGeometryChunk>(1, &gFwd.frameAlloc);
    chunk->localToWorldMat = MAT4_IDENT;

    if (viewData.lastChunk)
        viewData.lastChunk->nextChunk = chunk;
    else
        viewData.chunkList = chunk;
    viewData.lastChunk = chunk;

    ++viewData.numGeometryChunks;

    return chunk;        
}

void R::NewFrame()
{
    ASSERT(gFwd.initialized);
    gFwd.frameAlloc.Reset();

    for (RViewData& vdata : gFwd.viewPool) {
        vdata.chunkList = nullptr;
        vdata.lastChunk = nullptr;
        vdata.lightBounds = nullptr;
        vdata.lightProps = nullptr;
        vdata.numLights = 0;
        vdata.numGeometryChunks = 0;
    }
}

RView R::CreateView(RViewType viewType)
{
    RViewData data {
        .type = viewType,
    };

    switch (viewType) {
    case RViewType::FwdLight:
        data.fwdLightHandle = gFwd.fwdLightViewPool.Add(RFwdLightViewData {});
        break;
    case RViewType::ShadowMap:
        data.shadowMapHandle = gFwd.shadowMapViewPool.Add(RShadowMapViewData {});
        break;
    default:
        ASSERT(0);
        break;
    }

    RViewHandle handle = gFwd.viewPool.Add(data);
    RView view {
        .mHandle = handle,
        .mThreadId = Thread::GetCurrentId()
    };

    return view;
}

void R::DestroyView(RView& view)
{
    if (!view.mHandle.IsValid())
        return;

    RViewData& viewData = gFwd.viewPool.Data(view.mHandle);
    switch (viewData.type) {
    case RViewType::FwdLight:
        _DestroyFwdLightViewResources(gFwd.fwdLightViewPool.Data(viewData.fwdLightHandle));
        gFwd.fwdLightViewPool.Remove(viewData.fwdLightHandle);
        break;
    case RViewType::ShadowMap:
        _DestroyShadowMapViewResources(gFwd.shadowMapViewPool.Data(viewData.shadowMapHandle));
        gFwd.shadowMapViewPool.Remove(viewData.shadowMapHandle);
        break;
    default:
        ASSERT(0);
        break;
    }

    gFwd.viewPool.Remove(view.mHandle);
    view = {};
}

void R::ShadowMap::Update(RView& view, GfxCommandBuffer& cmd)
{
    PROFILE_ZONE("R_ShadowMapUpdate");
    RViewData& viewData = gFwd.viewPool.Data(view.mHandle);
    ASSERT_MSG(viewData.type == RViewType::ShadowMap, "View is not ShadowMap type");
    RShadowMapViewData& shadowData = gFwd.shadowMapViewPool.Data(viewData.shadowMapHandle);
    Mat4 worldToClipMat = viewData.worldToClipMat;

    if (!shadowData.ubShadowMap.IsValid()) {
        GfxBufferDesc bufferDesc {
            .sizeBytes = sizeof(Mat4),
            .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Uniform,
            .arena = GfxMemoryArena::DynamicBufferGPU,
            .perFrameUpdates = true
        };
        shadowData.ubShadowMap = GfxBackend::CreateBuffer(bufferDesc);
    }

    {
        GfxHelperBufferUpdateScope updater(cmd, shadowData.ubShadowMap, uint32(-1), GfxShaderStage::Vertex);
        memcpy(updater.mData, &worldToClipMat, sizeof(Mat4));
    }
}

void R::ShadowMap::Render(RView& view, GfxCommandBuffer& cmd, GfxImageHandle shadowMapDepthImage)
{
    ASSERT(shadowMapDepthImage.IsValid());

    RViewData& viewData = gFwd.viewPool.Data(view.mHandle);
    ASSERT_MSG(viewData.type == RViewType::ShadowMap, "View is not ShadowMap type");
    const RShadowMapViewData& shadowData = gFwd.shadowMapViewPool.Data(viewData.shadowMapHandle);
    ASSERT_MSG(shadowData.ubShadowMap.IsValid(), "ShadowMap::Update must be called before Render");
    
    // ShadowMap
    {
        GPU_PROFILE_ZONE(cmd, "ShaowMapRender");
        PROFILE_ZONE("R_ShadowMap");
        cmd.TransitionImage(shadowMapDepthImage, GfxImageTransition::RenderTarget, GfxImageTransitionFlags::DepthWrite);

        GfxBackendRenderPass zprepass { 
            .depthAttachment = {
                .image = shadowMapDepthImage,
                .clear = true,
                .clearValue = {
                    .depth = 1.0f
                }
            },
            .hasDepth = true
        };
        cmd.BeginRenderPass(zprepass);

        cmd.BindPipeline(gFwd.pShadowMap);
        {
            const GfxImageDesc& imgDesc = GfxBackend::GetImageDesc(shadowMapDepthImage);
            GfxViewport vp {
                .width = float(imgDesc.width),
                .height = float(imgDesc.height)
            };
            cmd.SetViewports(0, 1, &vp);

            RectInt rc(0, 0, imgDesc.width, imgDesc.height);
            cmd.SetScissors(0, 1, &rc);
        }

        RGeometryChunk* chunk = viewData.chunkList;
        while (chunk) {
            cmd.PushConstants(gFwd.pZPrepassLayout, "PerObjectData", &chunk->localToWorldMat, sizeof(Mat4));

            cmd.BindVertexBuffers(0, 1, &chunk->posVertexBuffer, &chunk->posVertexBufferOffset);
            cmd.BindIndexBuffer(chunk->indexBuffer, chunk->indexBufferOffset, GfxIndexType::Uint32);

            GfxBindingDesc bindings[] = {
                {
                    .name = "PerFrameData",
                    .buffer = shadowData.ubShadowMap
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

}

bool R::IsEnabled()
{
    return gFwd.initialized;
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

