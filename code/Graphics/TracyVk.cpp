#ifndef __TRACY_VK_CPP__
#define __TRACY_VK_CPP__

#ifndef __GRAPHICS_CPP__
    #error "This file depends on Graphics.cpp for compilation"
#endif

#include "../Core/TracyHelper.h"

#ifdef TRACY_ENABLE

// Check for dependencies
#ifndef __GRAPHICS_VK_CPP__
    #error "This file depends on GraphicsVk.cpp for compilation"
#endif

#include "../External/vulkan/include/vulkan.h"

#include "../Core/Atomic.h"

// TracyQueue.hpp
enum class GpuContextType : uint8_t
{
    Invalid,
    OpenGl,
    Vulkan,
    OpenCL,
    Direct3D12,
    Direct3D11
};

static constexpr uint32 kTracy_GpuContextCalibration = 1 << 0;      // TracyQueue.hpp -> GpuContextFlags

static constexpr const uint32 kProfileMaxQueries = 64*1024;

struct GfxProfileQueryContext
{
    uint8 id;
    VkQueryPool queryPool;
    AtomicLock queueLock;
    uint32 queryCount;
    
    uint64 deviation;
    int64 prevCalibration;
    int64 qpcToNs;
    uint32 head;
    uint32 tail;
    uint32 oldCount;
    int64* res;
};

struct GfxProfileState
{
    GfxProfileQueryContext gfxQueries[kMaxFramesInFlight];
    VkTimeDomainEXT timeDomain;
    uint8 uniqueIdGenerator;
    bool initialized;
};

static GfxProfileState gGfxProfile;

INLINE uint16 gfxProfileGetNextQueryId(GfxProfileQueryContext* ctx)
{
    AtomicLockScope lock(ctx->queueLock);
    uint32 id = ctx->head;
    ctx->head = (ctx->head + 1) % ctx->queryCount;
    ASSERT(ctx->head != ctx->tail);
    return static_cast<uint16>(id);
}

static void gfxProfileCalibrate(const GfxProfileQueryContext& ctx, int64* tCpu, int64* tGpu)
{
    VkCalibratedTimestampInfoEXT spec[2] = {
        { VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT, nullptr, VK_TIME_DOMAIN_DEVICE_EXT },
        { VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT, nullptr, gGfxProfile.timeDomain },
    };
    uint64 ts[2];
    uint64 deviation;
    do {
        VkExtensionApi::vkGetCalibratedTimestampsEXT(gVk.device, 2, spec, ts, &deviation);
    } while(deviation > ctx.deviation);

    #if PLATFORM_WINDOWS
        *tGpu = ts[0];
        *tCpu = _private::__tracy_get_time() * ctx.qpcToNs;
    #elif (PLATFORM_LINUX || PLATFORM_ANDROID) && defined CLOCK_MONOTONIC_RAW
        *tGpu = ts[0];
        *tCpu = ts[1];
    #else
        ASSERT(0);
    #endif
}

static bool gfxInitializeProfileQueryContext(GfxProfileQueryContext* ctx, uint8 uniqueId, VkCommandPool cmdPool)
{
    // Start creating query pool from max (uint16_max) and come down until it's successfully created
    VkQueryPool queryPool;
    uint32 queryCount = kProfileMaxQueries;
    VkQueryPoolCreateInfo poolInfo {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = queryCount
    };

    while (vkCreateQueryPool(gVk.device, &poolInfo, nullptr, &queryPool) != VK_SUCCESS) {
        queryCount >>= 1;
        poolInfo.queryCount = queryCount;
    }

    if (queryPool == VK_NULL_HANDLE) {
        logError("Gfx: Creating Query pool failed");
        return false;
    }

    ctx->queryPool = queryPool;
    ctx->queryCount = queryCount;
    ctx->res = memAllocZeroTyped<int64>(queryCount, memDefaultAlloc()); // TODO
    
    VkCommandBuffer vkCmdBuffer;
    VkCommandBufferAllocateInfo allocInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cmdPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    if (vkAllocateCommandBuffers(gVk.device, &allocInfo, &vkCmdBuffer) != VK_SUCCESS) {
        return false;
    }

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &vkCmdBuffer
    };

    vkBeginCommandBuffer(vkCmdBuffer, &beginInfo);
    vkCmdResetQueryPool(vkCmdBuffer, queryPool, 0, queryCount);
    vkEndCommandBuffer(vkCmdBuffer);
    vkQueueSubmit(gVk.gfxQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(gVk.gfxQueue);
    vkResetCommandBuffer(vkCmdBuffer, 0);
        
    int64 tgpu;
    if (gGfxProfile.timeDomain == VK_TIME_DOMAIN_DEVICE_EXT) {
        vkBeginCommandBuffer(vkCmdBuffer, &beginInfo);
        vkCmdWriteTimestamp(vkCmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, 0);
        vkEndCommandBuffer(vkCmdBuffer);
        vkQueueSubmit(gVk.gfxQueue, 1, &submitInfo, VK_NULL_HANDLE );
        vkQueueWaitIdle(gVk.gfxQueue);
        vkResetCommandBuffer(vkCmdBuffer, 0);

        vkGetQueryPoolResults(gVk.device, queryPool, 0, 1, sizeof(tgpu), &tgpu, sizeof(tgpu), 
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT );

        vkBeginCommandBuffer(vkCmdBuffer, &beginInfo);
        vkCmdResetQueryPool(vkCmdBuffer, queryPool, 0, 1);
        vkEndCommandBuffer(vkCmdBuffer);
        vkQueueSubmit(gVk.gfxQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(gVk.gfxQueue);
        vkResetCommandBuffer(vkCmdBuffer, 0);
    }
    else {
        // calibration (VK_EXT_calibrated_timestamps)
        constexpr uint32 kNumProbes = 32;

        VkCalibratedTimestampInfoEXT spec[2] = {
            { VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT, nullptr, VK_TIME_DOMAIN_DEVICE_EXT },
            { VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT, nullptr, gGfxProfile.timeDomain },
        };
        uint64 ts[2];
        uint64 deviation[kNumProbes];
        for(uint32 i = 0; i < kNumProbes; i++)
            VkExtensionApi::vkGetCalibratedTimestampsEXT(gVk.device, 2, spec, ts, &deviation[i]);

        uint64 minDeviation = deviation[0];
        for (uint32 i = 1; i < kNumProbes; i++) {
            if (minDeviation > deviation[i])
                minDeviation = deviation[i];
        }
        ctx->deviation = minDeviation*3 / 2;

        #if PLATFORM_WINDOWS
            LARGE_INTEGER t;
            QueryPerformanceFrequency(&t);
            ctx->qpcToNs = int64(1000000000. / t.QuadPart);
        #endif

        gfxProfileCalibrate(*ctx, &ctx->prevCalibration, &tgpu);
    }
    
    vkFreeCommandBuffers(gVk.device, cmdPool, 1, &vkCmdBuffer);

    ASSERT(gGfxProfile.uniqueIdGenerator < UINT8_MAX);
    ctx->id = uniqueId;

    ___tracy_gpu_new_context_data newContextData {
        .gpuTime = tgpu,
        .period = gVk.deviceProps.limits.timestampPeriod,
        .context = uniqueId,
        .flags = gGfxProfile.timeDomain != VK_TIME_DOMAIN_DEVICE_EXT ? (uint8)kTracy_GpuContextCalibration : (uint8)0,
        .type = static_cast<uint8>(GpuContextType::Vulkan)
    };
    ___tracy_emit_gpu_new_context_serial(newContextData);
    
    return true;
}

static void gfxReleaseProfileQueryContext(GfxProfileQueryContext* ctx)
{
    if (ctx->queryPool)
        vkDestroyQueryPool(gVk.device, ctx->queryPool, nullptr); // TODO: use allocator
    memFree(ctx->res, memDefaultAlloc());
}

static bool gfxInitializeProfiler()
{
    VkTimeDomainEXT timeDomain = VK_TIME_DOMAIN_DEVICE_EXT;

    if (gfxHasDeviceExtension(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME)) {
        if (!VkExtensionApi::vkGetPhysicalDeviceCalibrateableTimeDomainsEXT) {
            VkExtensionApi::vkGetPhysicalDeviceCalibrateableTimeDomainsEXT = 
                (PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT)vkGetInstanceProcAddr(gVk.instance, "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT");
        }
        if (!VkExtensionApi::vkGetCalibratedTimestampsEXT) {
            VkExtensionApi::vkGetCalibratedTimestampsEXT = 
                (PFN_vkGetCalibratedTimestampsEXT)vkGetInstanceProcAddr(gVk.instance, "vkGetCalibratedTimestampsEXT");
        }
    
        uint32_t num;
        VkExtensionApi::vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(gVk.physicalDevice, &num, nullptr);
        if (num > 4) 
            num = 4;
        VkTimeDomainEXT data[4];
        VkExtensionApi::vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(gVk.physicalDevice, &num, data);
        VkTimeDomainEXT supportedDomain = (VkTimeDomainEXT)-1;
        #if PLATFORM_WINDOWS
            supportedDomain = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT;
        #elif (PLATFORM_LINUX || PLATFORM_ANDROID) && defined CLOCK_MONOTONIC_RAW
            supportedDomain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT;
        #endif
    
        for(uint32_t i = 0; i < num; i++) {
            if(data[i] == supportedDomain) {
                timeDomain = data[i];
                break;
            }
        }
    }

    gGfxProfile.timeDomain = timeDomain;

    //------------------------------------------------------------------------
    // Make a temp command pool
    VkCommandPool cmdPool;
    VkCommandPoolCreateInfo poolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = gVk.gfxQueueFamilyIndex
    };
    if (vkCreateCommandPool(gVk.device, &poolCreateInfo, nullptr, &cmdPool) != VK_SUCCESS) {
        return false;
    }

    //------------------------------------------------------------------------
    const char* name = "GfxQueue";
    for (uint32 i = 0; i < kMaxFramesInFlight; i++) {
        GfxProfileQueryContext* ctx = &gGfxProfile.gfxQueries[i];
        if (!gfxInitializeProfileQueryContext(ctx, gGfxProfile.uniqueIdGenerator++, cmdPool)) {
            vkDestroyCommandPool(gVk.device, cmdPool, nullptr);
            ASSERT(0);
            return false;
        }

        ___tracy_emit_gpu_context_name_serial(___tracy_gpu_context_name_data {
            .context = ctx->id,
            .name = name,
            .len = static_cast<uint16>(strLen(name))
        });
    }

    vkDestroyCommandPool(gVk.device, cmdPool, nullptr);

    gGfxProfile.initialized = true;
    return true;
}

static void gfxReleaseProfiler()
{
    if (gGfxProfile.initialized) {
        for (uint32 i = 0; i < kMaxFramesInFlight; i++) {
            gfxReleaseProfileQueryContext(&gGfxProfile.gfxQueries[i]);
        }
    }
}

void gfxProfileZoneBegin(uint64 srcloc)
{
    if (!gGfxProfile.initialized)
        return;

    VkCommandBuffer cmdBuffer = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBuffer != VK_NULL_HANDLE, "GPU profile zone must be inside command-buffer recording");

    uint32 frameIdx = atomicLoad32Explicit(&gVk.currentFrameIdx, AtomicMemoryOrder::Acquire);
    GfxProfileQueryContext* ctx = &gGfxProfile.gfxQueries[frameIdx];

    uint16 queryId = gfxProfileGetNextQueryId(ctx);
    vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, ctx->queryPool, queryId);

    ___tracy_emit_gpu_zone_begin_alloc_serial(___tracy_gpu_zone_begin_data {
        .srcloc = srcloc,
        .queryId = queryId,
        .context = ctx->id
    });
}

void gfxProfileZoneEnd()
{
    if (!gGfxProfile.initialized)
        return;

    VkCommandBuffer cmdBuffer = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBuffer != VK_NULL_HANDLE, "GPU profile zone must be inside command-buffer recording");

    uint32 frameIdx = atomicLoad32Explicit(&gVk.currentFrameIdx, AtomicMemoryOrder::Acquire);
    GfxProfileQueryContext* ctx = &gGfxProfile.gfxQueries[frameIdx];

    uint16 queryId = gfxProfileGetNextQueryId(ctx);
    vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, ctx->queryPool, queryId);

    ___tracy_emit_gpu_zone_end_serial(___tracy_gpu_zone_end_data {
        .queryId = queryId,
        .context = ctx->id
    });
}

static bool gfxHasProfileSamples()
{
    if (!gGfxProfile.initialized || gVk.prevFrameIdx == gVk.currentFrameIdx)
        return false;

    // Collect the samples from the previous frame
    GfxProfileQueryContext* ctx = &gGfxProfile.gfxQueries[gVk.prevFrameIdx];

    bool isVoid = ctx->tail == ctx->head;
    return !isVoid;   
}

static void gfxProfileCollectSamples()
{
    if (!gGfxProfile.initialized || gVk.prevFrameIdx == gVk.currentFrameIdx)
        return;

    VkCommandBuffer cmdBuffer = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBuffer != VK_NULL_HANDLE, "GPU collect samples must be inside command-buffer recording");
    
    // Collect the samples from the previous frame
    GfxProfileQueryContext* ctx = &gGfxProfile.gfxQueries[gVk.prevFrameIdx];

    bool isVoid = ctx->tail == ctx->head;
    PROFILE_ZONE_COLOR(0xff0000, !isVoid);

    if (isVoid) 
        return;

    #ifdef TRACY_ON_DEMAND
        if(!___tracy_connected())
        {
            vkCmdResetQueryPool(cmdBuffer, ctx->queryPool, 0, ctx->queryCount);
            ctx->head = ctx->tail = ctx->oldCount = 0;
            int64_t tgpu;
            if(gGfxProfile.timeDomain != VK_TIME_DOMAIN_DEVICE_EXT) 
                gfxProfileCalibrate(*ctx, &ctx->prevCalibration, &tgpu);
            return;
        }
    #endif

    uint32 count;
    if(ctx->oldCount != 0) {
        count = ctx->oldCount;
        ctx->oldCount = 0;
    }
    else {
        count = ctx->head < ctx->tail ? ctx->queryCount - ctx->tail : ctx->head - ctx->tail;
    }

    if(vkGetQueryPoolResults(gVk.device, ctx->queryPool, ctx->tail, count, sizeof(int64)*ctx->queryCount, 
                             ctx->res, sizeof(int64_t), VK_QUERY_RESULT_64_BIT) == VK_NOT_READY)
    {
        ctx->oldCount = count;
        return;
    }

    for(uint32 idx = 0; idx < count; idx++) {
        ___tracy_emit_gpu_time_serial(___tracy_gpu_time_data {
            .gpuTime = ctx->res[idx],
            .queryId = uint16(ctx->tail + idx),
            .context = ctx->id
        });
    }

    if(gGfxProfile.timeDomain != VK_TIME_DOMAIN_DEVICE_EXT) {
        int64 tgpu, tcpu;
        gfxProfileCalibrate(*ctx, &tcpu, &tgpu);
        const int64 refCpu = _private::__tracy_get_time();
        const int64 delta = tcpu - ctx->prevCalibration;
        if(delta > 0) {
            ctx->prevCalibration = tcpu;
            ___tracy_emit_gpu_calibrate_serial(_private::___tracy_gpu_calibrate_data {
                .gpuTime = tgpu,
                .cpuTime = refCpu,
                .deltaTime = delta,
                .context = ctx->id
            });
        }
    }

    vkCmdResetQueryPool(cmdBuffer, ctx->queryPool, ctx->tail, count);

    ctx->tail += count;
    if(ctx->tail == ctx->queryCount) 
        ctx->tail = 0;
}
#endif // TRACY_ENABLE

#endif // __TRACY_VK_CPP__