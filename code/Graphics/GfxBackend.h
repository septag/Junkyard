#pragma once

#include "../Core/Base.h"

#include "GfxBackendTypes.h"

struct GfxCommandBuffer
{
    uint32 mGeneration;
    uint16 mCmdBufferIndex;
    uint8 mQueueIndex;
    uint8 mDrawsToSwapchain : 1;
    uint8 mIsRecording : 1;
    uint8 mIsInRenderPass : 1;
    uint8 mShouldSubmit : 1;

    void BeginRenderPass(const GfxBackendRenderPass& pass);
    void EndRenderPass();

    void CopyImageToSwapchain(GfxImageHandle imgHandle);
    void CopyBufferToBuffer(GfxBufferHandle srcHandle, GfxBufferHandle dstHandle, GfxShaderStage stagesUsed = GfxShaderStage::All,
                            size_t srcOffset = 0, size_t dstOffset = 0, size_t sizeBytes = 0);
    void CopyBufferToImage(GfxBufferHandle srcHandle, GfxImageHandle dstHandle, GfxShaderStage stagesUsed = GfxShaderStage::All,
                           uint16 startMipIndex = 0, uint16 mipCount = UINT16_MAX);

    void BatchCopyBufferToBuffer(uint32 numParams, const GfxCopyBufferToBufferParams* params);
    void BatchCopyBufferToImage(uint32 numParams, const GfxCopyBufferToImageParams* params);

    void MapBuffer(GfxBufferHandle buffHandle, void** outPtr, size_t* outSizeBytes = nullptr);
    void FlushBuffer(GfxBufferHandle buffHandle, const GfxFlushRange* range = nullptr);

    void BatchMapBuffer(uint32 numParams, const GfxBufferHandle* handles, GfxMapResult* mapResults);
    void BatchFlushBuffer(uint32 numBuffers, const GfxBufferHandle* bufferHandles, const GfxFlushRange* ranges = nullptr);

    void ClearImageColor(GfxImageHandle imgHandle, Color4u color);
    void ClearImageColor(GfxImageHandle imgHandle, Float4 color);
    void ClearSwapchainColor(Float4 color);

    // Push constants are declared in the shaders by putting [[vk_push_constant]] annotation before cbuffers
    void PushConstants(GfxPipelineLayoutHandle layoutHandle, const char* name, const void* data, uint32 dataSize);
    template <typename _T> void PushConstants(GfxPipelineLayoutHandle layout, const char* name, const _T& data) { PushConstants(layout, name, &data, sizeof(data)); }

    void PushBindings(GfxPipelineLayoutHandle layoutHandle, uint32 numBindings, const GfxBindingDesc* bindings);

    // 'itemIndices' is the index of the item that is written previously (see for each set. See GfxHelperDescriptorBuffer) 
    // CountOf(itemIndices) == setCount
    void SetDescriptorBufferOffsets(GfxPipelineLayoutHandle layoutHandle, uint32 setIndex, uint32 setCount, const uint32* itemIndices);
    void SetDescriptorBufferOffset(GfxPipelineLayoutHandle layoutHandle, uint32 setIndex, uint32 itemIndex);

    void BindPipeline(GfxPipelineHandle pipeHandle);
    void BindVertexBuffers(uint32 firstBinding, uint32 numBindings, const GfxBufferHandle* vertexBuffers, const uint64* offsets);
    void BindIndexBuffer(GfxBufferHandle indexBuffer, uint64 offset, GfxIndexType indexType);
    void BindDescriptorBuffers(uint32 numBindings, const GfxBufferHandle* descriptorBuffers);

    void Dispatch(uint32 groupCountX, uint32 groupCountY, uint32 groupCountZ);

    void TransitionBuffer(GfxBufferHandle buffHandle, GfxBufferTransition transition);
    void TransitionImage(GfxImageHandle imgHandle, GfxImageTransition transition, GfxImageTransitionFlags flags = GfxImageTransitionFlags::None);

    void Draw(uint32 vertexCount, uint32 instanceCount, uint32 firstVertex, uint32 firstInstance);
    void DrawIndexed(uint32 indexCount, uint32 instanceCount, uint32 firstIndex, uint32 vertexOffset, uint32 firstInstance);

    void SetScissors(uint32 firstScissor, uint32 numScissors, const RectInt* scissors);
    void SetViewports(uint32 firstViewport, uint32 numViewports, const GfxViewport* viewports);
    void SetCullMode(GfxCullMode cullMode);
    void SetFrontFace(GfxFrontFace frontFace);
    void EnableAlphaToCoverage(bool enable);
    void EnableColorBlend(uint32 firstAttachment, uint32 numAttachments, const uint32* enableFlags);

    void HelperSetFullscreenViewportAndScissor();
};

namespace GfxBackend
{
    bool Initialize();
    void Release();

    void Begin();
    void End();

    void SubmitQueue(GfxQueueType queueType, GfxQueueType dependentQueues = GfxQueueType::None);

    [[nodiscard]] GfxCommandBuffer BeginCommandBuffer(GfxQueueType queueType);
    void EndCommandBuffer(GfxCommandBuffer& cmdBuffer);

    GfxImageHandle CreateImage(const GfxImageDesc& desc);
    void DestroyImage(GfxImageHandle& handle);
    void BatchCreateImage(uint32 numImages, const GfxImageDesc* descs, GfxImageHandle* outHandles);
    void BatchDestroyImage(uint32 numImages, GfxImageHandle* handles);
    const GfxImageDesc& GetImageDesc(GfxImageHandle handle);

    GfxPipelineLayoutHandle CreatePipelineLayout(const GfxShader& shader, const GfxPipelineLayoutDesc& desc);
    void DestroyPipelineLayout(GfxPipelineLayoutHandle& handle);

    GfxBufferHandle CreateBuffer(const GfxBufferDesc& desc);
    void DestroyBuffer(GfxBufferHandle& handle);
    void BatchCreateBuffer(uint32 numBuffers, const GfxBufferDesc* descs, GfxBufferHandle* outHandles);
    void BatchDestroyBuffer(uint32 numBuffers, GfxBufferHandle* handles);

    // We can have different permutations for each shader by having specialization constants
    // The syntax in the shader is [SpecializationConstant] annotation before scalar types (float, bool, int)
    // So instead of reloading shader asset with different defines, we can pass these constants (name/value pairs) here which is considerably faster and more robust 
    GfxPipelineHandle CreateGraphicsPipeline(const GfxShader& shader, GfxPipelineLayoutHandle layoutHandle, 
                                             const GfxGraphicsPipelineDesc& desc, uint32 numPermutVars = 0,
                                             const GfxShaderPermutationVar* permutVars = nullptr);
    GfxPipelineHandle CreateComputePipeline(const GfxShader& shader, GfxPipelineLayoutHandle layoutHandle, 
                                            uint32 numPermutVars = 0, const GfxShaderPermutationVar* permutVars = nullptr);
    void DestroyPipeline(GfxPipelineHandle& handle);

    GfxSamplerHandle CreateSampler(const GfxSamplerDesc& desc);
    void DestroySampler(GfxSamplerHandle& handle);
    void SetupImmutableSamplers(const GfxImmutableSamplersDesc& desc);

    GfxFormat GetSwapchainFormat();
    Mat4 GetSwapchainTransformMat();
    Int2 GetSwapchainExtent();
    GfxFormat GetValidDepthStencilFormat();
    GfxFormat GetValidDepthFormat();

    void BeginRenderFrameSync();
    void EndRenderFrameSync();

    float GetRenderTimeMS();

    void ReloadShaderPipelines(const GfxShader& shader);

    bool IsIntegratedGPU();
} // Gfx

struct GfxHelperBufferUpdateScope
{
    GfxHelperBufferUpdateScope() = delete;
    GfxHelperBufferUpdateScope(const GfxHelperBufferUpdateScope&) = delete;

    explicit GfxHelperBufferUpdateScope(GfxCommandBuffer& cmd, GfxBufferHandle handle, uint32 size, GfxShaderStage bufferUsageStage);
    ~GfxHelperBufferUpdateScope();

    void* mData;
    uint32 mSize;

private:
    GfxCommandBuffer& mCmd;
    GfxBufferHandle mBuffer;
    GfxBufferHandle mStagingBuffer;
    GfxShaderStage mBufferUsageStage;
};

// Used for large uniform chunks that contain multiple cbuffers of the same type
struct GfxHelperUniformBuffer
{
    GfxHelperUniformBuffer() = delete;
    GfxHelperUniformBuffer(const GfxHelperUniformBuffer&) = delete;

    static size_t GetMemoryRequirement(uint32 itemSize, uint32 numItems);

    explicit GfxHelperUniformBuffer(void* dataPtr, uint32 itemSize, uint32 numItems);

    // Returns the offset to the start of the data that was written
    // We can pass this offset to GfxHelperDescriptorBuffer::WriteUniformBuffer
    uint32 Write(uint32 index, const void* data, uint32 dataSize);
    template <typename _T> uint32 Write(uint32 index, const _T& data) { return Write(index, &data, sizeof(data)); }

private:
    uint8* mData;
    uint32 mStride;
    uint32 mMaxItems;
};

// Used for filling large descriptor buffers
struct GfxHelperDescriptorBuffer
{
    GfxHelperDescriptorBuffer() = delete;
    GfxHelperDescriptorBuffer(const GfxHelperDescriptorBuffer&) = delete;

    static size_t GetMemoryRequirement(GfxPipelineLayoutHandle layoutHandle, uint32 descriptorSetIndex, uint32 numItems);

    explicit GfxHelperDescriptorBuffer(void* dataPtr, GfxPipelineLayoutHandle layoutHandle, uint32 descriptorSetIndex);

    void WriteBindings(uint32 index, uint32 numBindings, const GfxBindingDesc* bindings);

private:
    uint8* mData;
    GfxPipelineLayoutHandle mLayoutHandle;
    uint32 mDescriptorSetIndex;
};

//----------------------------------------------------------------------------------------------------------------------
// TODO: GPU Profiling
#ifdef TRACY_ENABLE
    #include "../Core/TracyHelper.h"

    struct GpuProfilerScope
    {
        GpuProfilerScope() = delete;
        GpuProfilerScope(const GpuProfilerScope&) = delete;

        GpuProfilerScope(GfxCommandBuffer& cmdBuffer, const ___tracy_source_location_data* sourceLoc, int callstackDepth, 
                         bool isActive, bool isAlloc);
        ~GpuProfilerScope();

        GfxCommandBuffer& mCmdBuffer;
        const bool mIsActive;
    };

//----------------------------------------------------------------------------------------------------------------------
// Profiling Macros
    #if defined TRACY_HAS_CALLSTACK && defined TRACY_CALLSTACK
        #define GPU_PROFILE_ZONE_OPT(cmdBuffer, name, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(___tracy_gpu_zone,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            GpuProfilerScope CONCAT(__gpu_profiler,__LINE__)(cmdBuffer, &CONCAT(___tracy_gpu_zone,__LINE__), TRACY_CALLSTACK, active, false)
        #define GPU_PROFILE_ZONE_ALLOC_OPT(cmdBuffer, name, active) \
            struct ___tracy_source_location_data CONCAT(___tracy_gpu_zone,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            GpuProfilerScope CONCAT(__gpu_profiler,__LINE__)(cmdBuffer, &CONCAT(___tracy_gpu_zone,__LINE__), TRACY_CALLSTACK, active, true)
        #define GPU_PROFILE_ZONE_COLOR_OPT(cmdBuffer, name, color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(___tracy_gpu_zone,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            GpuProfilerScope CONCAT(__gpu_profiler,__LINE__)(cmdBuffer, &CONCAT(___tracy_gpu_zone,__LINE__), TRACY_CALLSTACK, active, false)
        #define GPU_PROFILE_ZONE_ALLOC_COLOR_OPT(cmdBuffer, name, color, active) \
            struct ___tracy_source_location_data CONCAT(___tracy_gpu_zone,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            GpuProfilerScope CONCAT(__gpu_profiler,__LINE__)(cmdBuffer, &CONCAT(___tracy_gpu_zone,__LINE__), TRACY_CALLSTACK, active, true)

        #define GPU_PROFILE_ZONE(cmdBuffer, name) GPU_PROFILE_ZONE_OPT(cmdBuffer, name, true)
        #define GPU_PROFILE_ZONE_ALLOC(cmdBuffer, name) GPU_PROFILE_ZONE_ALLOC_OPT(cmdBuffer, name, true)
        #define GPU_PROFILE_ZONE_COLOR(cmdBuffer, name, color) GPU_PROFILE_ZONE_COLOR_OPT(cmdBuffer, name, color, true)
        #define GPU_PROFILE_ZONE_ALLOC_COLOR(cmdBuffer, name, color) GPU_PROFILE_ZONE_ALLOC_COLOR_OPT(cmdBuffer, name, color, true)
    #else
        #define GPU_PROFILE_ZONE_OPT(cmdBuffer, name, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(___tracy_gpu_zone,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            GpuProfilerScope CONCAT(__gpu_profiler,__LINE__)(cmdBuffer, &CONCAT(___tracy_gpu_zone,__LINE__), 0, active, false)
        #define GPU_PROFILE_ZONE_ALLOC_OPT(cmdBuffer, name, active) \
            struct ___tracy_source_location_data CONCAT(___tracy_gpu_zone,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            GpuProfilerScope CONCAT(__gpu_profiler,__LINE__)(cmdBuffer, &CONCAT(___tracy_gpu_zone,__LINE__), 0, active, true)
        #define GPU_PROFILE_ZONE_COLOR_OPT(cmdBuffer, name, color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(___tracy_gpu_zone,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            GpuProfilerScope CONCAT(__gpu_profiler,__LINE__)(cmdBuffer, &CONCAT(___tracy_gpu_zone,__LINE__), 0, active, false)
        #define GPU_PROFILE_ZONE_ALLOC_COLOR_OPT(cmdBuffer, name, color, active) \
            struct ___tracy_source_location_data CONCAT(___tracy_gpu_zone,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            GpuProfilerScope CONCAT(__gpu_profiler,__LINE__)(cmdBuffer, &CONCAT(___tracy_gpu_zone,__LINE__), 0, active, true)

        #define GPU_PROFILE_ZONE(cmdBuffer, name) GPU_PROFILE_ZONE_OPT(cmdBuffer, name, true)
        #define GPU_PROFILE_ZONE_ALLOC(cmdBuffer, name) GPU_PROFILE_ZONE_ALLOC_OPT(cmdBuffer, name, true)
        #define GPU_PROFILE_ZONE_COLOR(cmdBuffer, name, color) GPU_PROFILE_ZONE_COLOR_OPT(cmdBuffer, name, color, true)
        #define GPU_PROFILE_ZONE_ALLOC_COLOR(cmdBuffer, name, color) GPU_PROFILE_ZONE_ALLOC_COLOR_OPT(cmdBuffer, name, color, true)
    #endif // TRACY_HAS_CALLBACK
#else
    #define GPU_PROFILE_ZONE_OPT(cmdBuffer, name, active)
    #define GPU_PROFILE_ZONE_ALLOC_OPT(cmdBuffer, name, active)
    #define GPU_PROFILE_ZONE_COLOR_OPT(cmdBuffer, name, color, active)
    #define GPU_PROFILE_ZONE_ALLOC_COLOR_OPT(cmdBuffer, name, color, active)

    #define GPU_PROFILE_ZONE(cmdBuffer, name)
    #define GPU_PROFILE_ZONE_ALLOC(cmdBuffer, name)
    #define GPU_PROFILE_ZONE_COLOR(cmdBuffer, name, color)
    #define GPU_PROFILE_ZONE_ALLOC_COLOR(cmdBuffer, name, color)
#endif  // TRACY_ENABLE
