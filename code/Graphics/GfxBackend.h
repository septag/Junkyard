#pragma once

#include "../Core/Base.h"
#include "../Core/MathTypes.h"
#include "../Common/CommonTypes.h"

#include "Graphics.h"

inline constexpr uint32 GFXBACKEND_MAX_RENDERPASS_COLOR_ATTACHMENTS = 8;
inline constexpr uint32 GFXBACKEND_MAX_MIPS_PER_IMAGE = 12;  // up to 4096

enum class GfxBackendMemoryArena : uint8 
{
    PersistentGPU = 0,
    PersistentCPU,
    TransientCPU
};

enum class GfxBackendQueueType : uint8
{
    None = 0,
    Graphics = 0x1,
    Compute = 0x2,
    Transfer = 0x4,
    Present = 0x8
};
ENABLE_BITMASK(GfxBackendQueueType);

// VkImageUsageFlagBits
enum class GfxBackendImageUsageFlags : uint32
{
    TransferSrc = 0x00000001,
    TransferDst = 0x00000002,
    Sampled = 0x00000004,
    Storage = 0x00000008,
    ColorAttachment = 0x00000010,
    DepthStencilAttachment = 0x00000020,
    TransientAttachment = 0x00000040,
    InputAttachment = 0x00000080
};
ENABLE_BITMASK(GfxBackendImageUsageFlags);

// VkBufferUsageFlagBits
enum class GfxBackendBufferUsageFlags : uint32
{
    TransferSrc = 0x00000001,
    TransferDst = 0x00000002,
    UniformTexel = 0x00000004,
    StorageTexel = 0x00000008,
    Uniform = 0x00000010,
    Storage = 0x00000020,
    Index = 0x00000040,
    Vertex = 0x00000080,
    Indirect = 0x00000100
};
ENABLE_BITMASK(GfxBackendBufferUsageFlags);

// VkImageType
enum class GfxBackendImageType : uint32
{
    Image1D = 0,
    Image2D,
    Image3D
};
ENABLE_BITMASK(GfxBackendImageType);

// VkSampleCountFlagBits
enum class GfxBackendSampleCountFlags : uint32
{
    SampleCount1 = 0x00000001,
    SampleCount2 = 0x00000002,
    SampleCount4 = 0x00000004,
    SampleCount8 = 0x00000008,
    SampleCount16 = 0x00000010,
    SampleCount32 = 0x00000020,
    SampleCount64 = 0x00000040
};
ENABLE_BITMASK(GfxBackendSampleCountFlags);

struct GfxBackendImageDesc
{
    uint16 width;
    uint16 height;
    uint16 depth = 1;
    uint16 numMips = 1;
    uint16 numArrayLayers = 1;
    GfxBackendSampleCountFlags multisampleFlags = GfxBackendSampleCountFlags::SampleCount1;
    GfxBackendImageType type = GfxBackendImageType::Image2D;
    GfxFormat format;
    GfxBackendImageUsageFlags usageFlags = GfxBackendImageUsageFlags::Sampled;
    GfxBackendMemoryArena arena;
    uint32 mipOffsets[GFXBACKEND_MAX_MIPS_PER_IMAGE];
};

struct GfxBackendPipelineLayoutDesc
{
    struct Binding 
    {
        const char* name;
        GfxDescriptorType type;
        GfxShaderStage stagesUsed;
        uint32 arrayCount = 1;
        uint8 setIndex = 0;        // DescriptorSet Id
    };

    struct PushConstant
    {
        const char* name;
        GfxShaderStage stagesUsed;
        uint32 offset;
        uint32 size;
    };
    
    uint32 numBindings;
    const Binding* bindings;
    uint32 numPushConstants;
    const PushConstant* pushConstants;
    bool usePushDescriptors = true;    
};

struct GfxBackendGraphicsPipelineDesc
{
    GfxPrimitiveTopology inputAssemblyTopology = GfxPrimitiveTopology::TriangleList;
    
    uint32 numVertexInputAttributes;
    const GfxVertexInputAttributeDesc* vertexInputAttributes;

    uint32 numVertexBufferBindings;
    const GfxVertexBufferBindingDesc* vertexBufferBindings;

    GfxRasterizerDesc rasterizer;
    GfxBlendDesc blend;
    GfxDepthStencilDesc depthStencil;
};

struct GfxBackendBindingDesc
{   
    const char* name;

    union {
        uint32 imageArrayCount = 1;

        struct {
            uint32 offset = 0;
            uint32 size = 0;
        } bufferRange;
    };

    union
    {
        GfxBufferHandle buffer;
        GfxImageHandle image;
        const GfxImageHandle* imageArray;
    };
};

struct GfxBackendBufferDesc
{
    size_t sizeBytes;
    GfxBackendBufferUsageFlags usageFlags;
    GfxBackendMemoryArena arena;
};

enum class GfxBackendBufferTransition
{
    TransferWrite
};

enum class GfxBackendImageTransition
{
    ComputeWrite,
    CopySource
};

struct GfxBackendRenderPassAttachment
{
    GfxImageHandle image;
    bool load;
    bool clear;

    union {
        Float4 clearColor;
        float clearDepth;
        uint32 clearStencil;
    };
};

struct GfxBackendRenderPass
{
    RectInt cropRect = RECTINT_EMPTY;
    uint32 numAttachments;
    GfxBackendRenderPassAttachment colorAttachments[GFXBACKEND_MAX_RENDERPASS_COLOR_ATTACHMENTS];
    GfxBackendRenderPassAttachment depthAttachment;
    GfxBackendRenderPassAttachment stencilAttachment;
    bool swapchain;
    bool hasDepth;
    bool hasStencil;
};

struct GfxBackendCommandBuffer
{
    uint32 mGeneration;
    uint16 mCmdBufferIndex;
    uint8 mQueueIndex;
    uint8 mDrawsToSwapchain : 1;

    void BeginRenderPass(const GfxBackendRenderPass& pass);
    void EndRenderPass();

    void CopyImageToSwapchain(GfxImageHandle imgHandle);
    void CopyBufferToBuffer(GfxBufferHandle srcHandle, GfxBufferHandle dstHandle, GfxShaderStage stagesUsed = GfxShaderStage::All,
                            size_t srcOffset = 0, size_t dstOffset = 0, size_t sizeBytes = 0);
    void CopyBufferToImage(GfxBufferHandle srcHandle, GfxImageHandle dstHandle, GfxShaderStage stagesUsed = GfxShaderStage::All,
                           uint16 startMipIndex = 0, uint16 mipCount = UINT16_MAX);


    void MapBuffer(GfxBufferHandle buffHandle, void** outPtr, size_t* outSizeBytes);
    void FlushBuffer(GfxBufferHandle buffHandle);

    void ClearImageColor(GfxImageHandle imgHandle, Color color);
    void ClearImageColor(GfxImageHandle imgHandle, Float4 color);
    void ClearSwapchainColor(Float4 color);

    void PushConstants(GfxPipelineLayoutHandle layoutHandle, const char* name, const void* data, uint32 dataSize);
    template <typename _T> void PushConstants(GfxPipelineLayoutHandle layout, const char* name, const _T& data);

    void PushBindings(GfxPipelineLayoutHandle layoutHandle, uint32 numBindings, const GfxBackendBindingDesc* bindings);

    void BindPipeline(GfxPipelineHandle pipeHandle);
    void Dispatch(uint32 groupCountX, uint32 groupCountY, uint32 groupCountZ);

    void TransitionBuffer(GfxBufferHandle buffHandle, GfxBackendBufferTransition transition);
    void TransitionImage(GfxImageHandle imgHandle, GfxBackendImageTransition transition);

    void Draw(uint32 vertexCount, uint32 instanceCount, uint32 firstVertex, uint32 firstInstance);
    void DrawIndexed(uint32 indexCount, uint32 instanceCount, uint32 firstIndex, uint32 vertexOffset, uint32 firstInstance);
};

namespace GfxBackend
{
    bool Initialize();
    void Release();

    void Begin();
    void End();

    void SubmitQueue(GfxBackendQueueType queueType, GfxBackendQueueType dependentQueues = GfxBackendQueueType::None);

    [[nodiscard]] GfxBackendCommandBuffer BeginCommandBuffer(GfxBackendQueueType queueType);
    void EndCommandBuffer(GfxBackendCommandBuffer cmdBuffer);

    GfxImageHandle CreateImage(const GfxBackendImageDesc& desc);
    void DestroyImage(GfxImageHandle handle);
    const GfxBackendImageDesc& GetImageDesc(GfxImageHandle handle);

    GfxPipelineLayoutHandle CreatePipelineLayout(const GfxShader& shader, const GfxBackendPipelineLayoutDesc& desc);
    void DestroyPipelineLayout(GfxPipelineLayoutHandle handle);

    GfxBufferHandle CreateBuffer(const GfxBackendBufferDesc& desc);
    void DestroyBuffer(GfxBufferHandle handle);

    GfxPipelineHandle CreateGraphicsPipeline(const GfxShader& shader, GfxPipelineLayoutHandle layoutHandle, const GfxBackendGraphicsPipelineDesc& desc);
    GfxPipelineHandle CreateComputePipeline(const GfxShader& shader, GfxPipelineLayoutHandle layoutHandle);
    void DestroyPipeline(GfxPipelineHandle handle);
} // Gfx

//----------------------------------------------------------------------------------------------------------------------
template <typename _T>
inline void GfxBackendCommandBuffer::PushConstants(GfxPipelineLayoutHandle layout, const char* name, const _T& data)
{
    PushConstants(layout, data, sizeof(data));
}
