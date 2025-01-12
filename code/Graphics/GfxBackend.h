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
    void FlushBuffer(GfxBufferHandle buffHandle);

    void BatchMapBuffer(uint32 numParams, const GfxBufferHandle* handles, GfxMapResult* mapResults);
    void BatchFlushBuffer(uint32 numBuffers, const GfxBufferHandle* bufferHandles);

    void ClearImageColor(GfxImageHandle imgHandle, Color color);
    void ClearImageColor(GfxImageHandle imgHandle, Float4 color);
    void ClearSwapchainColor(Float4 color);

    void PushConstants(GfxPipelineLayoutHandle layoutHandle, const char* name, const void* data, uint32 dataSize);
    template <typename _T> void PushConstants(GfxPipelineLayoutHandle layout, const char* name, const _T& data);

    void PushBindings(GfxPipelineLayoutHandle layoutHandle, uint32 numBindings, const GfxBindingDesc* bindings);

    void BindPipeline(GfxPipelineHandle pipeHandle);
    void BindVertexBuffers(uint32 firstBinding, uint32 numBindings, const GfxBufferHandle* vertexBuffers, const uint64* offsets);
    void BindIndexBuffer(GfxBufferHandle indexBuffer, uint64 offset, GfxIndexType indexType);

    void Dispatch(uint32 groupCountX, uint32 groupCountY, uint32 groupCountZ);

    void TransitionBuffer(GfxBufferHandle buffHandle, GfxBufferTransition transition);
    void TransitionImage(GfxImageHandle imgHandle, GfxImageTransition transition);

    void Draw(uint32 vertexCount, uint32 instanceCount, uint32 firstVertex, uint32 firstInstance);
    void DrawIndexed(uint32 indexCount, uint32 instanceCount, uint32 firstIndex, uint32 vertexOffset, uint32 firstInstance);

    void SetScissors(uint32 firstScissor, uint32 numScissors, const RectInt* scissors);
    void SetViewports(uint32 firstViewport, uint32 numViewports, const GfxViewport* viewports);
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

    GfxPipelineHandle CreateGraphicsPipeline(const GfxShader& shader, GfxPipelineLayoutHandle layoutHandle, const GfxGraphicsPipelineDesc& desc);
    GfxPipelineHandle CreateComputePipeline(const GfxShader& shader, GfxPipelineLayoutHandle layoutHandle);
    void DestroyPipeline(GfxPipelineHandle& handle);

    GfxSamplerHandle CreateSampler(const GfxSamplerDesc& desc);
    void DestroySampler(GfxSamplerHandle& handle);

    GfxFormat GetSwapchainFormat();
    Mat4 GetSwapchainTransformMat();
    Int2 GetSwapchainExtent();

    void BeginRenderFrameSync();
    void EndRenderFrameSync();

    float GetRenderTimeNS();

    void ReloadShaderPipelines(const GfxShader& shader);

} // Gfx

//----------------------------------------------------------------------------------------------------------------------
// TODO: Profiling
#ifdef TRACY_ENABLE_TODO
    #include "../Core/TracyHelper.h"

    namespace Tracy 
    {
        namespace _private
        {
            API void ProfileZoneBegin(uint64 srcloc);
            API void ProfileZoneEnd();

            struct TracyGpuZoneScope
            {
                bool _active;

                TracyGpuZoneScope() = delete;
                explicit TracyGpuZoneScope(bool active, uint64 srcloc) : _active(active) 
                {
                    if (active)
                        ProfileZoneBegin(srcloc);
                }
            
                ~TracyGpuZoneScope()
                {
                    if (_active)
                        ProfileZoneEnd();
                }
            };
        }   // _private
    } // Tracy

    #define PROFILE_GPU_ZONE(active) \
        Tracy::_private::TracyGpuZoneScope(active, Tracy::_private::__tracy_alloc_source_loc(__LINE__, __FILE__, __func__))
    #define PROFILE_GPU_ZONE_NAME(name, active) \
        Tracy::_private::TracyGpuZoneScope(active, Tracy::_private::__tracy_alloc_source_loc(__LINE__, __FILE__, __func__, name))
    #define PROFILE_GPU_ZONE_BEGIN(active) \
        do { if (active) Tracy::_private::profileGpuZoneBegin(Tracy::_private::__tracy_alloc_source_loc(__LINE__, __FILE__, __func__));  } while(0)
    #define PROFILE_GPU_ZONE_NAME_BEGIN(name, active) \
        do { if (active) Tracy::_private::profileGpuZoneBegin(Tracy::_private::__tracy_alloc_source_loc(__LINE__, __FILE__, __func__, name));  } while(0)
    #define PROFILE_GPU_ZONE_END(active)  \
        do { if (active) Tracy::_private::profileGpuZoneEnd();  } while(0)
#else
    #define PROFILE_GPU_ZONE(active)
    #define PROFILE_GPU_ZONE_NAME(name, active)
    #define PROFILE_GPU_ZONE_BEGIN(active)
    #define PROFILE_GPU_ZONE_END(active)
#endif // TRACY_ENABLE


//----------------------------------------------------------------------------------------------------------------------
template <typename _T>
inline void GfxCommandBuffer::PushConstants(GfxPipelineLayoutHandle layout, const char* name, const _T& data)
{
    PushConstants(layout, name, &data, sizeof(data));
}
