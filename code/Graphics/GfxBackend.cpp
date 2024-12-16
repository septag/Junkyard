#include "GfxBackend.h"

// define VULKAN_API_WORKAROUND_10XEDITOR for the 10x cpp parser workaround
#ifndef __10X__
    #define VK_NO_PROTOTYPES
#endif

#if PLATFORM_WINDOWS
    #include "../Core/IncludeWin.h"
    #include <vulkan/vulkan.h>
    #include <vulkan/vulkan_win32.h>
#elif PLATFORM_ANDROID
    #include <vulkan/vulkan.h>
    #include <vulkan/vulkan_android.h>
#elif PLATFORM_APPLE
    #include <MoltenVk/mvk_vulkan.h>
#else
    #error "Not implemented"
#endif

#include "../Core/Allocators.h"
#include "../Core/System.h"
#include "../Core/Log.h"
#include "../Core/MathAll.h"
#include "../Core/Arrays.h"
#include "../Core/StringUtil.h"
#include "../Core/Atomic.h"
#include "../Core/Pools.h"
#include "../Core/Hash.h"
#include "../Core/Jobs.h"

#include "../Common/Application.h"
#include "../Common/JunkyardSettings.h"
#include "../Common/CommonTypes.h"

#include "../Engine.h"

#include "../External/OffsetAllocator/offsetAllocator.hpp"

#ifndef __10X__
    #include "../External/volk/volk.h"
#endif

static constexpr uint32 GFXBACKEND_MAX_SWAP_CHAIN_IMAGES = 3;
static constexpr uint32 GFXBACKEND_MAX_GARBAGE_COLLECT_PER_FRAME = 32;
static constexpr uint32 GFXBACKEND_BACKBUFFER_COUNT = 3;
static constexpr uint32 GFXBACKEND_FRAMES_IN_FLIGHT = 2;
static constexpr uint32 GFXBACKEND_MAX_SETS_PER_PIPELINE = 4;

#if PLATFORM_WINDOWS
static const char* GFXBACKEND_DEFAULT_INSTANCE_EXTENSIONS[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
#elif PLATFORM_ANDROID 
static const char* GFXBACKEND_DEFAULT_INSTANCE_EXTENSIONS[] = {"VK_KHR_surface", "VK_KHR_android_surface"};
#elif PLATFORM_APPLE
static const char* GFXBACKEND_DEFAULT_INSTANCE_EXTENSIONS[] = {"VK_KHR_surface", "VK_EXT_metal_surface"};
#endif

struct GfxBackendAllocator final : MemAllocator
{
    void* Malloc(size_t size, uint32 align) override;
    void* Realloc(void* ptr, size_t size, uint32 align) override;
    void  Free(void* ptr, uint32 align) override;
    MemAllocatorType GetType() const override { return MemAllocatorType::Heap; }

    void Initialize(MemAllocator* alloc, size_t poolSize, bool debugMode = false);
    void Release();

    MemTlsfAllocator mTlsfAlloc;
    SpinLockMutex mMutex;
};

struct GfxBackendVkAllocator
{
    static void* VkAlloc(void* pUserData, size_t size, size_t align, VkSystemAllocationScope allocScope);
    static void* VkRealloc(void* pUserData, void* pOriginal, size_t size, size_t align, VkSystemAllocationScope allocScope);
    static void VkFree(void* pUserData, void* pPtr);
    static void VkInternalAllocFn(void* pUserData, size_t size, VkInternalAllocationType allocType, VkSystemAllocationScope allocScope);
    static void VkInternalFreeFn(void* pUserData, size_t size, VkInternalAllocationType allocType, VkSystemAllocationScope allocScope);

    GfxBackendVkAllocator();
    inline operator const VkAllocationCallbacks*() const { return &mCallbacks; }

    VkAllocationCallbacks mCallbacks;
};

struct GfxBackendSwapchain
{
    uint32 backbufferIdx;
    uint32 numImages;
    VkSwapchainKHR handle;
    VkSurfaceFormatKHR format;
    VkImage images[GFXBACKEND_BACKBUFFER_COUNT];
    VkImageView imageViews[GFXBACKEND_BACKBUFFER_COUNT];
    VkSemaphore imageReadySemaphores[GFXBACKEND_BACKBUFFER_COUNT];
    VkSemaphore presentSemaphores[GFXBACKEND_BACKBUFFER_COUNT];
    VkExtent2D extent;
    uint32 imageIndex;
    bool resize;

    void GoNext() { backbufferIdx = (backbufferIdx + 1) % GFXBACKEND_BACKBUFFER_COUNT; }
    VkSemaphore GetSwapchainSemaphore() { return imageReadySemaphores[backbufferIdx]; }
    VkSemaphore GetPresentSemaphore() { return presentSemaphores[backbufferIdx]; }
    VkImage GetImage() { return images[imageIndex]; }
    VkImageView GetImageView() { return imageViews[imageIndex]; }
    void AcquireImage();
};

struct GfxBackendSwapchainInfo
{
    VkSurfaceCapabilitiesKHR caps;
    uint32 numFormats;
    uint32 numPresentModes;
    VkSurfaceFormatKHR* formats;
    VkPresentModeKHR* presentModes;
};

struct GfxBackendQueueFamily
{
    GfxBackendQueueType type;
    uint32 count;
};

struct GfxBackendQueueSubmitRequest
{
    GfxBackendQueueType type;
    GfxBackendQueueType dependents;
    VkCommandBuffer* cmdBuffers;
    VkFence fence;
    uint32 numCmdBuffers;
};

struct GfxBackendCommandBufferContext
{
    VkCommandPool pool;
    Array<VkCommandBuffer> cmdBufferFreeList;   // Stale CmdBuffers. Ready to be reused
    Array<VkCommandBuffer> cmdBuffers;  // Currently submitted or being recorded 

    Array<VkFence> fenceFreeList;
    Array<VkFence> fences;      // A fence will be created for each batch of submitted cmdBuffers

    uint32 cmdBufferIndex;    // index until the last submit call
};

struct GfxBackendQueue
{
    struct WaitSemaphore
    {
        VkSemaphore semaphore;
        VkPipelineStageFlags stageFlags;
    };

    struct PendingBarrier
    {
        enum Type
        {
            BUFFER,
            IMAGE
        };

        Type type;

        union {
            GfxBufferHandle bufferHandle;
            GfxImageHandle imageHandle;
        };

        union {
            VkBufferMemoryBarrier2 bufferBarrier;
            VkImageMemoryBarrier2 imageBarrier;
        };
    };

    VkQueue handle;
    GfxBackendQueueType type;
    uint32 familyIdx;
    float priority;
    bool supportsTransfer;
    GfxBackendCommandBufferContext cmdBufferContexts[GFXBACKEND_FRAMES_IN_FLIGHT];
    VkSemaphore mySemaphore;
    Array<WaitSemaphore> waitSemaphores;
    Array<VkSemaphore> signalSemaphores;
    Array<PendingBarrier> pendingBarriers;  // Buffers transfers coming into this queue
    GfxBackendQueueType internalDependents;
};

struct GfxBackendQueueManager
{
    bool Initialize();
    void PostInitialize();   // Calls after vkDevice is created (See GfxBackend::Initialize)
    void Release();

    void BeginFrame();

    void SubmitQueue(GfxBackendQueueType queueType, GfxBackendQueueType dependentQueues);

    inline uint32 FindQueue(GfxBackendQueueType type) const;
    inline uint32 GetQueueCount() const { return mNumQueues; }
    inline GfxBackendQueue& GetQueue(uint32 index) const { ASSERT(index < mNumQueues); return mQueues[index]; }
    inline VkCommandBuffer GetCommandBufferHandle(const GfxBackendCommandBuffer& cmdBuffer);
    inline uint32 GetGeneration() const { return mGeneration; }
    inline uint32 GetFrameIndex() const { return mFrameIndex; }

private:
    static int SubmitThread(void* userData);
    bool SubmitQueueInternal(GfxBackendQueueSubmitRequest& req);
    void SetupQueuesForDiscreteDevice();
    void SetupQueuesForItegratedDevice();
    void MergeQueues();
    uint32 AssignQueueFamily(GfxBackendQueueType type, GfxBackendQueueType preferNotHave = GfxBackendQueueType::None);
    bool InitializeCommandBufferContext(GfxBackendCommandBufferContext& ctx, uint32 queueFamilyIndex);
    void ReleaseCommandBufferContext(GfxBackendCommandBufferContext& ctx);

    SpinLockMutex mRequestMutex;
    Semaphore mRequestsSemaphore;
    Thread mThread;

    uint32 mGeneration;
    uint32 mFrameIndex;

    GfxBackendQueueFamily* mQueueFamilies;
    uint32 mNumQueueFamilies;

    GfxBackendQueue* mQueues;
    uint32 mNumQueues;

    Array<GfxBackendQueueSubmitRequest*> mSubmitRequests;
    bool mQuit;
};

struct GfxBackendInstance
{
    VkInstance handle;
    uint32 numLayers;
    uint32 numExtensions;
    VkLayerProperties* layers;
    VkExtensionProperties* extensions;
};

struct GfxBackendVkExtensions
{
    bool hasDebugUtils;
    bool hasNonSemanticInfo;
    bool hasMemoryBudget;
    bool hasAstcDecodeMode;
    bool hasPipelineExecutableProperties;
};

struct GfxBackendGpu
{
    VkPhysicalDevice handle;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceVulkan11Properties props2;
    VkPhysicalDeviceVulkan12Properties props3;
    VkPhysicalDeviceVulkan13Properties props4;
    VkPhysicalDeviceFeatures features;
    VkPhysicalDeviceVulkan11Features features2;
    VkPhysicalDeviceVulkan12Features features3;
    VkPhysicalDeviceVulkan13Features features4;
    VkExtensionProperties* extensions;
    uint32 numExtensions;
};

struct GfxBackendDeviceMemory
{
    VkDeviceMemory handle;
    VkDeviceSize offset = VkDeviceSize(-1);
    void* mappedData;       // optional: only available when heap is HOST_VISIBLE
    GfxBackendMemoryArena arena;

    uint32 isHeapDeviceLocal : 1;   // Accessible by GPU (fast) 
    uint32 isCpuVisible : 1;        // Can be written by CPU
    uint32 isCached : 1;            // Faster for small frequent updates
    uint32 isCoherent : 1;          // No need to flush/map (potentially slower)
    uint32 isLazilyAlloc : 1;       // TBR

    bool IsValid() { return handle != nullptr || offset == -1; }
};

struct GfxBackendMemoryBumpAllocator
{
    bool Initialize(VkDeviceSize maxSize, uint32 memoryTypeIndex);
    void Release();
    GfxBackendDeviceMemory Malloc(const VkMemoryRequirements& memReq);
    void Reset() { mOffset = 0; }

private:
    SpinLockMutex mMutex;
    VkDeviceMemory mDeviceMem;
    VkDeviceSize mCapacity;
    VkDeviceSize mOffset;
    uint32 mMemTypeIndex;
    VkMemoryPropertyFlags mTypeFlags;
    VkMemoryHeapFlags mHeapFlags;
    void* mMappedData;      // This is for HOST_VISIBLE memory where we can map the entire buffer upfront
};

struct GfxBackendDeviceMemoryManager
{
    bool Initialize();
    void Release();

    GfxBackendDeviceMemory Malloc(const VkMemoryRequirements& memReq, GfxBackendMemoryArena arena);
    void Free(GfxBackendDeviceMemory mem);

    void ResetTransientAllocators(uint32 frameIndex);

    inline VkDeviceSize& GetDeviceMemoryBudget(uint32 typeIndex);
    inline const VkPhysicalDeviceMemoryProperties& GetProps() const { return mProps; }

private:
    uint32 FindDeviceMemoryType(VkMemoryPropertyFlags flags, bool localdeviceHeap, VkMemoryPropertyFlags fallbackFlags = 0);

    VkPhysicalDeviceMemoryProperties mProps;
    VkPhysicalDeviceMemoryBudgetPropertiesEXT mBudget;   // only valid with
    
    GfxBackendMemoryBumpAllocator mPersistentGPU;
    GfxBackendMemoryBumpAllocator mPersistentCPU;
    GfxBackendMemoryBumpAllocator mTransientCPU[GFXBACKEND_FRAMES_IN_FLIGHT];

    uint32 mStagingIndex;
};

struct GfxBackendGarbage
{
    enum class Type
    {
        Pipeline,
        PipelineLayout,
        DescriptorSetLayout,
        Buffer,
        Image,
        Sampler,
        ImageView
    };

    Type type;
    uint64 frameIdx;

    union {
        VkPipeline pipeline;
        VkPipelineLayout pipelineLayout;
        VkDescriptorSetLayout dsetLayout;
        VkBuffer buffer;
        VkImage image;
        VkSampler sampler;
        VkImageView imageView;
    };
};

struct GfxBackendImage
{
    VkImage handle;
    VkImageView viewHandle;
    GfxBackendImageDesc desc;
    GfxBackendDeviceMemory mem;
    VkImageLayout layout;
    VkPipelineStageFlags2 transitionedStage;
    VkAccessFlagBits2 transitionedAccess;
};

struct GfxBackendBuffer
{
    VkBuffer handle;
    GfxBackendBufferDesc desc;
    GfxBackendDeviceMemory mem;
    VkPipelineStageFlags2 transitionedStage;
    VkAccessFlagBits2 transitionedAccess;
};

struct GfxBackendPipelineLayout
{
    struct Binding
    {
        String32 name;
        uint32 arrayCount;     // For descriptor_indexing
        uint8 setIndex;       
    };

    VkPipelineLayout handle;
    uint32 hash;
    uint32 numBindings;
    uint32 refCount;
    uint32 numPushConstants;
    uint32 numSets;
    Binding* bindings; // count = numBindings
    VkDescriptorSetLayoutBinding* bindingsVk;   // count = numBindings. bindings[].setIndex shows where this binding belongs to
    VkDescriptorSetLayout* sets;    // count = numSets
    VkPushConstantRange* pushConstantRanges; // count = numPushConstants
    uint32* bindingNameHashes;  // count = numBindings
    uint32* pushConstantNameHashes;     // count = numPushConstants
};

struct GfxBackendPipeline
{
    enum PipelineType
    {
        PipelineTypeGraphics,
        PipelineTypeCompute
    };

    VkPipeline handle;
    PipelineType type;

    // TODO: HotReload stuff
};

struct GfxBackendVk
{
    Mutex garbageMtx;
    MemProxyAllocator parentAlloc;
    MemProxyAllocator runtimeAlloc;
    MemProxyAllocator driverAlloc;
    GfxBackendAllocator runtimeAllocBase;
    GfxBackendAllocator driverAllocBase;
    GfxBackendVkAllocator vkAlloc;
    Signal frameSyncSignal;
    
    GfxBackendInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    GfxBackendGpu gpu;
    VkDevice device;
    VkSurfaceKHR surface;
    GfxBackendSwapchainInfo swapchainInfo;
    GfxBackendSwapchain swapchain;
    GfxBackendVkExtensions extApi;
    GfxBackendDeviceMemoryManager memMan;
    GfxBackendQueueManager queueMan;

    HandlePool<GfxImageHandle, GfxBackendImage> images;
    HandlePool<GfxBufferHandle, GfxBackendBuffer> buffers;
    HandlePool<GfxPipelineLayoutHandle, GfxBackendPipelineLayout*> pipelineLayouts;
    HandlePool<GfxPipelineHandle, GfxBackendPipeline> pipelines;
    
    Array<GfxBackendGarbage> garbage;
    uint64 presentFrame;
};

static GfxBackendVk gBackendVk;

namespace GfxBackend
{
    [[maybe_unused]] INLINE bool _FormatIsDepthStencil(GfxFormat fmt)
    {
        return  fmt == GfxFormat::D32_SFLOAT ||
            fmt == GfxFormat::D16_UNORM_S8_UINT ||
            fmt == GfxFormat::D24_UNORM_S8_UINT ||
            fmt == GfxFormat::D32_SFLOAT_S8_UINT ||
            fmt == GfxFormat::S8_UINT;
    }

    [[maybe_unused]] INLINE bool _FormatHasDepth(GfxFormat fmt)
    {
        return  fmt == GfxFormat::D32_SFLOAT ||
            fmt == GfxFormat::D16_UNORM_S8_UINT ||
            fmt == GfxFormat::D24_UNORM_S8_UINT ||
            fmt == GfxFormat::D32_SFLOAT_S8_UINT;
    }

    [[maybe_unused]] INLINE bool _FormatHasStencil(GfxFormat fmt)
    {
        return  fmt == GfxFormat::D24_UNORM_S8_UINT ||
            fmt == GfxFormat::D16_UNORM_S8_UINT ||
            fmt == GfxFormat::D32_SFLOAT_S8_UINT ||
            fmt == GfxFormat::S8_UINT;
    }

    // Returns the proper vulkan stage based the destination queue type and the stage that buffer should be transitioned to
    static inline VkPipelineStageFlags2 _GetBufferDestStageFlags(GfxBackendQueueType type, GfxShaderStage dstStages)
    {
        VkPipelineStageFlags2 flags = 0;
        if (type == GfxBackendQueueType::Graphics) {
            if (IsBitsSet<GfxShaderStage>(dstStages, GfxShaderStage::Vertex)) 
                flags |= VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
            if (IsBitsSet<GfxShaderStage>(dstStages, GfxShaderStage::Fragment)) 
                flags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        }
        else if (type == GfxBackendQueueType::Compute) {
            flags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        }

        return flags;
    }
    
    // Gets the stage that the buffer is currently transitioned to. And returns the stage that the buffer is suppose to be transitioned
    static inline VkPipelineStageFlags2 _GetBufferSourceStageFlags(VkPipelineStageFlags2 curStageFlags)
    {
        if (curStageFlags & VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT) 
            return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        return VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    }

    static inline VkPipelineStageFlags2 _GetImageDestStageFlags(GfxBackendQueueType type, GfxShaderStage dstStages)
    {
        VkPipelineStageFlags2 flags = 0;
        if (type == GfxBackendQueueType::Graphics) {
            if (IsBitsSet<GfxShaderStage>(dstStages, GfxShaderStage::Vertex)) 
                flags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
            if (IsBitsSet<GfxShaderStage>(dstStages, GfxShaderStage::Fragment)) 
                flags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        }
        else if (type == GfxBackendQueueType::Compute) {
            flags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        }

        return flags;
    }

    // Gets the stage that the buffer is currently transitioned to. And returns the stage that the buffer is suppose to be transitioned
    static inline VkPipelineStageFlags2 _GetImageSourceStageFlags(VkPipelineStageFlags2 curStageFlags)
    {
        if (curStageFlags & VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT) 
            return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        return curStageFlags;
    }

    static inline const GfxShaderParameterInfo* _FindShaderParam(const GfxShader& shader, const char* paramName)
    {
        for (uint32 i = 0; i < shader.numParams; i++) {
            if (Str::IsEqual(shader.params[i].name, paramName))
                return &shader.params[i];
        }
        return nullptr;
    }

    inline VkCommandBuffer _GetCommandBufferHandle(const GfxBackendCommandBuffer& cmdBuffer)
    {
        const GfxBackendQueue& queue = gBackendVk.queueMan.GetQueue(cmdBuffer.mQueueIndex);
        ASSERT_MSG(cmdBuffer.mGeneration == gBackendVk.queueMan.GetGeneration(), 
                   "EndCommandBuffer must be called before resetting the queue");

        const GfxBackendCommandBufferContext& cmdBufferMan = queue.cmdBufferContexts[gBackendVk.queueMan.GetFrameIndex()];
        return cmdBufferMan.cmdBuffers[cmdBuffer.mCmdBufferIndex];
    }

    static void _CollectGarbage(bool force)
    {
        uint64 frameIdx = Engine::GetFrameIndex();
        const uint32 numFramesToWait = GFXBACKEND_FRAMES_IN_FLIGHT;

        MutexScope lock(gBackendVk.garbageMtx);
        uint32 destroyCount = 0;
        for (uint32 i = 0; i < gBackendVk.garbage.Count() && (destroyCount < GFXBACKEND_MAX_GARBAGE_COLLECT_PER_FRAME || force);) {
            const GfxBackendGarbage& garbage = gBackendVk.garbage[i];
            if (force || frameIdx > (garbage.frameIdx + numFramesToWait)) {
                destroyCount++;
                switch (garbage.type) {
                case GfxBackendGarbage::Type::Pipeline:
                    vkDestroyPipeline(gBackendVk.device, garbage.pipeline, gBackendVk.vkAlloc);
                    break;
                case GfxBackendGarbage::Type::PipelineLayout:
                    vkDestroyPipelineLayout(gBackendVk.device, garbage.pipelineLayout, gBackendVk.vkAlloc);
                    break;
                case GfxBackendGarbage::Type::DescriptorSetLayout:
                    vkDestroyDescriptorSetLayout(gBackendVk.device, garbage.dsetLayout, gBackendVk.vkAlloc);
                    break;
                case GfxBackendGarbage::Type::Buffer:
                    vkDestroyBuffer(gBackendVk.device, garbage.buffer, gBackendVk.vkAlloc);
                    break;
                case GfxBackendGarbage::Type::Image:
                    vkDestroyImage(gBackendVk.device, garbage.image, gBackendVk.vkAlloc);
                    break;
                case GfxBackendGarbage::Type::Sampler:
                    vkDestroySampler(gBackendVk.device, garbage.sampler, gBackendVk.vkAlloc);
                    break;
                case GfxBackendGarbage::Type::ImageView:
                    vkDestroyImageView(gBackendVk.device, garbage.imageView, gBackendVk.vkAlloc);
                    break;
                default:
                    destroyCount--;
                    break;
                }

                gBackendVk.garbage.RemoveAndSwap(i);
            }
            else {
                ++i;
            }
        }
    }

    static bool _HasExtension(const VkExtensionProperties* extensions, uint32 numExtensions, const char* name)
    {
        for (uint32 i = 0; i < numExtensions; i++) {
            if (Str::IsEqual(extensions[i].extensionName, name))
                return true;
        }

        return false;
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL _DebugUtilsCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
        void* userData)
    {
        UNUSED(userData);

        char typeStr[128];  typeStr[0] = '\0';
        if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)  
            Str::Concat(typeStr, sizeof(typeStr), "[V]");
        if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)  
            Str::Concat(typeStr, sizeof(typeStr), "[P]");

        switch (messageSeverity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            LOG_VERBOSE("Gfx: %s%s", typeStr, callbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            LOG_INFO("Gfx: %s%s", typeStr, callbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            LOG_WARNING("Gfx: %s%s", typeStr, callbackData->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            LOG_ERROR("Gfx: %s%s", typeStr, callbackData->pMessage);
            break;
        default:
            break;
        }
        return VK_FALSE;
    }

    static bool _InitializeInstance(const SettingsJunkyard& settings)
    {
        GfxBackendInstance& inst = gBackendVk.instance;

        auto HasLayer = [&inst](const char* layerName)
        {
            for (uint32 i = 0; i < inst.numLayers; i++) {
                if (Str::IsEqual(inst.layers[i].layerName, layerName))
                    return true;
            }
            return false;
        };

        //----------------------------------------------------------------------------------------------------------------------        
        // Layers
        vkEnumerateInstanceLayerProperties(&inst.numLayers, nullptr);
        if (inst.numLayers) {
            inst.layers = Mem::AllocTyped<VkLayerProperties>(inst.numLayers, &gBackendVk.parentAlloc);
            vkEnumerateInstanceLayerProperties(&inst.numLayers, inst.layers);
        }

        // To set our maximum API version, we need to query for VkEnumerateInstanceVersion (vk1.1)
        // This is just for the supported vulkan.dll API version, not the GPU driver itself
        if (!vkEnumerateInstanceVersion) {
            LOG_ERROR("Vulkan API doesn't support vkEnumerateInstanceVersion. Install the latest VulkanSDK runtime");
            return false;
        }

        uint32 apiVersionVk = VK_API_VERSION_1_0;
        vkEnumerateInstanceVersion(&apiVersionVk);
        if (apiVersionVk < VK_API_VERSION_1_3) {
            LOG_ERROR("Vulkan API doesn't support version 1.3, Install the latest VulkanSDK runtime");
            return false;
        }

        //----------------------------------------------------------------------------------------------------------------------
        // Instance Layers
        StaticArray<const char*, 4> enabledLayers;
        if (settings.graphics.validate) {
            if (HasLayer("VK_LAYER_KHRONOS_validation")) {
                enabledLayers.Push("VK_LAYER_KHRONOS_validation");
            }
            else {
                LOG_ERROR("Gfx: Vulkan backend doesn't have validation layer support. Turn it off in the settings.");
                return false;
            }
        }

        VkApplicationInfo appInfo {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = settings.app.appName,
            .applicationVersion = JUNKYARD_VERSION,
            .pEngineName = "JunkyardEngine",
            .engineVersion = JUNKYARD_VERSION,
            .apiVersion = apiVersionVk
        };

        VkInstanceCreateInfo instCreateInfo {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = enabledLayers.Count(),
            .ppEnabledLayerNames = enabledLayers.Ptr()
        };

        if (enabledLayers.Count()) {
            LOG_INFO("Enabled Vulkan layers:");
            for (const char* layer: enabledLayers)
                LOG_INFO("\t%s", layer);
        }

        //----------------------------------------------------------------------------------------------------------------------
        // Extensions
        vkEnumerateInstanceExtensionProperties(nullptr, &inst.numExtensions, nullptr);
        if (inst.numExtensions) {
            inst.extensions = Mem::AllocTyped<VkExtensionProperties>(inst.numExtensions, &gBackendVk.parentAlloc);
            vkEnumerateInstanceExtensionProperties(nullptr, &inst.numExtensions, inst.extensions);

            if (settings.graphics.listExtensions) {
                LOG_VERBOSE("Instance Extensions (%u):", inst.numExtensions);
                for (uint32 i = 0; i < inst.numExtensions; i++)
                    LOG_VERBOSE("\t%s", inst.extensions[i].extensionName);
            }
        }

        StaticArray<const char*, 32> enabledExtensions;
        for (uint32 i = 0; i < CountOf(GFXBACKEND_DEFAULT_INSTANCE_EXTENSIONS); i++)
            enabledExtensions.Push(GFXBACKEND_DEFAULT_INSTANCE_EXTENSIONS[i]);

        if constexpr (!CONFIG_FINAL_BUILD) {
            if (_HasExtension(inst.extensions, inst.numExtensions, "VK_EXT_debug_utils")) {
                enabledExtensions.Push("VK_EXT_debug_utils");
                gBackendVk.extApi.hasDebugUtils = true;
            }
        }

        // Validation and it's features
        VkValidationFeaturesEXT validationFeatures;
        StaticArray<VkValidationFeatureEnableEXT, 5> validationFeatureFlags;

        if (settings.graphics.validate) {
            MemTempAllocator tempAlloc;
            uint32 numValidationExtensions = 0;
            bool hasValidationFeaturesExt = false;
            vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &numValidationExtensions, nullptr);
            if (numValidationExtensions) {
                VkExtensionProperties* validationExtensions = Mem::AllocTyped<VkExtensionProperties>(numValidationExtensions, &tempAlloc);
                vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &numValidationExtensions, validationExtensions);
                hasValidationFeaturesExt = _HasExtension(validationExtensions, numValidationExtensions, "VK_EXT_validation_features");
            }

            bool validateFeaturesEnabled = settings.graphics.validateBestPractices || settings.graphics.validateSynchronization;
            if (validateFeaturesEnabled && hasValidationFeaturesExt) {
                enabledExtensions.Push("VK_EXT_validation_features");
                if (settings.graphics.validateBestPractices)
                    validationFeatureFlags.Push(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
                if (settings.graphics.validateSynchronization) 
                    validationFeatureFlags.Push(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
                validationFeatures = {
                    .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
                    .pNext = nullptr,
                    .enabledValidationFeatureCount = validationFeatureFlags.Count(),
                    .pEnabledValidationFeatures = validationFeatureFlags.Ptr()
                };

                ASSERT(instCreateInfo.pNext == nullptr);
                instCreateInfo.pNext = &validationFeatures;
            }
        }

        instCreateInfo.enabledExtensionCount = enabledExtensions.Count();
        instCreateInfo.ppEnabledExtensionNames = enabledExtensions.Ptr();

        if (enabledExtensions.Count()) {
            LOG_VERBOSE("Enabled Vulkan instance extensions:");
            for (const char* ext: enabledExtensions) 
                LOG_VERBOSE("\t%s", ext);
        }

        if (VkResult r = vkCreateInstance(&instCreateInfo, gBackendVk.vkAlloc, &inst.handle); r != VK_SUCCESS) {
            const char* errorCode;
            switch (r) {
            case VK_ERROR_OUT_OF_HOST_MEMORY:       errorCode = "VK_ERROR_OUT_OF_HOST_MEMORY"; break;
            case VK_ERROR_OUT_OF_DEVICE_MEMORY:     errorCode = "VK_ERROR_OUT_OF_DEVICE_MEMORY"; break;
            case VK_ERROR_INITIALIZATION_FAILED:    errorCode = "VK_ERROR_INITIALIZATION_FAILED"; break;
            case VK_ERROR_LAYER_NOT_PRESENT:        errorCode = "VK_ERROR_LAYER_NOT_PRESENT"; break;
            case VK_ERROR_EXTENSION_NOT_PRESENT:    errorCode = "VK_ERROR_EXTENSION_NOT_PRESENT"; break;
            case VK_ERROR_INCOMPATIBLE_DRIVER:      errorCode = "VK_ERROR_INCOMPATIBLE_DRIVER"; break;
            default:    errorCode = "UNKNOWN";  break;
            }
            LOG_ERROR("Gfx: Creating vulkan instance failed (Reason: %s)", errorCode);
            return false;
        }

        LOG_INFO("(init) Vulkan instance created");

        volkLoadInstance(gBackendVk.instance.handle);
        return true;
    }

    static void _ReleaseInstance()
    {
        GfxBackendInstance& inst = gBackendVk.instance;
        MemAllocator* alloc = &gBackendVk.parentAlloc;

        Mem::Free(inst.extensions, alloc);
        Mem::Free(inst.layers, alloc);

        vkDestroyInstance(inst.handle, gBackendVk.vkAlloc);

        memset(&inst, 0x0, sizeof(inst));
    }

    static VkSurfaceKHR _CreateWindowSurface(void* windowHandle)
    {
        VkSurfaceKHR surface = nullptr;
        #if PLATFORM_WINDOWS
        VkWin32SurfaceCreateInfoKHR surfaceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
            .hinstance = (HMODULE)App::GetNativeAppHandle(),
            .hwnd = (HWND)windowHandle
        };
    
        vkCreateWin32SurfaceKHR(gBackendVk.instance.handle, &surfaceCreateInfo, gBackendVk.vkAlloc, &surface);
        #elif PLATFORM_ANDROID
        VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
            .window = (ANativeWindow*)windowHandle
        };
    
        vkCreateAndroidSurfaceKHR(gBackendVk.instance.handle, &surfaceCreateInfo, gBackendVk.vkAlloc, &surface);
        #elif PLATFORM_APPLE
        VkMetalSurfaceCreateInfoEXT surfaceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
            .pLayer = windowHandle
        };
    
        vkCreateMetalSurfaceEXT(gBackendVk.instance.handle, &surfaceCreateInfo, gBackendVk.vkAlloc, &surface);
        #else
        #error "Not implemented"
        #endif
        return surface;
    }

    static bool _InitializeGPU(const SettingsJunkyard& settings)
    {
        uint32 gpuIndex = settings.graphics.gpuIndex;
        GfxBackendGpu& gpu = gBackendVk.gpu;

        MemTempAllocator tempAlloc;
        uint32 numGPUs = 0;
        vkEnumeratePhysicalDevices(gBackendVk.instance.handle, &numGPUs, nullptr);
        ASSERT_ALWAYS(numGPUs, "Something went seriously wrong. No GPUs found for Vulkan");
        VkPhysicalDevice* gpus = tempAlloc.MallocTyped<VkPhysicalDevice>(numGPUs);
        vkEnumeratePhysicalDevices(gBackendVk.instance.handle, &numGPUs, gpus);

        if (gpuIndex == -1) {
            // Prefer discrete GPUs over integrated ones by default unless we set preferIntegratedGpu setting
            VkPhysicalDeviceType preferedType = settings.graphics.preferIntegratedGpu ? 
                VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU : VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

            for (uint32 i = 0; i < numGPUs; i++) {
                VkPhysicalDeviceProperties deviceProps {};
                vkGetPhysicalDeviceProperties(gpus[i], &deviceProps);
                if (deviceProps.deviceType == preferedType) {
                    gpuIndex = i;
                    break;
                }
            }
            
            if (gpuIndex == -1)
                gpuIndex = 0;

            gpu.handle = gpus[gpuIndex];
        }
        else {
            if (gpuIndex >= numGPUs) {
                LOG_WARNING("Chosen GPU (%u) doesn't exist on the current system, choosing index (%u)", gpuIndex, numGPUs - 1);
                gpuIndex = numGPUs - 1;
            }

            gpu.handle = gpus[gpuIndex];
        }

        if (!gpu.handle) {
            LOG_ERROR("Gfx: No compatible GPU found");
            return false;
        }

        // Gather info and features
        vkGetPhysicalDeviceProperties(gpu.handle, &gpu.props);

        // Estimate GPU memory
        VkDeviceSize heapSize = 0;
        {
            VkPhysicalDeviceMemoryProperties memProps;
            vkGetPhysicalDeviceMemoryProperties(gpu.handle, &memProps);
            for (uint32 i = 0; i < memProps.memoryHeapCount; i++) 
                heapSize += (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? memProps.memoryHeaps[i].size : 0;
        }

        const char* gpuType;
        switch (gpu.props.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:      gpuType = "DISCRETE"; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:    gpuType = "INTEGRATED"; break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:               gpuType = "CPU"; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:       gpuType = "VIRTUAL"; break;
        default:                                        gpuType = "UnknownType";    break;
        }

        uint32 major = VK_API_VERSION_MAJOR(gpu.props.apiVersion);
        uint32 minor = VK_API_VERSION_MINOR(gpu.props.apiVersion);

        LOG_INFO("(init) GPU: %s (%s) (Index=%u)", gpu.props.deviceName, gpuType, gpuIndex);
        LOG_INFO("(init) GPU memory: %_$$$llu", heapSize);
        LOG_INFO("(init) GPU driver vulkan version: %u.%u", major, minor);

        // TODO: Make this more flexible for MoltenVK
        if (major < 1 || minor < 3) {
            LOG_ERROR("Gfx: Minimum supported Vulkan version is 1.3, but the GPU supports version %u.%u", major, minor);
            return false;
        }

        VkPhysicalDeviceProperties2 props {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &gpu.props2
        };

        gpu.props2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES,
            .pNext = &gpu.props3
        };

        gpu.props3 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,
            .pNext = &gpu.props4
        };

        gpu.props4 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES
        };

        vkGetPhysicalDeviceProperties2(gpu.handle, &props);

        LOG_INFO("(init) GPU driver: %s - %s", gpu.props3.driverName, gpu.props3.driverInfo);
        LOG_INFO("(init) GPU driver conformance version: %d.%d.%d-%d", 
                 gpu.props3.conformanceVersion.major,
                 gpu.props3.conformanceVersion.minor,
                 gpu.props3.conformanceVersion.subminor,
                 gpu.props3.conformanceVersion.patch);

        // Features
        VkPhysicalDeviceFeatures2 features {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &gpu.features2
        };

        gpu.features2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
            .pNext = &gpu.features3
        };

        gpu.features3 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = &gpu.features4
        };

        gpu.features4 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        };

        vkGetPhysicalDeviceFeatures2(gpu.handle, &features);
        gpu.features = features.features;

        // Extensions
        vkEnumerateDeviceExtensionProperties(gpu.handle, nullptr, &gpu.numExtensions, nullptr);
        if (gpu.numExtensions > 0) {
            gpu.extensions = Mem::AllocTyped<VkExtensionProperties>(gpu.numExtensions, &gBackendVk.parentAlloc);
            vkEnumerateDeviceExtensionProperties(gpu.handle, nullptr, &gpu.numExtensions, gpu.extensions);

            if (settings.graphics.listExtensions) {
                LOG_VERBOSE("Device Extensions (%u):", gpu.extensions);
                for (uint32 i = 0; i < gpu.numExtensions; i++) 
                    LOG_VERBOSE("\t%s", gpu.extensions[i].extensionName);
            }
        }

        return true;
    }

    static bool _InitializeDevice(const SettingsJunkyard& settings)
    {
        StaticArray<const char*, 32> enabledExtensions;
        StaticArray<const char*, 32> enabledFeatures;
        GfxBackendGpu& gpu = gBackendVk.gpu;

        auto CheckAddExtension = [&gpu, &enabledExtensions](const char* name, bool required = false)->bool
        {
            if (_HasExtension(gpu.extensions, gpu.numExtensions, name)) {
                enabledExtensions.Push(name);
                return true;
            }
            else {
                if (required)
                    LOG_ERROR("Gfx: %s extension is missing but required by the engine", name);
                else 
                    LOG_WARNING("Gfx: %s extension is not supported on the device", name);
                return false;
            }
        };

        // Features
        if (!gpu.features4.dynamicRendering) {
            LOG_ERROR("Gfx: Dynamic rendering feature is required (VK_KHR_dynamic_rendering)");
            return false;
        }
        enabledFeatures.Push("DynamicRendering (VK_KHR_dynamic_rendering)");

        if (!gpu.features4.synchronization2) {
            LOG_ERROR("Gfx: Synchronization2 feature is required (VK_KHR_synchronization2)");
            return false;
        }
        enabledFeatures.Push("Synchronization2 (VK_KHR_synchronization2)");

        if (!gpu.features3.descriptorIndexing) {
            LOG_ERROR("Gfx: descriptorIndexing feature is required (VK_EXT_descriptor_indexing)");
            return false;
        }
        enabledFeatures.Push("DescriptorIndexing (VK_EXT_descriptor_indexing)");

        if (!gpu.features3.uniformBufferStandardLayout) {
            LOG_ERROR("Gfx: Standard uniform buffer layout feature is required (VK_KHR_uniform_buffer_standard_layout)");
            return false;
        }
        enabledFeatures.Push("UniformBufferStandardLayout (VK_KHR_uniform_buffer_standard_layout)");
        if (enabledFeatures.Count()) {
            LOG_VERBOSE("Check device features (%u):", enabledFeatures.Count());
            for (const char* name : enabledFeatures) 
                LOG_VERBOSE("\t%s", name);
        }

        // Required extensions
        if (!settings.graphics.headless && !CheckAddExtension("VK_KHR_swapchain", true))
            return false;

        if (!CheckAddExtension("VK_KHR_push_descriptor", true))
            return false;

        // Optional extensions and features
        gBackendVk.extApi.hasNonSemanticInfo = CheckAddExtension("VK_KHR_shader_non_semantic_info");
        gBackendVk.extApi.hasMemoryBudget = CheckAddExtension("VK_EXT_memory_budget");
        if constexpr (PLATFORM_MOBILE)
            gBackendVk.extApi.hasAstcDecodeMode = CheckAddExtension("VK_EXT_astc_decode_mode");
        gBackendVk.extApi.hasPipelineExecutableProperties = CheckAddExtension("VK_KHR_pipeline_executable_properties");

        if (enabledExtensions.Count()) {
            LOG_VERBOSE("Enabled device extensions (%u):", enabledExtensions.Count());
            for (const char* ext : enabledExtensions)
                LOG_VERBOSE("\t%s", ext);
        }

        // Gather Queues
        StaticArray<VkDeviceQueueCreateInfo, 4> queueCreateInfos;
        for (uint32 i = 0; i < gBackendVk.queueMan.GetQueueCount(); i++) {
            const GfxBackendQueue& queue = gBackendVk.queueMan.GetQueue(i);
            if (settings.graphics.headless && IsBitsSet(queue.type, GfxBackendQueueType::Graphics|GfxBackendQueueType::Present))
                continue;

            VkDeviceQueueCreateInfo createInfo {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = queue.familyIdx,
                .queueCount = 1,
                .pQueuePriorities = &queue.priority
            };
            queueCreateInfos.Push(createInfo);
        }

        // Create device (logical)
        VkDeviceCreateInfo devCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = gBackendVk.queueMan.GetQueueCount(),
            .pQueueCreateInfos = queueCreateInfos.Ptr(),
            .enabledExtensionCount = enabledExtensions.Count(),
            .ppEnabledExtensionNames = enabledExtensions.Ptr()
        };

        // Enable extensions and features
        void** deviceNext = const_cast<void**>(&devCreateInfo.pNext);
        {
            // We already queried all the features in InitializeGPU
            // Just use all the existing features. Unless we explicitly want to turn something off
            VkPhysicalDeviceFeatures2 features {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                .pNext = &gpu.features2,
                .features = gpu.features
            };

            *deviceNext = &features;
            deviceNext = &gpu.features4.pNext;
        }

        VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR enableExecProps {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR,
            .pipelineExecutableInfo = VK_TRUE
        };
        if (settings.graphics.shaderDumpProperties && gBackendVk.extApi.hasPipelineExecutableProperties) {
            *deviceNext = &enableExecProps;
            deviceNext = &enableExecProps.pNext;
        }

        if (vkCreateDevice(gpu.handle, &devCreateInfo, gBackendVk.vkAlloc, &gBackendVk.device) != VK_SUCCESS) {
            LOG_ERROR("Gfx: CreateDevice failed");
            return false;
        }
        LOG_INFO("(init) Vulkan device created");

        volkLoadDevice(gBackendVk.device);

        return true;
    }

    static void _ReleaseDevice()
    {
        MemAllocator* alloc = &gBackendVk.parentAlloc;

        if (gBackendVk.device) 
            vkDestroyDevice(gBackendVk.device, gBackendVk.vkAlloc);
        
        Mem::Free(gBackendVk.gpu.extensions, alloc);
    }

    static bool _ResizeSwapchain(GfxBackendSwapchain* swapchain, VkSurfaceKHR surface, Int2 size)
    {
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gBackendVk.gpu.handle, gBackendVk.surface, &gBackendVk.swapchainInfo.caps);

        // Take care of possible swapchain transform, specifically on android!
        // https://android-developers.googleblog.com/2020/02/handling-device-orientation-efficiently.html
        # if PLATFORM_ANDROID
        const VkSurfaceCapabilitiesKHR& swapchainCaps = gBackendVk.swapchainInfo.caps;
        if (swapchainCaps.currentTransform & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR)
            App::AndroidSetFramebufferTransform(AppFramebufferTransform::Rotate90);
        if (swapchainCaps.currentTransform & VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR)
            App::AndroidSetFramebufferTransform(AppFramebufferTransform::Rotate180);
        if (swapchainCaps.currentTransform & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR)
            App::AndroidSetFramebufferTransform(AppFramebufferTransform::Rotate270);
        #endif

        const GfxBackendSwapchainInfo& info = gBackendVk.swapchainInfo;
        VkSurfaceFormatKHR chosenFormat {};

        for (uint32 i = 0; i < info.numFormats; i++) {
            if (info.formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || info.formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
                chosenFormat = info.formats[i];
                break;
            }
        }

        if (chosenFormat.format == VK_FORMAT_UNDEFINED) {
            LOG_ERROR("Gfx: No compatible swapchain format found");
            return false;
        }

        VkPresentModeKHR presentMode = SettingsJunkyard::Get().graphics.enableVsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;

        // Verify that SwapChain has support for this present mode
        bool presentModeIsSupported = false;
        for (uint32 i = 0; i < info.numPresentModes; i++) {
            if (info.presentModes[i] == presentMode) {
                presentModeIsSupported = true;
                break;
            }
        }

        if (!presentModeIsSupported) {
            LOG_WARNING("Gfx: PresentMode: %u is not supported by device, choosing default: %u", presentMode, info.presentModes[0]);
            presentMode = info.presentModes[0];
        }

        swapchain->backbufferIdx = 0;
        swapchain->extent = {
            Clamp<uint32>(size.x, info.caps.minImageExtent.width, info.caps.maxImageExtent.width), 
            Clamp<uint32>(size.y, info.caps.minImageExtent.height, info.caps.maxImageExtent.height)
        };

        // https://android-developers.googleblog.com/2020/02/handling-device-orientation-efficiently.html
        if (App::GetFramebufferTransform() == AppFramebufferTransform::Rotate90 || 
            App::GetFramebufferTransform() == AppFramebufferTransform::Rotate270)
        {
            Swap(swapchain->extent.width, swapchain->extent.height);
        }

        uint32 numImages = Clamp(GFXBACKEND_BACKBUFFER_COUNT, info.caps.minImageCount, info.caps.maxImageCount);
        VkSwapchainCreateInfoKHR createInfo {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = surface,
            .minImageCount = numImages,
            .imageFormat = chosenFormat.format,
            .imageColorSpace = chosenFormat.colorSpace,
            .imageExtent =  swapchain->extent,
            .imageArrayLayers = 1, // 2 for stereoscopic
            .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .preTransform = info.caps.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = presentMode,
            .clipped = VK_TRUE
        };

        const char* formatName = "Unknown";
        if (chosenFormat.format == VK_FORMAT_B8G8R8A8_UNORM)
            formatName = "BGRA_UNORM";
        else if (chosenFormat.format == VK_FORMAT_R8G8B8A8_UNORM)
            formatName = "RGBA_UNORM";
        LOG_VERBOSE("(init) Swapchain %ux%ux%u (%s)", swapchain->extent.width, swapchain->extent.height, numImages, formatName);

        if (swapchain->handle) 
            vkDestroySwapchainKHR(gBackendVk.device, swapchain->handle, gBackendVk.vkAlloc);


        if (vkCreateSwapchainKHR(gBackendVk.device, &createInfo, gBackendVk.vkAlloc, &swapchain->handle) != VK_SUCCESS) {
            LOG_ERROR("Gfx: CreateSwapchain failed");
            return false;
        }
        
        uint32 numActualImages = 0;
        vkGetSwapchainImagesKHR(gBackendVk.device, swapchain->handle, &numActualImages, nullptr);
        ASSERT(numActualImages == numImages);
        vkGetSwapchainImagesKHR(gBackendVk.device, swapchain->handle, &numActualImages, swapchain->images);
        swapchain->numImages = numActualImages;

        // Views
        for (uint32 i = 0; i < numActualImages; i++) {
            VkImageViewCreateInfo viewCreateInfo {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = swapchain->images[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = chosenFormat.format,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1
                }
            };

            if (vkCreateImageView(gBackendVk.device, &viewCreateInfo, gBackendVk.vkAlloc, &swapchain->imageViews[i]) != VK_SUCCESS) {
                LOG_ERROR("Gfx: CreateSwapchain create views failed");
                return false;
            }
        }

        swapchain->format = chosenFormat;
        swapchain->resize = false;

        return true;
    }

    static bool _InitializeSwapchain(GfxBackendSwapchain* swapchain, VkSurfaceKHR surface, Int2 size)
    {
        if (!_ResizeSwapchain(swapchain, surface, size))
            return false;

        // Semaphores
        VkSemaphoreCreateInfo semCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        for (uint32 i = 0; i < GFXBACKEND_BACKBUFFER_COUNT; i++) {
            vkCreateSemaphore(gBackendVk.device, &semCreateInfo, gBackendVk.vkAlloc, &swapchain->imageReadySemaphores[i]);
            vkCreateSemaphore(gBackendVk.device, &semCreateInfo, gBackendVk.vkAlloc, &swapchain->presentSemaphores[i]);
        }

        return true;
    }

    static void _ReleaseSwapchain(GfxBackendSwapchain* swapchain)
    {
        ASSERT(swapchain);

        for (uint32 i = 0; i < swapchain->numImages; i++) {
            if (swapchain->imageViews[i])
                vkDestroyImageView(gBackendVk.device, swapchain->imageViews[i], gBackendVk.vkAlloc);
        }

        if (swapchain->handle) 
            vkDestroySwapchainKHR(gBackendVk.device, swapchain->handle, gBackendVk.vkAlloc);

        for (uint32 i = 0; i < GFXBACKEND_BACKBUFFER_COUNT; i++) {
            vkDestroySemaphore(gBackendVk.device, swapchain->imageReadySemaphores[i], gBackendVk.vkAlloc);
            vkDestroySemaphore(gBackendVk.device, swapchain->presentSemaphores[i], gBackendVk.vkAlloc);
        }

        memset(swapchain, 0x0, sizeof(*swapchain));
    }

    static void _TransitionImageTEMP(VkCommandBuffer cmd, VkImage image, VkImageLayout curLayout, VkImageLayout newLayout)
    {
        VkImageAspectFlags aspect = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        VkImageMemoryBarrier2 imageBarrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT|VK_ACCESS_2_MEMORY_READ_BIT,
            .oldLayout = curLayout,
            .newLayout = newLayout,
            .image = image,
            .subresourceRange = {
                .aspectMask = aspect,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .layerCount = VK_REMAINING_ARRAY_LAYERS
            }
        };

        VkDependencyInfo depInfo {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &imageBarrier
        };

        vkCmdPipelineBarrier2(cmd, &depInfo);
    }

    static void _CopyImageToImageTEMP(VkCommandBuffer cmd, VkImage source, VkImage dest, VkExtent2D srcExtent, VkExtent2D destExtent)
    {
        VkImageBlit2 blitRegion {
            .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1
            },
            .srcOffsets = {
                {0, 0, 0},
                {int(srcExtent.width), int(srcExtent.height), 1}
            },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1
            },
            .dstOffsets = {
                {0, 0, 0},
                {int(destExtent.width), int(destExtent.height), 1}
            },
        };

        VkBlitImageInfo2 blitInfo {
            .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
            .srcImage = source,
            .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .dstImage = dest,
            .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .regionCount = 1,
            .pRegions = &blitRegion,
            .filter = VK_FILTER_LINEAR
        };

        vkCmdBlitImage2(cmd, &blitInfo);
    }
} // GfxBackend

bool GfxBackend::Initialize()
{
    TimerStopWatch stopwatch;

    // Disable some implicit layers (TEMP?)
    if constexpr (PLATFORM_WINDOWS) {
        OS::SetEnvVar("DISABLE_LAYER_NV_OPTIMUS_1", "1");
        OS::SetEnvVar("DISABLE_VULKAN_OBS_CAPTURE", "1");
    }

    if (volkInitialize() != VK_SUCCESS) {
        LOG_ERROR("Volk failed to initialize. Possibly VulkanSDK is not installed (or MoltenVK dll is missing on Mac)");
        return false;
    }

    const SettingsJunkyard& settings = SettingsJunkyard::Get();
    
    // Setup allocators
    // - Parent allocator is based off engine's main heap
    // - Runtime allocator is all the allocations that the backend does by itself
    // - Driver allocator is all the allocations that is coming from the driver
    // - VkAlloc is just the vulkan callbacks that diverts all the incoming calls from the driver to Driver allocator
    // - RuntimeAllocBase/DriverAllocBase are the actual TLSF allocators, they are called by their corrosponding proxy allocators
    bool debugAllocs = settings.engine.debugAllocations;

    Engine::HelperInitializeProxyAllocator(&gBackendVk.parentAlloc, "GfxBackend");

    gBackendVk.runtimeAllocBase.Initialize(&gBackendVk.parentAlloc, SIZE_MB, debugAllocs);
    gBackendVk.driverAllocBase.Initialize(&gBackendVk.parentAlloc, 32*SIZE_MB, debugAllocs);
    Engine::HelperInitializeProxyAllocator(&gBackendVk.runtimeAlloc, "GfxBackend.Runtime", &gBackendVk.runtimeAllocBase);
    Engine::HelperInitializeProxyAllocator(&gBackendVk.driverAlloc, "GfxBackend.Vulkan", &gBackendVk.driverAllocBase);

    Engine::RegisterProxyAllocator(&gBackendVk.parentAlloc);
    Engine::RegisterProxyAllocator(&gBackendVk.runtimeAlloc);
    Engine::RegisterProxyAllocator(&gBackendVk.driverAlloc);
    
    if (!_InitializeInstance(settings))
        return false;

    if (gBackendVk.extApi.hasDebugUtils) {
        VkDebugUtilsMessengerCreateInfoEXT debugUtilsInfo {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = 
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
            .messageType = 
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = _DebugUtilsCallback,
            .pUserData = nullptr
        };

        if (vkCreateDebugUtilsMessengerEXT(gBackendVk.instance.handle, &debugUtilsInfo, 
                                           gBackendVk.vkAlloc, &gBackendVk.debugMessenger) != VK_SUCCESS) 
        {
            LOG_ERROR("Gfx: vkCreateDebugUtilsMessengerEXT failed");
            return false;
        }
    }

    if (!_InitializeGPU(settings))
        return false;

    // Window surface
    if (!settings.graphics.headless) {
        gBackendVk.surface = _CreateWindowSurface(App::GetNativeWindowHandle());
        if (!gBackendVk.surface) {
            LOG_ERROR("Gfx: Creating window surface failed");
            return false;
        }
    }

    if (!gBackendVk.queueMan.Initialize())
        return false;

    if (!_InitializeDevice(settings))
        return false;
    gBackendVk.queueMan.PostInitialize();
    
    if (!gBackendVk.memMan.Initialize()) {
        LOG_ERROR("Gfx: Device memory memory failed to initialize");
        return false;
    }

    // Swapchain and it's capabilities
    // We can only create this after device is created. 
    if (!settings.graphics.headless) {
        uint32 numFormats;
        uint32 numPresentModes;

        // TODO: Maybe also take these into InitializeSwapchain and use different data structuring for swapchains
        vkGetPhysicalDeviceSurfaceFormatsKHR(gBackendVk.gpu.handle, gBackendVk.surface, &numFormats, nullptr);
        gBackendVk.swapchainInfo.numFormats = numFormats;
        gBackendVk.swapchainInfo.formats = Mem::AllocTyped<VkSurfaceFormatKHR>(numFormats, &gBackendVk.parentAlloc);
        vkGetPhysicalDeviceSurfaceFormatsKHR(gBackendVk.gpu.handle, gBackendVk.surface, &numFormats, gBackendVk.swapchainInfo.formats);

        vkGetPhysicalDeviceSurfacePresentModesKHR(gBackendVk.gpu.handle, gBackendVk.surface, &numPresentModes, nullptr);
        gBackendVk.swapchainInfo.numPresentModes = numPresentModes;
        gBackendVk.swapchainInfo.presentModes = Mem::AllocTyped<VkPresentModeKHR>(numPresentModes, &gBackendVk.parentAlloc);
        vkGetPhysicalDeviceSurfacePresentModesKHR(gBackendVk.gpu.handle, gBackendVk.surface, &numPresentModes, gBackendVk.swapchainInfo.presentModes);

        if (!_InitializeSwapchain(&gBackendVk.swapchain, gBackendVk.surface, Int2(App::GetFramebufferWidth(), App::GetFramebufferHeight())))
            return false;
    }

    gBackendVk.images.SetAllocator(&gBackendVk.runtimeAlloc);
    gBackendVk.pipelineLayouts.SetAllocator(&gBackendVk.runtimeAlloc);
    gBackendVk.garbage.SetAllocator(&gBackendVk.runtimeAlloc);
    gBackendVk.garbageMtx.Initialize();

    gBackendVk.frameSyncSignal.Initialize();

    return true;
}

void GfxBackend::Begin()
{
    ASSERT_MSG(Engine::IsMainThread(), "Update can only be called in the main thread");

    // GPU -> CPU sync
    gBackendVk.queueMan.BeginFrame();

    gBackendVk.swapchain.AcquireImage();
}

void GfxBackendCommandBuffer::ClearImageColor(GfxImageHandle imgHandle, Color color)
{
    ClearImageColor(imgHandle, Color::ToFloat4(color));
}

void GfxBackendCommandBuffer::ClearImageColor(GfxImageHandle imgHandle, Float4 color)
{
    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    GfxBackendImage& image = gBackendVk.images.Data(imgHandle);

    // TEMP
    GfxBackend::_TransitionImageTEMP(cmdVk, image.handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    image.layout = VK_IMAGE_LAYOUT_GENERAL;

    VkClearColorValue clearVal = {{color.x, color.y, color.z, color.w}};
    VkImageSubresourceRange clearRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .layerCount = VK_REMAINING_ARRAY_LAYERS
    };

    vkCmdClearColorImage(cmdVk, image.handle, image.layout, &clearVal, 1, &clearRange);
}

void GfxBackendCommandBuffer::ClearSwapchainColor(Float4 color)
{
    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);

    VkImage imageVk = gBackendVk.swapchain.GetImage();

    GfxBackend::_TransitionImageTEMP(cmdVk, imageVk, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    VkClearColorValue clearVal = {{color.x, color.y, color.z, color.w}};
    VkImageSubresourceRange clearRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .layerCount = VK_REMAINING_ARRAY_LAYERS
    };
    vkCmdClearColorImage(cmdVk, imageVk, VK_IMAGE_LAYOUT_GENERAL, &clearVal, 1, &clearRange);
    GfxBackend::_TransitionImageTEMP(cmdVk, imageVk, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    mDrawsToSwapchain = true;
    gBackendVk.queueMan.GetQueue(mQueueIndex).internalDependents |= GfxBackendQueueType::Present;
}

void GfxBackendCommandBuffer::CopyImageToSwapchain(GfxImageHandle imgHandle)
{
    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    GfxBackendImage& image = gBackendVk.images.Data(imgHandle);
    VkImage swapchainImage = gBackendVk.swapchain.GetImage();
    
    GfxBackend::_TransitionImageTEMP(cmdVk, swapchainImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkExtent2D extent { image.desc.width, image.desc.height };
    GfxBackend::_CopyImageToImageTEMP(cmdVk, image.handle, swapchainImage, extent, gBackendVk.swapchain.extent);
    GfxBackend::_TransitionImageTEMP(cmdVk, swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    mDrawsToSwapchain = true;
    gBackendVk.queueMan.GetQueue(mQueueIndex).internalDependents |= GfxBackendQueueType::Present;
}

void GfxBackend::End()
{
    // CPU <-> CPU sync: Transient memory and CommandBuffers
    // Before we move on to the next frame, we must make sure that no transient memory allocation or CommandBuffer creation is left out and leaked to next frame
    // Locked when a CommandBuffer is created or Transient memory is created
    // Unlocked when all CommandBuffers are submitted and objects binded to Transient memory are destroyed
    if (!gBackendVk.frameSyncSignal.WaitOnCondition([](int value, int ref) { return value > ref; }, 0, UINT32_MAX)) {
        LOG_WARNING("Either some transient resources are not destroyed. Or CommandBuffers are not submitted in the current frame");
    }

    // Present
    {
        VkSemaphore waitSemaphore = gBackendVk.swapchain.GetPresentSemaphore();
        VkPresentInfoKHR presentInfo {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &waitSemaphore,
            .swapchainCount = 1,
            .pSwapchains = &gBackendVk.swapchain.handle,
            .pImageIndices = &gBackendVk.swapchain.imageIndex
        };

        uint32 queueIndex = gBackendVk.queueMan.FindQueue(GfxBackendQueueType::Present);
        ASSERT(queueIndex != -1);
        VkResult r = vkQueuePresentKHR(gBackendVk.queueMan.GetQueue(queueIndex).handle, &presentInfo);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            gBackendVk.swapchain.resize = true;
        }
        else if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            // TODO: VK_SUBOPTIMAL_KHR doc says " A swapchain no longer matches the surface properties exactly, but can still be used to present to the surface successfully."
            //       But I need to investigate a bit more on when this happens actually
            ASSERT_ALWAYS(false, "Gfx: Present swapchain failed");
        }
    }

    gBackendVk.swapchain.GoNext();
    _CollectGarbage(false);

    if (gBackendVk.swapchain.resize) {
        vkDeviceWaitIdle(gBackendVk.device);
        GfxBackend::_ResizeSwapchain(&gBackendVk.swapchain, gBackendVk.surface, 
                                     Int2(App::GetFramebufferWidth(), App::GetFramebufferHeight()));
    }

    ++gBackendVk.presentFrame;
}

void GfxBackend::Release()
{
    MemAllocator* alloc = &gBackendVk.parentAlloc;

    if (gBackendVk.device)
        vkDeviceWaitIdle(gBackendVk.device);
    gBackendVk.queueMan.Release();

    _CollectGarbage(true);

    gBackendVk.pipelineLayouts.Free();
    gBackendVk.images.Free();
    gBackendVk.garbage.Free();
    gBackendVk.garbageMtx.Release();

    gBackendVk.memMan.Release();
    _ReleaseSwapchain(&gBackendVk.swapchain);

    _ReleaseDevice();

    if (gBackendVk.surface)
        vkDestroySurfaceKHR(gBackendVk.instance.handle, gBackendVk.surface, gBackendVk.vkAlloc);
    if (gBackendVk.debugMessenger) 
        vkDestroyDebugUtilsMessengerEXT(gBackendVk.instance.handle, gBackendVk.debugMessenger, gBackendVk.vkAlloc);

    Mem::Free(gBackendVk.swapchainInfo.formats, alloc);
    Mem::Free(gBackendVk.swapchainInfo.presentModes, alloc);

    _ReleaseInstance();
    gBackendVk.frameSyncSignal.Release();

    gBackendVk.runtimeAllocBase.Release();
    gBackendVk.driverAllocBase.Release();
    gBackendVk.driverAlloc.Release();
    gBackendVk.runtimeAlloc.Release();
    gBackendVk.parentAlloc.Release();
}

//   █████╗ ██╗     ██╗      ██████╗  ██████╗
//  ██╔══██╗██║     ██║     ██╔═══██╗██╔════╝
//  ███████║██║     ██║     ██║   ██║██║     
//  ██╔══██║██║     ██║     ██║   ██║██║     
//  ██║  ██║███████╗███████╗╚██████╔╝╚██████╗
//  ╚═╝  ╚═╝╚══════╝╚══════╝ ╚═════╝  ╚═════╝

void* GfxBackendAllocator::Malloc(size_t size, uint32 align)
{
    SpinLockMutexScope lk(mMutex);
    return mTlsfAlloc.Malloc(size, align);
}

void* GfxBackendAllocator::Realloc(void* ptr, size_t size, uint32 align)
{
    SpinLockMutexScope lk(mMutex);
    return mTlsfAlloc.Realloc(ptr, size, align);
}

void GfxBackendAllocator::Free(void* ptr, uint32 align)
{
    SpinLockMutexScope lk(mMutex);
    mTlsfAlloc.Free(ptr, align);
}

void GfxBackendAllocator::Initialize(MemAllocator* alloc, size_t poolSize, bool debugMode)
{
    mTlsfAlloc.Initialize(alloc, poolSize, debugMode);
}

void GfxBackendAllocator::Release()
{
    mTlsfAlloc.Release();
}

//----------------------------------------------------------------------------------------------------------------------
GfxBackendVkAllocator::GfxBackendVkAllocator()
{
    mCallbacks = {
        .pUserData = this,
        .pfnAllocation = VkAlloc,
        .pfnReallocation = VkRealloc,
        .pfnFree = VkFree,
        .pfnInternalAllocation = VkInternalAllocFn,
        .pfnInternalFree = VkInternalFreeFn
    };
}

void* GfxBackendVkAllocator::VkAlloc(void*, size_t size, size_t align, VkSystemAllocationScope)
{
    // Align to minimum of 32 bytes 
    // because we don't know the size of alignment on free, we need to always force alignment!
    if (gBackendVk.driverAllocBase.mTlsfAlloc.IsDebugMode()) {
        uint32 minAlign = CONFIG_MACHINE_ALIGNMENT << 1;
        align = Max(minAlign, uint32(align));
    }

    return gBackendVk.driverAlloc.Malloc(size, uint32(align));
}

void* GfxBackendVkAllocator::VkRealloc(void*, void* pOriginal, size_t size, size_t align, VkSystemAllocationScope)
{
    [[maybe_unused]] void* freePtr = pOriginal;
    if (gBackendVk.driverAllocBase.mTlsfAlloc.IsDebugMode()) {
        uint32 minAlign = CONFIG_MACHINE_ALIGNMENT << 1;
        align = Max(minAlign, uint32(align));
    }

    return gBackendVk.driverAlloc.Realloc(pOriginal, size, uint32(align));
}

void GfxBackendVkAllocator::VkFree(void*, void* pPtr)
{
    // TODO: we have to know the alignment here, this is not exactly the best approach
    if (gBackendVk.driverAllocBase.mTlsfAlloc.IsDebugMode())
        gBackendVk.driverAlloc.Free(pPtr, CONFIG_MACHINE_ALIGNMENT << 1);
    else 
        gBackendVk.driverAlloc.Free(pPtr);
}

void GfxBackendVkAllocator::VkInternalAllocFn(void*, size_t, VkInternalAllocationType, VkSystemAllocationScope)
{
    // TODO
}

void GfxBackendVkAllocator::VkInternalFreeFn(void*, size_t, VkInternalAllocationType, VkSystemAllocationScope)
{
    // TODO
}
//----------------------------------------------------------------------------------------------------------------------

GfxBackendCommandBuffer GfxBackend::BeginCommandBuffer(GfxBackendQueueType queueType)
{
    ASSERT_MSG(!Jobs::IsRunningOnCurrentThread(), "BeginCommandBuffer cannot be called on Task threads");

    gBackendVk.frameSyncSignal.Increment();

    uint32 queueIndex = gBackendVk.queueMan.FindQueue(queueType);
    ASSERT(queueIndex != -1);
    GfxBackendQueue& queue = gBackendVk.queueMan.GetQueue(queueIndex);
    ASSERT(queue.handle);
    GfxBackendCommandBufferContext& cmdBufferCtx = queue.cmdBufferContexts[gBackendVk.queueMan.GetFrameIndex()];
    VkCommandBuffer cmdVk;
    
    uint32 cmdBufferIndex = cmdBufferCtx.cmdBuffers.Count();
    ASSERT(cmdBufferIndex < UINT16_MAX);

    if (!cmdBufferCtx.cmdBufferFreeList.IsEmpty()) {
        cmdVk = cmdBufferCtx.cmdBufferFreeList.PopLast();
    }
    else {
        VkCommandBufferAllocateInfo cmdBufferAllocInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = cmdBufferCtx.pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };

        if (vkAllocateCommandBuffers(gBackendVk.device, &cmdBufferAllocInfo, &cmdVk) != VK_SUCCESS) {
            ASSERT_ALWAYS(0, "AllocateCommandBuffers failed");
        }
    }

    ASSERT(cmdVk);
    cmdBufferCtx.cmdBuffers.Push(cmdVk);

    VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT 
    };

    [[maybe_unused]] VkResult r = vkBeginCommandBuffer(cmdVk, &beginInfo);
    ASSERT(r == VK_SUCCESS);

    GfxBackendCommandBuffer cmdBuffer {
        .mGeneration = gBackendVk.queueMan.GetGeneration(),
        .mCmdBufferIndex = uint16(cmdBufferIndex),
        .mQueueIndex = uint8(queueIndex)
    };

    // Record all pending buffer memory barriers
    if (!queue.pendingBarriers.IsEmpty()) {
        MemTempAllocator tempAlloc;

        VkBufferMemoryBarrier2* bufferBarriers = tempAlloc.MallocTyped<VkBufferMemoryBarrier2>(queue.pendingBarriers.Count());
        VkImageMemoryBarrier2* imageBarriers = tempAlloc.MallocTyped<VkImageMemoryBarrier2>(queue.pendingBarriers.Count());
        uint32 numBufferBarriers = 0;
        uint32 numImageBarriers = 0;

        for (uint32 i = 0; i < queue.pendingBarriers.Count(); i++) {
            const GfxBackendQueue::PendingBarrier& b = queue.pendingBarriers[i];
            if (b.type == GfxBackendQueue::PendingBarrier::BUFFER) {
                GfxBackendBuffer& buffer = gBackendVk.buffers.Data(b.bufferHandle);
                uint32 index = numBufferBarriers++;
                bufferBarriers[index] = b.bufferBarrier;
                bufferBarriers[index].buffer = buffer.handle;
                buffer.transitionedStage = bufferBarriers[index].dstStageMask;
                buffer.transitionedAccess = bufferBarriers[index].dstAccessMask;
            }
            else if (b.type == GfxBackendQueue::PendingBarrier::IMAGE) {
                GfxBackendImage& img = gBackendVk.images.Data(b.imageHandle);
                uint32 index = numImageBarriers++;
                imageBarriers[index] = b.imageBarrier;
                imageBarriers[index].image = img.handle;
                img.transitionedStage = imageBarriers[index].dstStageMask;
                img.transitionedAccess = imageBarriers[index].dstAccessMask;
            }
        }

        VkDependencyInfo depInfo {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = numBufferBarriers,
            .pBufferMemoryBarriers = bufferBarriers,
            .imageMemoryBarrierCount = numImageBarriers,
            .pImageMemoryBarriers = imageBarriers
        };
        
        vkCmdPipelineBarrier2(cmdVk, &depInfo);
        queue.pendingBarriers.Clear();
    }

    return cmdBuffer;
}

void GfxBackend::EndCommandBuffer(GfxBackendCommandBuffer cmdBuffer)
{
    VkCommandBuffer handle = GfxBackend::_GetCommandBufferHandle(cmdBuffer);
    [[maybe_unused]] VkResult r = vkEndCommandBuffer(handle);
    ASSERT(r == VK_SUCCESS);
}

void GfxBackendSwapchain::AcquireImage()
{
    [[maybe_unused]] VkResult r = vkAcquireNextImageKHR(gBackendVk.device, handle, UINT64_MAX, imageReadySemaphores[backbufferIdx], 
                                                        nullptr, &imageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR)
        resize = true;
    else if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) 
        ASSERT_ALWAYS(0, "Gfx: AcquireSwapchain failed");
}

bool GfxBackendMemoryBumpAllocator::Initialize(VkDeviceSize maxSize, uint32 memoryTypeIndex)
{
    ASSERT(memoryTypeIndex != -1);
    ASSERT(gBackendVk.device);
    ASSERT(maxSize);

    mMemTypeIndex = memoryTypeIndex;
    mCapacity = maxSize;
    mOffset = 0;

    VkMemoryAllocateInfo allocInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = maxSize,
        .memoryTypeIndex = memoryTypeIndex
    };

    if (gBackendVk.extApi.hasMemoryBudget) 
        ASSERT_MSG(gBackendVk.memMan.GetDeviceMemoryBudget(mMemTypeIndex) >= maxSize, "Not enough GPU memory available in the specified heap");

    VkResult r = vkAllocateMemory(gBackendVk.device, &allocInfo, gBackendVk.vkAlloc, &mDeviceMem);
    if (r != VK_SUCCESS) {
        MEM_FAIL();
        return false;
    }

    if (gBackendVk.extApi.hasMemoryBudget)
        Atomic::FetchSub(&gBackendVk.memMan.GetDeviceMemoryBudget(mMemTypeIndex), maxSize);

    const VkMemoryType& memType = gBackendVk.memMan.GetProps().memoryTypes[memoryTypeIndex];
    mTypeFlags = memType.propertyFlags;
    mHeapFlags = gBackendVk.memMan.GetProps().memoryHeaps[memType.heapIndex].flags;

    if (mTypeFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        r = vkMapMemory(gBackendVk.device, mDeviceMem, 0, VK_WHOLE_SIZE, 0, &mMappedData);
        ASSERT(r == VK_SUCCESS);
    }

    return true;
}

void GfxBackendMemoryBumpAllocator::Release()
{
    if (mDeviceMem) {
        if (mMappedData)
            vkUnmapMemory(gBackendVk.device, mDeviceMem);
        vkFreeMemory(gBackendVk.device, mDeviceMem, gBackendVk.vkAlloc);
    }
    mDeviceMem = nullptr;
    mOffset = 0;
    mCapacity = 0;
    mMemTypeIndex = 0;
}

GfxBackendDeviceMemory GfxBackendMemoryBumpAllocator::Malloc(const VkMemoryRequirements& memReq)
{
    if (!((memReq.memoryTypeBits >> mMemTypeIndex) & 0x1)) {
        ASSERT_ALWAYS(0, "Allocation for this resource is not supported by this memory type");
        return GfxBackendDeviceMemory {};
    }

    ASSERT(memReq.alignment);

    SpinLockMutexScope lock(mMutex);
    VkDeviceSize offset = mOffset;
    if (offset % memReq.alignment != 0)
        offset = AlignValue<VkDeviceSize>(offset, memReq.alignment);
    mOffset = offset + memReq.size;
    if (mOffset > mCapacity) {
        MEM_FAIL();
        return GfxBackendDeviceMemory {};
    }

    GfxBackendDeviceMemory mem {
        .handle = mDeviceMem,
        .offset = offset,
        .mappedData = mMappedData ? ((uint8*)mMappedData + offset) : nullptr,
        .isHeapDeviceLocal = (mHeapFlags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
        .isCpuVisible = (mTypeFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        .isCached = (mTypeFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) == VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        .isCoherent = (mTypeFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        .isLazilyAlloc = (mTypeFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) == VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT
    };

    return mem;
}

GfxImageHandle GfxBackend::CreateImage(const GfxBackendImageDesc& desc)
{
    ASSERT(desc.numMips <= GFXBACKEND_MAX_MIPS_PER_IMAGE);

    VkImageCreateInfo imageCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VkImageType(desc.type),
        .format = VkFormat(desc.format),
        .extent = {
            .width = desc.width,
            .height = desc.height,
            .depth = desc.depth
        },
        .mipLevels = desc.numMips,
        .arrayLayers = desc.numArrayLayers,
        .samples = VkSampleCountFlagBits(desc.multisampleFlags),
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VkImageUsageFlags(desc.usageFlags)
    };

    VkImage imageVk;
    VkResult r = vkCreateImage(gBackendVk.device, &imageCreateInfo, gBackendVk.vkAlloc, &imageVk);
    if (r != VK_SUCCESS)
        return GfxImageHandle();

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(gBackendVk.device, imageVk, &memReq);
    GfxBackendDeviceMemory mem = gBackendVk.memMan.Malloc(memReq, desc.arena);
    vkBindImageMemory(gBackendVk.device, imageVk, mem.handle, mem.offset);

    // View
    VkImageView imageViewVk;
    {
        // TEMP: view type can be cube / array / etc.
        VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
        switch (desc.type) {
            case GfxBackendImageType::Image1D: viewType = VK_IMAGE_VIEW_TYPE_1D; break;
            case GfxBackendImageType::Image2D: viewType = VK_IMAGE_VIEW_TYPE_2D; break;
            case GfxBackendImageType::Image3D: viewType = VK_IMAGE_VIEW_TYPE_3D; break;
        }

        VkImageViewCreateInfo viewCreateInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = imageVk,
            .viewType = viewType,
            .format = VkFormat(desc.format),
            .subresourceRange {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = desc.numMips,
                .layerCount = desc.numArrayLayers
            }
        };

        r = vkCreateImageView(gBackendVk.device, &viewCreateInfo, gBackendVk.vkAlloc, &imageViewVk);
        if (r != VK_SUCCESS) {
            vkDestroyImage(gBackendVk.device, imageVk, gBackendVk.vkAlloc);
            return GfxImageHandle();
        }
    }

    GfxBackendImage image {
        .handle = imageVk,
        .viewHandle = imageViewVk,
        .desc = desc,
        .mem = mem
    };
    return gBackendVk.images.Add(image);
}

void GfxBackend::DestroyImage(GfxImageHandle handle)
{
    if (handle.IsValid()) {
        GfxBackendImage& image = gBackendVk.images.Data(handle);

        {
            MutexScope lock(gBackendVk.garbageMtx);
            if (image.handle) {
                gBackendVk.garbage.Push({
                    .type = GfxBackendGarbage::Type::Image,
                    .frameIdx = gBackendVk.presentFrame,
                    .image = image.handle
                });
            }

            if (image.viewHandle) {
                gBackendVk.garbage.Push({
                    .type = GfxBackendGarbage::Type::ImageView,
                    .frameIdx = gBackendVk.presentFrame,
                    .imageView = image.viewHandle
                });
            }
        }

        gBackendVk.images.Remove(handle);
    }
}

const GfxBackendImageDesc& GfxBackend::GetImageDesc(GfxImageHandle handle)
{
    const GfxBackendImage& image = gBackendVk.images.Data(handle);
    return image.desc;
}

GfxPipelineLayoutHandle GfxBackend::CreatePipelineLayout(const GfxShader& shader, const GfxBackendPipelineLayoutDesc& desc)
{
    ASSERT(desc.numBindings);
    ASSERT(desc.bindings);

    struct DescriptorSetRef
    {
        uint32 startIndex;
        uint32 count;
    };

    MemTempAllocator tempAlloc;
    
    // Construct Vulkan-specific structs for bindings and their names
    VkDescriptorSetLayoutBinding* bindingsVk = tempAlloc.MallocTyped<VkDescriptorSetLayoutBinding>(desc.numBindings);
    Array<const char*> names(&tempAlloc);
    Array<GfxBackendPipelineLayoutDesc::Binding> bindings(&tempAlloc);
    StaticArray<DescriptorSetRef, GFXBACKEND_MAX_SETS_PER_PIPELINE> sets;

    bindings.Reserve(desc.numBindings);
    names.Reserve(desc.numBindings + desc.numPushConstants);
    
    // Sort all bindings by their set index
    for (uint32 i = 0; i < desc.numBindings; i++)  {
        ASSERT(desc.bindings[i].setIndex < GFXBACKEND_MAX_SETS_PER_PIPELINE);
        bindings.PushAndSort(desc.bindings[i], [](const GfxBackendPipelineLayoutDesc::Binding& a, GfxBackendPipelineLayoutDesc::Binding& b) {
            return int(a.setIndex) - int(b.setIndex);
        });
    }

    uint32 setBindingStartIndex = 0;
    uint32 setBindingCount = 0;
    uint8 setIndex = bindings[0].setIndex;
    for (uint32 i = 0; i < bindings.Count(); i++) {
        const GfxBackendPipelineLayoutDesc::Binding& binding = bindings[i];
        ASSERT(binding.arrayCount > 0);
        ASSERT(binding.name && binding.name[0]);

        const GfxShaderParameterInfo* shaderParam = _FindShaderParam(shader, binding.name);
        ASSERT_MSG(shaderParam != nullptr, "Shader parameter '%s' does not exist in shader '%s'", binding.name, shader.name);
        ASSERT_MSG(!shaderParam->isPushConstant, "Shader parameter '%s' is a push-constant in shader '%s'. cannot be used as regular uniform", 
                   binding.name, shader.name);
        if (!shaderParam)
            continue;

        names.Push(binding.name);
        bindingsVk[i] = {
            .binding = shaderParam->bindingIdx,
            .descriptorType = VkDescriptorType(binding.type),
            .descriptorCount = binding.arrayCount,
            .stageFlags = VkShaderStageFlags(binding.stagesUsed)
        };

        if (binding.setIndex != setIndex) {
            sets.Push({setBindingStartIndex, setBindingCount});
            setBindingCount = 1;
            setBindingStartIndex = i;
        }
        else {
            ++setBindingCount;
        }
    }
    sets.Push({setBindingStartIndex, setBindingCount});

    VkPushConstantRange* pushConstantsVk = nullptr;
    if (desc.numPushConstants)
        pushConstantsVk = tempAlloc.MallocTyped<VkPushConstantRange>(desc.numPushConstants);
    uint32 totalPushConstantSize = 0;
    for (uint32 i = 0; i < desc.numPushConstants; i++) {
        names.Push(desc.pushConstants[i].name);

        pushConstantsVk[i] = {
            .stageFlags = VkShaderStageFlags(desc.pushConstants[i].stagesUsed),
            .offset = desc.pushConstants[i].offset,
            .size = desc.pushConstants[i].size
        };

        totalPushConstantSize += desc.pushConstants[i].size;
    }
    ASSERT_ALWAYS(totalPushConstantSize <= gBackendVk.gpu.props.limits.maxPushConstantsSize, 
                  "PushConstants are too big (%u bytes but the limit is %u bytes)", 
                  totalPushConstantSize, gBackendVk.gpu.props.limits.maxPushConstantsSize);

    // HASH everything related to pipeline layout
    // Search in existing descriptor set layouts and try to find a match. 
    HashMurmur32Incremental hasher;
    hasher.Add<VkDescriptorSetLayoutBinding>(bindingsVk, bindings.Count())
          .AddCStringArray(names.Ptr(), bindings.Count() + desc.numPushConstants)
          .Add<DescriptorSetRef>(sets.Ptr(), sets.Count())
          .Add<bool>(desc.usePushDescriptors);
    if (desc.numPushConstants)
        hasher.Add<VkPushConstantRange>(pushConstantsVk, desc.numPushConstants);
    uint32 hash = hasher.Hash();
    
    if (GfxPipelineLayoutHandle layoutHandle = gBackendVk.pipelineLayouts.FindIf(
        [hash](const GfxBackendPipelineLayout* item)->bool { return item->hash == hash; }); 
        layoutHandle.IsValid())
    {
        GfxBackendPipelineLayout* item = gBackendVk.pipelineLayouts.Data(layoutHandle);
        ++item->refCount;
        return layoutHandle;
    }

    // Create pipeline data
    MemSingleShotMalloc<GfxBackendPipelineLayout> mallocator;
    mallocator.AddMemberArray<GfxBackendPipelineLayout::Binding>(offsetof(GfxBackendPipelineLayout, bindings), bindings.Count());
    mallocator.AddMemberArray<uint32>(offsetof(GfxBackendPipelineLayout, bindingNameHashes), bindings.Count());
    mallocator.AddMemberArray<VkDescriptorSetLayoutBinding>(offsetof(GfxBackendPipelineLayout, bindingsVk), bindings.Count());
    mallocator.AddMemberArray<VkDescriptorSetLayout>(offsetof(GfxBackendPipelineLayout, sets), sets.Count());
    if (desc.numPushConstants) {
        mallocator.AddMemberArray<VkPushConstantRange>(offsetof(GfxBackendPipelineLayout, pushConstantRanges), desc.numPushConstants);
        mallocator.AddMemberArray<uint32>(offsetof(GfxBackendPipelineLayout, pushConstantNameHashes), desc.numPushConstants);
    }
    GfxBackendPipelineLayout* layout = mallocator.Calloc(&gBackendVk.runtimeAlloc);
    
    layout->hash = hash;
    layout->numBindings = bindings.Count();
    layout->refCount = 1;
    layout->numPushConstants = desc.numPushConstants;
    layout->numSets = sets.Count();

    // Binding meta data
    memcpy(layout->bindingsVk, bindingsVk, bindings.Count()*sizeof(VkDescriptorSetLayoutBinding));
    for (uint32 i = 0; i < bindings.Count(); i++) {
        const GfxBackendPipelineLayoutDesc::Binding& srcBinding = bindings[i];
        GfxBackendPipelineLayout::Binding& dstBinding = layout->bindings[i];

        dstBinding.name = srcBinding.name;
        dstBinding.arrayCount = srcBinding.arrayCount;
        dstBinding.setIndex = srcBinding.setIndex;

        layout->bindingNameHashes[i] = Hash::Fnv32Str(srcBinding.name);
    }

    // PushConstant meta data
    if (desc.numPushConstants) {
        memcpy(layout->pushConstantRanges, pushConstantsVk, desc.numPushConstants*sizeof(VkPushConstantRange));
        for (uint32 i = 0; i < desc.numPushConstants; i++)
            layout->pushConstantNameHashes[i] = Hash::Fnv32Str(desc.pushConstants[i].name);
    }

    // Create the descriptor set layouts 
    for (uint32 i = 0; i < sets.Count(); i++) {
        const DescriptorSetRef& set = sets[i];
        VkDescriptorSetLayoutBinding* setBindings = tempAlloc.MallocTyped<VkDescriptorSetLayoutBinding>(set.count);
        ASSERT(set.startIndex < bindings.Count());
        ASSERT(set.startIndex + set.count <= bindings.Count());
        memcpy(setBindings, &bindingsVk[set.startIndex], set.count*sizeof(VkDescriptorSetLayoutBinding));

        VkDescriptorSetLayoutCreateInfo setCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .flags = desc.usePushDescriptors ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR : 0u,
            .bindingCount = set.count,
            .pBindings = setBindings
        };

        // VK_EXT_descriptor_indexing
        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT layoutBindingFlags {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT,
            .bindingCount = set.count,
        };

        VkDescriptorBindingFlags* setBindingFlags = tempAlloc.MallocTyped<VkDescriptorBindingFlags>(set.count);
        for (uint32 k = 0; k < set.count; k++)
            setBindingFlags[k] = (setBindings[k].descriptorCount > 1) ? VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT: 0;
        layoutBindingFlags.pBindingFlags = setBindingFlags;
        setCreateInfo.pNext = &layoutBindingFlags;

        if (vkCreateDescriptorSetLayout(gBackendVk.device, &setCreateInfo, gBackendVk.vkAlloc, &layout->sets[i]) != VK_SUCCESS) {
            MemSingleShotMalloc<GfxBackendPipelineLayout>::Free(layout, &gBackendVk.runtimeAlloc);      
            ASSERT(0);
            return GfxPipelineLayoutHandle();
        }
    }

    // Now create pipeline layout itself
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = sets.Count(),
        .pSetLayouts = layout->sets,
        .pushConstantRangeCount = desc.numPushConstants,
        .pPushConstantRanges = layout->pushConstantRanges
    };
    if (vkCreatePipelineLayout(gBackendVk.device, &pipelineLayoutCreateInfo, gBackendVk.vkAlloc, &layout->handle) != VK_SUCCESS) {
        MemSingleShotMalloc<GfxBackendPipelineLayout>::Free(layout, &gBackendVk.runtimeAlloc);
        ASSERT(0);
        return GfxPipelineLayoutHandle();
    }

    return gBackendVk.pipelineLayouts.Add(layout);
}

void GfxBackend::DestroyPipelineLayout(GfxPipelineLayoutHandle handle)
{
    if (handle.IsValid()) {
        GfxBackendPipelineLayout* pipelineLayout = gBackendVk.pipelineLayouts.Data(handle);
    
        MutexScope lock(gBackendVk.garbageMtx);

        for (uint32 i = 0; i < pipelineLayout->numSets; i++)  {
            gBackendVk.garbage.Push({
                .type = GfxBackendGarbage::Type::DescriptorSetLayout,
                .frameIdx = gBackendVk.presentFrame,
                .dsetLayout = pipelineLayout->sets[i]
            });
        }

        if (pipelineLayout->handle) {
            gBackendVk.garbage.Push({
                .type = GfxBackendGarbage::Type::PipelineLayout,
                .frameIdx = gBackendVk.presentFrame,
                .pipelineLayout = pipelineLayout->handle
            });
        }

        MemSingleShotMalloc<GfxBackendPipelineLayout>::Free(pipelineLayout, &gBackendVk.runtimeAlloc);
        gBackendVk.pipelineLayouts.Remove(handle);
    }
}

GfxPipelineHandle GfxBackend::CreateGraphicsPipeline(const GfxShader& shader, GfxPipelineLayoutHandle layoutHandle, 
                                                     const GfxBackendGraphicsPipelineDesc& desc)
{
    MemTempAllocator tempAlloc;

    const GfxShaderStageInfo* vsInfo = nullptr; 
    const GfxShaderStageInfo* psInfo = nullptr; 

    for (uint32 i = 0; i < shader.numStages; i++) {
        if (shader.stages[i].stage == GfxShaderStage::Vertex)
            vsInfo = &shader.stages[i];
        if (shader.stages[i].stage == GfxShaderStage::Fragment)
            psInfo = &shader.stages[i];
    }
    ASSERT_MSG(vsInfo, "Shader '%s' is missing Vertex shader program", shader.name);
    ASSERT_MSG(psInfo, "Shader '%s' is missing Pixel shader program", shader.name);

    VkPipelineLayout layoutVk = gBackendVk.pipelineLayouts.Data(layoutHandle)->handle;
    VkShaderModule psShaderModule;
    VkShaderModule vsShaderModule;
    {
        VkShaderModuleCreateInfo shaderStageCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = vsInfo->dataSize,
            .pCode = (const uint32*)vsInfo->data.Get()
        };

        if (vkCreateShaderModule(gBackendVk.device, &shaderStageCreateInfo, gBackendVk.vkAlloc, &vsShaderModule) != VK_SUCCESS) {
            LOG_ERROR("Gfx: Failed to compile Vertex module for shader '%s'", shader.name);
            return GfxPipelineHandle();
        }
    }

    {
        VkShaderModuleCreateInfo shaderStageCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = psInfo->dataSize,
            .pCode = (const uint32*)psInfo->data.Get()
        };

        if (vkCreateShaderModule(gBackendVk.device, &shaderStageCreateInfo, gBackendVk.vkAlloc, &psShaderModule) != VK_SUCCESS) {
            LOG_ERROR("Gfx: Failed to compile Pixel module for shader '%s'", shader.name);
            return GfxPipelineHandle();
        }
    }


    VkPipelineShaderStageCreateInfo shaderStages[] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vsShaderModule,
            .pName = "main"
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = psShaderModule,
            .pName = "main"
        }
    };

    ASSERT_MSG(desc.numVertexBufferBindings > 0, "Must provide vertex buffer bindings");
    VkVertexInputBindingDescription* vertexBindingDescs = 
        tempAlloc.MallocTyped<VkVertexInputBindingDescription>(desc.numVertexBufferBindings);
    for (uint32 i = 0; i < desc.numVertexBufferBindings; i++) {
        vertexBindingDescs[i] = {
            .binding = desc.vertexBufferBindings[i].binding,
            .stride = desc.vertexBufferBindings[i].stride,
            .inputRate = VkVertexInputRate(desc.vertexBufferBindings[i].inputRate)
        };
    }

    ASSERT_MSG(desc.numVertexInputAttributes == shader.numVertexAttributes, 
               "Provided number of vertex attributes does not match with the compiled shader");

    VkVertexInputAttributeDescription* vertexInputAtts = 
        tempAlloc.MallocTyped<VkVertexInputAttributeDescription>(desc.numVertexInputAttributes);
    for (uint32 i = 0; i < desc.numVertexInputAttributes; i++) {
        // Validation:
        // Semantic/SemanticIndex
        ASSERT_MSG(desc.vertexInputAttributes[i].semantic == shader.vertexAttributes[i].semantic &&
                   desc.vertexInputAttributes[i].semanticIdx == shader.vertexAttributes[i].semanticIdx, 
                   "Vertex input attributes does not match with shader: (Index: %u, Shader: %s%u, Desc: %s%u)",
                   i, shader.vertexAttributes[i].semantic, shader.vertexAttributes[i].semanticIdx, 
                   desc.vertexInputAttributes[i].semantic.CStr(), desc.vertexInputAttributes[i].semanticIdx);
        // Format: Current exception is "COLOR" with RGBA8_UNORM on the CPU side and RGBA32_SFLOAT on shader side
        ASSERT_MSG(desc.vertexInputAttributes[i].format == shader.vertexAttributes[i].format ||
                   (desc.vertexInputAttributes[i].semantic == "COLOR" && 
                   desc.vertexInputAttributes[i].format == GfxFormat::R8G8B8A8_UNORM &&
                   shader.vertexAttributes[i].format == GfxFormat::R32G32B32A32_SFLOAT),
                   "Vertex input attribute formats do not match");
        
        vertexInputAtts[i] = {
            .location = shader.vertexAttributes[i].location,
            .binding = desc.vertexInputAttributes[i].binding,
            .format = static_cast<VkFormat>(desc.vertexInputAttributes[i].format),
            .offset = desc.vertexInputAttributes[i].offset
        };
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = desc.numVertexBufferBindings,
        .pVertexBindingDescriptions = vertexBindingDescs,
        .vertexAttributeDescriptionCount = desc.numVertexInputAttributes,
        .pVertexAttributeDescriptions = vertexInputAtts
    };

    // InputAssembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { 
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VkPrimitiveTopology(desc.inputAssemblyTopology)
    };

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = desc.rasterizer.depthClampEnable,
        .rasterizerDiscardEnable = desc.rasterizer.rasterizerDiscardEnable,
        .polygonMode = VkPolygonMode(desc.rasterizer.polygonMode),
        .cullMode = VkCullModeFlags(desc.rasterizer.cullMode),
        .frontFace = VkFrontFace(desc.rasterizer.frontFace),
        .depthBiasEnable = desc.rasterizer.depthBiasEnable,
        .depthBiasConstantFactor = desc.rasterizer.depthBiasConstantFactor,
        .depthBiasClamp = desc.rasterizer.depthBiasClamp,
        .depthBiasSlopeFactor = desc.rasterizer.depthBiasSlopeFactor,
        .lineWidth = desc.rasterizer.lineWidth
    };

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f, // Optional
        .pSampleMask = nullptr, // Optional
        .alphaToCoverageEnable = VK_FALSE, // Optional
        .alphaToOneEnable = VK_FALSE // Optional
    };

    // Blending
    uint32 numBlendAttachments = Max(desc.blend.numAttachments, 1u);
    const GfxBlendAttachmentDesc* blendAttachmentDescs = !desc.blend.attachments ? GfxBlendAttachmentDesc::GetDefault() : desc.blend.attachments;
        
    VkPipelineColorBlendAttachmentState* colorBlendAttachments = 
        tempAlloc.MallocTyped<VkPipelineColorBlendAttachmentState>(numBlendAttachments);
    for (uint32 i = 0; i < numBlendAttachments; i++) {
        const GfxBlendAttachmentDesc& ba = blendAttachmentDescs[i];
        VkPipelineColorBlendAttachmentState state {
            .blendEnable = ba.enable,
            .srcColorBlendFactor = static_cast<VkBlendFactor>(ba.srcColorBlendFactor),
            .dstColorBlendFactor = static_cast<VkBlendFactor>(ba.dstColorBlendFactor),
            .colorBlendOp = static_cast<VkBlendOp>(ba.blendOp),
            .srcAlphaBlendFactor = static_cast<VkBlendFactor>(ba.srcAlphaBlendFactor),
            .dstAlphaBlendFactor = static_cast<VkBlendFactor>(ba.dstAlphaBlendFactor),
            .alphaBlendOp = static_cast<VkBlendOp>(ba.alphaBlendOp),
            .colorWriteMask = static_cast<VkColorComponentFlags>(ba.colorWriteMask)
        };
        colorBlendAttachments[i] = state;
    }

    VkPipelineColorBlendStateCreateInfo colorBlend {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = desc.blend.logicOpEnable,
        .logicOp = static_cast<VkLogicOp>(desc.blend.logicOp),
        .attachmentCount = numBlendAttachments,
        .pAttachments = colorBlendAttachments,
        .blendConstants = {
            desc.blend.blendConstants[0], 
            desc.blend.blendConstants[1], 
            desc.blend.blendConstants[2], 
            desc.blend.blendConstants[3]
        }
    };

    // Dyanamic state
    // TODO: maybe also make use of new VK_EXT_extended_dynamic_state and VK_EXT_extended_dynamic_state2 extensions
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR            
    };
    VkPipelineDynamicStateCreateInfo dynamicState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = CountOf(dynamicStates),
        .pDynamicStates = dynamicStates
    };

    // ViewportState (dynamic)
    // TODO: Add scissors and valid viewport counts to desc
    VkPipelineViewportStateCreateInfo viewportState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = nullptr, // Dynamic state
        .scissorCount = 1,
        .pScissors = nullptr   // Dynamic state
    };

    // DepthStencil
    VkPipelineDepthStencilStateCreateInfo depthStencil {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = desc.depthStencil.depthTestEnable,
        .depthWriteEnable = desc.depthStencil.depthWriteEnable,
        .depthCompareOp = static_cast<VkCompareOp>(desc.depthStencil.depthCompareOp),
        .depthBoundsTestEnable = desc.depthStencil.depthBoundsTestEnable,
        .stencilTestEnable = desc.depthStencil.stencilTestEnable,
        .minDepthBounds = desc.depthStencil.minDepthBounds,
        .maxDepthBounds = desc.depthStencil.maxDepthBounds
    };

    
    VkGraphicsPipelineCreateInfo pipelineInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .flags = gBackendVk.extApi.hasPipelineExecutableProperties ? VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR : (VkPipelineCreateFlags)0,
        .stageCount = CountOf(shaderStages),
        .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlend,
        .pDynamicState = &dynamicState,
        .layout = layoutVk,
        .renderPass = nullptr, // TODO
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1
    };

    VkPipeline pipelineVk;
    if (vkCreateGraphicsPipelines(gBackendVk.device, nullptr /* TODO */, 1, &pipelineInfo, gBackendVk.vkAlloc, &pipelineVk) != VK_SUCCESS) {
        LOG_ERROR("Gfx: Failed to create graphics pipeline for shader '%s'", shader.name);
        return GfxPipelineHandle();
    }

    // Should we keep these shader modules ?
    vkDestroyShaderModule(gBackendVk.device, vsShaderModule, gBackendVk.vkAlloc);
    vkDestroyShaderModule(gBackendVk.device, psShaderModule, gBackendVk.vkAlloc);

    // TODO: gfxSavePipelineBinaryProperties()
    GfxBackendPipeline pipeline {
        .handle = pipelineVk,
        .type = GfxBackendPipeline::PipelineTypeGraphics
    };

    return gBackendVk.pipelines.Add(pipeline);
}

GfxPipelineHandle GfxBackend::CreateComputePipeline(const GfxShader& shader, GfxPipelineLayoutHandle layoutHandle)
{
    MemTempAllocator tempAlloc;

    const GfxShaderStageInfo* csInfo = nullptr; 

    for (uint32 i = 0; i < shader.numStages; i++) {
        if (shader.stages[i].stage == GfxShaderStage::Compute)
            csInfo = &shader.stages[i];
    }
    ASSERT_MSG(csInfo, "Shader '%s' is missing Compute shader program", shader.name);

    VkPipelineLayout layoutVk = gBackendVk.pipelineLayouts.Data(layoutHandle)->handle;
    VkShaderModule csShaderModule;

    VkShaderModuleCreateInfo shaderStageCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = csInfo->dataSize,
        .pCode = (const uint32*)csInfo->data.Get()
    };

    if (vkCreateShaderModule(gBackendVk.device, &shaderStageCreateInfo, gBackendVk.vkAlloc, &csShaderModule) != VK_SUCCESS) {
        LOG_ERROR("Gfx: Failed to compile Compute module for shader '%s'", shader.name);
        return GfxPipelineHandle();
    }

    VkComputePipelineCreateInfo pipelineCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = csShaderModule,
            .pName = "main"
        },
        .layout = layoutVk,
    };

    VkPipeline pipelineVk;
    if (vkCreateComputePipelines(gBackendVk.device, nullptr /* TODO */, 1, &pipelineCreateInfo, gBackendVk.vkAlloc, &pipelineVk) != VK_SUCCESS) {
        LOG_ERROR("Gfx: Failed to create compute pipeline for shader '%s'", shader.name);
        return GfxPipelineHandle();
    }

    // Should we keep the shader module ?
    vkDestroyShaderModule(gBackendVk.device, csShaderModule, gBackendVk.vkAlloc);

    // TODO: gfxSavePipelineBinaryProperties()
    GfxBackendPipeline pipeline {
        .handle = pipelineVk,
        .type = GfxBackendPipeline::PipelineTypeCompute
    };

    return gBackendVk.pipelines.Add(pipeline);
}


void GfxBackend::DestroyPipeline(GfxPipelineHandle handle)
{
    if (handle.IsValid()) {
        GfxBackendPipeline& pipeline = gBackendVk.pipelines.Data(handle);
        if (pipeline.handle) {
            MutexScope lock(gBackendVk.garbageMtx);
            gBackendVk.garbage.Push({
                .type = GfxBackendGarbage::Type::Pipeline,
                .frameIdx = gBackendVk.presentFrame,
                .pipeline = pipeline.handle
            });
        }
        gBackendVk.pipelines.Remove(handle);
    }
}

void GfxBackendCommandBuffer::PushConstants(GfxPipelineLayoutHandle layoutHandle, const char* name, const void* data, uint32 dataSize)
{
    ASSERT(data);
    ASSERT(dataSize);
    ASSERT(name);

    GfxBackendPipelineLayout& layout = *gBackendVk.pipelineLayouts.Data(layoutHandle);
    VkPipelineLayout layoutVk = layout.handle;
    ASSERT(layoutVk);
   
    VkPushConstantRange* range = nullptr;
    uint32 nameHash = Hash::Fnv32Str(name);
    for (uint32 i = 0; i < layout.numPushConstants; i++) {
        if (layout.pushConstantNameHashes[i] == nameHash) {
            range = &layout.pushConstantRanges[i];
            break;
        }        
    }

    ASSERT_MSG(range, "PushConstants '%s' not found in pipeline layout", name);
    ASSERT_MSG(range->size == dataSize, "PushConstants '%s' data size mismatch", name);

    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    vkCmdPushConstants(cmdVk, layoutVk, range->stageFlags, range->offset, range->size, data);
}

void GfxBackendCommandBuffer::PushBindings(GfxPipelineLayoutHandle layoutHandle, uint32 numBindings, const GfxBackendBindingDesc* bindings)
{
    // TODO: remove stage argument
    ASSERT(numBindings);
    ASSERT(bindings);

    GfxBackendPipelineLayout& layout = *gBackendVk.pipelineLayouts.Data(layoutHandle);
    VkPipelineLayout layoutVk = layout.handle;
    ASSERT(layoutVk);

    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);

    MemTempAllocator tempAlloc;

    // First element in mapping item is the index in layout bindings, second one is the index in 'bindings' argument
    using BindMappingItem = Pair<uint32, uint32>;
    Array<BindMappingItem> bindMappings[GFXBACKEND_MAX_SETS_PER_PIPELINE];
    VkShaderStageFlags stagesUsed = 0;
    for (uint32 i = 0; i < GFXBACKEND_MAX_SETS_PER_PIPELINE; i++)
        bindMappings->SetAllocator(&tempAlloc);

    for (uint32 i = 0; i < numBindings; i++) {
        const GfxBackendBindingDesc& binding = bindings[i];
        uint32 nameHash = Hash::Fnv32Str(binding.name);
        uint32 foundBinding = uint32(-1);
        for (uint32 k = 0; k < layout.numBindings; k++) {
            if (layout.bindingNameHashes[k] == nameHash) {
                foundBinding = k;
                break;
            }
        }

        ASSERT_MSG(foundBinding != -1, "Binding '%s' doesn't exist in this pipeline layout", binding.name);

        uint32 setIndex = layout.bindings[foundBinding].setIndex;
        const VkDescriptorSetLayoutBinding& bindingVk = layout.bindingsVk[foundBinding];
        bindMappings[setIndex].Push(BindMappingItem(foundBinding, i));

        stagesUsed |= bindingVk.stageFlags;
    }

    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_MAX_ENUM;
    if (VkShaderStageFlags(stagesUsed) & (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT))
        bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    else if (VkShaderStageFlags(stagesUsed) & VK_SHADER_STAGE_COMPUTE_BIT)
        bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    ASSERT(bindPoint != VK_PIPELINE_BIND_POINT_MAX_ENUM);

    for (uint32 setIdx = 0; setIdx < GFXBACKEND_MAX_SETS_PER_PIPELINE; setIdx++) {
        if (bindMappings[setIdx].IsEmpty())
            continue;

        uint32 numSetBindings = bindMappings[setIdx].Count();

        // Write descriptor sets for each set
        Array<VkDescriptorImageInfo> imageInfos(&tempAlloc);
        Array<VkDescriptorBufferInfo> bufferInfos(&tempAlloc);
        VkWriteDescriptorSet* descriptorWrites = tempAlloc.MallocTyped<VkWriteDescriptorSet>(numSetBindings);

        for (uint32 i = 0; i < numSetBindings; i++) {
            uint32 layoutBindingIdx = bindMappings[setIdx][i].first;
            uint32 idx = bindMappings[setIdx][i].second;
            const GfxBackendBindingDesc& binding = bindings[idx];
            const VkDescriptorSetLayoutBinding& bindingVk = layout.bindingsVk[layoutBindingIdx];

            VkDescriptorImageInfo* pImageInfo = nullptr;
            VkImageLayout imgLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            VkDescriptorBufferInfo* pBufferInfo;

            switch (bindingVk.descriptorType) {
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: 
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            {
                const GfxBackendBuffer& buffer = gBackendVk.buffers.Data(binding.buffer);
                pBufferInfo = bufferInfos.Push();
                *pBufferInfo = {
                    .buffer = buffer.handle,
                    .offset = binding.bufferRange.offset,
                    .range = binding.bufferRange.size == 0 ? VK_WHOLE_SIZE : binding.bufferRange.size
                };
                break;
            } 
            case VK_DESCRIPTOR_TYPE_SAMPLER:
            {
                ASSERT(0);
                /*
                imageInfos[i] = {
                    .sampler = binding.image.IsValid() ? gVk.pools.mImages.Data(binding.image).sampler : VK_NULL_HANDLE,
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                };
                pImageInfo = &imageInfos[i];
                */
                break;
            }
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            {
                ASSERT(0);
                /*
                if (!binding.imageArrayCount) {
                    const GfxImageData* imageData = binding.image.IsValid() ? &gVk.pools.mImages.Data(binding.image) : nullptr;
                    imageInfos[i] = VkDescriptorImageInfo {
                        .sampler = imageData ? imageData->sampler : VK_NULL_HANDLE,
                        .imageView = imageData ? imageData->view : VK_NULL_HANDLE,
                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    };
                    pImageInfo = &imageInfos[i];
                }
                else {
                    // VK_EXT_descriptor_indexing
                    // TODO: (DescriptorIndexing) do the same for SampledImages ? need to see how Samplers end up like
                    descriptorCount = binding.imageArrayCount;
                    pImageInfo = tempAlloc.MallocTyped<VkDescriptorImageInfo>(binding.imageArrayCount);
                    for (uint32 img = 0; img < binding.imageArrayCount; img++) {
                        const GfxImageData* imageData = binding.imageArray[img].IsValid() ? &gVk.pools.mImages.Data(binding.imageArray[img]) : nullptr;
                        pImageInfo[img] = VkDescriptorImageInfo {
                            .sampler = imageData ? imageData->sampler : VK_NULL_HANDLE,
                            .imageView = imageData ? imageData->view : VK_NULL_HANDLE,
                            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        };
                    }
                }
                */
                break;
            }
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                if (!imgLayout)
                    imgLayout = VK_IMAGE_LAYOUT_GENERAL;
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                if (!imgLayout)
                    imgLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                pImageInfo = imageInfos.Push();
                *pImageInfo = {
                    .imageView = binding.image.IsValid() ? gBackendVk.images.Data(binding.image).viewHandle : VK_NULL_HANDLE,
                    .imageLayout = imgLayout
                };
                break;

            default:
                ASSERT_MSG(0, "Descriptor type is not implemented");
                break;
            }    // switch(bindingVk.descriptorType)

            descriptorWrites[i] = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstBinding = bindingVk.binding,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VkDescriptorType(bindingVk.descriptorType),
                .pImageInfo = pImageInfo,
                .pBufferInfo = nullptr,
                .pTexelBufferView = nullptr
            };
        } // foreach binding

        vkCmdPushDescriptorSetKHR(cmdVk, bindPoint, layoutVk, setIdx, numSetBindings, descriptorWrites);
    } // foreach descriptor set
}

void GfxBackendCommandBuffer::BindPipeline(GfxPipelineHandle pipeHandle)
{
    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    GfxBackendPipeline& pipe = gBackendVk.pipelines.Data(pipeHandle);

    VkPipelineBindPoint bindPoint = pipe.type == GfxBackendPipeline::PipelineTypeCompute ? 
        VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    vkCmdBindPipeline(cmdVk, bindPoint, pipe.handle);
}

void GfxBackendCommandBuffer::Dispatch(uint32 groupCountX, uint32 groupCountY, uint32 groupCountZ)
{
    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    vkCmdDispatch(cmdVk, groupCountX, groupCountY, groupCountZ);
}


GfxBufferHandle GfxBackend::CreateBuffer(const GfxBackendBufferDesc& desc)
{
    ASSERT(desc.sizeBytes);

    VkBufferCreateInfo bufferCreateInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = desc.sizeBytes,
        .usage = VkBufferUsageFlags(desc.usageFlags)
    };

    VkBuffer bufferVk;
    VkResult r = vkCreateBuffer(gBackendVk.device, &bufferCreateInfo, gBackendVk.vkAlloc, &bufferVk);
    if (r != VK_SUCCESS)
        return GfxBufferHandle();

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(gBackendVk.device, bufferVk, &memReq);
    GfxBackendDeviceMemory mem = gBackendVk.memMan.Malloc(memReq, desc.arena);
    vkBindBufferMemory(gBackendVk.device, bufferVk, mem.handle, mem.offset);

    GfxBackendBuffer buffer {
        .handle = bufferVk,
        .desc = desc,
        .mem = mem
    };

    return gBackendVk.buffers.Add(buffer);
}

void GfxBackend::DestroyBuffer(GfxBufferHandle handle)
{
    if (handle.IsValid()) {
        GfxBackendBuffer& buffer = gBackendVk.buffers.Data(handle);

        MutexScope lock(gBackendVk.garbageMtx);
        gBackendVk.garbage.Push({
            .type = GfxBackendGarbage::Type::Buffer,
            .frameIdx = gBackendVk.presentFrame,
            .buffer = buffer.handle
        });

        gBackendVk.buffers.Remove(handle);

        if (buffer.mem.arena == GfxBackendMemoryArena::TransientCPU) {
            gBackendVk.frameSyncSignal.Decrement();
            gBackendVk.frameSyncSignal.Raise();
        }
    }
}

GfxBackendDeviceMemory GfxBackendDeviceMemoryManager::Malloc(const VkMemoryRequirements& memReq, GfxBackendMemoryArena arena)
{
    GfxBackendDeviceMemory mem {
        .arena = arena
    };

    switch (arena) {
        case GfxBackendMemoryArena::PersistentGPU:
            mem = mPersistentGPU.Malloc(memReq);
            break;

        case GfxBackendMemoryArena::PersistentCPU:
            mem = mPersistentCPU.Malloc(memReq);
            break;

        case GfxBackendMemoryArena::TransientCPU:
            gBackendVk.frameSyncSignal.Increment();
            mem = mTransientCPU[mStagingIndex].Malloc(memReq);
            break;

        default:
            ASSERT_MSG(0, "Not implemented");
    }

    mem.arena = arena;
    return mem;
}

void GfxBackendDeviceMemoryManager::Free(GfxBackendDeviceMemory)
{
}

void GfxBackendDeviceMemoryManager::ResetTransientAllocators(uint32 frameIndex)
{
    // We also reset the staging memory allocators 
    // NOTE that this is on the assumption that we only have 1 transfer queue
    // Otherwise we should go with another approach entirely or have a pair of staging allocators per transfer queue
    mTransientCPU[frameIndex].Reset();
    mStagingIndex = frameIndex;
}

bool GfxBackendDeviceMemoryManager::Initialize()
{
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProps {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT
    };

    VkPhysicalDeviceMemoryProperties2 memProps {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
        .pNext = gBackendVk.extApi.hasMemoryBudget ? &budgetProps : nullptr
    };

    vkGetPhysicalDeviceMemoryProperties2(gBackendVk.gpu.handle, &memProps);

    mProps = memProps.memoryProperties;
    mBudget = budgetProps;

    auto GetTypeStr = [](VkMemoryPropertyFlags flags, uint32 index)->const char* 
    {
        static String<128> str;
        str.FormatSelf("%u (", index);
        if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
            str.Append("DeviceLocal-");
        if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            str.Append("HostVisible-");
        if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
            str.Append("HostCoherent-");
        if (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
            str.Append("HostCached-");
        if (flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)
            str.Append("LazilyAllocated-");
        if (flags & VK_MEMORY_PROPERTY_PROTECTED_BIT)
            str.Append("Protected-");
        if (flags & VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD)
            str.Append("DeviceCoherent-");
        if (flags & VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD)
            str.Append("DeviceCached-");
        if (flags & VK_MEMORY_PROPERTY_RDMA_CAPABLE_BIT_NV)
            str.Append("RDMA-");
        str.Append(")");

        return str.CStr();
    };

    LOG_VERBOSE("GPU memory overview:");
    for (uint32 i = 0; i < mProps.memoryHeapCount; i++) {
        String<256> str;
        bool first = true;
        String<32> freeStr;

        if (gBackendVk.extApi.hasMemoryBudget)
            freeStr.FormatSelf("%_$$$llu/", mBudget.heapBudget[i]);

        str.FormatSelf("\tHeap #%u (%s%_$$$llu): ", i+1, freeStr.CStr(), mProps.memoryHeaps[i].size);
        for (uint32 k = 0; k < mProps.memoryTypeCount; k++) {
            if (mProps.memoryTypes[k].heapIndex == i) {
                if (!first)
                    str.Append(", ");
                str.Append(GetTypeStr(mProps.memoryTypes[k].propertyFlags, k));
                first = false;
            }   
        }

        LOG_VERBOSE(str.CStr());
    }

    if (!mPersistentGPU.Initialize(128*SIZE_MB, FindDeviceMemoryType(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true)))
        return false;

    {
        VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        VkMemoryPropertyFlags fallbackFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        if (!mPersistentCPU.Initialize(128*SIZE_MB, FindDeviceMemoryType(flags, false, fallbackFlags)))
            return false;
    }

    for (uint32 i = 0; i < GFXBACKEND_FRAMES_IN_FLIGHT; i++) {
        VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        VkMemoryPropertyFlags fallbackFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        if (!mTransientCPU[i].Initialize(128*SIZE_MB, FindDeviceMemoryType(flags, false, fallbackFlags)))
            return false;
    }

    return true;
}

void GfxBackendDeviceMemoryManager::Release()
{
    mPersistentGPU.Release();
    mPersistentCPU.Release();
    for (uint32 i = 0; i < GFXBACKEND_FRAMES_IN_FLIGHT; i++)
        mTransientCPU[i].Release();
}

inline VkDeviceSize& GfxBackendDeviceMemoryManager::GetDeviceMemoryBudget(uint32 typeIndex)
{
    ASSERT(gBackendVk.extApi.hasMemoryBudget);
    GfxBackendDeviceMemoryManager& ctx = gBackendVk.memMan;
    ASSERT(typeIndex != -1 && typeIndex < ctx.mProps.memoryTypeCount);
    ASSERT(ctx.mProps.memoryTypes[typeIndex].heapIndex < ctx.mProps.memoryHeapCount);
    return ctx.mBudget.heapBudget[ctx.mProps.memoryTypes[typeIndex].heapIndex];
}

uint32 GfxBackendDeviceMemoryManager::FindDeviceMemoryType(VkMemoryPropertyFlags flags, bool localdeviceHeap, VkMemoryPropertyFlags fallbackFlags)
{
    GfxBackendDeviceMemoryManager& ctx = gBackendVk.memMan;

    // First look for the exact flag
    for (uint32 i = 0; i < ctx.mProps.memoryTypeCount; i++) {
        const VkMemoryType& type = ctx.mProps.memoryTypes[i];
        if (localdeviceHeap && !(ctx.mProps.memoryHeaps[type.heapIndex].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT))
            continue;

        if (type.propertyFlags == flags)
            return i;
    }

    // As the first fallback, choose the type that matches the flags
    for (uint32 i = 0; i < ctx.mProps.memoryTypeCount; i++) {
        const VkMemoryType& type = ctx.mProps.memoryTypes[i];
        if (localdeviceHeap && !(ctx.mProps.memoryHeaps[type.heapIndex].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT))
            continue;

        if (type.propertyFlags & flags)
            return i;
    }

    // As the second fallback, do this all over again with the fallbackFlag
    if (fallbackFlags)
        return FindDeviceMemoryType(fallbackFlags, localdeviceHeap);

    return uint32(-1);
}

bool GfxBackendQueueManager::Initialize()
{
    mRequestsSemaphore.Initialize();

    // Enumerate queue families
    GfxBackendGpu& gpu = gBackendVk.gpu;
    ASSERT(gpu.handle);

    MemAllocator* alloc = &gBackendVk.parentAlloc;
    MemTempAllocator tempAlloc;

    vkGetPhysicalDeviceQueueFamilyProperties(gpu.handle, &mNumQueueFamilies, nullptr);
    ASSERT_ALWAYS(mNumQueueFamilies, "There should be at least 1 queue family on this hardware");
    mQueueFamilies = Mem::AllocTyped<GfxBackendQueueFamily>(mNumQueueFamilies, alloc);
        
    VkQueueFamilyProperties* families = tempAlloc.MallocTyped<VkQueueFamilyProperties>(mNumQueueFamilies);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu.handle, &mNumQueueFamilies, families);
        
    for (uint32 i = 0; i < mNumQueueFamilies; i++) {
        GfxBackendQueueFamily& fam = mQueueFamilies[i];
        const VkQueueFamilyProperties& props = families[i];

        if (props.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            fam.type |= GfxBackendQueueType::Graphics;
        if (props.queueFlags & VK_QUEUE_COMPUTE_BIT)
            fam.type |= GfxBackendQueueType::Compute;
        if (props.queueFlags & VK_QUEUE_TRANSFER_BIT)
            fam.type |= GfxBackendQueueType::Transfer;

        fam.count = props.queueCount;

        if (gBackendVk.surface) {
            VkBool32 supportsPresentation = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(gpu.handle, i, gBackendVk.surface, &supportsPresentation);
            if (supportsPresentation)
                fam.type |= GfxBackendQueueType::Present;
        }
    }

    LOG_VERBOSE("(init) Found total %u queue families", mNumQueueFamilies);

    if (gpu.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        SetupQueuesForDiscreteDevice();
    else {
        ASSERT(0);  // TODO: not implemented
    }
    MergeQueues();

    ThreadDesc thrdDesc {
        .entryFn = GfxBackendQueueManager::SubmitThread,
        .userData = this,
        .name = "GfxSubmitQueue"
    };

    mThread.Start(thrdDesc);
    return true;
}

void GfxBackendQueueManager::PostInitialize()
{
    ASSERT(gBackendVk.device);

    // Fetch queues from the device
    for (uint32 i = 0; i < mNumQueues; i++) {
        GfxBackendQueue& queue = mQueues[i];
        ASSERT(queue.handle == nullptr);

        vkGetDeviceQueue(gBackendVk.device, queue.familyIdx, 0, &queue.handle);
        ASSERT_ALWAYS(queue.handle, "Something went wrong! Cannot fetch device queue. Invalid queue family");

        queue.waitSemaphores.SetAllocator(&gBackendVk.runtimeAlloc);
        queue.signalSemaphores.SetAllocator(&gBackendVk.runtimeAlloc);
        queue.pendingBarriers.SetAllocator(&gBackendVk.runtimeAlloc);

        VkSemaphoreCreateInfo semCreateInfo { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        vkCreateSemaphore(gBackendVk.device, &semCreateInfo, gBackendVk.vkAlloc, &queue.mySemaphore);
    }

    // Queues are setup and ready
    // CommandBuffer managers for each queue
    for (uint32 i = 0; i < mNumQueues; i++) {
        GfxBackendQueue& queue = mQueues[i];
        for (uint32 k = 0; k < GFXBACKEND_FRAMES_IN_FLIGHT; k++) {
            if (!InitializeCommandBufferContext(queue.cmdBufferContexts[k], queue.familyIdx)) {
                LOG_WARNING("Gfx: CommandBuffer manager init failed for queue %u", i);
                ASSERT(0);
            }
        }
    }

}

void GfxBackendQueueManager::Release()
{
    // Quit submission thread and evict all queues
    mQuit = true;
    mRequestsSemaphore.Post();
    mThread.Stop();
    mRequestsSemaphore.Release();
    mSubmitRequests.Free();

    for (uint32 i = 0; i < mNumQueues; i++) {
        GfxBackendQueue& queue = mQueues[i];

        for (uint32 k = 0; k < GFXBACKEND_FRAMES_IN_FLIGHT; k++) 
            ReleaseCommandBufferContext(queue.cmdBufferContexts[k]);

        vkDestroySemaphore(gBackendVk.device, queue.mySemaphore, gBackendVk.vkAlloc);
        queue.waitSemaphores.Free();
        queue.signalSemaphores.Free();
        queue.pendingBarriers.Free();
    }

    Mem::Free(mQueueFamilies, &gBackendVk.parentAlloc);
    Mem::Free(mQueues, &gBackendVk.parentAlloc);
}

void GfxBackendQueueManager::SetupQueuesForDiscreteDevice()
{
    MemAllocator* alloc = &gBackendVk.parentAlloc;

    // Discrete GPUs:
    //  (1) Graphics + Present 
    //  (1) Transfer: Preferebly exclusive 
    //  (1) Compute: Preferebly exclusive
    mNumQueues = 3;
    mQueues = Mem::AllocZeroTyped<GfxBackendQueue>(mNumQueues, alloc);

    {
        // Note that we also require Transfer for the first Graphics queue in order to do frequent buffer updates
        mQueues[0] = {
            .type = GfxBackendQueueType::Graphics|GfxBackendQueueType::Present,
            .familyIdx = AssignQueueFamily(GfxBackendQueueType::Graphics|GfxBackendQueueType::Present|GfxBackendQueueType::Transfer),
            .priority = 1.0f,
            .supportsTransfer = true
        }; 

        if (mQueues[0].familyIdx != -1) {
            LOG_VERBOSE("\tGraphics queue from index: %u", mQueues[0].familyIdx);
        }
        else {
            LOG_ERROR("Gfx: Graphics queue not found");
            ASSERT(0);
        }
    }

    {
        // TODO: Make this optional since graphics queue should also have Transfer capability
        mQueues[1] = {
            .type = GfxBackendQueueType::Transfer,
            .familyIdx = AssignQueueFamily(GfxBackendQueueType::Transfer, GfxBackendQueueType::Graphics|GfxBackendQueueType::Compute),
            .priority = 1.0f,
            .supportsTransfer = true
        };

        if (mQueues[1].familyIdx != -1) {
            LOG_VERBOSE("\tTransfer queue from index: %u", mQueues[1].familyIdx);
        }
        else {
            LOG_ERROR("Gfx: Transfer queue not found");
            ASSERT(0);
        }
    }

    {
        mQueues[2] = {
            .type = GfxBackendQueueType::Compute,
            .familyIdx = AssignQueueFamily(GfxBackendQueueType::Compute, GfxBackendQueueType::Graphics|GfxBackendQueueType::Transfer),
            .priority = 1.0f
        };

        if (mQueues[2].familyIdx != -1) {
            LOG_VERBOSE("\tCompute queue from index: %u", mQueues[2].familyIdx);
        }
        else {
            LOG_ERROR("Gfx: Compute queue not found");
            ASSERT(0);
        }
    }
}

void GfxBackendQueueManager::SetupQueuesForItegratedDevice()
{
    ASSERT(0);
    // TODO
}

void GfxBackendQueueManager::MergeQueues()
{
    // Merge all the queues that has the same family index
    for (uint32 i = 1; i < mNumQueues; i++) {
        GfxBackendQueue& queue = mQueues[i];
        for (uint32 k = 0; k < i; k++) {
            if (mQueues[k].familyIdx == queue.familyIdx) {
                queue.type |= mQueues[k].type;
                queue.supportsTransfer |= mQueues[k].supportsTransfer;

                if (k != mNumQueues-1)
                    Swap<GfxBackendQueue>(mQueues[k], mQueues[mNumQueues-1]);

                --mNumQueues;
                --i;
                break;
            }
        }
    }
}

uint32 GfxBackendQueueManager::AssignQueueFamily(GfxBackendQueueType type, GfxBackendQueueType preferNotHave)
{
    ASSERT(mNumQueueFamilies);

    uint32 familyIndex = uint32(-1);
    for (uint32 i = 0; i < mNumQueueFamilies; i++) {
        if (IsBitsSet<GfxBackendQueueType>(mQueueFamilies[i].type, type) && mQueueFamilies[i].count) {
            if (preferNotHave != GfxBackendQueueType::None) {
                if (!IsBitsSet(mQueueFamilies[i].type, preferNotHave)) {
                    familyIndex = i;
                    break;
                }
            }
            else {
                familyIndex = i;
                break;
            }
        }
    }

    // If not found, try finding a queue without any exclusions
    if (familyIndex == -1 && preferNotHave != GfxBackendQueueType::None) 
        return AssignQueueFamily(type);
    else
        return familyIndex;
}

inline uint32 GfxBackendQueueManager::FindQueue(GfxBackendQueueType type) const
{
    for (uint32 i = 0; i < mNumQueues; i++) {
        if (IsBitsSet(mQueues[i].type, type))
            return i;
    }
        
    return uint32(-1);
}

void GfxBackend::SubmitQueue(GfxBackendQueueType queueType, GfxBackendQueueType dependentQueues)
{
    ASSERT_MSG(!Jobs::IsRunningOnCurrentThread(), "Submit cannot be called on Task threads");

    gBackendVk.queueMan.SubmitQueue(queueType, dependentQueues);
}

int GfxBackendQueueManager::SubmitThread(void* userData)
{
    GfxBackendQueueManager* self = (GfxBackendQueueManager*)userData;
    
    while (!self->mQuit) {
        self->mRequestsSemaphore.Wait();

        GfxBackendQueueSubmitRequest* req = nullptr;
        {
            SpinLockMutexScope lock(self->mRequestMutex);
            if (!self->mSubmitRequests.IsEmpty()) 
                req = self->mSubmitRequests.PopFirst();
        }

        if (req) {
            if (req->type != GfxBackendQueueType::None)
                self->SubmitQueueInternal(*req);

            MemSingleShotMalloc<GfxBackendQueueSubmitRequest>::Free(req, &gBackendVk.runtimeAlloc);
        }
    }

    return 0;
}

void GfxBackendQueueManager::SubmitQueue(GfxBackendQueueType queueType, GfxBackendQueueType dependentQueues)
{
    uint32 queueIndex = FindQueue(queueType);
    ASSERT(queueIndex != -1);
    GfxBackendQueue& queue = GetQueue(queueIndex);

    // Take all the command-buffers since last Submit call and pass it to the submission thread
    GfxBackendCommandBufferContext& cmdBufferCtx = queue.cmdBufferContexts[mFrameIndex];
    uint32 numCmdBuffers = cmdBufferCtx.cmdBuffers.Count() - cmdBufferCtx.cmdBufferIndex;
    if (!numCmdBuffers)
        return;

    MemSingleShotMalloc<GfxBackendQueueSubmitRequest> mallocator;
    mallocator.AddMemberArray<VkCommandBuffer>(offsetof(GfxBackendQueueSubmitRequest, cmdBuffers), numCmdBuffers);
    GfxBackendQueueSubmitRequest* req = mallocator.Calloc(&gBackendVk.runtimeAlloc);
    req->type = queueType;
    req->numCmdBuffers = numCmdBuffers;
    for (uint32 i = cmdBufferCtx.cmdBufferIndex; i < cmdBufferCtx.cmdBuffers.Count(); i++)
        req->cmdBuffers[i - cmdBufferCtx.cmdBufferIndex] = cmdBufferCtx.cmdBuffers[i];

    cmdBufferCtx.cmdBufferIndex = cmdBufferCtx.cmdBuffers.Count();

    // Also add injected dependent queues
    req->dependents = dependentQueues | queue.internalDependents;
    queue.internalDependents = GfxBackendQueueType::None;

    // Create a fence for each submission
    if (!cmdBufferCtx.fenceFreeList.IsEmpty()) {
        req->fence = cmdBufferCtx.fenceFreeList.PopLast();
    }
    else {
        VkFenceCreateInfo fenceCreateInfo { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        [[maybe_unused]] VkResult r = vkCreateFence(gBackendVk.device, &fenceCreateInfo, gBackendVk.vkAlloc, &req->fence);
        ASSERT_ALWAYS(r == VK_SUCCESS, "vkCreateFence failed");
    }
    cmdBufferCtx.fences.Push(req->fence);        

    {
        SpinLockMutexScope lock(mRequestMutex);
        mSubmitRequests.Push(req);
    }

    mRequestsSemaphore.Post();
}

bool GfxBackendQueueManager::SubmitQueueInternal(GfxBackendQueueSubmitRequest& req)
{
    uint32 queueIndex = FindQueue(req.type);
    ASSERT(queueIndex != -1);
    GfxBackendQueue& queue = mQueues[queueIndex];

    // Connecte dependencies
    // Each queue has it's own signal semaphore
    // When we have dependents, then add the current queue's signal semaphore to the dependent's wait semaphore
    // This forms a dependency chain

    // TODO: We can have tune this to be more specific
    auto GetStageFlag = [](GfxBackendQueueType type)->VkPipelineStageFlags
    {
        switch (type) {
        case GfxBackendQueueType::Graphics: return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case GfxBackendQueueType::Compute:  return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        case GfxBackendQueueType::Transfer: return VK_PIPELINE_STAGE_TRANSFER_BIT;
        default: return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        }
    };

    // Check for swapchain draw within command-buffers
    if (IsBitsSet<GfxBackendQueueType>(req.dependents, GfxBackendQueueType::Present)) 
    {
        ASSERT(req.type == GfxBackendQueueType::Graphics);
        // Notify the queue that the next Submit is gonna depend on swapchain
        queue.waitSemaphores.Push({gBackendVk.swapchain.GetSwapchainSemaphore(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT});
        queue.signalSemaphores.Push(gBackendVk.swapchain.GetPresentSemaphore());
    }

    if (IsBitsSet<GfxBackendQueueType>(req.dependents, GfxBackendQueueType::Graphics)) {
        ASSERT(req.type != GfxBackendQueueType::Graphics);
        GfxBackendQueue& graphicsQueue = mQueues[FindQueue(GfxBackendQueueType::Graphics)];
        graphicsQueue.waitSemaphores.Push({queue.mySemaphore, GetStageFlag(req.type)});
        queue.signalSemaphores.Push(queue.mySemaphore);
    }

    if (IsBitsSet<GfxBackendQueueType>(req.dependents, GfxBackendQueueType::Compute)) {
        ASSERT(req.type != GfxBackendQueueType::Compute);
        GfxBackendQueue& computeQueue = mQueues[FindQueue(GfxBackendQueueType::Compute)];
        computeQueue.waitSemaphores.Push({queue.mySemaphore, GetStageFlag(req.type)});
        queue.signalSemaphores.Push(queue.mySemaphore);
    }

    if (IsBitsSet<GfxBackendQueueType>(req.dependents, GfxBackendQueueType::Transfer)) {
        ASSERT(req.type != GfxBackendQueueType::Transfer);
        GfxBackendQueue& transferQueue = mQueues[FindQueue(GfxBackendQueueType::Transfer)];
        transferQueue.waitSemaphores.Push({queue.mySemaphore, GetStageFlag(req.type)});
        queue.signalSemaphores.Push(queue.mySemaphore);
    }

    // Submit
    MemTempAllocator tempAlloc;
    VkSemaphore* waitSemaphores = nullptr;
    VkPipelineStageFlags* waitStageFlags = nullptr;
    uint32 numWaitSemaphores = queue.waitSemaphores.Count();

    if (numWaitSemaphores) {
        waitSemaphores = tempAlloc.MallocTyped<VkSemaphore>(numWaitSemaphores);
        waitStageFlags = tempAlloc.MallocTyped<VkPipelineStageFlags>(numWaitSemaphores);
        for (uint32 i = 0; i < numWaitSemaphores; i++) {
            waitSemaphores[i] = queue.waitSemaphores[i].semaphore;
            waitStageFlags[i] = queue.waitSemaphores[i].stageFlags;
        }
    }
    VkSubmitInfo submitInfo {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = numWaitSemaphores,
        .pWaitSemaphores = waitSemaphores,
        .pWaitDstStageMask = waitStageFlags,    
        .commandBufferCount = req.numCmdBuffers,
        .pCommandBuffers = req.cmdBuffers,
        .signalSemaphoreCount = queue.signalSemaphores.Count(),
        .pSignalSemaphores = queue.signalSemaphores.Ptr(),
    };


    // TODO: maybe implement synchronization2 (more granual control?) with vkQueueSubmit
    if (vkQueueSubmit(queue.handle, 1, &submitInfo, req.fence) != VK_SUCCESS) {
        ASSERT_MSG(0, "Gfx: Submitting queue failed");
        return false;
    }

    queue.waitSemaphores.Clear();
    queue.signalSemaphores.Clear();

    gBackendVk.frameSyncSignal.Decrement(req.numCmdBuffers);
    gBackendVk.frameSyncSignal.Raise();

    return true;
}

void GfxBackendQueueManager::BeginFrame()
{
    ++mGeneration;
    mFrameIndex = mGeneration % GFXBACKEND_FRAMES_IN_FLIGHT;

    for (uint32 i = 0; i < mNumQueues; i++) {
        GfxBackendQueue& queue = mQueues[i];
        GfxBackendCommandBufferContext& cmdBufferCtx = queue.cmdBufferContexts[mFrameIndex];
    
        // Wait for all submitted command-buffers to finish in the queue
        if (!cmdBufferCtx.fences.IsEmpty()) {
            [[maybe_unused]] VkResult r = vkWaitForFences(gBackendVk.device, cmdBufferCtx.fences.Count(), cmdBufferCtx.fences.Ptr(), true, UINT64_MAX);
            ASSERT(r == VK_SUCCESS);
            vkResetFences(gBackendVk.device, cmdBufferCtx.fences.Count(), cmdBufferCtx.fences.Ptr());

            cmdBufferCtx.fenceFreeList.Extend(cmdBufferCtx.fences);
            cmdBufferCtx.fences.Clear();
        }

        // Now we can safely reset the command-pool and free the CommandBuffers
        vkResetCommandPool(gBackendVk.device, cmdBufferCtx.pool, 0);
        cmdBufferCtx.cmdBufferFreeList.Extend(cmdBufferCtx.cmdBuffers);
        cmdBufferCtx.cmdBuffers.Clear();
        cmdBufferCtx.cmdBufferIndex = 0;
    }

    gBackendVk.memMan.ResetTransientAllocators(mFrameIndex);
}

bool GfxBackendQueueManager::InitializeCommandBufferContext(GfxBackendCommandBufferContext& ctx, uint32 queueFamilyIndex)
{
    ASSERT(gBackendVk.device);

    VkCommandPoolCreateInfo poolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = queueFamilyIndex
    };

    if (vkCreateCommandPool(gBackendVk.device, &poolCreateInfo, gBackendVk.vkAlloc, &ctx.pool) != VK_SUCCESS) {
        LOG_ERROR("Gfx: Create command pool failed");
        return false;
    }

    ctx.cmdBuffers.SetAllocator(&gBackendVk.runtimeAlloc);
    ctx.cmdBufferFreeList.SetAllocator(&gBackendVk.runtimeAlloc);
    ctx.fences.SetAllocator(&gBackendVk.runtimeAlloc);
    ctx.fenceFreeList.SetAllocator(&gBackendVk.runtimeAlloc);

    return true;
}

void GfxBackendQueueManager::ReleaseCommandBufferContext(GfxBackendCommandBufferContext& ctx)
{
    if (ctx.pool)
        vkDestroyCommandPool(gBackendVk.device, ctx.pool, gBackendVk.vkAlloc);

    for (VkFence fence : ctx.fenceFreeList)
        vkDestroyFence(gBackendVk.device, fence, gBackendVk.vkAlloc);
    for (VkFence fence : ctx.fences)
        vkDestroyFence(gBackendVk.device, fence, gBackendVk.vkAlloc);

    ctx.cmdBuffers.Free();
    ctx.cmdBufferFreeList.Free();
    ctx.fences.Free();
    ctx.fenceFreeList.Free();
}

void GfxBackendCommandBuffer::MapBuffer(GfxBufferHandle buffHandle, void** outPtr, size_t* outSizeBytes)
{
    ASSERT(outPtr);
    ASSERT(outSizeBytes);

    GfxBackendBuffer& buffer = gBackendVk.buffers.Data(buffHandle);
    ASSERT_MSG(buffer.mem.mappedData, "Buffer is not mappable");

    *outPtr = buffer.mem.mappedData;
    *outSizeBytes = buffer.desc.sizeBytes;
}

void GfxBackendCommandBuffer::FlushBuffer(GfxBufferHandle buffHandle)
{
    GfxBackendBuffer& buffer = gBackendVk.buffers.Data(buffHandle);
    if (!buffer.mem.isCoherent) {
        size_t alignedSize = AlignValue(buffer.desc.sizeBytes, gBackendVk.gpu.props.limits.nonCoherentAtomSize);
        VkMappedMemoryRange memRange {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = buffer.mem.handle,
            .offset = buffer.mem.offset,
            .size = alignedSize
        };
        vkFlushMappedMemoryRanges(gBackendVk.device, 1, &memRange);
    }
}

void GfxBackendCommandBuffer::CopyBufferToBuffer(GfxBufferHandle srcHandle, GfxBufferHandle dstHandle, GfxShaderStage stagesUsed, 
                                                 size_t srcOffset, size_t dstOffset, size_t sizeBytes)
{
    GfxBackendQueue& queue = gBackendVk.queueMan.GetQueue(mQueueIndex);
    ASSERT_MSG(IsBitsSet<GfxBackendQueueType>(queue.type, GfxBackendQueueType::Transfer) || queue.supportsTransfer,
               "Cannot do buffer copies on non-Transfer queues");

    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    GfxBackendBuffer& srcBuffer = gBackendVk.buffers.Data(srcHandle);
    GfxBackendBuffer& dstBuffer = gBackendVk.buffers.Data(dstHandle);

    if (sizeBytes == 0)
        sizeBytes = Min(srcBuffer.desc.sizeBytes, dstBuffer.desc.sizeBytes);
    ASSERT(sizeBytes <= srcBuffer.desc.sizeBytes);
    ASSERT(sizeBytes <= dstBuffer.desc.sizeBytes);

    VkBufferCopy copyRegion {
        .srcOffset = srcOffset,
        .dstOffset = dstOffset,
        .size = sizeBytes
    };

    vkCmdCopyBuffer(cmdVk, srcBuffer.handle, dstBuffer.handle, 1, &copyRegion);

    StaticArray<GfxBackendQueueType, 4> dstQueues;
    if (IsBitsSet<GfxShaderStage>(stagesUsed, GfxShaderStage::Vertex) ||
        IsBitsSet<GfxShaderStage>(stagesUsed, GfxShaderStage::Fragment))
    {
        dstQueues.Push(GfxBackendQueueType::Graphics);
    }

    if (IsBitsSet<GfxShaderStage>(stagesUsed, GfxShaderStage::Compute))
        dstQueues.Push(GfxBackendQueueType::Compute);

    for (GfxBackendQueueType dstQueueType : dstQueues) {
        uint32 dstQueueIndex = gBackendVk.queueMan.FindQueue(dstQueueType);
        ASSERT(dstQueueIndex != -1);

        if (mQueueIndex == dstQueueIndex) {
            // Unified queue
            VkBufferMemoryBarrier2 barrier {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                .dstStageMask = GfxBackend::_GetBufferDestStageFlags(dstQueueType, stagesUsed),
                .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = dstBuffer.handle,
                .offset = dstOffset,
                .size = sizeBytes
            };

            VkDependencyInfo depInfo {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .bufferMemoryBarrierCount = 1,
                .pBufferMemoryBarriers = &barrier
            };

            dstBuffer.transitionedStage = barrier.dstStageMask;
            dstBuffer.transitionedAccess = barrier.dstAccessMask;
            vkCmdPipelineBarrier2(cmdVk, &depInfo);
        }
        else {
            // Separate queue
            // We have to do queue ownership transfer first
            GfxBackendQueue& dstQueue = gBackendVk.queueMan.GetQueue(dstQueueIndex);

            VkBufferMemoryBarrier2 barrier {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                .srcQueueFamilyIndex = mQueueIndex,
                .dstQueueFamilyIndex = dstQueueIndex,
                .buffer = dstBuffer.handle,
                .offset = dstOffset,
                .size = sizeBytes
            };

            VkDependencyInfo depInfo {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .bufferMemoryBarrierCount = 1,
                .pBufferMemoryBarriers = &barrier
            };

            vkCmdPipelineBarrier2(cmdVk, &depInfo);

            // TODO: Assert that dstQueue is not being recorded
            GfxBackendQueue::PendingBarrier dstBarrier {
                .type = GfxBackendQueue::PendingBarrier::BUFFER,
                .bufferHandle = dstHandle,
                .bufferBarrier = {
                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                    .dstStageMask = GfxBackend::_GetBufferDestStageFlags(dstQueueType, stagesUsed),
                    .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
                    .srcQueueFamilyIndex = mQueueIndex,
                    .dstQueueFamilyIndex = dstQueueIndex,
                    .offset = dstOffset,
                    .size = sizeBytes
                }
            };

            queue.internalDependents |= dstQueueType;
            dstQueue.pendingBarriers.Push(dstBarrier);
        }
    }
}

void GfxBackendCommandBuffer::CopyBufferToImage(GfxBufferHandle srcHandle, GfxImageHandle dstHandle, GfxShaderStage stagesUsed, 
                                                uint16 startMipIndex, uint16 mipCount)
{
    ASSERT(mipCount);

    GfxBackendQueue& queue = gBackendVk.queueMan.GetQueue(mQueueIndex);
    ASSERT_MSG(IsBitsSet<GfxBackendQueueType>(queue.type, GfxBackendQueueType::Transfer) || queue.supportsTransfer,
               "Cannot do buffer copies on non-Transfer queues");

    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    GfxBackendBuffer& srcBuffer = gBackendVk.buffers.Data(srcHandle);
    GfxBackendImage& dstImage = gBackendVk.images.Data(dstHandle);

    ASSERT(startMipIndex < dstImage.desc.numMips);
    mipCount = Min<uint16>(mipCount, dstImage.desc.numMips - startMipIndex);

    VkImageAspectFlags aspect = 0;
    VkImageLayout dstLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (GfxBackend::_FormatHasDepth(dstImage.desc.format)) {
        aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
        dstLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    }
    if (GfxBackend::_FormatHasStencil(dstImage.desc.format)) {
        aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        dstLayout = VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL;
    }
    if (aspect == 0) {
        aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        dstLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
    }

    VkImageSubresourceRange subresourceRange {
        .aspectMask = 0,
        .baseMipLevel = startMipIndex,
        .levelCount = mipCount,
        .baseArrayLayer = 0,
        .layerCount = 1
    };

    VkImageMemoryBarrier2 preCopyBarrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = dstImage.layout,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dstImage.handle,
        .subresourceRange = subresourceRange
    };
    VkDependencyInfo preCopyDepInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &preCopyBarrier
    };
    vkCmdPipelineBarrier2(cmdVk, &preCopyDepInfo);

    // Perform copy
    VkBufferImageCopy imageCopies[GFXBACKEND_MAX_MIPS_PER_IMAGE];
    uint16 endMipIndex = startMipIndex + mipCount;
    for (uint16 i = startMipIndex; i < endMipIndex; i++) {
        uint16 mipWidth = Min<uint16>(1, dstImage.desc.width >> i);
        uint16 mipHeight = Min<uint16>(1, dstImage.desc.height >> i);

        imageCopies[i] = {
            .bufferOffset = dstImage.desc.mipOffsets[i],
            .bufferRowLength = 0,   
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = aspect,
                .mipLevel = i, 
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {mipWidth, mipHeight, 0}
        };
    }

    vkCmdCopyBufferToImage(cmdVk, srcBuffer.handle, dstImage.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipCount, imageCopies);

    StaticArray<GfxBackendQueueType, 4> dstQueues;
    if (IsBitsSet<GfxShaderStage>(stagesUsed, GfxShaderStage::Vertex) ||
        IsBitsSet<GfxShaderStage>(stagesUsed, GfxShaderStage::Fragment))
    {
        dstQueues.Push(GfxBackendQueueType::Graphics);
    }

    if (IsBitsSet<GfxShaderStage>(stagesUsed, GfxShaderStage::Compute))
        dstQueues.Push(GfxBackendQueueType::Compute);

    for (GfxBackendQueueType dstQueueType : dstQueues) {
        uint32 dstQueueIndex = gBackendVk.queueMan.FindQueue(dstQueueType);
        ASSERT(dstQueueIndex != -1);

        if (mQueueIndex == dstQueueIndex) {
            // Unified Queue
            VkImageMemoryBarrier2 barrier {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = GfxBackend::_GetImageDestStageFlags(dstQueueType, stagesUsed),
                .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = dstLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = dstImage.handle,
                .subresourceRange = subresourceRange
            };
            VkDependencyInfo depInfo {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &barrier
            };

            vkCmdPipelineBarrier2(cmdVk, &depInfo);

            dstImage.transitionedStage = barrier.dstStageMask;
            dstImage.transitionedAccess = barrier.dstAccessMask;
        }
        else {
            // Separate queue
            GfxBackendQueue& dstQueue = gBackendVk.queueMan.GetQueue(dstQueueIndex);

            VkImageMemoryBarrier2 barrier {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = dstLayout,
                .srcQueueFamilyIndex = mQueueIndex,
                .dstQueueFamilyIndex = dstQueueIndex,
                .image = dstImage.handle,
                .subresourceRange = subresourceRange
            };
            VkDependencyInfo depInfo {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &barrier
            };

            vkCmdPipelineBarrier2(cmdVk, &depInfo);

            // TODO: Assert that dstQueue is not being recorded
            GfxBackendQueue::PendingBarrier dstBarrier {
                .type = GfxBackendQueue::PendingBarrier::IMAGE,
                .imageHandle = dstHandle,
                .imageBarrier = {
                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                    .dstStageMask = GfxBackend::_GetBufferDestStageFlags(dstQueueType, stagesUsed),
                    .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT,
                    .srcQueueFamilyIndex = mQueueIndex,
                    .dstQueueFamilyIndex = dstQueueIndex,
                    .subresourceRange = subresourceRange
                }
            };

            queue.internalDependents |= dstQueueType;
            dstQueue.pendingBarriers.Push(dstBarrier);
        }
    }
}


void GfxBackendCommandBuffer::TransitionBuffer(GfxBufferHandle buffHandle, GfxBackendBufferTransition transition)
{
    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    GfxBackendBuffer& buffer = gBackendVk.buffers.Data(buffHandle);
    GfxBackendQueue& queue = gBackendVk.queueMan.GetQueue(mQueueIndex);

    VkBufferMemoryBarrier2 barrier {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .buffer = buffer.handle,
        .size = VK_WHOLE_SIZE
    };
    
    switch (transition) {
        case GfxBackendBufferTransition::TransferWrite:
            ASSERT_MSG(IsBitsSet<GfxBackendQueueType>(queue.type, GfxBackendQueueType::Transfer) || queue.supportsTransfer,
                       "Cannot do transfer transitions on non-Transfer queues");

            barrier.srcStageMask = GfxBackend::_GetBufferSourceStageFlags(buffer.transitionedStage);
            barrier.srcAccessMask = buffer.transitionedAccess;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
        default:
            break;
    }

    VkDependencyInfo depInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &barrier
    };

    buffer.transitionedStage = barrier.dstStageMask;
    buffer.transitionedAccess = barrier.dstAccessMask;
    vkCmdPipelineBarrier2(cmdVk, &depInfo);
}

void GfxBackendCommandBuffer::TransitionImage(GfxImageHandle imgHandle, GfxBackendImageTransition transition)
{
    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    GfxBackendImage& image = gBackendVk.images.Data(imgHandle);

    VkImageAspectFlags aspect = 0;
    if (GfxBackend::_FormatHasDepth(image.desc.format))
        aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if (GfxBackend::_FormatHasStencil(image.desc.format)) 
        aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    if (aspect == 0)
        aspect = VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageMemoryBarrier2 barrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .oldLayout = image.layout,
        .image = image.handle,
        .subresourceRange = {
            .aspectMask = aspect,
            .levelCount = VK_REMAINING_MIP_LEVELS,
            .layerCount = VK_REMAINING_ARRAY_LAYERS
        }
    };

    switch (transition) {
        case GfxBackendImageTransition::ComputeWrite:
            barrier.srcStageMask = GfxBackend::_GetImageSourceStageFlags(image.transitionedStage);
            barrier.srcAccessMask = image.transitionedAccess;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            break;

        case GfxBackendImageTransition::CopySource:
            barrier.srcStageMask = GfxBackend::_GetImageSourceStageFlags(image.transitionedStage);
            barrier.srcAccessMask = image.transitionedAccess;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            break;
    }

    VkDependencyInfo depInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };

    vkCmdPipelineBarrier2(cmdVk, &depInfo);

    image.layout = barrier.newLayout;
    image.transitionedStage = barrier.dstStageMask;
    image.transitionedAccess = barrier.dstAccessMask;
}

void GfxBackendCommandBuffer::BeginRenderPass(const GfxBackendRenderPass& pass)
{
    auto MakeRenderingAttachmentInfo = [](const GfxBackendRenderPassAttachment& srcAttachment, VkImageView view, VkImageLayout layout)
    {
        ASSERT(view);
        ASSERT_MSG(!(srcAttachment.load & srcAttachment.clear), "Cannot have both load/clear ops on color attachment");

        VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        if (srcAttachment.load)
            loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        else if (srcAttachment.clear)
            loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

        VkClearValue clearValue {};
        if (layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            clearValue.color = {srcAttachment.clearColor.x, srcAttachment.clearColor.y, srcAttachment.clearColor.z, srcAttachment.clearColor.w};
        }
        else if (layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL || 
                 layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
                layout == VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL)
        {
            clearValue.depthStencil = {.depth = srcAttachment.clearDepth, .stencil = srcAttachment.clearStencil};
        }
        else {
            ASSERT(0);
        }

        VkRenderingAttachmentInfo info {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = view,
            .imageLayout = layout,
            .loadOp = loadOp,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = clearValue,
        };

        return info;
    };

    ASSERT_MSG(!(pass.swapchain & (pass.hasDepth|pass.hasStencil)), "Swapchain doesn't have depth/stencil attachments");
    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);

    uint32 numColorAttachments = pass.swapchain ? pass.numAttachments : 1;
    ASSERT(numColorAttachments);
    ASSERT(numColorAttachments < GFXBACKEND_MAX_RENDERPASS_COLOR_ATTACHMENTS);
    VkRenderingAttachmentInfo colorAttachments[GFXBACKEND_MAX_RENDERPASS_COLOR_ATTACHMENTS];

    uint16 width = 0;
    uint16 height = 0;
    for (uint32 i = 0; i < numColorAttachments; i++) {
        const GfxBackendRenderPassAttachment& srcAttachment = pass.colorAttachments[i];
        if (width == 0 && height == 0) {
            if (pass.swapchain) {
                width = uint16(gBackendVk.swapchain.extent.width);
                height = uint16(gBackendVk.swapchain.extent.height);
            }
            else {
                GfxBackendImage& image = gBackendVk.images.Data(srcAttachment.image);
                width = image.desc.width;
                height = image.desc.height;
            }

        }
        else {
            GfxBackendImage& image = gBackendVk.images.Data(srcAttachment.image);
            ASSERT_MSG(width == image.desc.width && height == image.desc.height, 
                       "All attachments in the renderpass should have equal dimensions");
        }

        VkImageView view = pass.swapchain ? gBackendVk.swapchain.GetImageView() : gBackendVk.images.Data(srcAttachment.image).viewHandle;
        colorAttachments[i] = MakeRenderingAttachmentInfo(srcAttachment, view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }

    VkRect2D renderArea;
    if (pass.cropRect.IsEmpty()) {
        renderArea = {
            .offset = {0, 0},
            .extent = {width, height}
        };
    }
    else {
        renderArea = {
            .offset = {pass.cropRect.xmin, pass.cropRect.ymin},
            .extent = {uint32(pass.cropRect.Width()), uint32(pass.cropRect.Height())}
        };
    }

    VkRenderingAttachmentInfo* depthAttachment = nullptr;
    VkRenderingAttachmentInfo* stencilAttachment = nullptr;
    // TODO: depth/stencil
    ASSERT(!pass.hasDepth && !pass.hasStencil);

    VkRenderingInfo renderInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = renderArea,
        .layerCount = 1,
        .viewMask = 0,
        .colorAttachmentCount = numColorAttachments,
        .pColorAttachments = colorAttachments,
        .pDepthAttachment = depthAttachment,
        .pStencilAttachment = stencilAttachment
    };
    vkCmdBeginRendering(cmdVk, &renderInfo);

    mDrawsToSwapchain = pass.swapchain;
}

void GfxBackendCommandBuffer::EndRenderPass()
{
    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    vkCmdEndRendering(cmdVk);

    if (mDrawsToSwapchain) 
        gBackendVk.queueMan.GetQueue(mQueueIndex).internalDependents |= GfxBackendQueueType::Present;
}

void GfxBackendCommandBuffer::Draw(uint32 vertexCount, uint32 instanceCount, uint32 firstVertex, uint32 firstInstance)
{
    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    vkCmdDraw(cmdVk, vertexCount, instanceCount, firstVertex, firstInstance);
}

void GfxBackendCommandBuffer::DrawIndexed(uint32 indexCount, uint32 instanceCount, uint32 firstIndex, uint32 vertexOffset, uint32 firstInstance)
{
    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    vkCmdDrawIndexed(cmdVk, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

