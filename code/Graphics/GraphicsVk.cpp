#ifndef __GRAPHICS_VK_CPP__
#define __GRAPHICS_VK_CPP__

#ifndef __GRAPHICS_CPP__
    #error "This file depends on Graphics.cpp for compilation"
#endif

#include "Graphics.h"
#include "Shader.h"

#define VK_NO_PROTOTYPES
#if !PLATFORM_APPLE
    #include "../External/vulkan/include/vulkan.h"
#endif

// Vulkan platform headers
#if PLATFORM_WINDOWS
    #include "../Core/IncludeWin.h"
    #include "../External/vulkan/include/vulkan_win32.h"
#elif PLATFORM_ANDROID
    #include "../External/vulkan/include/vulkan_android.h"
#elif PLATFORM_APPLE
    #include <mvk_vulkan.h>
#else
    #error "Not implemented"
#endif

// Always include volk after vulkan.h
#define VOLK_IMPLEMENTATION
#if PLATFORM_WINDOWS
    #define VK_USE_PLATFORM_WIN32_KHR
//#elif PLATFORM_APPLE
//    #define VK_USE_PLATFORM_MACOS_MVK
#endif
//#define VOLK_VULKAN_H_PATH "../External/vulkan/include/vulkan.h"
#include "../External/volk/volk.h"

#include "../Core/Allocators.h"
#include "../Core/StringUtil.h"
#include "../Core/System.h"
#include "../Core/Hash.h"
#include "../Core/Buffers.h"
#include "../Core/Settings.h"
#include "../Core/Atomic.h"
#include "../Core/Log.h"
#include "../Core/BlitSort.h"
#include "../Core/TracyHelper.h"
#include "../Core/MathAll.h"

#include "../Engine.h"
#include "../VirtualFS.h"
#include "../Application.h"
#include "../JunkyardSettings.h"

#include "ValidateEnumsVk.inl"

//------------------------------------------------------------------------
// VMA
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4100)    // unreferenced formal parameter
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4189)    // local variable is initialized but not referenced
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4127)    // conditional expression is constant
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wnullability-completeness")
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wunused-variable")
#define VMA_ASSERT(expr) ASSERT(expr)
#define VMA_HEAVY_ASSERT(expr) 
#define VMA_CONFIGURATION_USER_INCLUDES_H "../../../Core/Base.h"
class VmaMutex
{
public:
    VmaMutex() { m_Mutex.Initialize(); }
    ~VmaMutex() { m_Mutex.Release(); }
    void Lock() { m_Mutex.Enter(); }
    void Unlock() { m_Mutex.Exit(); }
    bool TryLock() { return m_Mutex.TryEnter(); }
private:
    Mutex m_Mutex;
};

#define VMA_MUTEX VmaMutex
#define VMA_SORT(begin, end, cmp) BlitSort<VmaDeviceMemoryBlock*>(begin, (size_t)(end-begin),  \
    [](const VmaDeviceMemoryBlock* a, const VmaDeviceMemoryBlock* b)->int {  \
        auto _c = cmp;  \
        return _c(const_cast<VmaDeviceMemoryBlock*>(a), const_cast<VmaDeviceMemoryBlock*>(b)) ? -1 : 1; \
    }) 
#define VMA_MAX(v1, v2)    Max(v1, v2)
#define VMA_MIN(v1, v2)    Min(v1, v2)
#define VMA_ASSERT(expr) ASSERT(expr)
#define VMA_STATS_STRING_ENABLED 0
#include "../External/vma/include/vk_mem_alloc.h"
PRAGMA_DIAGNOSTIC_POP()
//------------------------------------------------------------------------

static constexpr uint32 kMaxSwapchainImages = 3;
static constexpr uint32 kMaxFramesInFlight = 2;
static constexpr uint32 kMaxDescriptorSetLayoutPerPipeline = 3;

static constexpr const char* kGfxAllocName = "Graphics";
static constexpr const char* kVulkanAllocName = "Vulkan";

namespace _limits
{
    static constexpr uint32 kGfxMaxBuffers = 1024;
    static constexpr uint32 kGfxMaxImages = 1024;
    static constexpr uint32 kGfxMaxDescriptorSets = 256;
    static constexpr uint32 kGfxMaxDescriptorSetLayouts = 256;
    static constexpr uint32 kGfxMaxPipelines = 256;
    static constexpr uint32 kGfxMaxPipelineLayouts = 256;
    static constexpr uint32 kGfxMaxGarbage = 512;
    static constexpr size_t kGfxRuntimeSize = 32*kMB;
}

// Fwd (ImageVk.cpp)
static bool gfxInitializeImageManager();
static void gfxUpdateImageDescriptorSetCache(GfxDescriptorSet dset, uint32 numBindings, const GfxDescriptorBindingDesc* bindings);

// Fwd (TracyVk.cpp)
#ifdef TRACY_ENABLE
    static bool gfxInitializeProfiler();
    static void gfxReleaseProfiler();
    static void gfxProfileCollectSamples();
    static bool gfxHasProfileSamples();
#endif

// TODO: make a vulkan allocator and fill it out in all API calls

struct GfxSwapchainSupportDetails
{
    VkSurfaceCapabilitiesKHR caps;
    uint32 numFormats;
    uint32 numPresentModes;
    VkSurfaceFormatKHR* formats;
    VkPresentModeKHR* presentModes;
};

struct GfxSwapchain
{
    bool init;
    uint32 imageIdx;    // current image index that we are able to get in the running frame
    uint32 numImages;
    VkSwapchainKHR swapchain;
    VkImage images[kMaxSwapchainImages];               // count: numImages
    VkImageView imageViews[kMaxSwapchainImages];       // count: numImages
    VkFramebuffer framebuffers[kMaxSwapchainImages];   // count: numImages
    VkExtent2D extent;
    VkFormat colorFormat;
    VkRenderPass renderPass;
    GfxImage depthImage;
}; 

struct GfxBufferData
{
    GfxBufferType           type;
    GfxBufferUsage          memUsage;
    uint32                  size;
    VmaAllocation           allocation;
    VkMemoryPropertyFlags   memFlags;
    VkBuffer                buffer;
    VkBuffer                stagingBuffer;
    VmaAllocation           stagingAllocation;
    void*                   mappedBuffer;

#if !CONFIG_FINAL_BUILD
    void*                   stackframes[8];
    uint16                  numStackframes;
#endif
};

struct GfxImageData
{
    uint32         width;
    uint32         height;
    uint32         numMips;
    GfxBufferUsage memUsage;
    size_t         sizeBytes;
    VkImage        image;
    VkImageView    view;
    VkSampler      sampler;
    VmaAllocation  allocation;

#if !CONFIG_FINAL_BUILD
    void*          stackframes[8];
    uint16         numStackframes;
#endif
};

struct GfxDescriptorSetLayoutData
{
    struct Binding 
    {
        const char* name;
        uint32      nameHash;
        uint32      variableDescCount;
        VkDescriptorSetLayoutBinding vkBinding;
    };

    uint32                hash;
    VkDescriptorSetLayout layout;
    uint32                numBindings;
    uint32                refCount;
    Binding*              bindings;

#if !CONFIG_FINAL_BUILD
    void*          stackframes[8];
    uint16         numStackframes;
#endif
};

struct GfxPipelineLayoutData
{
    uint32                     hash;
    uint32                     numDescriptorSetLayouts;
    GfxDescriptorSetLayout     descriptorSetLayouts[kMaxDescriptorSetLayoutPerPipeline];
    VkPipelineLayout           layout;
    uint32                     refCount;        // number of cached items referenced by pipeline objects

#if !CONFIG_FINAL_BUILD
    void*          stackframes[8];
    uint16         numStackframes;
#endif
};

struct GfxPipelineData
{
    VkPipeline pipeline;
    GfxPipelineLayout pipelineLayout;
    VkGraphicsPipelineCreateInfo* gfxCreateInfo;    // Keep this to be able recreate pipelines anytime
    uint32 shaderHash;

#if !CONFIG_FINAL_BUILD
    void*          stackframes[8];
    uint16         numStackframes;
#endif
};

struct GfxDescriptorSetData
{
    GfxDescriptorSetLayout layout;
    VkDescriptorSet descriptorSet;

#if !CONFIG_FINAL_BUILD
    void*          stackframes[8];
    uint16         numStackframes;
#endif
};

struct GfxCommandBufferThreadData
{
    uint64 lastResetFrame;
    VkCommandPool commandPools[kMaxFramesInFlight];
    VkCommandBuffer curCmdBuffer;
    Array<VkCommandBuffer> freeLists[kMaxFramesInFlight];
    Array<VkCommandBuffer> cmdBuffers[kMaxFramesInFlight];
    bool initialized;
    bool deferredCmdBuffer;
    bool renderingToSwapchain;
};

// TODO: see if we can remove the mutex here, it creates way too much contention
struct GfxObjectPools
{
    enum PoolIndex
    {
        BUFFERS = 0,
        IMAGES,
        PIPELINE_LAYOUTS,
        PIPELINES,
        DESCRIPTOR_SETS,
        DESCRIPTOR_SET_LAYOUTS,
        POOL_COUNT
    };

    AtomicLock locks[POOL_COUNT];

    HandlePool<GfxBuffer, GfxBufferData> buffers;
    HandlePool<GfxImage, GfxImageData> images;
    HandlePool<GfxPipelineLayout, GfxPipelineLayoutData> pipelineLayouts;
    HandlePool<GfxPipeline, GfxPipelineData> pipelines;
    HandlePool<GfxDescriptorSet, GfxDescriptorSetData> descriptorSets;
    HandlePool<GfxDescriptorSetLayout, GfxDescriptorSetLayoutData> descriptorSetLayouts;

    void Initialize();
    void Release();
    void DetectAndReleaseLeaks();
};

struct GfxGarbage
{
    enum class Type
    {
        Pipeline,
        Buffer
    };

    Type type;
    uint64 frameIdx;

    union {
        VkPipeline pipeline;
        VkBuffer buffer;
    };

    VmaAllocation allocation;
};

struct GfxDeferredCommand
{
    using ExecuteCallback = void(*)(VkCommandBuffer cmdBuff, const Blob& paramsBlob);
    
    uint32 paramsOffset;        // Offset in the buffer
    uint32 paramsSize;
    ExecuteCallback executeFn;
};

struct GfxHeapAllocator final : Allocator
{
    void* Malloc(size_t size, uint32 align) override;
    void* Realloc(void* ptr, size_t size, uint32 align) override;
    void  Free(void* ptr, uint32 align) override;
    AllocatorType GetType() const override { return AllocatorType::Heap; }
    
    static void* vkAlloc(void* pUserData, size_t size, size_t align, VkSystemAllocationScope allocScope);
    static void* vkRealloc(void* pUserData, void* pOriginal, size_t size, size_t align, VkSystemAllocationScope allocScope);
    static void vkFree(void* pUserData, void* pPtr);
    static void vkInternalAllocFn(void* pUserData, size_t size, VkInternalAllocationType allocType, VkSystemAllocationScope allocScope);
    static void vkInternalFreeFn(void* pUserData, size_t size, VkInternalAllocationType allocType, VkSystemAllocationScope allocScope); 
};

struct GfxVkState
{
    bool initialized;

    MemTlsfAllocator tlsfAlloc;
    MemThreadSafeAllocator runtimeAlloc;   // All allocations during runtime are allocated from this
    GfxHeapAllocator alloc;
    VkAllocationCallbacks allocVk;

    VkInstance instance;
    GfxApiVersion apiVersion;
    VkExtensionProperties* instanceExtensions;
    uint32 numInstanceExtensions;
    VkExtensionProperties* deviceExtensions;
    uint32 numDeviceExtensions;
    VkLayerProperties* layers;
    uint32 numLayers;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkDebugReportCallbackEXT debugReportCallback;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceProperties deviceProps;
    VkPhysicalDeviceVulkan11Properties deviceProps11;
    VkPhysicalDeviceVulkan12Properties deviceProps12;
    VkPhysicalDeviceFeatures deviceFeatures;
    VkPhysicalDeviceVulkan11Features deviceFeatures11;
    VkPhysicalDeviceVulkan12Features deviceFeatures12;
    VkDevice device;
    uint32 gfxQueueFamilyIndex;
    uint32 presentQueueFamilyIndex;
    VkQueue gfxQueue;
    VkQueue presentQueue;
    VkSurfaceKHR surface;
    GfxSwapchainSupportDetails swapchainSupport;
    GfxSwapchain swapchain;
    VkDescriptorPool descriptorPool;

    VkQueryPool queryPool[kMaxFramesInFlight];
    atomicUint32 queryFirstCall;

    VkSemaphore imageAvailSemaphores[kMaxFramesInFlight];
    VkSemaphore renderFinishedSemaphores[kMaxFramesInFlight];
    VkFence inflightFences[kMaxFramesInFlight];
    VkFence inflightImageFences[kMaxSwapchainImages];  // count: Swapchain.numImages

    VmaAllocator vma;
    GfxObjectPools pools;

    Mutex shaderPipelinesTableMtx;
    HashTable<Array<GfxPipeline>> shaderPipelinesTable;
    
    Mutex garbageMtx;
    Array<GfxGarbage> garbage;
    
    AtomicLock pendingCmdBuffersLock;
    StaticArray<VkCommandBuffer, 32> pendingCmdBuffers;

    AtomicLock threadDataLock;
    StaticArray<GfxCommandBufferThreadData*, 32> initializedThreadData;

    atomicUint32 currentFrameIdx;
    uint32       prevFrameIdx;

    Blob deferredCmdBuffer;
    Array<GfxDeferredCommand> deferredCmds;
    Mutex deferredCommandsMtx;

    GfxBudgetStats::DescriptorBudgetStats descriptorStats;

    size_t initHeapStart;
    size_t initHeapSize;

    bool hasAstcDecodeMode;     // VK_EXT_astc_decode_mode extension is available. use it for ImageViews
    bool hasPipelineExecutableProperties;
    bool hasMemoryBudget;
    bool hasHostQueryReset;
    bool hasFloat16Support;
    bool hasDescriptorIndexing;
};

namespace VkExtensionApi
{
    static PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion;

    static PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT;
    static PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT;

    static PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT;
    static PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT;

    static PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT vkGetPhysicalDeviceCalibrateableTimeDomainsEXT;
    static PFN_vkGetCalibratedTimestampsEXT vkGetCalibratedTimestampsEXT;

    static PFN_vkGetPipelineExecutablePropertiesKHR vkGetPipelineExecutablePropertiesKHR;
    static PFN_vkGetPipelineExecutableStatisticsKHR vkGetPipelineExecutableStatisticsKHR;
    static PFN_vkGetPipelineExecutableInternalRepresentationsKHR vkGetPipelineExecutableInternalRepresentationsKHR;

    static PFN_vkGetPhysicalDeviceProperties2KHR vkGetPhysicalDeviceProperties2KHR;
    static PFN_vkResetQueryPoolEXT vkResetQueryPoolEXT;
};

static GfxVkState gVk;
static thread_local GfxCommandBufferThreadData gCmdBufferThreadData;

static constexpr const char* kVkValidationLayer = "VK_LAYER_KHRONOS_validation";
static constexpr const char* kAdrenoDebugLayer = "VK_LAYER_ADRENO_debug";

#if PLATFORM_WINDOWS
    static const char* kGfxVkExtensions[] = {
        "VK_KHR_surface",
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };
#elif PLATFORM_ANDROID 
    static const char* kGfxVkExtensions[] = {
        "VK_KHR_surface",
        "VK_KHR_android_surface"
    };
#elif PLATFORM_APPLE
    static const char* kGfxVkExtensions[] = {
        "VK_KHR_surface",
        "VK_EXT_metal_surface"
    };
#endif

#define VK_FAILED(_e) _e != VK_SUCCESS

INLINE bool gfxHasVulkanVersion(GfxApiVersion version)
{
    return uint32(gVk.apiVersion) >= uint32(version) && 
           uint32(gVk.apiVersion) < uint32(GfxApiVersion::_Vulkan);
}

INLINE bool gfxHasLayer(const char* layerName)
{
    for (uint32 i = 0; i < gVk.numLayers; i++) {
        if (strIsEqual(gVk.layers[i].layerName, layerName))
            return true;
    }
    return false;
}


static VkFormat gfxFindSupportedFormat(const VkFormat* formats, uint32 numFormats, 
    VkImageTiling tiling, VkFormatFeatureFlags features)
{
    VkFormatProperties props;
    for (uint32 i = 0; i < numFormats; i++) {
        vkGetPhysicalDeviceFormatProperties(gVk.physicalDevice, formats[i], &props);
        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return formats[i];
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return formats[i];
        }
    }
    ASSERT_MSG(0, "Gfx: Could not find the format(s)");
    return VK_FORMAT_UNDEFINED;
}

INLINE VkFormat gfxFindDepthFormat()
{
    const VkFormat candidateFormats[] = {
        VK_FORMAT_D32_SFLOAT, 
        VK_FORMAT_D32_SFLOAT_S8_UINT, 
        VK_FORMAT_D24_UNORM_S8_UINT};
    return gfxFindSupportedFormat(candidateFormats, 3, 
        VK_IMAGE_TILING_OPTIMAL, 
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

bool gfxHasDeviceExtension(const char* extension)
{
    for (uint32 i = 0; i < gVk.numDeviceExtensions; i++) {
        if (strIsEqual(gVk.deviceExtensions[i].extensionName, extension)) 
            return true;
    }

    return false;
}

bool gfxHasInstanceExtension(const char* extension)
{
    for (uint32 i = 0; i < gVk.numInstanceExtensions; i++) {
        if (strIsEqual(gVk.instanceExtensions[i].extensionName, extension)) 
            return true;
    }

    return false;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL gfxDebugUtilsMessageFn(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData)
{
    UNUSED(userData);

    char typeStr[128];  typeStr[0] = '\0';
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)  
        strConcat(typeStr, sizeof(typeStr), "[V]");
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)  
        strConcat(typeStr, sizeof(typeStr), "[P]");

    switch (messageSeverity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        logVerbose("Gfx: %s%s", typeStr, callbackData->pMessage);
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        logInfo("Gfx: %s%s", typeStr, callbackData->pMessage);
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        if (!settingsGet().graphics.enableAdrenoDebug) {
            if (strFindStr(callbackData->pMessage, "VKDBGUTILWARN"))
                return VK_FALSE;
        }
        logWarning("Gfx: %s%s", typeStr, callbackData->pMessage);
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        logError("Gfx: %s%s", typeStr, callbackData->pMessage);
        break;
    default:
        break;
    }
    return VK_FALSE;
}

static VkBool32 gfxDebugReportFn(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
    uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage,
    void* pUserData)
{
    UNUSED(object);
    UNUSED(location);
    UNUSED(messageCode);
    UNUSED(objectType);
    UNUSED(pUserData);

    // TODO: Crap VK_Adreno_debug layer bombards this callback with messages coming from a different thread each time!
    //       So we have to do a workaround for that and queue up all messages coming from that layer so we don't explode the logger/temp allocators
    if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
        logDebug("Gfx: [%s] %s", pLayerPrefix, pMessage);
    }
    else if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
        logInfo("Gfx: [%s] %s", pLayerPrefix, pMessage);
    }
    else if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
        logWarning("Gfx: [%s] %s", pLayerPrefix, pMessage);
    }
    else if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
        logWarning("Gfx: [%s] (PERFORMANCE) %s", pLayerPrefix, pMessage);
    }
    else if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
        logError("Gfx: [%s] %s", pLayerPrefix, pMessage);
    }

    return VK_FALSE;
}

INLINE bool gfxFormatIsDepthStencil(GfxFormat fmt)
{
    return  fmt == GfxFormat::D32_SFLOAT ||
            fmt == GfxFormat::D16_UNORM_S8_UINT ||
            fmt == GfxFormat::D24_UNORM_S8_UINT ||
            fmt == GfxFormat::D32_SFLOAT_S8_UINT ||
            fmt == GfxFormat::S8_UINT;
}

INLINE bool gfxFormatHasDepth(GfxFormat fmt)
{
    return  fmt == GfxFormat::D32_SFLOAT ||
            fmt == GfxFormat::D16_UNORM_S8_UINT ||
            fmt == GfxFormat::D24_UNORM_S8_UINT ||
            fmt == GfxFormat::D32_SFLOAT_S8_UINT;
}

INLINE bool gfxFormatHasStencil(GfxFormat fmt)
{
    return  fmt == GfxFormat::D24_UNORM_S8_UINT ||
            fmt == GfxFormat::D16_UNORM_S8_UINT ||
            fmt == GfxFormat::D32_SFLOAT_S8_UINT ||
            fmt == GfxFormat::S8_UINT;
}

static VkCommandBuffer gfxGetNewCommandBuffer()
{
    PROFILE_ZONE(true);

    uint32 frameIdx = atomicLoad32Explicit(&gVk.currentFrameIdx, AtomicMemoryOrder::Acquire);
    
    if (!gCmdBufferThreadData.initialized) {
        VkCommandPoolCreateInfo poolCreateInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = 0,
            .queueFamilyIndex = gVk.gfxQueueFamilyIndex
        };

        for (uint32 i = 0; i < kMaxFramesInFlight; i++) {
            if (vkCreateCommandPool(gVk.device, &poolCreateInfo, &gVk.allocVk, &gCmdBufferThreadData.commandPools[i]) != VK_SUCCESS) {
                ASSERT_MSG(0, "Creating command-pool failed");
                return VK_NULL_HANDLE;
            }

            gCmdBufferThreadData.freeLists[i].SetAllocator(&gVk.alloc);
            gCmdBufferThreadData.cmdBuffers[i].SetAllocator(&gVk.alloc);
        }
        
        gCmdBufferThreadData.lastResetFrame = engineFrameIndex();
        
        gCmdBufferThreadData.initialized = true;

        // Add to thread data collection for later house-cleaning
        AtomicLockScope lock(gVk.threadDataLock);
        gVk.initializedThreadData.Add(&gCmdBufferThreadData);
    }
    else {
        PROFILE_ZONE_NAME("ResetCommandPool", true);
        // Check if we need to reset command-pools
        // We only reset the command-pools after new frame is started. 
        uint64 engineFrame = engineFrameIndex();
        if (engineFrame > gCmdBufferThreadData.lastResetFrame) {
            gCmdBufferThreadData.lastResetFrame = engineFrame;
            vkResetCommandPool(gVk.device, gCmdBufferThreadData.commandPools[frameIdx], 0);

            gCmdBufferThreadData.freeLists[frameIdx].Extend(gCmdBufferThreadData.cmdBuffers[frameIdx]);
            gCmdBufferThreadData.cmdBuffers[frameIdx].Clear();
        }
    }

    VkCommandBuffer cmdBuffer;
    if (gCmdBufferThreadData.freeLists[frameIdx].Count() == 0) {
        VkCommandBufferAllocateInfo allocInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = gCmdBufferThreadData.commandPools[frameIdx],
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
    
        if (vkAllocateCommandBuffers(gVk.device, &allocInfo, &cmdBuffer) != VK_SUCCESS)
            return VK_NULL_HANDLE;
        
        gCmdBufferThreadData.cmdBuffers[frameIdx].Push(cmdBuffer);
    }
    else {
        cmdBuffer = gCmdBufferThreadData.freeLists[frameIdx].PopLast();
        gCmdBufferThreadData.cmdBuffers[frameIdx].Push(cmdBuffer);
    }
    
    return cmdBuffer;
}

// This is used for commands that needs to scheduled for later submittion
// It is actually very useful when called unexpectedly from anywhere like loaders
// Mainly for Copy/PipelineBarrier ops
static void gfxBeginDeferredCommandBuffer()
{
    if (gCmdBufferThreadData.curCmdBuffer == VK_NULL_HANDLE) {
        gCmdBufferThreadData.deferredCmdBuffer = true;
    }
}

static void gfxEndDeferredCommandBuffer()
{
    if (gCmdBufferThreadData.deferredCmdBuffer) {
        ASSERT(gCmdBufferThreadData.curCmdBuffer == VK_NULL_HANDLE);
        gCmdBufferThreadData.deferredCmdBuffer = false;
    }    
}

static void gfxDestroySwapchain(GfxSwapchain* swapchain)
{
    if (!swapchain || !swapchain->init)
        return;

    if (swapchain->renderPass)
        vkDestroyRenderPass(gVk.device, swapchain->renderPass, &gVk.allocVk);
    for (uint32 i = 0; i < swapchain->numImages; i++) {
        if (swapchain->imageViews[i]) 
            vkDestroyImageView(gVk.device, swapchain->imageViews[i], &gVk.allocVk);
        if (swapchain->framebuffers[i])
            vkDestroyFramebuffer(gVk.device, swapchain->framebuffers[i], &gVk.allocVk);
    }

    gfxDestroyImage(swapchain->depthImage);
    
    if (swapchain->swapchain) {
        vkDestroySwapchainKHR(gVk.device, swapchain->swapchain, &gVk.allocVk);
        swapchain->swapchain = VK_NULL_HANDLE;
    }

    swapchain->init = false;
}

static GfxPipelineLayout gfxCreatePipelineLayout(const Shader& shader,
                                                 const GfxDescriptorSetLayout* descriptorSetLayouts,
                                                 uint32 numDescriptorSetLayouts,
                                                 const GfxPushConstantDesc* pushConstants,
                                                 uint32 numPushConstants,
                                                 VkPipelineLayout* layoutOut)
{
    ASSERT_MSG(numDescriptorSetLayouts <= kMaxDescriptorSetLayoutPerPipeline, "Too many descriptor set layouts per-pipeline");

    // hash the layout bindings and look in cache
    HashMurmur32Incremental hasher(0x5eed1);
    uint32 hash = hasher.Add<GfxDescriptorSetLayout>(descriptorSetLayouts, numDescriptorSetLayouts)
                        .Add<GfxPushConstantDesc>(pushConstants, numPushConstants)
                        .Hash();

    atomicLockEnter(&gVk.pools.locks[GfxObjectPools::PIPELINE_LAYOUTS]);
    if (GfxPipelineLayout pipLayout = gVk.pools.pipelineLayouts.FindIf(
        [hash](const GfxPipelineLayoutData& item)->bool { return item.hash == hash; }); pipLayout.IsValid())
    {
        GfxPipelineLayoutData& item = gVk.pools.pipelineLayouts.Data(pipLayout);
        ++item.refCount;
        atomicLockExit(&gVk.pools.locks[GfxObjectPools::PIPELINE_LAYOUTS]);
        if (layoutOut)
            *layoutOut = item.layout;
        return pipLayout;
    }
    else {
        atomicLockExit(&gVk.pools.locks[GfxObjectPools::PIPELINE_LAYOUTS]);
    
        MemTempAllocator tempAlloc;
        
        VkDescriptorSetLayout* vkDescriptorSetLayouts = nullptr;
        if (numDescriptorSetLayouts) {   
            AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SET_LAYOUTS]);
            vkDescriptorSetLayouts = tempAlloc.MallocTyped<VkDescriptorSetLayout>(numDescriptorSetLayouts);
            for (uint32 i = 0; i < numDescriptorSetLayouts; i++) {
                GfxDescriptorSetLayoutData& dsLayoutData = gVk.pools.descriptorSetLayouts.Data(descriptorSetLayouts[i]);
                vkDescriptorSetLayouts[i] = dsLayoutData.layout;
                ASSERT(dsLayoutData.layout != VK_NULL_HANDLE);
            }
        }

        VkPushConstantRange* vkPushConstants = nullptr;
        if (numPushConstants) {
            vkPushConstants = tempAlloc.MallocTyped<VkPushConstantRange>(numPushConstants);
            for (uint32 i = 0; i < numPushConstants; i++) {
                ASSERT(pushConstants[i].name);
                [[maybe_unused]] const ShaderParameterInfo* paramInfo = shaderGetParam(shader, pushConstants[i].name);
                ASSERT_MSG(paramInfo, "PushConstant '%s' not found in shader '%s'", pushConstants[i].name, shader.name);
                ASSERT_MSG(paramInfo->isPushConstant, "Parameter '%s' is not a push constant in shader '%s'", paramInfo->name, shader.name);

                vkPushConstants[i] = VkPushConstantRange {
                    .stageFlags = static_cast<VkShaderStageFlags>(pushConstants[i].stages),
                    .offset = pushConstants[i].range.offset,
                    .size = pushConstants[i].range.size
                };
            }
        }

        VkPipelineLayout pipelineLayoutVk;
        VkPipelineLayoutCreateInfo pipelineLayoutInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = numDescriptorSetLayouts,
            .pSetLayouts = vkDescriptorSetLayouts,
            .pushConstantRangeCount = numPushConstants,
            .pPushConstantRanges = vkPushConstants
        };
        
        if (VK_FAILED(vkCreatePipelineLayout(gVk.device, &pipelineLayoutInfo, &gVk.allocVk, &pipelineLayoutVk))) {
            logError("Gfx: Failed to create pipeline layout");
            return GfxPipelineLayout();
        }
        
        AtomicLockScope mtx(gVk.pools.locks[GfxObjectPools::PIPELINE_LAYOUTS]);
        GfxPipelineLayoutData prevPipLayout;
        GfxPipelineLayoutData pipLayoutData = GfxPipelineLayoutData {
            .hash = hash,
            .numDescriptorSetLayouts = numDescriptorSetLayouts,
            .layout = pipelineLayoutVk,
            .refCount = 1
        };

        for (uint32 i = 0; i < numDescriptorSetLayouts; i++)
            pipLayoutData.descriptorSetLayouts[i] = descriptorSetLayouts[i];

        #if !CONFIG_FINAL_BUILD
            if (settingsGet().graphics.trackResourceLeaks)
                pipLayoutData.numStackframes = debugCaptureStacktrace(pipLayoutData.stackframes, (uint16)CountOf(pipLayoutData.stackframes), 2);
        #endif

        pipLayout = gVk.pools.pipelineLayouts.Add(pipLayoutData);
        if (layoutOut)
            *layoutOut = pipelineLayoutVk;
        return pipLayout;
    }
}

static void gfxDestroyPipelineLayout(GfxPipelineLayout layout)
{
    AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::PIPELINE_LAYOUTS]);
    GfxPipelineLayoutData& layoutData = gVk.pools.pipelineLayouts.Data(layout);
    ASSERT(layoutData.refCount > 0);
    if (--layoutData.refCount == 0) {
        if (layoutData.layout) 
            vkDestroyPipelineLayout(gVk.device, layoutData.layout, &gVk.allocVk);
        memset(&layoutData, 0x0, sizeof(layoutData));

        gVk.pools.pipelineLayouts.Remove(layout);
    }
}

bool gfxBeginCommandBuffer()
{
    ASSERT(gCmdBufferThreadData.curCmdBuffer == VK_NULL_HANDLE);
    ASSERT(!gCmdBufferThreadData.deferredCmdBuffer);
    PROFILE_ZONE(true);

    gCmdBufferThreadData.curCmdBuffer = gfxGetNewCommandBuffer();
    if (gCmdBufferThreadData.curCmdBuffer == VK_NULL_HANDLE)
        return false;

    VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT 
    };

    if (vkBeginCommandBuffer(gCmdBufferThreadData.curCmdBuffer, &beginInfo) != VK_SUCCESS) {
        gCmdBufferThreadData.curCmdBuffer = VK_NULL_HANDLE;
        return false;
    }

    // Start query for frametime on the first command-buffer only
    if (gVk.deviceProps.limits.timestampComputeAndGraphics) {
        uint32 expectedValue = 0;
        if (atomicCompareExchange32Weak(&gVk.queryFirstCall, &expectedValue, 1)) {
            //if (gVk.hasHostQueryReset)
                VkExtensionApi::vkResetQueryPoolEXT(gVk.device, gVk.queryPool[gVk.currentFrameIdx], 0, 2);
            vkCmdWriteTimestamp(gCmdBufferThreadData.curCmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                                gVk.queryPool[gVk.currentFrameIdx], 0);
        }
    }

    return true;
}

void gfxEndCommandBuffer()
{
    if (gCmdBufferThreadData.curCmdBuffer != VK_NULL_HANDLE) {
        [[maybe_unused]] VkResult r = vkEndCommandBuffer(gCmdBufferThreadData.curCmdBuffer);
        ASSERT(r == VK_SUCCESS);
    }
    else {
        ASSERT_MSG(0, "BeginCommandBuffer wasn't called successfully on this thread");
        return;
    }

    // Recoding finished, push it for submittion
    AtomicLockScope lock(gVk.pendingCmdBuffersLock);
    gVk.pendingCmdBuffers.Add(gCmdBufferThreadData.curCmdBuffer);
    gCmdBufferThreadData.curCmdBuffer = VK_NULL_HANDLE;
}

static void gfxCmdCopyBufferToImage(VkBuffer buffer, VkImage image, uint32 width, uint32 height, uint32 numMips, 
                                    const uint32* mipOffsets)
{
    VkBufferImageCopy regions[kGfxMaxMips];
    
    for (uint32 i = 0; i < numMips; i++) {
        regions[i] = VkBufferImageCopy {
            .bufferOffset = numMips > 1 ? mipOffsets[i] : 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = i,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {width, height, 1}
        };

        width = Max(width >> 1, 1u);
        height = Max(height >> 1, 1u);
    }

    if (gCmdBufferThreadData.deferredCmdBuffer) {
        MutexScope mtx(gVk.deferredCommandsMtx);
        Blob& b = gVk.deferredCmdBuffer;
        uint32 offset = uint32(b.Size());
        b.Write<VkBuffer>(buffer);
        b.Write<VkImage>(image);
        b.Write<uint32>(width);
        b.Write<uint32>(height);
        b.Write<uint32>(numMips);
        b.Write(regions, sizeof(VkBufferImageCopy)*numMips);

        gVk.deferredCmds.Push(GfxDeferredCommand {
            .paramsOffset = offset,
            .paramsSize = uint32(b.Size()) - offset,
            .executeFn = [](VkCommandBuffer cmdBuff, const Blob& paramsBlob) {
                VkBuffer buffer;
                VkImage image;
                uint32 width;
                uint32 height;
                uint32 numMips;
                VkBufferImageCopy regions[kGfxMaxMips];
                paramsBlob.Read<VkBuffer>(&buffer);
                paramsBlob.Read<VkImage>(&image);
                paramsBlob.Read<uint32>(&width);
                paramsBlob.Read<uint32>(&height);
                paramsBlob.Read<uint32>(&numMips);
                paramsBlob.Read(regions, sizeof(VkBufferImageCopy)*numMips);
                vkCmdCopyBufferToImage(cmdBuff, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, numMips, regions);
            }
        });
    }
    else {
        VkCommandBuffer cmdBuffer = gCmdBufferThreadData.curCmdBuffer;
        vkCmdCopyBufferToImage(cmdBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, numMips, regions);
    }
}

static void gfxCmdCopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, uint32 regionCount, const VkBufferCopy* pRegions)
{
    // TODO: most likely we need to insert barriers as well
    if (gCmdBufferThreadData.deferredCmdBuffer) {
        MutexScope mtx(gVk.deferredCommandsMtx);
        Blob& b = gVk.deferredCmdBuffer;
        uint32 offset = uint32(b.Size());
        b.Write<VkBuffer>(srcBuffer);
        b.Write<VkBuffer>(dstBuffer);
        b.Write<uint32>(regionCount);
        b.Write(pRegions, sizeof(VkBufferCopy)*regionCount);

        gVk.deferredCmds.Push(GfxDeferredCommand {
            .paramsOffset = offset,
            .paramsSize = uint32(b.Size()) - offset,
            .executeFn = [](VkCommandBuffer cmdBuff, const Blob& paramsBlob) {
                VkBuffer srcBuffer;
                VkBuffer dstBuffer;
                uint32 regionCount;

                paramsBlob.Read<VkBuffer>(&srcBuffer);
                paramsBlob.Read<VkBuffer>(&dstBuffer);
                paramsBlob.Read<uint32>(&regionCount);
                VkBufferCopy* pRegions = nullptr;
                if (regionCount) {
                    pRegions = (VkBufferCopy*)alloca(sizeof(VkBufferCopy)*regionCount); ASSERT(pRegions);
                    paramsBlob.Read(pRegions, regionCount*sizeof(VkBufferCopy));
                }
                vkCmdCopyBuffer(cmdBuff, srcBuffer, dstBuffer, regionCount, pRegions);
            }
        });
    }
    else {
        VkCommandBuffer cmdBuffer = gCmdBufferThreadData.curCmdBuffer;
        vkCmdCopyBuffer(cmdBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
    }
}

static void gfxCmdPipelineBarrier(VkPipelineStageFlags srcStageMask,
                                  VkPipelineStageFlags dstStageMask,
                                  VkDependencyFlags dependencyFlags,
                                  uint32 memoryBarrierCount,
                                  const VkMemoryBarrier* pMemoryBarriers,
                                  uint32 bufferMemoryBarrierCount,
                                  const VkBufferMemoryBarrier* pBufferMemoryBarriers,
                                  uint32 imageMemoryBarrierCount,
                                  const VkImageMemoryBarrier* pImageMemoryBarriers)
{
    if (gCmdBufferThreadData.deferredCmdBuffer) {
        MutexScope mtx(gVk.deferredCommandsMtx);
        Blob& b = gVk.deferredCmdBuffer;
        uint32 offset = uint32(b.Size());
        b.Write<VkPipelineStageFlags>(srcStageMask);
        b.Write<VkPipelineStageFlags>(dstStageMask);
        b.Write<VkDependencyFlags>(dependencyFlags);
        b.Write<uint32>(memoryBarrierCount);
        if (memoryBarrierCount && pMemoryBarriers)
            b.Write(pMemoryBarriers, sizeof(VkMemoryBarrier)*memoryBarrierCount);
        b.Write<uint32>(bufferMemoryBarrierCount);
        if (bufferMemoryBarrierCount && pBufferMemoryBarriers)
            b.Write(pBufferMemoryBarriers, sizeof(VkBufferMemoryBarrier)*bufferMemoryBarrierCount);
        b.Write<uint32>(imageMemoryBarrierCount);
        if (imageMemoryBarrierCount && pImageMemoryBarriers)
            b.Write(pImageMemoryBarriers, sizeof(VkImageMemoryBarrier)*imageMemoryBarrierCount);

        gVk.deferredCmds.Push(GfxDeferredCommand {
            .paramsOffset = offset,
            .paramsSize = uint32(b.Size()) - offset,
            .executeFn = [](VkCommandBuffer cmdBuff, const Blob& paramsBlob) {
                VkPipelineStageFlags srcStageMask;  paramsBlob.Read<VkPipelineStageFlags>(&srcStageMask);
                VkPipelineStageFlags dstStageMask;  paramsBlob.Read<VkPipelineStageFlags>(&dstStageMask);
                VkDependencyFlags dependencyFlags;  paramsBlob.Read<VkDependencyFlags>(&dependencyFlags);
                uint32 memoryBarrierCount;    paramsBlob.Read<uint32>(&memoryBarrierCount);
                VkMemoryBarrier* pMemoryBarriers = nullptr;
                if (memoryBarrierCount) {
                    pMemoryBarriers = (VkMemoryBarrier*)alloca(memoryBarrierCount*sizeof(VkMemoryBarrier)); ASSERT(pMemoryBarriers);
                    paramsBlob.Read(pMemoryBarriers, sizeof(VkMemoryBarrier)*memoryBarrierCount);
                }

                uint32 bufferMemoryBarrierCount;  paramsBlob.Read<uint32>(&bufferMemoryBarrierCount);
                VkBufferMemoryBarrier* pBufferMemoryBarriers = nullptr;
                if (bufferMemoryBarrierCount) {
                    pBufferMemoryBarriers = (VkBufferMemoryBarrier*)alloca(bufferMemoryBarrierCount*sizeof(VkBufferMemoryBarrier)); ASSERT(pBufferMemoryBarriers);
                    paramsBlob.Read(pBufferMemoryBarriers, sizeof(VkBufferMemoryBarrier)*bufferMemoryBarrierCount);
                }
                uint32 imageMemoryBarrierCount;   paramsBlob.Read<uint32>(&imageMemoryBarrierCount);
                VkImageMemoryBarrier* pImageMemoryBarriers = nullptr;
                if (imageMemoryBarrierCount) {
                    pImageMemoryBarriers = (VkImageMemoryBarrier*)alloca(imageMemoryBarrierCount*sizeof(VkImageMemoryBarrier)); ASSERT(pImageMemoryBarriers);
                    paramsBlob.Read(pImageMemoryBarriers, sizeof(VkImageMemoryBarrier)*imageMemoryBarrierCount);
                }

                vkCmdPipelineBarrier(cmdBuff, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount,
                                    pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers,
                                    imageMemoryBarrierCount, pImageMemoryBarriers);
            }
        });
    }
    else {
        VkCommandBuffer cmdBuffer = gCmdBufferThreadData.curCmdBuffer;
        return vkCmdPipelineBarrier(cmdBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount,
                                    pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers,
                                    imageMemoryBarrierCount, pImageMemoryBarriers);
    }
}

static VkImageView gfxCreateImageViewVk(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags)
{
    
    VkImageViewCreateInfo viewInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = aspectFlags,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_EXT_astc_decode_mode.html
    VkImageViewASTCDecodeModeEXT astcDecodeMode;
    if (gVk.hasAstcDecodeMode) {
        // Translate the astc format to RGBA counterpart
        // sRGB decode not supported ? _disappointed_
        VkFormat decodeFormat = VK_FORMAT_UNDEFINED;
        switch (format) {
        case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:    
        case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
        case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
        case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
            decodeFormat = VK_FORMAT_R8G8B8A8_UNORM;
            break;
        default:
            break;
        }

        if (decodeFormat != VK_FORMAT_UNDEFINED) {
            astcDecodeMode = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_ASTC_DECODE_MODE_EXT,
                .pNext = nullptr,
                .decodeMode = decodeFormat
            };

            ASSERT(viewInfo.pNext == nullptr);
            viewInfo.pNext = &astcDecodeMode;
        }
    }

    VkImageView view;
    if (vkCreateImageView(gVk.device, &viewInfo, &gVk.allocVk, &view) != VK_SUCCESS) {
        logError("Gfx: CreateImageView failed");
        return VK_NULL_HANDLE;
    }

    return view;
}

static VkSampler gfxCreateSamplerVk(VkFilter minMagFilter, VkSamplerMipmapMode mipFilter, 
                                    VkSamplerAddressMode addressMode, float anisotropy)
{
    VkSamplerCreateInfo samplerInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = minMagFilter,
        .minFilter = minMagFilter,
        .mipmapMode = mipFilter,
        .addressModeU = addressMode,
        .addressModeV = addressMode,
        .addressModeW = addressMode,
        .mipLodBias = 0.0f,
        .anisotropyEnable = anisotropy > 1.0f ? VK_TRUE : VK_FALSE,
        .maxAnisotropy = Min(gVk.deviceProps.limits.maxSamplerAnisotropy, anisotropy),
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f, 
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE, 
    };

    VkSampler sampler;
    if (vkCreateSampler(gVk.device, &samplerInfo, &gVk.allocVk, &sampler) != VK_SUCCESS) {
        logError("Gfx: CreateSampler failed");
        return VK_NULL_HANDLE;
    }

    return sampler;
}

static VkRenderPass gfxCreateRenderPass(VkFormat format, VkFormat depthFormat = VK_FORMAT_UNDEFINED)
{
    VkAttachmentReference colorAttachmentRef {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkAttachmentReference depthAttachmentRef {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription subpass {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentRef,
        .pDepthStencilAttachment = nullptr
    };

    // Dependency defines what synchorinization point(s) the subpasses are depending on
    VkSubpassDependency dependency {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    StaticArray<VkAttachmentDescription, 2> attachments;
    attachments.Add(VkAttachmentDescription {
        .format = format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    });

    // Do we have a depth attachment ?
    if (depthFormat != VK_FORMAT_UNDEFINED) {
        attachments.Add(VkAttachmentDescription {
            .format = depthFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        });

        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        dependency.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    VkRenderPass renderPass;
    VkRenderPassCreateInfo renderPassInfo {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = attachments.Count(),
        .pAttachments = attachments.Ptr(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency
    };

    if (vkCreateRenderPass(gVk.device, &renderPassInfo, &gVk.allocVk, &renderPass) != VK_SUCCESS) {
        logError("Gfx: vkCreateRenderPass failed");
        return VK_NULL_HANDLE;
    }

    return renderPass;
}

static VkSurfaceKHR gfxCreateWindowSurface(void* windowHandle)
{
    VkSurfaceKHR surface = nullptr;
    #if PLATFORM_WINDOWS
        VkWin32SurfaceCreateInfoKHR surfaceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
            .hinstance = (HMODULE)appGetNativeAppHandle(),
            .hwnd = (HWND)windowHandle
        };
    
        vkCreateWin32SurfaceKHR(gVk.instance, &surfaceCreateInfo, &gVk.allocVk, &surface);
    #elif PLATFORM_ANDROID
        VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
            .window = (ANativeWindow*)windowHandle
        };
    
        vkCreateAndroidSurfaceKHR(gVk.instance, &surfaceCreateInfo, &gVk.allocVk, &surface);
    #elif PLATFORM_APPLE
        VkMetalSurfaceCreateInfoEXT surfaceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
            .pLayer = windowHandle
        };
    
        vkCreateMetalSurfaceEXT(gVk.instance, &surfaceCreateInfo, &gVk.allocVk, &surface);
    #else
        #error "Not implemented"
    #endif
    return surface;
}

static GfxSwapchain gfxCreateSwapchain(VkSurfaceKHR surface, uint16 width, uint16 height, 
                                       VkSwapchainKHR oldSwapChain = VK_NULL_HANDLE, bool depth = false)
{
    VkSurfaceFormatKHR format {};
    for (uint32 i = 0; i < gVk.swapchainSupport.numFormats; i++) {
        VkFormat fmt = gVk.swapchainSupport.formats[i].format;
        if (settingsGet().graphics.surfaceSRGB) {
            if((fmt == VK_FORMAT_B8G8R8A8_SRGB || fmt == VK_FORMAT_R8G8B8A8_SRGB) &&
               gVk.swapchainSupport.formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) 
            {
                format = gVk.swapchainSupport.formats[i];
                break;
            }
        }
        else {
            if (fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_R8G8B8A8_UNORM) {
                format = gVk.swapchainSupport.formats[i];
                break;
            }
        }
    }
    ASSERT_ALWAYS(format.format != VK_FORMAT_UNDEFINED, "Gfx: SwapChain PixelFormat is not supported");

    VkPresentModeKHR presentMode = settingsGet().graphics.enableVsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;

    // Verify that SwapChain has support for this present mode
    bool presentModeIsSupported = false;
    for (uint32 i = 0; i < gVk.swapchainSupport.numPresentModes; i++) {
        if (gVk.swapchainSupport.presentModes[i] == presentMode) {
            presentModeIsSupported = true;
            break;
        }
    }

    if (!presentModeIsSupported) {
        logWarning("Gfx: PresentMode: %u is not supported by device, choosing default: %u", presentMode, 
                   gVk.swapchainSupport.presentModes[0]);
        presentMode = gVk.swapchainSupport.presentModes[0];
    }

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gVk.physicalDevice, surface, &gVk.swapchainSupport.caps);
    VkExtent2D extent = {
        Clamp<uint32>(width, 
            gVk.swapchainSupport.caps.minImageExtent.width, 
            gVk.swapchainSupport.caps.maxImageExtent.width), 
        Clamp<uint32>(height,
            gVk.swapchainSupport.caps.minImageExtent.height,
            gVk.swapchainSupport.caps.maxImageExtent.height)
    };

    // https://android-developers.googleblog.com/2020/02/handling-device-orientation-efficiently.html
    if (appGetFramebufferTransform() == AppFramebufferTransform::Rotate90 || 
        appGetFramebufferTransform() == AppFramebufferTransform::Rotate270)
    {
       Swap(extent.width, extent.height);
    }

    uint32 minImages = Min(Clamp(gVk.swapchainSupport.caps.minImageCount + 1, 1u, 
                                 gVk.swapchainSupport.caps.maxImageCount), kMaxSwapchainImages);
    VkCompositeAlphaFlagBitsKHR compositeAlpha = 
        (gVk.swapchainSupport.caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ?
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

    VkSwapchainCreateInfoKHR createInfo {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = minImages,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,    // TODO: =2 if it's 3d stereoscopic
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,    // TODO: VK_IMAGE_USAGE_TRANSFER_DST_BIT if we are postprocessing
        .preTransform = gVk.swapchainSupport.caps.currentTransform,
        .compositeAlpha = compositeAlpha,    // TODO: what's this ?
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = oldSwapChain
    };

    const uint32 queueFamilyIndexes[] = {gVk.gfxQueueFamilyIndex, gVk.presentQueueFamilyIndex};
    if (gVk.gfxQueueFamilyIndex != gVk.presentQueueFamilyIndex) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndexes;
    }
    else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    VkSwapchainKHR swapchain;
    if (vkCreateSwapchainKHR(gVk.device, &createInfo, &gVk.allocVk, &swapchain) != VK_SUCCESS) {
        logError("Gfx: CreateSwapchain failed");
        return GfxSwapchain{};
    }

    uint32 numImages;
    vkGetSwapchainImagesKHR(gVk.device, swapchain, &numImages, nullptr);
    GfxSwapchain newSwapchain = {
        .numImages = numImages,
        .swapchain = swapchain,
        .extent = extent,
        .colorFormat = format.format,
    };
    vkGetSwapchainImagesKHR(gVk.device, swapchain, &newSwapchain.numImages, newSwapchain.images);

    // Views
    VkImageViewCreateInfo viewCreateInfo {};
    viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;

    for (uint32 i = 0; i < numImages; i++) {
        viewCreateInfo.image = newSwapchain.images[i];
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCreateInfo.format = format.format;
        viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCreateInfo.subresourceRange.baseMipLevel = 0;
        viewCreateInfo.subresourceRange.levelCount = 1;
        viewCreateInfo.subresourceRange.baseArrayLayer = 0;
        viewCreateInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(gVk.device, &viewCreateInfo, &gVk.allocVk, &newSwapchain.imageViews[i]) != VK_SUCCESS) {
            logError("Gfx: Creating Swapchain image views failed");
            gfxDestroySwapchain(&newSwapchain);
            return GfxSwapchain {};
        }
    }

    VkFormat depthFormat = gfxFindDepthFormat();
    if (depth) {
        GfxImage depthImage = gfxCreateImage(GfxImageDesc {
            .width = uint32(extent.width),
            .height = uint32(extent.height),
            .format = static_cast<GfxFormat>(depthFormat),
            .frameBuffer = true
        });

        if (!depthImage.IsValid()) {
            logError("Gfx: Creating Swapchain depth image failed");
            gfxDestroySwapchain(&newSwapchain);
            return GfxSwapchain {};
        }

        newSwapchain.depthImage = depthImage;
    }

    // RenderPasses
    newSwapchain.renderPass = gfxCreateRenderPass(format.format, depth ? depthFormat : VK_FORMAT_UNDEFINED);
    if (newSwapchain.renderPass == VK_NULL_HANDLE) {
        gfxDestroySwapchain(&newSwapchain);
        return GfxSwapchain {};
    }

    // Framebuffers
    AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::IMAGES]);
    VkImageView depthImageView = depth ? gVk.pools.images.Data(newSwapchain.depthImage).view : nullptr;
    for (uint32 i = 0; i < numImages; i++) {
        const VkImageView attachments[] = {newSwapchain.imageViews[i], depthImageView};
        
        VkFramebufferCreateInfo fbCreateInfo {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = newSwapchain.renderPass,
            .attachmentCount = depth ? 2u : 1u,
            .pAttachments = attachments,
            .width = extent.width,
            .height = extent.height,
            .layers = 1
        };
        if (vkCreateFramebuffer(gVk.device, &fbCreateInfo, &gVk.allocVk, &newSwapchain.framebuffers[i]) != VK_SUCCESS) {
            gfxDestroySwapchain(&newSwapchain);
            return GfxSwapchain {};
        }
    }

    newSwapchain.init = true;
    return newSwapchain;
}

bool _private::gfxInitialize()
{
    TimerStopWatch stopwatch;

    if (volkInitialize() != VK_SUCCESS) {
        logError("Volk failed to initialize. Possibly VulkanSDK is not installed (or MoltenVK dll is missing on Mac)");
        return false;
    }

    MemBumpAllocatorBase* initHeap = engineGetInitHeap();
    gVk.initHeapStart = initHeap->GetOffset();
    
    {
        size_t bufferSize = MemTlsfAllocator::GetMemoryRequirement(_limits::kGfxRuntimeSize);
        gVk.tlsfAlloc.Initialize(_limits::kGfxRuntimeSize, initHeap->Malloc(bufferSize), bufferSize, settingsGet().engine.debugAllocations);
        gVk.runtimeAlloc.SetAllocator(&gVk.tlsfAlloc);
    }

    const SettingsGraphics& settings = settingsGet().graphics;

    gVk.allocVk = VkAllocationCallbacks {
        .pUserData = &gVk.alloc,
        .pfnAllocation = GfxHeapAllocator::vkAlloc,
        .pfnReallocation = GfxHeapAllocator::vkRealloc,
        .pfnFree = GfxHeapAllocator::vkFree,
        .pfnInternalAllocation = GfxHeapAllocator::vkInternalAllocFn,
        .pfnInternalFree = GfxHeapAllocator::vkInternalFreeFn
    };

    gVk.pools.Initialize();

    //------------------------------------------------------------------------
    // Layers
    uint32 numLayers = 0;
    vkEnumerateInstanceLayerProperties(&numLayers, nullptr);
    if (numLayers) {
        gVk.layers = memAllocTyped<VkLayerProperties>(numLayers, initHeap);
        gVk.numLayers = numLayers;

        vkEnumerateInstanceLayerProperties(&numLayers, gVk.layers);
    }

    //------------------------------------------------------------------------
    // Instance Extensions
    uint32 numInstanceExtensions = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &numInstanceExtensions, nullptr);
    if (numInstanceExtensions) {
        gVk.instanceExtensions = memAllocTyped<VkExtensionProperties>(numInstanceExtensions, initHeap);
        gVk.numInstanceExtensions = numInstanceExtensions;

        vkEnumerateInstanceExtensionProperties(nullptr, &numInstanceExtensions, gVk.instanceExtensions);

        if (settings.listExtensions) {
            logVerbose("Instance Extensions (%u):", gVk.numInstanceExtensions);
            for (uint32 i = 0; i < numInstanceExtensions; i++) {
                logVerbose("\t%s", gVk.instanceExtensions[i].extensionName);
            }
        }
    }

    //------------------------------------------------------------------------
    // Instance
    VkApplicationInfo appInfo {};

    // To set our maximum API version, we need to query for VkEnumerateInstanceVersion (vk1.1)
    // As it states in the link below, only vulkan-1.0 implementations throw error if the requested API is greater than 1.0
    // But implementations 1.1 and higher doesn't care for vkCreateInstance
    // https://www.khronos.org/registry/vulkan/specs/1.3-extensions/html/vkspec.html#VkApplicationInfo

    // vkApiVersion is actually the API supported by the Vulkan dll, not the driver itself
    // For driver, we fetch that from DeviceProperties
    VkExtensionApi::vkEnumerateInstanceVersion = 
        (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"); 
    uint32 vkApiVersion = VK_API_VERSION_1_0;
    if (VkExtensionApi::vkEnumerateInstanceVersion) 
        VkExtensionApi::vkEnumerateInstanceVersion(&vkApiVersion);

    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Junkyard";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "JunkyardVkEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = vkApiVersion;

    VkInstanceCreateInfo instCreateInfo {};
    instCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instCreateInfo.pApplicationInfo = &appInfo;

    StaticArray<const char*, 4> enabledLayers;
    if (settings.validate) {
        if (gfxHasLayer(kVkValidationLayer)) {
            enabledLayers.Add(kVkValidationLayer);
        }
        else {
            logError("Gfx: Vulkan backend doesn't have validation layer support. Turn it off in the settings.");
            return false;
        }

        if (settings.enableAdrenoDebug) {
            if (gfxHasLayer(kAdrenoDebugLayer)) {
                enabledLayers.Add(kAdrenoDebugLayer);
            }
            else {
                logWarning("Gfx: VK_LAYER_ADRENO_debug is not present, but it is requested by the user in the settings");
            }
        }
    }

    instCreateInfo.enabledLayerCount = enabledLayers.Count();
    instCreateInfo.ppEnabledLayerNames = enabledLayers.Ptr();
   
    //------------------------------------------------------------------------
    // Instance extensions
    StaticArray<const char*, 32> enabledInstanceExtensions;
    for (uint32 i = 0; i < sizeof(kGfxVkExtensions)/sizeof(const char*); i++)
        enabledInstanceExtensions.Add(kGfxVkExtensions[i]);
    
    // Enable Validation
    VkValidationFeaturesEXT validationFeatures;
    StaticArray<VkValidationFeatureEnableEXT, 5> validationFeatureFlags;
    if (settings.validate) {
        if (gfxHasInstanceExtension("VK_EXT_debug_utils"))
            enabledInstanceExtensions.Add("VK_EXT_debug_utils");
        else if (gfxHasInstanceExtension("VK_EXT_debug_report"))
            enabledInstanceExtensions.Add("VK_EXT_debug_report");

        bool validateFeatures = settings.validateBestPractices || settings.validateSynchronization;
        // TODO: How can we know we have VK_Validation_Features ? 
        //       Because it is only enabled when debug layer is activated
        if (validateFeatures/* && gfxHasInstanceExtension("VK_EXT_validation_features")*/) {
            enabledInstanceExtensions.Add("VK_EXT_validation_features");
            
            if (settings.validateBestPractices)
                validationFeatureFlags.Add(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
            if (settings.validateSynchronization)
                validationFeatureFlags.Add(VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
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
    
    // physical device properties is always enabled if exists
    if (gfxHasInstanceExtension("VK_KHR_get_physical_device_properties2"))
        enabledInstanceExtensions.Add("VK_KHR_get_physical_device_properties2");

    instCreateInfo.enabledExtensionCount = enabledInstanceExtensions.Count();
    instCreateInfo.ppEnabledExtensionNames = enabledInstanceExtensions.Ptr();
    
    if (enabledLayers.Count()) {
        logVerbose("Enabled instance layers:");
        for (const char* layer: enabledLayers)
            logVerbose("\t%s", layer);
    }

    if (enabledInstanceExtensions.Count()) {
        logVerbose("Enabled instance extensions:");
        for (const char* ext: enabledInstanceExtensions) 
            logVerbose("\t%s", ext);
    }

    if (VkResult r = vkCreateInstance(&instCreateInfo, &gVk.allocVk, &gVk.instance); r != VK_SUCCESS) {
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
        logError("Gfx: Creating vulkan instance failed: %s", errorCode);
        return false;
    }
    logInfo("(init) Vulkan instance created");

    volkLoadInstanceOnly(gVk.instance);

    //------------------------------------------------------------------------
    // Validation layer and callbacks
    if (settings.validate) {
        if (gfxHasInstanceExtension("VK_EXT_debug_utils")) {
            
            VkExtensionApi::vkCreateDebugUtilsMessengerEXT = 
                (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(gVk.instance, "vkCreateDebugUtilsMessengerEXT");
            VkExtensionApi::vkDestroyDebugUtilsMessengerEXT = 
                (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(gVk.instance, "vkDestroyDebugUtilsMessengerEXT");
            
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
                .pfnUserCallback = gfxDebugUtilsMessageFn,
                .pUserData = nullptr
            };

            if (VkExtensionApi::vkCreateDebugUtilsMessengerEXT(gVk.instance, &debugUtilsInfo, &gVk.allocVk, &gVk.debugMessenger) != VK_SUCCESS) {
                logError("Gfx: vkCreateDebugUtilsMessengerEXT failed");
                return false;
            }
        }
        else if (gfxHasInstanceExtension("VK_EXT_debug_report")) {
            VkExtensionApi::vkCreateDebugReportCallbackEXT = 
                (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(gVk.instance, "vkCreateDebugReportCallbackEXT");
            VkExtensionApi::vkDestroyDebugReportCallbackEXT = 
                (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(gVk.instance, "vkDestroyDebugReportCallbackEXT");


            VkDebugReportCallbackCreateInfoEXT debugReportInfo {
                .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
                    .flags = 
                        VK_DEBUG_REPORT_INFORMATION_BIT_EXT |
                        VK_DEBUG_REPORT_WARNING_BIT_EXT |
                        VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
                        VK_DEBUG_REPORT_ERROR_BIT_EXT |
                        VK_DEBUG_REPORT_DEBUG_BIT_EXT,
                    .pfnCallback = gfxDebugReportFn,
                    .pUserData = nullptr,
            };
            
            if (VkExtensionApi::vkCreateDebugReportCallbackEXT(gVk.instance, &debugReportInfo, &gVk.allocVk, &gVk.debugReportCallback) != VK_SUCCESS) {
                logError("Gfx: vkCreateDebugReportCallbackEXT failed");
                return false;
            }
        }
    }

    //------------------------------------------------------------------------
    // Surface (Implementation is platform dependent)
    if (!settings.headless) {
        gVk.surface = gfxCreateWindowSurface(appGetNativeWindowHandle());
        if (!gVk.surface) {
            logError("Gfx: Creating window surface failed");
            return false;
        }
    }

    //------------------------------------------------------------------------
    // Physical Device(s)
    uint32 numDevices = 0;
    uint32 gfxQueueFamilyIdx = UINT32_MAX;
    uint32 presentQueueFamilyIdx = UINT32_MAX;

    vkEnumeratePhysicalDevices(gVk.instance, &numDevices, nullptr);
    auto devices = (VkPhysicalDevice*)alloca(numDevices * sizeof(VkPhysicalDevice));
    ASSERT_ALWAYS(devices, "Out of stack memory");
    vkEnumeratePhysicalDevices(gVk.instance, &numDevices, devices);
    if (numDevices > 0) {
        for (uint32 i = 0; i < numDevices; i++) {
            {   // ignore the ones who doesn't have Graphics queue for non-headless mode
                uint32 numQueueFamily = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &numQueueFamily, nullptr);
                VkQueueFamilyProperties* queueFamilyProps = (VkQueueFamilyProperties*)alloca(sizeof(VkQueueFamilyProperties)*numQueueFamily);
                vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &numQueueFamily, queueFamilyProps);

                bool graphicsQueueSupport = false;
                bool transferQueueSupport = false;
                bool presentSupport = false;
                for (uint32 k = 0; k < numQueueFamily; k++) {
                    if (queueFamilyProps[k].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                        graphicsQueueSupport = true;
                        gfxQueueFamilyIdx = k;
                    }

                    if (queueFamilyProps[k].queueFlags & VK_QUEUE_TRANSFER_BIT) {
                        transferQueueSupport = true;
                    }

                    if (gVk.surface) {
                        VkBool32 vkPresentSupport = 0;
                        vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], k, gVk.surface, &vkPresentSupport);
                        if (vkPresentSupport) {
                            presentQueueFamilyIdx = k;
                            presentSupport = true;
                        }
                    }
                }

                if (!graphicsQueueSupport || !presentSupport) {
                    gfxQueueFamilyIdx = UINT32_MAX;
                    presentQueueFamilyIdx = UINT32_MAX;
                    if (!settings.headless)
                        continue;
                }
            }

            gVk.physicalDevice = devices[i];

            // Prefer discrete GPUs
            VkPhysicalDeviceProperties devProps;
            vkGetPhysicalDeviceProperties(devices[i], &devProps);
            if (devProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                break;
            }
        }

        if (gVk.physicalDevice == VK_NULL_HANDLE) {
            logError("Gfx: No compatible vulkan device found");
            return false;
        }

    }
    else {
        logError("Gfx: No compatible vulkan device found");
        return false;
    }

    //------------------------------------------------------------------------
    // Physical device is created, gather information about driver/hardware and show it before we continue initialization other stuff
    {
        vkGetPhysicalDeviceProperties(gVk.physicalDevice, &gVk.deviceProps);

        VkDeviceSize heapSize = 0;
        {
            VkPhysicalDeviceMemoryProperties memProps;
            vkGetPhysicalDeviceMemoryProperties(gVk.physicalDevice, &memProps);
            for (uint32 i = 0; i < memProps.memoryHeapCount; i++) {
                heapSize += (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? 
                    memProps.memoryHeaps[i].size : 0;
            }
        }

        const char* gpuType;
        switch (gVk.deviceProps.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:      gpuType = "DISCRETE"; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:    gpuType = "INTEGRATED"; break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:               gpuType = "CPU"; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:       gpuType = "VIRTUAL"; break;
        default:                                        gpuType = "UnknownType";    break;
        }
        logInfo("(init) GPU: %s (%s)", gVk.deviceProps.deviceName, gpuType);
        logInfo("(init) GPU memory: %_$$$llu", heapSize);

        uint32 major = VK_API_VERSION_MAJOR(gVk.deviceProps.apiVersion);
        uint32 minor = VK_API_VERSION_MINOR(gVk.deviceProps.apiVersion);
        logInfo("(init) GPU driver vulkan version: %u.%u", major, minor);

        if (major == 1) {
            switch (minor) {
            case 0:     gVk.apiVersion = GfxApiVersion::Vulkan_1_0; break;
            case 1:     gVk.apiVersion = GfxApiVersion::Vulkan_1_1; break;
            case 2:     gVk.apiVersion = GfxApiVersion::Vulkan_1_2; break;
            case 3:     gVk.apiVersion = GfxApiVersion::Vulkan_1_3; break;
            default:    gVk.apiVersion = GfxApiVersion::_Vulkan;    ASSERT_MSG(0, "Unknown api version. update code"); break;
            }
        }

        // VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES got introduced in vk1.2
        // https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/4818
        if (gfxHasVulkanVersion(GfxApiVersion::Vulkan_1_2) && gfxHasInstanceExtension("VK_KHR_get_physical_device_properties2")) {
            gVk.deviceProps11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
            VkPhysicalDeviceProperties2 props2 {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                .pNext = &gVk.deviceProps11
            };

            gVk.deviceProps12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
            gVk.deviceProps11.pNext = &gVk.deviceProps12;

            VkExtensionApi::vkGetPhysicalDeviceProperties2KHR = (PFN_vkGetPhysicalDeviceProperties2KHR)
                vkGetInstanceProcAddr(gVk.instance, "vkGetPhysicalDeviceProperties2KHR");
            VkExtensionApi::vkGetPhysicalDeviceProperties2KHR(gVk.physicalDevice, &props2);
            
            logInfo("(init) GPU driver: %s - %s", gVk.deviceProps12.driverName, gVk.deviceProps12.driverInfo);
            logInfo("(init) GPU driver conformance version: %d.%d.%d-%d", 
                gVk.deviceProps12.conformanceVersion.major,
                gVk.deviceProps12.conformanceVersion.minor,
                gVk.deviceProps12.conformanceVersion.subminor,
                gVk.deviceProps12.conformanceVersion.patch);
        }

        // Get device features based on the vulkan API
        if (gfxHasVulkanVersion(GfxApiVersion::Vulkan_1_1)) {
            gVk.deviceFeatures11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
            VkPhysicalDeviceFeatures2 features2 {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                .pNext = &gVk.deviceFeatures11
            };
            
            if (gfxHasVulkanVersion(GfxApiVersion::Vulkan_1_2)) {
                gVk.deviceFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
                gVk.deviceFeatures11.pNext = &gVk.deviceFeatures12;
            }

            vkGetPhysicalDeviceFeatures2(gVk.physicalDevice, &features2);
            gVk.deviceFeatures = features2.features;
        }
        else {
            vkGetPhysicalDeviceFeatures(gVk.physicalDevice, &gVk.deviceFeatures);
        }
    }

    //------------------------------------------------------------------------
    // Device extensions
    uint32 numDevExtensions;
    vkEnumerateDeviceExtensionProperties(gVk.physicalDevice, nullptr, &numDevExtensions, nullptr);
    if (numDevExtensions > 0) {
        gVk.numDeviceExtensions = numDevExtensions;
        gVk.deviceExtensions = memAllocTyped<VkExtensionProperties>(numDevExtensions, initHeap);
        vkEnumerateDeviceExtensionProperties(gVk.physicalDevice, nullptr, &numDevExtensions, gVk.deviceExtensions);

        if (settings.listExtensions) {
            logVerbose("Device Extensions (%u):", gVk.numDeviceExtensions);
            for (uint32 i = 0; i < numDevExtensions; i++) {
                logVerbose("\t%s", gVk.deviceExtensions[i].extensionName);
            }
        }
    }

    //------------------------------------------------------------------------
    // Logical device and Queues
    StaticArray<VkDeviceQueueCreateInfo, 4> queueCreateInfos;
    if (!settings.headless) {
        // Avoid creating queues that are in the same family
        const float queuePriority = 1.0f;
        const uint32 queueFamilyIndexes[] = {gfxQueueFamilyIdx, presentQueueFamilyIdx};
        for (uint32 i = 0; i < CountOf(queueFamilyIndexes); i++) {
            uint32 queueIndex = queueFamilyIndexes[i];
            bool duplicateIndex = false;
            for (uint32 k = i; k-- > 0;) {
                if (queueIndex == queueFamilyIndexes[k]) {
                    duplicateIndex = true;
                    break;
                }
            }

            if (!duplicateIndex) {
                VkDeviceQueueCreateInfo queueCreateInfo {};
                queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueCreateInfo.queueFamilyIndex = queueIndex;
                queueCreateInfo.queueCount = 1;
                queueCreateInfo.pQueuePriorities = &queuePriority;
                queueCreateInfos.Add(queueCreateInfo);
            }
        }
    }

    //------------------------------------------------------------------------
    // Device Extensions that we need
    gVk.hasAstcDecodeMode = gfxHasDeviceExtension("VK_EXT_astc_decode_mode");
    gVk.hasMemoryBudget = gfxHasDeviceExtension("VK_EXT_memory_budget");

    gVk.hasHostQueryReset = gfxHasDeviceExtension("VK_EXT_host_query_reset");
    if (gfxHasVulkanVersion(GfxApiVersion::Vulkan_1_2) && !gVk.deviceFeatures12.hostQueryReset)
        gVk.hasHostQueryReset = false;

    gVk.hasFloat16Support = gfxHasDeviceExtension("VK_KHR_shader_float16_int8");
    if (gfxHasVulkanVersion(GfxApiVersion::Vulkan_1_2) && !gVk.deviceFeatures12.shaderFloat16)
        gVk.hasFloat16Support = false;

    gVk.hasDescriptorIndexing = gfxHasDeviceExtension("VK_EXT_descriptor_indexing");

    StaticArray<const char*, 32> enabledDeviceExtensions;
    if (!settings.headless) {
        if (gfxHasDeviceExtension("VK_KHR_swapchain"))
            enabledDeviceExtensions.Add("VK_KHR_swapchain");
        if (gVk.hasAstcDecodeMode)
            enabledDeviceExtensions.Add("VK_EXT_astc_decode_mode");
    }

    #ifdef TRACY_ENABLE
        if (gfxHasDeviceExtension(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME)) {
            enabledDeviceExtensions.Add(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
        }
    #endif

    if (settings.shaderDumpProperties && 
        gfxHasDeviceExtension("VK_KHR_pipeline_executable_properties") &&
        gfxHasInstanceExtension("VK_KHR_get_physical_device_properties2"))
    {
        gVk.hasPipelineExecutableProperties = true;
        enabledDeviceExtensions.Add("VK_KHR_pipeline_executable_properties");

        VkExtensionApi::vkGetPipelineExecutablePropertiesKHR = (PFN_vkGetPipelineExecutablePropertiesKHR)
            vkGetInstanceProcAddr(gVk.instance, "vkGetPipelineExecutablePropertiesKHR");
        VkExtensionApi::vkGetPipelineExecutableStatisticsKHR = (PFN_vkGetPipelineExecutableStatisticsKHR)
            vkGetInstanceProcAddr(gVk.instance, "vkGetPipelineExecutableStatisticsKHR");
        VkExtensionApi::vkGetPipelineExecutableInternalRepresentationsKHR = (PFN_vkGetPipelineExecutableInternalRepresentationsKHR)
            vkGetInstanceProcAddr(gVk.instance, "vkGetPipelineExecutableInternalRepresentationsKHR");
    }

    if (gVk.hasMemoryBudget)
        enabledDeviceExtensions.Add("VK_EXT_memory_budget");
    if (gVk.hasHostQueryReset) {
        enabledDeviceExtensions.Add("VK_EXT_host_query_reset");
        VkExtensionApi::vkResetQueryPoolEXT = (PFN_vkResetQueryPoolEXT)vkGetInstanceProcAddr(gVk.instance, "vkResetQueryPoolEXT");
    }
    if (gVk.hasFloat16Support)
        enabledDeviceExtensions.Add("VK_KHR_shader_float16_int8");
    if (gVk.hasDescriptorIndexing)
        enabledDeviceExtensions.Add("VK_EXT_descriptor_indexing");

    // Enabled layers
    VkDeviceCreateInfo devCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = queueCreateInfos.Count(),
        .pQueueCreateInfos = queueCreateInfos.Ptr(),
        .enabledLayerCount = enabledLayers.Count(),
        .ppEnabledLayerNames = enabledLayers.Ptr(),
        .enabledExtensionCount = enabledDeviceExtensions.Count(),
        .ppEnabledExtensionNames = enabledDeviceExtensions.Ptr(),
        .pEnabledFeatures = &gVk.deviceFeatures,
    };

    // Fill device pNext chain
    void** deviceNext = const_cast<void**>(&devCreateInfo.pNext);
    VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR enableExecProps {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR,
        .pipelineExecutableInfo = VK_TRUE
    };
    if (settings.shaderDumpProperties && gVk.hasPipelineExecutableProperties) {
        *deviceNext = &enableExecProps;
        deviceNext = &enableExecProps.pNext;
    }
    
    VkPhysicalDeviceHostQueryResetFeatures enableHostReset {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
        .hostQueryReset = true
    };
    if (gVk.hasHostQueryReset) {
        *deviceNext = &enableHostReset;
        deviceNext = &enableHostReset.pNext;
    }

    VkPhysicalDeviceDescriptorIndexingFeatures enableDescriptorIndexing {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT,
        .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
        .descriptorBindingVariableDescriptorCount = VK_TRUE,
        .runtimeDescriptorArray = VK_TRUE
    };
    if (gVk.hasDescriptorIndexing) {
        *deviceNext = &enableDescriptorIndexing;
        deviceNext = &enableDescriptorIndexing.pNext;
    }

    if (enabledDeviceExtensions.Count()) {
        logVerbose("Enabled device extensions:");
        for (const char* ext : enabledDeviceExtensions) {
            logVerbose("\t%s", ext);
        }
    }

    if (vkCreateDevice(gVk.physicalDevice, &devCreateInfo, &gVk.allocVk, &gVk.device) != VK_SUCCESS) {
        logError("Gfx: vkCreateDevice failed");
        return false;
    }

    logInfo("(init) Vulkan device created");
    volkLoadDevice(gVk.device);

    //------------------------------------------------------------------------
    // VMA
    {
        static const VmaVulkanFunctions vmaFuncs {
            .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
            .vkGetDeviceProcAddr = vkGetDeviceProcAddr
        };

        uint32 vulkanApiVersion = 0;
        switch (gVk.apiVersion) {
        case GfxApiVersion::Vulkan_1_0: vulkanApiVersion = VK_API_VERSION_1_0; break;
        case GfxApiVersion::Vulkan_1_1: vulkanApiVersion = VK_API_VERSION_1_1; break;
        case GfxApiVersion::Vulkan_1_2: vulkanApiVersion = VK_API_VERSION_1_2; break;
        case GfxApiVersion::Vulkan_1_3: vulkanApiVersion = VK_API_VERSION_1_3; break;
        default: ASSERT(0);
        }

        VmaAllocatorCreateInfo vmaCreateInfo {
            .flags = gVk.hasMemoryBudget ? VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT : 0u,
            .physicalDevice = gVk.physicalDevice,
            .device = gVk.device,
            .pAllocationCallbacks = &gVk.allocVk,
            .pDeviceMemoryCallbacks = nullptr, // TODO: can be used to hook memory calls
            .pVulkanFunctions = &vmaFuncs,
            .instance = gVk.instance,
            .vulkanApiVersion = vulkanApiVersion
        };

        if (vmaCreateAllocator(&vmaCreateInfo, &gVk.vma) != VK_SUCCESS) {
            logError("Gfx: Creating VMA allocator failed");
            return false;
        }
    }

    //------------------------------------------------------------------------
    // Graphics/Present Queue
    if (!settings.headless) {
        ASSERT(gfxQueueFamilyIdx != UINT32_MAX);
        vkGetDeviceQueue(gVk.device, gfxQueueFamilyIdx, 0, &gVk.gfxQueue);
        ASSERT_ALWAYS(gVk.gfxQueue != VK_NULL_HANDLE, "vkGetDeviceQueue failed");

        ASSERT(presentQueueFamilyIdx != UINT32_MAX);
        vkGetDeviceQueue(gVk.device, presentQueueFamilyIdx, 0, &gVk.presentQueue);
        ASSERT_ALWAYS(gVk.presentQueue != VK_NULL_HANDLE, "vkGetDeviceQueue failed");

        gVk.gfxQueueFamilyIndex = gfxQueueFamilyIdx;
        gVk.presentQueueFamilyIndex = presentQueueFamilyIdx;
    }

    // Deferred Command Buffer (for DMA / Barriers)
    gVk.deferredCommandsMtx.Initialize();
    gVk.deferredCmds.SetAllocator(&gVk.alloc);
    gVk.deferredCmdBuffer.SetAllocator(&gVk.alloc);
    gVk.deferredCmdBuffer.SetGrowPolicy(Blob::GrowPolicy::Linear);

    //------------------------------------------------------------------------
    // SwapChain support and capabilities
    if (!settings.headless) {
        ASSERT(gVk.surface);

        uint32 numFormats;
        uint32 numPresentModes;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gVk.physicalDevice, gVk.surface, &gVk.swapchainSupport.caps);

        // Take care of possible swapchain transform, specifically on android!
        // https://android-developers.googleblog.com/2020/02/handling-device-orientation-efficiently.html
        # if PLATFORM_ANDROID
            const VkSurfaceCapabilitiesKHR& swapchainCaps = gVk.swapchainSupport.caps;
            if (swapchainCaps.currentTransform & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR)
                appAndroidSetFramebufferTransform(AppFramebufferTransform::Rotate90);
            if (swapchainCaps.currentTransform & VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR)
                appAndroidSetFramebufferTransform(AppFramebufferTransform::Rotate180);
            if (swapchainCaps.currentTransform & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR)
                appAndroidSetFramebufferTransform(AppFramebufferTransform::Rotate270);
        #endif
        
        vkGetPhysicalDeviceSurfaceFormatsKHR(gVk.physicalDevice, gVk.surface, &numFormats, nullptr);
        gVk.swapchainSupport.numFormats = numFormats;
        gVk.swapchainSupport.formats = memAllocTyped<VkSurfaceFormatKHR>(numFormats, initHeap);
        vkGetPhysicalDeviceSurfaceFormatsKHR(gVk.physicalDevice, gVk.surface, &numFormats, gVk.swapchainSupport.formats);

        vkGetPhysicalDeviceSurfacePresentModesKHR(gVk.physicalDevice, gVk.surface, &numPresentModes, nullptr);
        gVk.swapchainSupport.numPresentModes = numPresentModes;
        gVk.swapchainSupport.presentModes = memAllocTyped<VkPresentModeKHR>(numPresentModes, initHeap);
        vkGetPhysicalDeviceSurfacePresentModesKHR(gVk.physicalDevice, gVk.surface, &numPresentModes, 
            gVk.swapchainSupport.presentModes);

        gVk.swapchain = gfxCreateSwapchain(gVk.surface, appGetFramebufferWidth(), appGetFramebufferHeight(), nullptr, true);
    }

    //------------------------------------------------------------------------
    // Synchronization
    VkSemaphoreCreateInfo semaphoreCreateInfo {};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceCreateInfo {};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32 i = 0; i < kMaxFramesInFlight; i++) {
        if (VK_FAILED(vkCreateSemaphore(gVk.device, &semaphoreCreateInfo, &gVk.allocVk, &gVk.imageAvailSemaphores[i])) ||
            VK_FAILED(vkCreateSemaphore(gVk.device, &semaphoreCreateInfo, &gVk.allocVk, &gVk.renderFinishedSemaphores[i])))
        {
            logError("Gfx: vkCreateSemaphore failed");
            return false;
        }

        if (VK_FAILED(vkCreateFence(gVk.device, &fenceCreateInfo, &gVk.allocVk, &gVk.inflightFences[i]))) {
            logError("Gfx: vkCreateFence failed");
            return false;
        }
    }

    {   // Descriptor pool
        // TODO: add more pools when the maximum limits is exceeded

        GfxBudgetStats::DescriptorBudgetStats& descStats = gVk.descriptorStats;
        descStats.maxUniformBuffers = 128;  // TODO
        descStats.maxDynUniformBuffers = 32;    
        descStats.maxSamplers = 128;
        descStats.maxSampledImages = 128;
        descStats.maxCombinedImageSamplers = 128;

        VkDescriptorPoolSize poolSizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = descStats.maxUniformBuffers,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = descStats.maxDynUniformBuffers,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = descStats.maxSampledImages,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = descStats.maxCombinedImageSamplers,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = descStats.maxSamplers,
        }
        };
        
        VkDescriptorPoolCreateInfo poolInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
            .maxSets = _limits::kGfxMaxDescriptorSets,
            .poolSizeCount = sizeof(poolSizes)/sizeof(VkDescriptorPoolSize),
            .pPoolSizes = poolSizes
        };
        
        if (vkCreateDescriptorPool(gVk.device, &poolInfo, &gVk.allocVk, &gVk.descriptorPool) != VK_SUCCESS) {
            logError("Gfx: Create descriptor pool failed");
            return false;
        }
    }    

    // shader <-> pipeline management
    gVk.shaderPipelinesTableMtx.Initialize();

    gVk.shaderPipelinesTable.SetAllocator(&gVk.alloc);
    gVk.shaderPipelinesTable.Reserve(64);

    // Garbage collector
    gVk.garbageMtx.Initialize();
    {
        size_t bufferSize = Array<GfxGarbage>::GetMemoryRequirement(_limits::kGfxMaxGarbage);
        gVk.garbage.Reserve(_limits::kGfxMaxGarbage, memAlloc(bufferSize, initHeap), bufferSize);
    }

    logInfo("(init) Gfx initialized");

    //------------------------------------------------------------------------
    // Graphics Sub-systems/manager
    if (!gfxInitializeImageManager()) {
        logError("Gfx: Initializing image manager failed");
        return false;
    }
    logInfo("(init) Gfx image manager");

    //------------------------------------------------------------------------
    // Profiling
    #ifdef TRACY_ENABLE
        if (settings.enableGpuProfile) {
            if (!gfxInitializeProfiler()) {
                logError("Initializing GPU profiler failed");
                return false;
            }
        }
    #endif
    
    if (gVk.deviceProps.limits.timestampComputeAndGraphics && !settings.headless) {
        VkQueryPoolCreateInfo queryCreateInfo {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = 2
        };
        for (uint32 i = 0; i < kMaxFramesInFlight; i++) {
            if (vkCreateQueryPool(gVk.device, &queryCreateInfo, &gVk.allocVk, &gVk.queryPool[i]) != VK_SUCCESS) {
                logError("Gfx: Creating main query pool failed");
                return false;
            }
        }
    }

    gVk.initHeapSize = initHeap->GetOffset() - gVk.initHeapStart;
    gfxGetPhysicalDeviceProperties();       // call once just to populate the struct
    gVk.initialized = true;

    logVerbose("(init) Graphics initialized (%.1f ms)", stopwatch.ElapsedMS());
    return true;
}

void GfxObjectPools::Initialize()
{
    Allocator* initHeap = engineGetInitHeap();

    {
        size_t poolSize = HandlePool<GfxBuffer, GfxBufferData>::GetMemoryRequirement(_limits::kGfxMaxBuffers);
        buffers.Reserve(_limits::kGfxMaxBuffers, memAlloc(poolSize, initHeap), poolSize);
    }

    {
        size_t poolSize = HandlePool<GfxImage, GfxImageData>::GetMemoryRequirement(_limits::kGfxMaxImages);
        images.Reserve(_limits::kGfxMaxImages, memAlloc(poolSize, initHeap), poolSize);
    }

    {
        size_t poolSize = HandlePool<GfxDescriptorSet, GfxDescriptorSetData>::GetMemoryRequirement(_limits::kGfxMaxDescriptorSets);
        descriptorSets.Reserve(_limits::kGfxMaxDescriptorSets, memAlloc(poolSize, initHeap), poolSize);
    }

    {
        size_t poolSize = HandlePool<GfxDescriptorSetLayout, GfxDescriptorSetLayoutData>::GetMemoryRequirement(_limits::kGfxMaxDescriptorSetLayouts);
        descriptorSetLayouts.Reserve(_limits::kGfxMaxDescriptorSetLayouts, memAlloc(poolSize, initHeap), poolSize);
    }

    {
        size_t poolSize = HandlePool<GfxPipeline, GfxPipelineData>::GetMemoryRequirement(_limits::kGfxMaxPipelines);
        pipelines.Reserve(_limits::kGfxMaxPipelines, memAlloc(poolSize, initHeap), poolSize);
    }

    {
        size_t poolSize = HandlePool<GfxPipelineLayout, GfxPipelineLayoutData>::GetMemoryRequirement(_limits::kGfxMaxPipelineLayouts);
        pipelineLayouts.Reserve(_limits::kGfxMaxPipelineLayouts, memAlloc(poolSize, initHeap), poolSize);
    }
}

static void gfxCollectGarbage(bool force)
{
    uint64 frameIdx = engineFrameIndex();
    const uint32 numFramesToWait = kMaxFramesInFlight;

    for (uint32 i = 0; i < gVk.garbage.Count();) {
        const GfxGarbage& garbage = gVk.garbage[i];
        if (force || frameIdx > (garbage.frameIdx + numFramesToWait)) {
            switch (garbage.type) {
            case GfxGarbage::Type::Pipeline:
                vkDestroyPipeline(gVk.device, garbage.pipeline, &gVk.allocVk);
                break;
            case GfxGarbage::Type::Buffer:
                vmaDestroyBuffer(gVk.vma, garbage.buffer, garbage.allocation);
                break;
            default:
                break;
            }

            gVk.garbage.RemoveAndSwap(i);
            continue;
        }

        ++i;
    }
}

void _private::gfxRelease()
{
    if (gVk.instance == VK_NULL_HANDLE)
        return;

    if (gVk.device)
        vkDeviceWaitIdle(gVk.device);

    gfxCollectGarbage(true);

    #ifdef TRACY_ENABLE
       gfxReleaseProfiler();
    #endif
    for (uint32 i = 0; i < kMaxFramesInFlight; i++) {
        if (gVk.queryPool[i])
            vkDestroyQueryPool(gVk.device, gVk.queryPool[i], &gVk.allocVk);
    }

    { MutexScope mtx(gVk.shaderPipelinesTableMtx);
        const uint32* keys = gVk.shaderPipelinesTable.Keys();
        for (uint32 i = 0; i < gVk.shaderPipelinesTable.Capacity(); i++) {
            if (keys[i]) {
                gVk.shaderPipelinesTable.GetMutable(i).Free();
            }
        }
    }
    gVk.shaderPipelinesTableMtx.Release();
    gVk.shaderPipelinesTable.Free();
    
    if (gVk.device) {
        vkDestroyDescriptorPool(gVk.device, gVk.descriptorPool, &gVk.allocVk);

        // Release any allocated command buffer pools collected from threads
        for (GfxCommandBufferThreadData* threadData : gVk.initializedThreadData) {
            for (uint32 i = 0; i < kMaxFramesInFlight; i++) {
                vkDestroyCommandPool(gVk.device, threadData->commandPools[i], &gVk.allocVk);
                threadData->freeLists[i].Free();
                threadData->cmdBuffers[i].Free();
            }
            memset(threadData, 0x0, sizeof(*threadData));
        }

        for (uint32 i = 0; i < kMaxFramesInFlight; i++) {
            if (gVk.imageAvailSemaphores[i])
                vkDestroySemaphore(gVk.device, gVk.imageAvailSemaphores[i], &gVk.allocVk);
            if (gVk.renderFinishedSemaphores[i])
                vkDestroySemaphore(gVk.device, gVk.renderFinishedSemaphores[i], &gVk.allocVk);
            if (gVk.inflightFences[i])
                vkDestroyFence(gVk.device, gVk.inflightFences[i], &gVk.allocVk);
        }
    }    

    gVk.deferredCommandsMtx.Release();
    gVk.deferredCmds.Free();
    gVk.deferredCmdBuffer.Free();

    gfxDestroySwapchain(&gVk.swapchain);
    if (gVk.surface) 
        vkDestroySurfaceKHR(gVk.instance, gVk.surface, &gVk.allocVk);
    
    gVk.pools.DetectAndReleaseLeaks();
    vmaDestroyAllocator(gVk.vma);

    if (gVk.device)  
        vkDestroyDevice(gVk.device, &gVk.allocVk);
    if (gVk.debugMessenger) 
        VkExtensionApi::vkDestroyDebugUtilsMessengerEXT(gVk.instance, gVk.debugMessenger, &gVk.allocVk);
    if (gVk.debugReportCallback) 
        VkExtensionApi::vkDestroyDebugReportCallbackEXT(gVk.instance, gVk.debugReportCallback, &gVk.allocVk);

    vkDestroyInstance(gVk.instance, &gVk.allocVk);

    gVk.pools.Release();
    gVk.tlsfAlloc.Release();
    gVk.runtimeAlloc.SetAllocator(nullptr);
}

void GfxObjectPools::DetectAndReleaseLeaks()
{
    auto PrintStacktrace = [](const char* resourceName, void* ptr, void* const* stackframes, uint16 numStackframes) 
    {
        DebugStacktraceEntry entries[8];
        debugResolveStacktrace(numStackframes, stackframes, entries);
        logDebug("\t%s: 0x%llx", resourceName, ptr);
        for (uint16 si = 0; si < numStackframes; si++) 
            logDebug("\t\t- %s(%u)", entries[si].filename, entries[si].line);
    };

    [[maybe_unused]] bool trackResourceLeaks = settingsGet().graphics.trackResourceLeaks;

    if (gVk.pools.buffers.Count()) {
        logWarning("Gfx: Total %u buffers are not released. cleaning up...", gVk.pools.buffers.Count());
        for (uint32 i = 0; i < gVk.pools.buffers.Count(); i++) {
            GfxBuffer handle = gVk.pools.buffers.HandleAt(i);
            #if !CONFIG_FINAL_BUILD
                if (trackResourceLeaks) {
                    const GfxBufferData& bufferData = gVk.pools.buffers.Data(handle);
                    PrintStacktrace("Buffer", bufferData.buffer, bufferData.stackframes, bufferData.numStackframes);
                }
            #endif
            gfxDestroyBuffer(handle);
        }
    }

    if (gVk.pools.images.Count()) {
        logWarning("Gfx: Total %u images are not released. cleaning up...", gVk.pools.images.Count());
        for (uint32 i = 0; i < gVk.pools.images.Count(); i++) {
            GfxImage handle = gVk.pools.images.HandleAt(i);
            #if !CONFIG_FINAL_BUILD
                if (trackResourceLeaks) {
                    const GfxImageData& imageData = gVk.pools.images.Data(handle);
                    PrintStacktrace("Image", imageData.image, imageData.stackframes, imageData.numStackframes);
                }
            #endif

            gfxDestroyImage(handle);
        }
    }

    if (gVk.pools.pipelineLayouts.Count()) {
        logWarning("Gfx: Total %u pipeline layout are not released. cleaning up...", gVk.pools.pipelineLayouts.Count());
        for (uint32 i = 0; i < gVk.pools.pipelineLayouts.Count(); i++) {
            GfxPipelineLayout handle = gVk.pools.pipelineLayouts.HandleAt(i);
            #if !CONFIG_FINAL_BUILD
                if (trackResourceLeaks) {
                    const GfxPipelineLayoutData& pipLayout = gVk.pools.pipelineLayouts.Data(handle);
                    PrintStacktrace("PipelineLayout", pipLayout.layout, pipLayout.stackframes, pipLayout.numStackframes);
                }
            #endif

            gfxDestroyPipelineLayout(handle);
        }
    }

    if (gVk.pools.pipelines.Count()) {
        logWarning("Gfx: Total %u pipelines are not released. cleaning up...", gVk.pools.pipelines.Count());
        for (uint32 i = 0; i < gVk.pools.pipelines.Count(); i++) {
            GfxPipeline handle = gVk.pools.pipelines.HandleAt(i);
            #if !CONFIG_FINAL_BUILD
                if (trackResourceLeaks) {
                    const GfxPipelineData& pipData = gVk.pools.pipelines.Data(handle);
                    PrintStacktrace("Pipeline", pipData.pipeline, pipData.stackframes, pipData.numStackframes);
                }
            #endif
            gfxDestroyPipeline(handle);
        }
    }

    if (gVk.pools.descriptorSets.Count()) {
        logWarning("Gfx: Total %u descriptor sets are not released. cleaning up...", gVk.pools.descriptorSets.Count());
        for (uint32 i = 0; i < gVk.pools.descriptorSets.Count(); i++) {
            GfxDescriptorSet handle = gVk.pools.descriptorSets.HandleAt(i);
            #if !CONFIG_FINAL_BUILD
                if (trackResourceLeaks) {
                    const GfxDescriptorSetData& dsData = gVk.pools.descriptorSets.Data(handle);
                    PrintStacktrace("DescriptorSet", dsData.descriptorSet, dsData.stackframes, dsData.numStackframes);
                }
            #endif

            gfxDestroyDescriptorSet(handle);
        }
    }

    if (gVk.pools.descriptorSetLayouts.Count()) {
        logWarning("Gfx: Total %u descriptor sets layouts are not released. cleaning up...", gVk.pools.descriptorSetLayouts.Count());
        for (uint32 i = 0; i < gVk.pools.descriptorSetLayouts.Count(); i++) {
            GfxDescriptorSetLayout handle = gVk.pools.descriptorSetLayouts.HandleAt(i);
            #if !CONFIG_FINAL_BUILD
                if (trackResourceLeaks) {
                    const GfxDescriptorSetLayoutData& dsLayoutData = gVk.pools.descriptorSetLayouts.Data(handle);
                    PrintStacktrace("DescriptorSetLayout", dsLayoutData.layout, dsLayoutData.stackframes, dsLayoutData.numStackframes);
                }
            #endif

            gfxDestroyDescriptorSetLayout(handle);
        }
    }
}

void GfxObjectPools::Release()
{
    for (GfxDescriptorSetLayoutData& layout : gVk.pools.descriptorSetLayouts) 
        memFree(layout.bindings, &gVk.alloc);

    gVk.pools.buffers.Free();
    gVk.pools.images.Free();
    gVk.pools.pipelineLayouts.Free();
    gVk.pools.pipelines.Free();
    gVk.pools.descriptorSets.Free();
    gVk.pools.descriptorSetLayouts.Free();
}

void gfxResizeSwapchain(uint16 width, uint16 height)
{
    if (gVk.device)
        vkDeviceWaitIdle(gVk.device);
    
    gfxDestroySwapchain(&gVk.swapchain);

    uint32 oldWidth = gVk.swapchain.extent.width;
    uint32 oldHeight = gVk.swapchain.extent.height;
    
    gVk.swapchain = gfxCreateSwapchain(gVk.surface, width, height, nullptr, true);
    logDebug("Swapchain resized from %ux%u to %ux%u", oldWidth, oldHeight, width, height);

    if (gVk.device)
        vkDeviceWaitIdle(gVk.device);
}

void gfxDestroySurfaceAndSwapchain()
{
    if (gVk.device)
        vkDeviceWaitIdle(gVk.device);

    gfxDestroySwapchain(&gVk.swapchain);

    if (gVk.surface) {
        vkDestroySurfaceKHR(gVk.instance, gVk.surface, &gVk.allocVk);
        gVk.surface = nullptr;
    }
}

void gfxRecreateSurfaceAndSwapchain()
{
    if (gVk.device)
        vkDeviceWaitIdle(gVk.device);

    if (gVk.surface) 
        vkDestroySurfaceKHR(gVk.instance, gVk.surface, &gVk.allocVk);
    
    gVk.surface = gfxCreateWindowSurface(appGetNativeWindowHandle());
    ASSERT(gVk.surface);

    gfxDestroySwapchain(&gVk.swapchain);
    gVk.swapchain = gfxCreateSwapchain(gVk.surface, appGetFramebufferWidth(), appGetFramebufferHeight(), nullptr, true);

    if (gVk.device)
        vkDeviceWaitIdle(gVk.device);

    logDebug("Window surface (Handle = 0x%x) and swapchain (%ux%u) recreated.", 
             appGetNativeWindowHandle(), appGetFramebufferWidth(), appGetFramebufferHeight());
}

// Note: must be protected
static void gfxSubmitDeferredCommands()
{
    { MutexScope mtx(gVk.deferredCommandsMtx);
        if (gVk.deferredCmds.Count()) {
            gfxBeginCommandBuffer();
            ASSERT(gCmdBufferThreadData.curCmdBuffer != VK_NULL_HANDLE);
            VkCommandBuffer cmdBuffer = gCmdBufferThreadData.curCmdBuffer;
            Blob* paramsBlob = &gVk.deferredCmdBuffer;
            for (const GfxDeferredCommand& cmd : gVk.deferredCmds) {
                paramsBlob->SetOffset(cmd.paramsOffset);
                ASSERT(paramsBlob->ReadOffset() + cmd.paramsSize <= paramsBlob->Size());
                cmd.executeFn(cmdBuffer, *paramsBlob);
            }
            
            gVk.deferredCmds.Clear();
            gVk.deferredCmdBuffer.Reset();
            gfxEndCommandBuffer();
        }
    }
}

void gfxBeginFrame()
{
    PROFILE_ZONE(true);

    if (gVk.hasMemoryBudget) {
        ASSERT(engineFrameIndex() < UINT32_MAX);
        vmaSetCurrentFrameIndex(gVk.vma, uint32(engineFrameIndex()));
    }

    { PROFILE_ZONE_NAME("WaitForFence", true);
        vkWaitForFences(gVk.device, 1, &gVk.inflightFences[gVk.currentFrameIdx], VK_TRUE, UINT64_MAX);
    }

    gfxSubmitDeferredCommands();

    uint32 frameIdx = gVk.currentFrameIdx;
    uint32 imageIdx;

    { PROFILE_ZONE_NAME("AcquireNextImage", true);
        VkResult nextImageResult = vkAcquireNextImageKHR(gVk.device, gVk.swapchain.swapchain, UINT64_MAX,
                                                        gVk.imageAvailSemaphores[frameIdx],
                                                        VK_NULL_HANDLE, &imageIdx);
        if (nextImageResult == VK_ERROR_OUT_OF_DATE_KHR) {
            logDebug("Out-of-date swapchain: Recreating");
            gfxResizeSwapchain(appGetFramebufferWidth(), appGetFramebufferHeight());
        }
        else if (nextImageResult != VK_SUCCESS && nextImageResult != VK_SUBOPTIMAL_KHR) {
            ASSERT_MSG(false, "Gfx: Acquire swapchain failed: %d", nextImageResult);
            return;
        }
    }

    gVk.swapchain.imageIdx = imageIdx;
}

void gfxEndFrame()
{
    ASSERT_MSG(gVk.swapchain.imageIdx != UINT32_MAX, "gfxBeginFrame is not called");
    ASSERT_MSG(gCmdBufferThreadData.curCmdBuffer == VK_NULL_HANDLE, "Graphics should not be in recording state");
    PROFILE_ZONE(true);

    #ifdef TRACY_ENABLE
        if (gfxHasProfileSamples()) {
            gfxBeginCommandBuffer();
            gfxProfileCollectSamples();
            gfxEndCommandBuffer();
        }
    #endif
    
    uint32 frameIdx = gVk.currentFrameIdx;
    uint32 imageIdx = gVk.swapchain.imageIdx;
    VkCommandBuffer* cmdBuffersVk = nullptr;
    uint32 numCmdBuffers = 0;
    MemTempAllocator tmpAlloc;

    // Flip the current index here for other threads (mainly loaders) to submit to next frame
    {   AtomicLockScope lock(gVk.pendingCmdBuffersLock);
        cmdBuffersVk = memAllocCopy<VkCommandBuffer>(gVk.pendingCmdBuffers.Ptr(), gVk.pendingCmdBuffers.Count(), &tmpAlloc);
        numCmdBuffers = gVk.pendingCmdBuffers.Count();
        gVk.pendingCmdBuffers.Clear();
    }

    gVk.prevFrameIdx = frameIdx;
    atomicStore32Explicit(&gVk.currentFrameIdx, (frameIdx + 1) % kMaxFramesInFlight, AtomicMemoryOrder::Release);

    //------------------------------------------------------------------------
    // Submit last command-buffers + draw to swpachain framebuffer
    { PROFILE_ZONE_NAME("SubmitLast", true); 
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    
        VkSubmitInfo submitInfo {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &gVk.imageAvailSemaphores[frameIdx],
            .pWaitDstStageMask = &waitStage,
            .commandBufferCount = numCmdBuffers,
            .pCommandBuffers = cmdBuffersVk,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &gVk.renderFinishedSemaphores[frameIdx],
        };
        
        if (gVk.inflightImageFences[imageIdx] != VK_NULL_HANDLE)
            vkWaitForFences(gVk.device, 1, &gVk.inflightImageFences[imageIdx], VK_TRUE, UINT64_MAX);
        gVk.inflightImageFences[imageIdx] = gVk.inflightFences[frameIdx];
        
        vkResetFences(gVk.device, 1, &gVk.inflightFences[frameIdx]);
        if (vkQueueSubmit(gVk.gfxQueue, 1, &submitInfo, gVk.inflightFences[frameIdx]) != VK_SUCCESS) {
            ASSERT_MSG(0, "Gfx: Submitting graphics queue failed");
            return;
        }
    }
    
    //------------------------------------------------------------------------
    // Present Swapchain
    ASSERT_MSG(gVk.swapchain.imageIdx != UINT32_MAX, "gfxBeginFrame is not called");
    { PROFILE_ZONE_NAME("Present", true);
        VkPresentInfoKHR presentInfo {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &gVk.renderFinishedSemaphores[frameIdx],
            .swapchainCount = 1,
            .pSwapchains = &gVk.swapchain.swapchain,
            .pImageIndices = &imageIdx
        };
        VkResult presentResult = vkQueuePresentKHR(gVk.presentQueue, &presentInfo);
        
        // TODO: On mac we are getting VK_SUBOPTIMAL_KHR. Investigate why
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR/* || presentResult == VK_SUBOPTIMAL_KHR*/) {
            logDebug("Resized/Invalidated swapchain: Recreate");
            gfxResizeSwapchain(appGetFramebufferWidth(), appGetFramebufferHeight());
        }
        else if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR) {
            ASSERT_ALWAYS(false, "Gfx: Present swapchain failed");
            return;
        }
    }

    gVk.swapchain.imageIdx = UINT32_MAX;
    gfxCollectGarbage(false);
}

// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html#usage_patterns_advanced_data_uploading
GfxBuffer gfxCreateBuffer(const GfxBufferDesc& desc)
{
    ASSERT(desc.size);

    uint32 usageFlags = 0;
    switch (desc.type) {
    case GfxBufferType::Vertex:     usageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;    break;
    case GfxBufferType::Index:      usageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;     break;
    case GfxBufferType::Uniform:    usageFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;   break;
    default:                        ASSERT_MSG(0, "Invalid buffer type");                      break;
    }

    uint32 vmaFlags = 0;
    if (desc.usage == GfxBufferUsage::Stream) {
        vmaFlags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | 
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    GfxBufferUsage memUsage = desc.usage == GfxBufferUsage::Default ? GfxBufferUsage::Immutable : desc.usage;
    GfxBufferData bufferData {
        .type = desc.type,
        .memUsage = memUsage,
        .size = desc.size
    };
    
    // We always want to use the buffer as transfer destination for non-stream 
    // TODO: revisit the part about excluding integrated GPU from TRANSFER_DST
    if (memUsage != GfxBufferUsage::Stream || gVk.deviceProps.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkBufferCreateInfo bufferCreateInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = desc.size,
        .usage = usageFlags,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    VmaAllocationCreateInfo allocCreateInfo {
        .flags = vmaFlags,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VmaAllocationInfo allocInfo;
    if (vmaCreateBuffer(gVk.vma, &bufferCreateInfo, &allocCreateInfo, &bufferData.buffer, 
                        &bufferData.allocation, &allocInfo) != VK_SUCCESS)
    {
        ASSERT_MSG(0, "Create buffer failed");
        return GfxBuffer();
    }

    vmaGetAllocationMemoryProperties(gVk.vma, bufferData.allocation, &bufferData.memFlags);

    if (desc.usage == GfxBufferUsage::Immutable) {
        // For Immutable buffers, we should provide content data and fill it out. So we have an extra staging copy phase
        ASSERT_MSG(desc.content != nullptr, "Must provide content data for immutable buffers");

        if (bufferData.memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            memcpy(allocInfo.pMappedData, desc.content, desc.size);
        }
        else {
            VkBufferCreateInfo stageBufferCreateInfo {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = desc.size,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
            };

            VmaAllocationCreateInfo stageAllocCreateInfo {
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO,
            };

            VkBuffer stagingBuffer;
            VmaAllocation stagingAlloc;
            if (vmaCreateBuffer(gVk.vma, &stageBufferCreateInfo, &stageAllocCreateInfo, &stagingBuffer, &stagingAlloc, &allocInfo) != VK_SUCCESS) {
                vmaDestroyBuffer(gVk.vma, bufferData.buffer, bufferData.allocation);
                ASSERT_MSG(0, "Create staging buffer failed");
                return GfxBuffer();
            }

            memcpy(allocInfo.pMappedData, desc.content, desc.size);
            vmaFlushAllocation(gVk.vma, stagingAlloc, 0, VK_WHOLE_SIZE);

            gfxBeginDeferredCommandBuffer();
            VkBufferCopy copyRegion {
                .srcOffset = 0,
                .dstOffset = 0,
                .size = desc.size
            };
            gfxCmdCopyBuffer(stagingBuffer, bufferData.buffer, 1, &copyRegion);
            gfxEndDeferredCommandBuffer();

            MutexScope mtxGarbage(gVk.garbageMtx);
            gVk.garbage.Push(GfxGarbage {
                .type = GfxGarbage::Type::Buffer,
                .frameIdx = engineFrameIndex(),
                .buffer = stagingBuffer,
                .allocation = stagingAlloc
            });
        }
    }
    else if (desc.usage == GfxBufferUsage::Stream) {
        // Stream buffers are either mapped persistent or the device doesn't support it. so in that case, we have to create staging buffer
        if ((bufferData.memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            VkBufferCreateInfo stageBufferCreateInfo {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = desc.size,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
            };

            VmaAllocationCreateInfo stageAllocCreateInfo {
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO,
            };
            
            if (vmaCreateBuffer(gVk.vma, &stageBufferCreateInfo, &stageAllocCreateInfo, &bufferData.stagingBuffer, 
                                &bufferData.stagingAllocation, &allocInfo) != VK_SUCCESS)
            {
                vmaDestroyBuffer(gVk.vma, bufferData.buffer, bufferData.allocation);
                ASSERT_MSG(0, "Create staging buffer failed");
                return GfxBuffer();
            }

            bufferData.mappedBuffer = allocInfo.pMappedData;
        }
        else {
            bufferData.mappedBuffer = allocInfo.pMappedData;
        }
    }
    else {
        ASSERT_MSG(0, "Not Implemented");
    }

    #if !CONFIG_FINAL_BUILD
        if (settingsGet().graphics.trackResourceLeaks)
            bufferData.numStackframes = debugCaptureStacktrace(bufferData.stackframes, (uint16)CountOf(bufferData.stackframes), 2);
    #endif

    AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::BUFFERS]);
    return gVk.pools.buffers.Add(bufferData);
}

void gfxDestroyBuffer(GfxBuffer buffer)
{
    if (!buffer.IsValid())
        return;
    
    GfxBufferData bufferData;
    { AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::BUFFERS]);
        bufferData = gVk.pools.buffers.Data(buffer);
    }

    vmaDestroyBuffer(gVk.vma, bufferData.buffer, bufferData.allocation);
    if (bufferData.stagingBuffer) 
        vmaDestroyBuffer(gVk.vma, bufferData.stagingBuffer, bufferData.stagingAllocation);

    { AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::BUFFERS]);
       gVk.pools.buffers.Remove(buffer);
    }
}

void gfxCmdUpdateBuffer(GfxBuffer buffer, const void* data, uint32 size)
{
    ASSERT(data);
    ASSERT(size);

    GfxBufferData bufferData;
    {
        AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::BUFFERS]);
        bufferData = gVk.pools.buffers.Data(buffer);
    }
    ASSERT(size <= bufferData.size);
    ASSERT_MSG(bufferData.memUsage != GfxBufferUsage::Immutable, "Immutable buffers cannot be updated");
    ASSERT(bufferData.mappedBuffer);

    if (bufferData.memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        memcpy(bufferData.mappedBuffer, data, size);
    }
    else {
        ASSERT(bufferData.stagingBuffer);

        [[maybe_unused]] VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
        ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

        VkBufferCopy bufferCopy {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = size
        };
        memcpy(bufferData.mappedBuffer, data, size);
        vmaFlushAllocation(gVk.vma, bufferData.stagingAllocation, 0, size == bufferData.size ? VK_WHOLE_SIZE : size);

        gfxCmdCopyBuffer(bufferData.stagingBuffer, bufferData.buffer, 1, &bufferCopy);
    }
}

void gfxCmdPushConstants(GfxPipeline pipeline, GfxShaderStage stage, const void* data, uint32 size)
{
    VkPipelineLayout pipLayoutVk;
    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

    { 
        AtomicLockScope lk1(gVk.pools.locks[GfxObjectPools::PIPELINES]);
        const GfxPipelineData& pipData = gVk.pools.pipelines.Data(pipeline);
        AtomicLockScope lk2(gVk.pools.locks[GfxObjectPools::PIPELINE_LAYOUTS]);
        pipLayoutVk = gVk.pools.pipelineLayouts.Data(pipData.pipelineLayout).layout;
    }

    vkCmdPushConstants(cmdBufferVk, pipLayoutVk, static_cast<VkShaderStageFlags>(stage), 0, size, data);
}

GfxImage gfxCreateImage(const GfxImageDesc& desc)
{
    GfxBufferUsage memUsage = desc.usage == GfxBufferUsage::Default ? GfxBufferUsage::Immutable : desc.usage;
    ASSERT_MSG(memUsage == GfxBufferUsage::Immutable, "Other usages are not supported");

    uint32 usageVk = 0;
    if (desc.frameBuffer) {
        if (gfxFormatIsDepthStencil(desc.format))
            usageVk |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        else
            usageVk |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    if (desc.sampled) 
        usageVk |= VK_IMAGE_USAGE_SAMPLED_BIT;

    if (desc.content)
        usageVk |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    GfxImageData imageData {
        .width = desc.width,
        .height = desc.height,
        .numMips = desc.numMips,
        .memUsage = memUsage
    };

    VkImageCreateInfo imageCreateInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = static_cast<VkFormat>(desc.format),
        .extent = {
            .width = desc.width,
            .height = desc.height,
            .depth = 1
        },
        .mipLevels = desc.numMips,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usageVk,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VmaAllocationCreateInfo allocCreateInfo {
        .flags = 0,
        .usage = !desc.frameBuffer ? VMA_MEMORY_USAGE_AUTO : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    };

    VmaAllocationInfo allocInfo;
    if (vmaCreateImage(gVk.vma, &imageCreateInfo, &allocCreateInfo, &imageData.image, &imageData.allocation, 
                       &allocInfo) != VK_SUCCESS) 
    {
        return GfxImage();
    }

    imageData.sizeBytes = allocInfo.size;
    VkMemoryPropertyFlags memFlags;
    vmaGetMemoryTypeProperties(gVk.vma, allocInfo.memoryType, &memFlags);

    gfxBeginDeferredCommandBuffer();

    VkImageMemoryBarrier imageBarrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = imageData.image,
        .subresourceRange = {
            .baseMipLevel = 0,
            .levelCount = desc.numMips,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    if (desc.content) {
        ASSERT(desc.size);
        ASSERT_MSG(imageData.sizeBytes >= desc.size, "Provided image buffer does not fit into actual image buffer");
        
        // TODO: This part doesn't seem to work properly with integrated GPUs
        //       Because we should be getting VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, but we are not
        if (memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            void* buffer = nullptr;
            vmaMapMemory(gVk.vma, imageData.allocation, &buffer);
            ASSERT(buffer);
            memcpy(buffer, desc.content, desc.size);
            vmaUnmapMemory(gVk.vma, imageData.allocation);
            ASSERT(0);
        }
        else {
            // Create staging buffer and upload data
            VkBufferCreateInfo stageBufferCreateInfo {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = desc.size,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
            };

            VmaAllocationCreateInfo stageAllocCreateInfo {
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO
            };

            VkBuffer stagingBuffer;
            VmaAllocation stagingAlloc;
            if (vmaCreateBuffer(gVk.vma, &stageBufferCreateInfo, &stageAllocCreateInfo, &stagingBuffer, 
                &stagingAlloc, &allocInfo) != VK_SUCCESS) 
            {
                vmaDestroyImage(gVk.vma, imageData.image, imageData.allocation);
                return GfxImage();
            }

            void* stagingData = nullptr;
            vmaMapMemory(gVk.vma, stagingAlloc, &stagingData);
            ASSERT(stagingData);
            memcpy(stagingData, desc.content, desc.size);
            vmaUnmapMemory(gVk.vma, stagingAlloc);

            ASSERT(desc.sampled);
            
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageBarrier.srcAccessMask = 0;
            imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            gfxCmdPipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                                  0, 0, nullptr, 0, nullptr, 1, &imageBarrier);

            gfxCmdCopyBufferToImage(stagingBuffer, imageData.image, desc.width, desc.height, desc.numMips, desc.mipOffsets);

            MutexScope mtxGarbage(gVk.garbageMtx);
            gVk.garbage.Push(GfxGarbage {
                .type = GfxGarbage::Type::Buffer,
                .frameIdx = engineFrameIndex(),
                .buffer = stagingBuffer,
                .allocation = stagingAlloc
            });
        }
    }

    // Sampler / View
    VkFilter minMagFilter = VK_FILTER_MAX_ENUM;
    VkSamplerMipmapMode mipFilter = VK_SAMPLER_MIPMAP_MODE_MAX_ENUM;
    VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_MAX_ENUM;
    float anistoropy = desc.anisotropy <= 0 ? 1.0f : desc.anisotropy;

    switch (desc.samplerFilter) {
    case GfxSamplerFilterMode::Default:
    case GfxSamplerFilterMode::Nearest:               minMagFilter = VK_FILTER_NEAREST; mipFilter = VK_SAMPLER_MIPMAP_MODE_NEAREST; break;
    case GfxSamplerFilterMode::Linear:                minMagFilter = VK_FILTER_LINEAR;  mipFilter = VK_SAMPLER_MIPMAP_MODE_LINEAR;  break;
    case GfxSamplerFilterMode::NearestMipmapNearest:  minMagFilter = VK_FILTER_NEAREST; mipFilter = VK_SAMPLER_MIPMAP_MODE_NEAREST; break;
    case GfxSamplerFilterMode::NearestMipmapLinear:   minMagFilter = VK_FILTER_NEAREST; mipFilter = VK_SAMPLER_MIPMAP_MODE_LINEAR;  break;
    case GfxSamplerFilterMode::LinearMipmapNearest:   minMagFilter = VK_FILTER_LINEAR;  mipFilter = VK_SAMPLER_MIPMAP_MODE_NEAREST; break;
    case GfxSamplerFilterMode::LinearMipmapLinear:    minMagFilter = VK_FILTER_LINEAR;  mipFilter = VK_SAMPLER_MIPMAP_MODE_LINEAR;  break;
    }

    switch (desc.samplerWrap) {
    case GfxSamplerWrapMode::Default:
    case GfxSamplerWrapMode::Repeat:           addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT; break;
    case GfxSamplerWrapMode::ClampToEdge:      addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; break;
    case GfxSamplerWrapMode::ClampToBorder:    addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; break;
    case GfxSamplerWrapMode::MirroredRepeat:   addressMode = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; break;
    }

    if (desc.frameBuffer) {
        ASSERT(!desc.content);

        bool depthStencil = gfxFormatIsDepthStencil(desc.format);
        VkImageAspectFlags aspectFlags = depthStencil ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        imageData.view = gfxCreateImageViewVk(imageData.image, static_cast<VkFormat>(desc.format), aspectFlags);
        if (desc.sampled)
            imageData.sampler = gfxCreateSamplerVk(minMagFilter, mipFilter, addressMode, anistoropy);
        
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarrier.newLayout = depthStencil ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imageBarrier.srcAccessMask = 0;
        imageBarrier.dstAccessMask = depthStencil ? 
            (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT) : 
            (VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
        imageBarrier.subresourceRange.aspectMask = aspectFlags;
        gfxCmdPipelineBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                              depthStencil ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
                              0, 0, nullptr, 0, nullptr, 1, &imageBarrier);
    }
    else if (desc.sampled) {
        imageData.sampler = gfxCreateSamplerVk(minMagFilter, mipFilter, addressMode, anistoropy);
        imageData.view = gfxCreateImageViewVk(imageData.image, static_cast<VkFormat>(desc.format), VK_IMAGE_ASPECT_COLOR_BIT);
        
        ASSERT(desc.content);
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        gfxCmdPipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              0, 0, nullptr, 0, nullptr, 1, &imageBarrier);
    }
    
    gfxEndDeferredCommandBuffer();

#if !CONFIG_FINAL_BUILD
    if (settingsGet().graphics.trackResourceLeaks)
        imageData.numStackframes = debugCaptureStacktrace(imageData.stackframes, (uint16)CountOf(imageData.stackframes), 2);
#endif

    // MutexScope mtx(gVk.pools.mutex);
    return gVk.pools.images.Add(imageData);
}

void gfxDestroyImage(GfxImage image)
{
    if (!image.IsValid())
        return;

    AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::IMAGES]);
    GfxImageData& imageData = gVk.pools.images.Data(image);
    
    if (imageData.sizeBytes) {
        if (imageData.image) 
            vmaDestroyImage(gVk.vma, imageData.image, imageData.allocation);
        if (imageData.sampler)
            vkDestroySampler(gVk.device, imageData.sampler, &gVk.allocVk);
        if (imageData.view)
            vkDestroyImageView(gVk.device, imageData.view, &gVk.allocVk);
        memset(&imageData, 0x0, sizeof(imageData));
    }    

    gVk.pools.images.Remove(image);
}

static VkGraphicsPipelineCreateInfo* gfxDuplicateGraphicsPipelineCreateInfo(const VkGraphicsPipelineCreateInfo& pipelineInfo)
{
    // Child POD members with arrays inside
    MemSingleShotMalloc<VkPipelineVertexInputStateCreateInfo> pallocVertexInputInfo;
    pallocVertexInputInfo.AddMemberField<VkVertexInputBindingDescription>(
        offsetof(VkPipelineVertexInputStateCreateInfo, pVertexBindingDescriptions), 
        pipelineInfo.pVertexInputState->vertexBindingDescriptionCount);
    pallocVertexInputInfo.AddMemberField<VkVertexInputAttributeDescription>(
        offsetof(VkPipelineVertexInputStateCreateInfo, pVertexAttributeDescriptions), 
        pipelineInfo.pVertexInputState->vertexAttributeDescriptionCount);

    MemSingleShotMalloc<VkPipelineColorBlendStateCreateInfo> pallocColorBlendState;
    pallocColorBlendState.AddMemberField<VkPipelineColorBlendAttachmentState>(
        offsetof(VkPipelineColorBlendStateCreateInfo, pAttachments),
        pipelineInfo.pColorBlendState->attachmentCount);

    MemSingleShotMalloc<VkPipelineDynamicStateCreateInfo> pallocDynamicState;
    pallocDynamicState.AddMemberField<VkDynamicState>(
        offsetof(VkPipelineDynamicStateCreateInfo, pDynamicStates),
        pipelineInfo.pDynamicState->dynamicStateCount);

    // Main fields
    MemSingleShotMalloc<VkGraphicsPipelineCreateInfo, 12> mallocator;

    mallocator.AddMemberField<VkPipelineShaderStageCreateInfo>(
        offsetof(VkGraphicsPipelineCreateInfo, pStages),
        pipelineInfo.stageCount);
        
    mallocator.AddMemberChildPODField<MemSingleShotMalloc<VkPipelineVertexInputStateCreateInfo>>(
        pallocVertexInputInfo, offsetof(VkGraphicsPipelineCreateInfo, pVertexInputState), 1);

    mallocator.AddMemberField<VkPipelineInputAssemblyStateCreateInfo>(
        offsetof(VkGraphicsPipelineCreateInfo, pInputAssemblyState), 1);

    // skip pTessellationState

    mallocator.AddMemberField<VkPipelineViewportStateCreateInfo>(
        offsetof(VkGraphicsPipelineCreateInfo, pViewportState), 1);
        
    mallocator.AddMemberField<VkPipelineRasterizationStateCreateInfo>(
        offsetof(VkGraphicsPipelineCreateInfo, pRasterizationState), 1);

    mallocator.AddMemberField<VkPipelineMultisampleStateCreateInfo>(
        offsetof(VkGraphicsPipelineCreateInfo, pMultisampleState), 1);

    mallocator.AddMemberField<VkPipelineDepthStencilStateCreateInfo>(
        offsetof(VkGraphicsPipelineCreateInfo, pDepthStencilState), 1);

    mallocator.AddMemberChildPODField<MemSingleShotMalloc<VkPipelineColorBlendStateCreateInfo>>(
        pallocColorBlendState, offsetof(VkGraphicsPipelineCreateInfo, pColorBlendState), 1);

    mallocator.AddMemberChildPODField<MemSingleShotMalloc<VkPipelineDynamicStateCreateInfo>>(
        pallocDynamicState, offsetof(VkGraphicsPipelineCreateInfo, pDynamicState), 1);

    VkGraphicsPipelineCreateInfo* pipInfoNew = mallocator.Calloc(&gVk.alloc);

    // TODO: see if we can improve this part of the api
    pallocVertexInputInfo.Calloc((void*)pipInfoNew->pVertexInputState, 0);
    pallocColorBlendState.Calloc((void*)pipInfoNew->pColorBlendState, 0);
    pallocDynamicState.Calloc((void*)pipInfoNew->pDynamicState, 0);
        
    pipInfoNew->sType = pipelineInfo.sType;
    pipInfoNew->pNext = pipelineInfo.pNext;
    pipInfoNew->flags = pipelineInfo.flags;
    pipInfoNew->stageCount = pipelineInfo.stageCount;
    memcpy((void*)pipInfoNew->pStages, pipelineInfo.pStages, sizeof(VkPipelineShaderStageCreateInfo));
    memcpy((void*)pipInfoNew->pInputAssemblyState, pipelineInfo.pInputAssemblyState, sizeof(VkPipelineInputAssemblyStateCreateInfo));
    // pipInfoNew->pTessellationState = pipelineInfo.pTessellationState;
    memcpy((void*)pipInfoNew->pViewportState, pipelineInfo.pViewportState, sizeof(VkPipelineViewportStateCreateInfo));
    memcpy((void*)pipInfoNew->pRasterizationState, pipelineInfo.pRasterizationState, sizeof(VkPipelineRasterizationStateCreateInfo));
    memcpy((void*)pipInfoNew->pMultisampleState, pipelineInfo.pMultisampleState, sizeof(VkPipelineMultisampleStateCreateInfo));
    memcpy((void*)pipInfoNew->pDepthStencilState, pipelineInfo.pDepthStencilState, sizeof(VkPipelineDepthStencilStateCreateInfo));
    pipInfoNew->layout = pipelineInfo.layout;
    pipInfoNew->renderPass = pipelineInfo.renderPass;
    pipInfoNew->subpass = pipelineInfo.subpass;
    pipInfoNew->basePipelineHandle = pipelineInfo.basePipelineHandle;
    pipInfoNew->basePipelineIndex = pipelineInfo.basePipelineIndex;

    { // VkPipelineVertexInputStateCreateInfo
        VkPipelineVertexInputStateCreateInfo* vertexInputState = 
            const_cast<VkPipelineVertexInputStateCreateInfo*>(pipInfoNew->pVertexInputState);
        vertexInputState->sType = pipelineInfo.pVertexInputState->sType;
        vertexInputState->pNext = pipelineInfo.pVertexInputState->pNext;
        vertexInputState->flags = pipelineInfo.pVertexInputState->flags;
        vertexInputState->vertexBindingDescriptionCount = pipelineInfo.pVertexInputState->vertexBindingDescriptionCount;
        vertexInputState->vertexAttributeDescriptionCount = pipelineInfo.pVertexInputState->vertexAttributeDescriptionCount;
        memcpy((void*)vertexInputState->pVertexBindingDescriptions, pipelineInfo.pVertexInputState->pVertexBindingDescriptions,
            pipelineInfo.pVertexInputState->vertexBindingDescriptionCount*sizeof(VkVertexInputBindingDescription));
        memcpy((void*)vertexInputState->pVertexAttributeDescriptions, pipelineInfo.pVertexInputState->pVertexAttributeDescriptions,
            pipelineInfo.pVertexInputState->vertexAttributeDescriptionCount*sizeof(VkVertexInputAttributeDescription));
    }

    { // VkPipelineColorBlendStateCreateInfo
        VkPipelineColorBlendStateCreateInfo* colorBlendState =
            const_cast<VkPipelineColorBlendStateCreateInfo*>(pipInfoNew->pColorBlendState);
        colorBlendState->sType = pipelineInfo.pColorBlendState->sType;
        colorBlendState->pNext = pipelineInfo.pColorBlendState->pNext;
        colorBlendState->flags = pipelineInfo.pColorBlendState->flags;
        colorBlendState->logicOpEnable = pipelineInfo.pColorBlendState->logicOpEnable;
        colorBlendState->logicOp = pipelineInfo.pColorBlendState->logicOp;
        colorBlendState->attachmentCount = pipelineInfo.pColorBlendState->attachmentCount;

        memcpy((void*)colorBlendState->pAttachments, pipelineInfo.pColorBlendState->pAttachments,
            pipelineInfo.pColorBlendState->attachmentCount*sizeof(VkPipelineColorBlendAttachmentState));

        memcpy((void*)colorBlendState->blendConstants, pipelineInfo.pColorBlendState->blendConstants, 4*sizeof(float));
    }

    { // VkPipelineDynamicStateCreateInfo
        VkPipelineDynamicStateCreateInfo* dynamicState =
            const_cast<VkPipelineDynamicStateCreateInfo*>(pipInfoNew->pDynamicState);
        dynamicState->sType = pipelineInfo.pDynamicState->sType;
        dynamicState->pNext = pipelineInfo.pDynamicState->pNext;
        dynamicState->flags = pipelineInfo.pDynamicState->flags;
        dynamicState->dynamicStateCount = pipelineInfo.pDynamicState->dynamicStateCount;

        memcpy((void*)dynamicState->pDynamicStates, pipelineInfo.pDynamicState->pDynamicStates,
            pipelineInfo.pDynamicState->dynamicStateCount*sizeof(VkDynamicState));
    }
        
    return pipInfoNew;
}

static VkShaderModule gfxCreateShaderModuleVk(const char* name, const uint8* data, uint32 dataSize)
{
    ASSERT(data);
    ASSERT(dataSize);
        
    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = dataSize,
        .pCode = reinterpret_cast<const uint32*>(data)
    };
        
    VkShaderModule shaderModule;
    if (VK_FAILED(vkCreateShaderModule(gVk.device, &createInfo, &gVk.allocVk, &shaderModule))) {
        logError("Gfx: vkCreateShaderModule failed: %s", name);
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}

static VkPipelineShaderStageCreateInfo gfxCreateShaderStageVk(const ShaderStageInfo& shaderStage, VkShaderModule shaderModule)
{
    VkShaderStageFlagBits stageBits;
    switch (shaderStage.stage) {
    case ShaderStage::Vertex:       stageBits = VK_SHADER_STAGE_VERTEX_BIT;     break;
    case ShaderStage::Fragment:     stageBits = VK_SHADER_STAGE_FRAGMENT_BIT;   break;
    case ShaderStage::Compute:      stageBits = VK_SHADER_STAGE_COMPUTE_BIT;    break;
    default:    ASSERT_MSG(0, "Not implemented");   stageBits = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;  break;
    }
        
    VkPipelineShaderStageCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = stageBits,
        .module = shaderModule,
        .pName = "main"
    };
        
    return createInfo;
}

static void gfxSavePipelineBinaryProperties(const char* name, VkPipeline pip)
{
    ASSERT(gVk.hasPipelineExecutableProperties);

    MemTempAllocator tmpAlloc;
    Blob info;
    char lineStr[512];

    info.SetAllocator(&tmpAlloc);
    info.SetGrowPolicy(Blob::GrowPolicy::Linear);

    VkPipelineInfoKHR pipInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR,
        .pipeline = pip
    };

    uint32 numExecutables;
    if (VkExtensionApi::vkGetPipelineExecutablePropertiesKHR(gVk.device, &pipInfo, &numExecutables, NULL) == VK_SUCCESS 
        && numExecutables) 
    {
        VkPipelineExecutablePropertiesKHR* executableProperties = (VkPipelineExecutablePropertiesKHR*)
            alloca(numExecutables*sizeof(VkPipelineExecutablePropertiesKHR));
        ASSERT(executableProperties);
        memset(executableProperties, 0x0, sizeof(VkPipelineExecutablePropertiesKHR)*numExecutables);
        for (uint32 i = 0; i < numExecutables; i++)
            executableProperties[i].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR;

        VkExtensionApi::vkGetPipelineExecutablePropertiesKHR(gVk.device, &pipInfo, &numExecutables, executableProperties);
        for (uint32 i = 0; i < numExecutables; i++) {
            const VkPipelineExecutablePropertiesKHR& ep = executableProperties[i];

            strPrintFmt(lineStr, sizeof(lineStr), "%s - %s:\n", ep.name, ep.description);
            info.Write(lineStr, strLen(lineStr));

            VkPipelineExecutableInfoKHR pipExecInfo {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR,
                .pipeline = pip,
                .executableIndex = i
            };

            uint32 numStats;
            if (VkExtensionApi::vkGetPipelineExecutableStatisticsKHR(gVk.device, &pipExecInfo, &numStats, nullptr) == VK_SUCCESS &&
                numStats) 
            {
                VkPipelineExecutableStatisticKHR* stats = (VkPipelineExecutableStatisticKHR*)
                    alloca(sizeof(VkPipelineExecutableStatisticKHR)*numStats);
                ASSERT(stats);
                memset(stats, 0x0, sizeof(VkPipelineExecutableStatisticKHR)*numStats);
                for (uint32 statIdx = 0; statIdx < numStats; statIdx++)
                    stats[statIdx].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR;
                VkExtensionApi::vkGetPipelineExecutableStatisticsKHR(gVk.device, &pipExecInfo, &numStats, stats);
                for (uint32 statIdx = 0; statIdx < numStats; statIdx++) {
                    const VkPipelineExecutableStatisticKHR& stat = stats[statIdx];                    

                    char valueStr[32];
                    switch (stat.format) {
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:    
                        strCopy(valueStr, sizeof(valueStr), stat.value.b32 ? "True" : "False"); 
                        break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:     
                        strPrintFmt(valueStr, sizeof(valueStr), "%lld", stat.value.i64); 
                        break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:    
                        strPrintFmt(valueStr, sizeof(valueStr), "%llu", stat.value.u64); 
                        break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:   
                        strPrintFmt(valueStr, sizeof(valueStr), "%.3f", stat.value.f64); 
                        break;
                    default: ASSERT(0); break;
                    }

                    strPrintFmt(lineStr, sizeof(lineStr), "\t%s = %s\n", stat.name, valueStr);
                    info.Write(lineStr, strLen(lineStr));
                }
            }

            // TODO: we don't seem to be getting here, at least for nvidia drivers 
            #if 0
                uint32 numRepr;
                if (VkExtensionApi::vkGetPipelineExecutableInternalRepresentationsKHR(gVk.device, &pipExecInfo, &numRepr, nullptr) == VK_SUCCESS)
                {
                    if (numRepr) {
                        VkPipelineExecutableInternalRepresentationKHR* reprs = (VkPipelineExecutableInternalRepresentationKHR*)
                        alloca(sizeof(VkPipelineExecutableInternalRepresentationKHR)*numRepr);
                        ASSERT(reprs);
                        VkExtensionApi::vkGetPipelineExecutableInternalRepresentationsKHR(gVk.device, &pipExecInfo, &numRepr, reprs);
                        for (uint32 ri = 0; ri < numRepr; ri++) {
                            const VkPipelineExecutableInternalRepresentationKHR& repr = reprs[ri];
                            logDebug(repr.name);
                        }
                    }
                }
            #endif
        } // Foreach executable
    } 

    if (info.Size()) {
        // TODO: use async write 
        Path filepath(name);
        filepath.Append(".txt");
        vfsWriteFileAsync(filepath.CStr(), info, VfsFlags::AbsolutePath|VfsFlags::TextFile, 
                          [](const char* path, size_t, const Blob&, void*) { logVerbose("Written shader information to file: %s", path); },
                          nullptr);
    }
}

GfxPipeline gfxCreatePipeline(const GfxPipelineDesc& desc)
{
    MemTempAllocator tempAlloc;

    // Shaders
    Shader* shaderInfo = desc.shader;
    ASSERT(shaderInfo);
    
    const ShaderStageInfo* vsInfo = shaderGetStage(*shaderInfo, ShaderStage::Vertex);
    const ShaderStageInfo* fsInfo = shaderGetStage(*shaderInfo, ShaderStage::Fragment);
    if (!vsInfo || !fsInfo) {
        logError("Gfx: Pipeline failed. Shader doesn't have vs/fs stages: %s", shaderInfo->name);
        return GfxPipeline();
    }

    VkPipelineShaderStageCreateInfo shaderStages[] = {
        gfxCreateShaderStageVk(*vsInfo, gfxCreateShaderModuleVk(shaderInfo->name, vsInfo->data.Get(), vsInfo->dataSize)),
        gfxCreateShaderStageVk(*fsInfo, gfxCreateShaderModuleVk(shaderInfo->name, fsInfo->data.Get(), fsInfo->dataSize))
    };

    // Vertex inputs: combine bindings from the compiled shader and provided descriptions
    ASSERT_ALWAYS(desc.numVertexBufferBindings > 0, "Must provide vertex buffer bindings");
    VkVertexInputBindingDescription* vertexBindingDescs = tempAlloc.MallocTyped<VkVertexInputBindingDescription>(desc.numVertexBufferBindings);
    for (uint32 i = 0; i < desc.numVertexBufferBindings; i++) {
        vertexBindingDescs[i] = {
            .binding = desc.vertexBufferBindings[i].binding,
            .stride = desc.vertexBufferBindings[i].stride,
            .inputRate = static_cast<VkVertexInputRate>(desc.vertexBufferBindings[i].inputRate)
        };
    }

    ASSERT_ALWAYS(desc.numVertexInputAttributes == shaderInfo->numVertexAttributes, 
        "Provided number of vertex attributes does not match with the compiled shader");
    
    VkVertexInputAttributeDescription* vertexInputAtts = tempAlloc.MallocTyped<VkVertexInputAttributeDescription>(desc.numVertexInputAttributes);    
    for (uint32 i = 0; i < desc.numVertexInputAttributes; i++) {
        // Validation:
        // Semantic/SemanticIndex
        ASSERT_MSG(desc.vertexInputAttributes[i].semantic == shaderInfo->vertexAttributes[i].semantic &&
                   desc.vertexInputAttributes[i].semanticIdx == shaderInfo->vertexAttributes[i].semanticIdx, 
                   "Vertex input attributes does not match with shader: (Index: %u, Shader: %s%u, Desc: %s%u)",
                   i, shaderInfo->vertexAttributes[i].semantic, shaderInfo->vertexAttributes[i].semanticIdx, 
                   desc.vertexInputAttributes[i].semantic.CStr(), desc.vertexInputAttributes[i].semanticIdx);
        // Format: Current exception is "COLOR" with RGBA8_UNORM on the CPU side and RGBA32_SFLOAT on shader side
        ASSERT_MSG(desc.vertexInputAttributes[i].format == shaderInfo->vertexAttributes[i].format ||
                   (desc.vertexInputAttributes[i].semantic == "COLOR" && 
                   desc.vertexInputAttributes[i].format == GfxFormat::R8G8B8A8_UNORM &&
                   shaderInfo->vertexAttributes[i].format == GfxFormat::R32G32B32A32_SFLOAT),
                   "Vertex input attribute formats do not match");
        
        vertexInputAtts[i] = {
            .location = shaderInfo->vertexAttributes[i].location,
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

    VkPipelineLayout pipLayout = nullptr;
    GfxPipelineLayout pipelineLayout = gfxCreatePipelineLayout(*shaderInfo, 
                                                               desc.descriptorSetLayouts, desc.numDescriptorSetLayouts, 
                                                               desc.pushConstants, desc.numPushConstants, &pipLayout);
    ASSERT_ALWAYS(pipelineLayout.IsValid(), "Gfx: Create pipeline layout failed");
    
    // InputAssembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { 
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = static_cast<VkPrimitiveTopology>(desc.inputAssemblyTopology)
    };

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = desc.rasterizer.depthClampEnable,
        .rasterizerDiscardEnable = desc.rasterizer.rasterizerDiscardEnable,
        .polygonMode = static_cast<VkPolygonMode>(desc.rasterizer.polygonMode),
        .cullMode = static_cast<VkCullModeFlags>(desc.rasterizer.cullMode),
        .frontFace = static_cast<VkFrontFace>(desc.rasterizer.frontFace),
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
    const GfxBlendAttachmentDesc* blendAttachmentDescs = !desc.blend.attachments ? gfxBlendAttachmentDescGetDefault() : desc.blend.attachments;
        
    VkPipelineColorBlendAttachmentState* colorBlendAttachments = tempAlloc.MallocTyped<VkPipelineColorBlendAttachmentState>(numBlendAttachments);
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
        .flags = gVk.hasPipelineExecutableProperties ? VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR : (VkPipelineCreateFlags)0,
        .stageCount = 2,
        .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlend,
        .pDynamicState = &dynamicState,
        .layout = pipLayout,
        .renderPass = gVk.swapchain.renderPass,     // TODO: pipeline is tied to hardcoded renderpass
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1
    };

    VkPipeline pipeline;
    if (VK_FAILED(vkCreateGraphicsPipelines(gVk.device, VK_NULL_HANDLE, 1, &pipelineInfo, &gVk.allocVk, &pipeline))) {
        logError("Gfx: Creating graphics pipeline failed");
        return GfxPipeline();
    }

    if (gVk.hasPipelineExecutableProperties)
        gfxSavePipelineBinaryProperties(desc.shader->name, pipeline);

    for (uint32 i = 0; i < CountOf(shaderStages); i++)
        vkDestroyShaderModule(gVk.device, shaderStages[i].module, &gVk.allocVk);

    GfxPipelineData pipData {
        .pipeline = pipeline,
        .pipelineLayout = pipelineLayout,
        .gfxCreateInfo = gfxDuplicateGraphicsPipelineCreateInfo(pipelineInfo),
        .shaderHash = shaderInfo->hash
    };

    #if !CONFIG_FINAL_BUILD
        if (settingsGet().graphics.trackResourceLeaks)
            pipData.numStackframes = debugCaptureStacktrace(pipData.stackframes, (uint16)CountOf(pipData.stackframes), 2);
    #endif
    
    GfxPipeline pip;
    {
        AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::PIPELINES]);
        pip = gVk.pools.pipelines.Add(pipData);
    }

    { // Add to shader's used piplines list, so later we could iterate over them to recreate the pipelines
        MutexScope pipTableMtx(gVk.shaderPipelinesTableMtx);
        uint32 index = gVk.shaderPipelinesTable.Find(shaderInfo->hash);
        if (index != UINT32_MAX) {
            gVk.shaderPipelinesTable.GetMutable(index).Push(pip);
        }
        else {
            Array<GfxPipeline>* arr = PLACEMENT_NEW(gVk.shaderPipelinesTable.Add(shaderInfo->hash), Array<GfxPipeline>);
            arr->Push(pip);
        }
    }

    return pip;   
}

void gfxDestroyPipeline(GfxPipeline pipeline)
{
    if (!pipeline.IsValid())
        return;

    AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::PIPELINES]);
    GfxPipelineData& pipData = gVk.pools.pipelines.Data(pipeline);

    {   // Remove from shader <-> pipeline table
        MutexScope pipTableMtx(gVk.shaderPipelinesTableMtx);
        uint32 index = gVk.shaderPipelinesTable.Find(pipData.shaderHash);
        if (index != UINT32_MAX) {
            Array<GfxPipeline>& pipList = gVk.shaderPipelinesTable.GetMutable(index);
            uint32 pipIdx = pipList.FindIf([pipeline](const GfxPipeline& pip)->bool { return pip == pipeline; });
            if (pipIdx != UINT32_MAX)
                pipList.RemoveAndSwap(pipIdx);
            if (pipList.Count() == 0) {
                pipList.Free();
                gVk.shaderPipelinesTable.Remove(index);
            }
        }
    }

    MemSingleShotMalloc<VkGraphicsPipelineCreateInfo, 12> mallocator;
    mallocator.Free(pipData.gfxCreateInfo, &gVk.alloc);
    if (pipData.pipelineLayout.IsValid()) 
        gfxDestroyPipelineLayout(pipData.pipelineLayout);
    if (pipData.pipeline)
        vkDestroyPipeline(gVk.device, pipData.pipeline, &gVk.allocVk);

    gVk.pools.pipelines.Remove(pipeline);
}

void gfxCmdBeginSwapchainRenderPass(Color bgColor)
{
    ASSERT_MSG(gVk.swapchain.imageIdx != UINT32_MAX, "This function must be called within during frame rendering");

    PROFILE_ZONE(true);

    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

    uint32 imageIdx = gVk.swapchain.imageIdx;

    Float4 bgColor4f = colorToFloat4(bgColor);
    const VkClearValue clearValues[] = {
        { .color = {{bgColor4f.x, bgColor4f.y, bgColor4f.z, bgColor4f.w}} },
        { .depthStencil = {1.0f, 0} }
    };

    VkRenderPassBeginInfo renderPassInfo {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = gVk.swapchain.renderPass,
        .framebuffer = gVk.swapchain.framebuffers[imageIdx],
        .renderArea = {
            .offset = {0, 0},
            .extent = gVk.swapchain.extent
        },
        .clearValueCount = 2,
        .pClearValues = clearValues
    };
    
    vkCmdBeginRenderPass(cmdBufferVk, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    gCmdBufferThreadData.renderingToSwapchain = true;
}

void gfxCmdEndSwapchainRenderPass()
{
    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

    vkCmdEndRenderPass(cmdBufferVk);
    gCmdBufferThreadData.renderingToSwapchain = false;

    if (gVk.deviceProps.limits.timestampComputeAndGraphics) {
        // Assume this is the final pass that is called in the frame, write the end-frame query
        vkCmdWriteTimestamp(cmdBufferVk, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, gVk.queryPool[gVk.currentFrameIdx], 1);
        atomicStore32Explicit(&gVk.queryFirstCall, 0, AtomicMemoryOrder::Relaxed);
    }
}

GfxDescriptorSetLayout gfxCreateDescriptorSetLayout(const Shader& shader, const GfxDescriptorSetLayoutBinding* bindings, uint32 numBindings)
{
    ASSERT(numBindings);
    ASSERT(bindings);

    MemTempAllocator tmpAlloc;

    // Construct Vulkan-specific structs for bindings and their names
    VkDescriptorSetLayoutBinding* descriptorSetBindings = tmpAlloc.MallocTyped<VkDescriptorSetLayoutBinding>(numBindings);
    const char** names = tmpAlloc.MallocTyped<const char*>(numBindings);
    
    bool hasArrays = false;
    for (uint32 i = 0; i < numBindings; i++) {
        const GfxDescriptorSetLayoutBinding& dsLayoutBinding = bindings[i];
        ASSERT(dsLayoutBinding.arrayCount > 0);

        const ShaderParameterInfo* shaderParam = shaderGetParam(shader, dsLayoutBinding.name);
        ASSERT_MSG(shaderParam != nullptr, "Shader parameter '%s' does not exist in shader '%s'", dsLayoutBinding.name, shader.name);
        ASSERT_MSG(!shaderParam->isPushConstant, "Shader parameter '%s' is a push-constant in shader '%s'. cannot be used as regular uniform", dsLayoutBinding.name, shader.name);

        names[i] = shaderParam->name;    // Set the pointer to the field in ShaderParameterInfo because it is garuanteed to stay in mem
        descriptorSetBindings[i] = VkDescriptorSetLayoutBinding {
            .binding = shaderParam->bindingIdx,
            .descriptorType = static_cast<VkDescriptorType>(dsLayoutBinding.type),
            .descriptorCount = dsLayoutBinding.arrayCount,
            .stageFlags = static_cast<VkShaderStageFlags>(dsLayoutBinding.stages)
        };

        hasArrays = dsLayoutBinding.arrayCount > 1;
    }

    // Search in existing descriptor set layouts and try to find a match. 
    HashMurmur32Incremental hasher(0x5eed1);
    uint32 hash = hasher.Add<VkDescriptorSetLayoutBinding>(descriptorSetBindings, numBindings)
                        .AddCStringArray(names, numBindings)
                        .Hash();

    atomicLockEnter(&gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SET_LAYOUTS]);
    if (GfxDescriptorSetLayout layout = gVk.pools.descriptorSetLayouts.FindIf(
        [hash](const GfxDescriptorSetLayoutData& item)->bool { return item.hash == hash; }); layout.IsValid())
    {
        GfxDescriptorSetLayoutData& item = gVk.pools.descriptorSetLayouts.Data(layout);
        ++item.refCount;
        atomicLockExit(&gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SET_LAYOUTS]);
        return layout;
    }
    else {
        atomicLockExit(&gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SET_LAYOUTS]);

        VkDescriptorSetLayoutCreateInfo layoutCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = numBindings,
            .pBindings = descriptorSetBindings
        };

        // VK_EXT_descriptor_indexing
        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT layoutBindingFlags {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT,
            .bindingCount = numBindings
        };
        if (hasArrays && gVk.hasDescriptorIndexing) {
            VkDescriptorBindingFlagsEXT* bindingFlags = tmpAlloc.MallocTyped<VkDescriptorBindingFlagsEXT>(numBindings);
            for (uint32 i = 0; i < numBindings; i++)
                bindingFlags[i] = bindings[i].arrayCount > 1 ? VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT : 0;
            layoutBindingFlags.pBindingFlags = bindingFlags;
            layoutCreateInfo.pNext = &layoutBindingFlags;
        }
        
        VkDescriptorSetLayout dsLayout;
        if (vkCreateDescriptorSetLayout(gVk.device, &layoutCreateInfo, &gVk.allocVk, &dsLayout) != VK_SUCCESS) {
            logError("Gfx: CreateDescriptorSetLayout failed");
            return GfxDescriptorSetLayout();
        }

        // Copy layout bindings (for validation and lazy binding) and add descriptor set layout as Gfx object
        GfxDescriptorSetLayoutData dsLayoutData {
            .hash = hash,
            .layout = dsLayout,
            .numBindings = numBindings,
            .refCount = 1,
            .bindings = memAllocTyped<GfxDescriptorSetLayoutData::Binding>(numBindings, &gVk.alloc)
        };

        for (uint32 i = 0; i < numBindings; i++) {
            ASSERT(names[i]);
            dsLayoutData.bindings[i].name = names[i];
            dsLayoutData.bindings[i].nameHash = hashFnv32Str(names[i]);
            dsLayoutData.bindings[i].variableDescCount = bindings[i].arrayCount;
            memcpy(&dsLayoutData.bindings[i].vkBinding, &descriptorSetBindings[i], sizeof(VkDescriptorSetLayoutBinding));
        }

        #if !CONFIG_FINAL_BUILD
            if (settingsGet().graphics.trackResourceLeaks)
                dsLayoutData.numStackframes = debugCaptureStacktrace(dsLayoutData.stackframes, (uint16)CountOf(dsLayoutData.stackframes), 2);
        #endif

        AtomicLockScope mtx(gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SET_LAYOUTS]);
        GfxDescriptorSetLayoutData prevLayout;
        layout = gVk.pools.descriptorSetLayouts.Add(dsLayoutData, &prevLayout);

        memFree(prevLayout.bindings, &gVk.alloc);
        return layout;
    }

}

void gfxDestroyDescriptorSetLayout(GfxDescriptorSetLayout layout)
{
    if (!layout.IsValid())
        return;

    AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SET_LAYOUTS]);
    GfxDescriptorSetLayoutData& layoutData = gVk.pools.descriptorSetLayouts.Data(layout);
    ASSERT(layoutData.refCount > 0);
    if (--layoutData.refCount == 0) {
        if (layoutData.layout)
            vkDestroyDescriptorSetLayout(gVk.device, layoutData.layout, &gVk.allocVk);
        if (layoutData.bindings)
            memFree(layoutData.bindings, &gVk.alloc);
        memset(&layoutData, 0x0, sizeof(layoutData));

        gVk.pools.descriptorSetLayouts.Remove(layout);
    }
}

GfxDescriptorSet gfxCreateDescriptorSet(GfxDescriptorSetLayout layout)
{
    MemTempAllocator tempAlloc;
    VkDescriptorSetLayout vkLayout;

    uint32* variableDescCounts = nullptr;
    uint32 numVariableDescCounts = 0;

    {
        AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SET_LAYOUTS]);
        const GfxDescriptorSetLayoutData& layoutData = gVk.pools.descriptorSetLayouts.Data(layout);
        vkLayout = layoutData.layout;

        variableDescCounts = tempAlloc.MallocTyped<uint32>(layoutData.numBindings);

        for (uint32 i = 0; i < layoutData.numBindings; i++) {
            switch (layoutData.bindings[i].vkBinding.descriptorType) {
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:     ++gVk.descriptorStats.numUniformBuffers;     break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: ++gVk.descriptorStats.numDynUniformBuffers; break;
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:      ++gVk.descriptorStats.numSampledImages;      break;
            case VK_DESCRIPTOR_TYPE_SAMPLER:            ++gVk.descriptorStats.numSamplers;           break;
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: ++gVk.descriptorStats.numCombinedImageSamplers;    break;
            default:                                    break;
            }

            if (layoutData.bindings[i].variableDescCount > 1) 
                variableDescCounts[numVariableDescCounts++] = layoutData.bindings[i].variableDescCount;
        }
    }    

    VkDescriptorSetAllocateInfo allocInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = gVk.descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &vkLayout
    };

    // VK_EXT_descriptor_indexing
    VkDescriptorSetVariableDescriptorCountAllocateInfoEXT variableDescriptorCountAllocInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT,
        .descriptorSetCount = numVariableDescCounts,
        .pDescriptorCounts = numVariableDescCounts ? variableDescCounts : nullptr
    };
    if (gVk.hasDescriptorIndexing)
        allocInfo.pNext = &variableDescriptorCountAllocInfo;

    GfxDescriptorSetData descriptorSetData { .layout = layout };
    if (vkAllocateDescriptorSets(gVk.device, &allocInfo, &descriptorSetData.descriptorSet) != VK_SUCCESS) {
        logError("Gfx: AllocateDescriptorSets failed");
        return GfxDescriptorSet();
    }

    #if !CONFIG_FINAL_BUILD
        if (settingsGet().graphics.trackResourceLeaks)
            descriptorSetData.numStackframes = debugCaptureStacktrace(descriptorSetData.stackframes, (uint16)CountOf(descriptorSetData.stackframes), 2);
    #endif

    AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SETS]);
    return gVk.pools.descriptorSets.Add(descriptorSetData);
}

void gfxDestroyDescriptorSet(GfxDescriptorSet dset)
{
    if (!dset.IsValid())
        return;

    GfxDescriptorSetData dsetData;
    {
        AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SETS]);
        dsetData = gVk.pools.descriptorSets.Data(dset);
    }

    {
        AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SET_LAYOUTS]);
        ASSERT_MSG(gVk.pools.descriptorSetLayouts.IsValid(dsetData.layout), 
                   "Cannot destroy descriptor set. Make sure you do not destroy the parent pipeline before this");
        const GfxDescriptorSetLayoutData& layoutData = gVk.pools.descriptorSetLayouts.Data(dsetData.layout);

        // decrement descriptors
        GfxBudgetStats::DescriptorBudgetStats& dstats = gVk.descriptorStats;
        
        for (uint32 i = 0; i < layoutData.numBindings; i++) {
            switch (layoutData.bindings[i].vkBinding.descriptorType) {
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:     ASSERT(dstats.numUniformBuffers); --dstats.numUniformBuffers;  break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: ASSERT(dstats.numDynUniformBuffers); --dstats.numDynUniformBuffers; break;
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:      ASSERT(dstats.numSampledImages);  --dstats.numSampledImages;   break;
            case VK_DESCRIPTOR_TYPE_SAMPLER:            ASSERT(dstats.numSamplers);       --dstats.numSamplers;       break;
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: ASSERT(dstats.numCombinedImageSamplers);    --dstats.numCombinedImageSamplers;  break;
            default:                                    break;
            }
        }
    }

    vkFreeDescriptorSets(gVk.device, gVk.descriptorPool, 1, &dsetData.descriptorSet);
    gVk.pools.descriptorSets.Remove(dset);
}

void gfxUpdateDescriptorSet(GfxDescriptorSet dset, uint32 numBindings, const GfxDescriptorBindingDesc* bindings)
{
    auto findDescriptorBindingByNameHash = 
        [](uint32 nameHash, uint32 numBindings, const GfxDescriptorSetLayoutData::Binding* bindings)->uint32 
    {
        for (uint32 i = 0; i < numBindings; i++) {
            if (nameHash == bindings[i].nameHash) 
                return i;
        }
        return UINT32_MAX;
    };

    GfxDescriptorSetData dsetData; 
    
    {   
        AtomicLockScope lk1(gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SETS]);
        dsetData = gVk.pools.descriptorSets.Data(dset);
    }

    MemTempAllocator tempAlloc;

    AtomicLockScope lk2(gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SET_LAYOUTS]);
    GfxDescriptorSetLayoutData& layoutData = gVk.pools.descriptorSetLayouts.Data(dsetData.layout);
    bool hasImage = false;

    VkWriteDescriptorSet* dsWrites = tempAlloc.MallocTyped<VkWriteDescriptorSet>(layoutData.numBindings);
    ASSERT(numBindings == layoutData.numBindings); // can be removed in case we wanted to update sets partially

    VkDescriptorBufferInfo* bufferInfos = tempAlloc.MallocTyped<VkDescriptorBufferInfo>(numBindings);
    VkDescriptorImageInfo* imageInfos = tempAlloc.MallocTyped<VkDescriptorImageInfo>(numBindings);

    for (uint32 i = 0; i < numBindings; i++) {
        const GfxDescriptorBindingDesc& binding = bindings[i];
            
        // TODO: match binding names. if they don't match, try to find it in the list
        uint32 nameHash = hashFnv32Str(binding.name);
        const GfxDescriptorSetLayoutData::Binding* layoutBinding;
        if (nameHash != layoutData.bindings[i].nameHash) {
            if (uint32 bindingIdx = findDescriptorBindingByNameHash(nameHash, layoutData.numBindings, layoutData.bindings);
                bindingIdx != UINT32_MAX) 
            {
                layoutBinding = &layoutData.bindings[bindingIdx];
            }
            else {
                ASSERT_ALWAYS(0, "Descriptor layout binding '%s' not found", bindings[i].name);
                layoutBinding = nullptr;
            }
        }
        else {
            layoutBinding = &layoutData.bindings[i];
        }

        ASSERT_MSG(layoutBinding->vkBinding.descriptorType == static_cast<VkDescriptorType>(binding.type), 
                    "Descriptor binding type doesn't match with the provided argument: (InShader: %u != Arg: %u)", 
                    uint32(layoutBinding->vkBinding.descriptorType), 
                    uint32(binding.type));

        VkDescriptorBufferInfo* pBufferInfo = nullptr;
        VkDescriptorImageInfo* pImageInfo = nullptr;
        uint32 descriptorCount = 1;

        switch (binding.type) {
        case GfxDescriptorType::UniformBuffer: 
        case GfxDescriptorType::UniformBufferDynamic:
        {
            AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::BUFFERS]);
            const GfxBufferData& bufferData = gVk.pools.buffers.Data(binding.buffer.buffer);
            bufferInfos[i] = {
                .buffer = bufferData.buffer,
                .offset = binding.buffer.offset,
                .range = binding.buffer.size == 0 ? VK_WHOLE_SIZE : binding.buffer.size
            };
            pBufferInfo = &bufferInfos[i];
            break;
        } 
        case GfxDescriptorType::Sampler:
        {
            AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::IMAGES]);
            imageInfos[i] = {
                .sampler = binding.image.IsValid() ? gVk.pools.images.Data(binding.image).sampler : VK_NULL_HANDLE,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
            pImageInfo = &imageInfos[i];
            break;
        }
        case GfxDescriptorType::CombinedImageSampler:
        {
            AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::IMAGES]);
            if (!binding.imageArrayCount) {
                const GfxImageData* imageData = binding.image.IsValid() ? &gVk.pools.images.Data(binding.image) : nullptr;
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
                    const GfxImageData* imageData = binding.imageArray[img].IsValid() ? &gVk.pools.images.Data(binding.imageArray[img]) : nullptr;
                    pImageInfo[img] = VkDescriptorImageInfo {
                        .sampler = imageData ? imageData->sampler : VK_NULL_HANDLE,
                        .imageView = imageData ? imageData->view : VK_NULL_HANDLE,
                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    };
                }
            }
            hasImage = true;
            break;
        }
        case GfxDescriptorType::SampledImage:
        {
            AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::IMAGES]);
            imageInfos[i] = {
                .imageView = binding.image.IsValid() ? gVk.pools.images.Data(binding.image).view : VK_NULL_HANDLE,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
            pImageInfo = &imageInfos[i];
            hasImage = true;
            break;
        }
        default:
            ASSERT_MSG(0, "Descriptor type is not implemented");
            break;
        }

        dsWrites[i] = VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = dsetData.descriptorSet,
            .dstBinding = layoutBinding->vkBinding.binding,
            .dstArrayElement = 0,
            .descriptorCount = descriptorCount,
            .descriptorType = layoutBinding->vkBinding.descriptorType,
            .pImageInfo = pImageInfo,
            .pBufferInfo = pBufferInfo,
            .pTexelBufferView = nullptr
        };
    }

    vkUpdateDescriptorSets(gVk.device, layoutData.numBindings, dsWrites, 0, nullptr);

    // Save descriptor set bindings for texture management (reloads)
    if (hasImage)
        gfxUpdateImageDescriptorSetCache(dset, numBindings, bindings);
}

void gfxCmdBindDescriptorSets(GfxPipeline pipeline, uint32 numDescriptorSets, const GfxDescriptorSet* descriptorSets, 
                              const uint32* dynOffsets, uint32 dynOffsetCount)
{
    ASSERT(numDescriptorSets > 0);
    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;

    MemTempAllocator tempAlloc;
    VkDescriptorSet* descriptorSetsVk = tempAlloc.MallocTyped<VkDescriptorSet>(numDescriptorSets);
    VkPipelineLayout pipLayoutVk;

    {
        AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SETS]);
        for (uint32 i = 0; i < numDescriptorSets; i++) {
            const GfxDescriptorSetData& dsData = gVk.pools.descriptorSets.Data(descriptorSets[i]);
            descriptorSetsVk[i] = dsData.descriptorSet;
        }
    }

    {
        AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::PIPELINES]);
        AtomicLockScope lk2(gVk.pools.locks[GfxObjectPools::PIPELINE_LAYOUTS]);
        pipLayoutVk = gVk.pools.pipelineLayouts.Data(gVk.pools.pipelines.Data(pipeline).pipelineLayout).layout;
    }
    
    vkCmdBindDescriptorSets(cmdBufferVk, VK_PIPELINE_BIND_POINT_GRAPHICS, pipLayoutVk, 
                            0, numDescriptorSets, descriptorSetsVk, dynOffsetCount, dynOffsets);
}

void gfxCmdBindPipeline(GfxPipeline pipeline)
{
    VkPipeline pipVk;
    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

    {
        AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::PIPELINES]);
        pipVk = gVk.pools.pipelines.Data(pipeline).pipeline;
    }

    vkCmdBindPipeline(cmdBufferVk, VK_PIPELINE_BIND_POINT_GRAPHICS, pipVk);
}

const GfxBlendAttachmentDesc* gfxBlendAttachmentDescGetDefault()
{
    static GfxBlendAttachmentDesc desc {
        .enable = true,
        .srcColorBlendFactor = GfxBlendFactor::One,
        .dstColorBlendFactor = GfxBlendFactor::Zero,
        .blendOp = GfxBlendOp::Add,
        .srcAlphaBlendFactor = GfxBlendFactor::One,
        .dstAlphaBlendFactor = GfxBlendFactor::Zero,
        .alphaBlendOp = GfxBlendOp::Add,
        .colorWriteMask = GfxColorComponentFlags::All
    };
    return &desc;
}

const GfxBlendAttachmentDesc* gfxBlendAttachmentDescGetAlphaBlending()
{
    static GfxBlendAttachmentDesc desc {
        .enable = true,
        .srcColorBlendFactor = GfxBlendFactor::SrcAlpha,
        .dstColorBlendFactor = GfxBlendFactor::OneMinusSrcAlpha,
        .blendOp = GfxBlendOp::Add,
        .srcAlphaBlendFactor = GfxBlendFactor::One,
        .dstAlphaBlendFactor = GfxBlendFactor::Zero,
        .alphaBlendOp = GfxBlendOp::Add,
        .colorWriteMask = GfxColorComponentFlags::RGB
    };
    return &desc;
}

// https://android-developers.googleblog.com/2020/02/handling-device-orientation-efficiently.html
static Pair<Int2, Int2> gfxTransformRectangleBasedOnOrientation(int x, int y, int w, int h, bool isSwapchain)
{
    int bufferWidth = appGetFramebufferWidth();
    int bufferHeight = appGetFramebufferHeight();

    if (!isSwapchain)
        return Pair<Int2, Int2>(Int2(x, y), Int2(w, h));

    switch (appGetFramebufferTransform()) {
    case AppFramebufferTransform::None:     
        return Pair<Int2, Int2>(Int2(x, y), Int2(w, h));
    case AppFramebufferTransform::Rotate90:
        Swap(bufferWidth, bufferHeight);
        return Pair<Int2, Int2>(
           Int2(bufferWidth - h - y, x),
           Int2(h, w));
    case AppFramebufferTransform::Rotate180:
        return Pair<Int2, Int2>(
            Int2(bufferWidth - w - x, bufferHeight - h - y),
            Int2(w, h));
    case AppFramebufferTransform::Rotate270:
        Swap(bufferWidth, bufferHeight);
        return Pair<Int2, Int2>(
           Int2(y, bufferHeight - w - x),
           Int2(h, w));
    }

    return Pair<Int2, Int2>(Int2(x, y), Int2(w, h));
}

void gfxCmdSetScissors(uint32 firstScissor, uint32 numScissors, const Recti* scissors, bool isSwapchain)
{
    ASSERT(numScissors);

    MemTempAllocator tmpAlloc;
    VkRect2D* scissorsVk = tmpAlloc.MallocTyped<VkRect2D>(numScissors);
    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

    for (uint32 i = 0; i < numScissors; i++) {
        const Recti& scissor = scissors[i];
        Pair<Int2, Int2> transformed = 
            gfxTransformRectangleBasedOnOrientation(scissor.xmin, scissor.ymin, 
                                                    rectiWidth(scissor), rectiHeight(scissor), isSwapchain);
        scissorsVk[i].offset.x = transformed.first.x;
        scissorsVk[i].offset.y = transformed.first.y;
        scissorsVk[i].extent.width = transformed.second.x;
        scissorsVk[i].extent.height = transformed.second.y;
    }

    vkCmdSetScissor(cmdBufferVk, firstScissor, numScissors, scissorsVk);                    
}

void gfxCmdSetViewports(uint32 firstViewport, uint32 numViewports, const GfxViewport* viewports, bool isSwapchain)
{
    ASSERT(numViewports);

    MemTempAllocator tmpAlloc;
    VkViewport* viewportsVk = tmpAlloc.MallocTyped<VkViewport>(numViewports);
    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

    for (uint32 i = 0; i < numViewports; i++) {
        Pair<Int2, Int2> transformed = 
            gfxTransformRectangleBasedOnOrientation(int(viewports[i].x), int(viewports[i].y), 
                                                    int(viewports[i].width), int(viewports[i].height), isSwapchain);

        viewportsVk[i].x = float(transformed.first.x);
        viewportsVk[i].y = float(transformed.first.y);
        viewportsVk[i].width = float(transformed.second.x);
        viewportsVk[i].height = float(transformed.second.y);
        viewportsVk[i].minDepth = viewports[i].minDepth;
        viewportsVk[i].maxDepth = viewports[i].maxDepth;        
    }

    vkCmdSetViewport(cmdBufferVk, firstViewport, numViewports, viewportsVk);
}

void gfxCmdDraw(uint32 vertexCount, uint32 instanceCount, uint32 firstVertex, uint32 firstInstance)
{
    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

    vkCmdDraw(cmdBufferVk, vertexCount, instanceCount, firstVertex, firstInstance);

}

void gfxCmdDrawIndexed(uint32 indexCount, uint32 instanceCount, uint32 firstIndex, uint32 vertexOffset, uint32 firstInstance)
{
    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

    vkCmdDrawIndexed(cmdBufferVk, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void gfxCmdBindVertexBuffers(uint32 firstBinding, uint32 numBindings, const GfxBuffer* vertexBuffers, const uint64* offsets)
{
    static_assert(sizeof(uint64) == sizeof(VkDeviceSize));

    VkBuffer* buffersVk = (VkBuffer*)alloca(sizeof(VkBuffer)*numBindings);
    ASSERT_ALWAYS(buffersVk, "Out of stack memory");

    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

    { 
        AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::BUFFERS]);
        for (uint32 i = 0; i < numBindings; i++) {
            const GfxBufferData& vb = gVk.pools.buffers.Data(vertexBuffers[i]);
            buffersVk[i] = vb.buffer;
        }
    }
    
    vkCmdBindVertexBuffers(cmdBufferVk, firstBinding, numBindings, buffersVk, reinterpret_cast<const VkDeviceSize*>(offsets));
}

void gfxCmdBindIndexBuffer(GfxBuffer indexBuffer, uint64 offset, GfxIndexType indexType)
{
    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

    VkBuffer bufferVk;
    {
        AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::BUFFERS]);
        bufferVk = gVk.pools.buffers.Data(indexBuffer).buffer;
    }

    vkCmdBindIndexBuffer(cmdBufferVk, bufferVk, static_cast<VkDeviceSize>(offset), static_cast<VkIndexType>(indexType));
}

void gfxWaitForIdle()
{
    if (gVk.gfxQueue)
        vkQueueWaitIdle(gVk.gfxQueue);
}

GfxImageInfo gfxImageGetInfo(GfxImage img)
{
    AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::IMAGES]);
    const GfxImageData& data = gVk.pools.images.Data(img);
    return GfxImageInfo {
        .width = data.width,
        .height = data.height,
        .memUsage = data.memUsage,
        .sizeBytes = data.sizeBytes
    };
}

void _private::gfxRecreatePipelinesWithNewShader(uint32 shaderHash, Shader* shader)
{
    MutexScope mtx(gVk.shaderPipelinesTableMtx);
    uint32 index = gVk.shaderPipelinesTable.Find(shaderHash);
    if (index != UINT32_MAX) {
        const Array<GfxPipeline>& pipelineList = gVk.shaderPipelinesTable.Get(index);

        MemTempAllocator tmpAlloc;
        GfxPipelineData* pipDatas;
        {
            AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::PIPELINES]);
            pipDatas = tmpAlloc.MallocTyped<GfxPipelineData>(pipelineList.Count());
            for (uint32 i = 0; i < pipelineList.Count(); i++) {
                const GfxPipelineData& srcData = gVk.pools.pipelines.Data(pipelineList[i]);
                pipDatas[i] = srcData;
                pipDatas[i].gfxCreateInfo = memAllocCopy<VkGraphicsPipelineCreateInfo>(srcData.gfxCreateInfo, 1, &tmpAlloc);
            }
        }
        
        for (uint32 i = 0; i < pipelineList.Count(); i++) {
            const GfxPipelineData& pipData = pipDatas[i];
           
            // Recreate shaders only
            const ShaderStageInfo* vsInfo = shaderGetStage(*shader, ShaderStage::Vertex);
            const ShaderStageInfo* fsInfo = shaderGetStage(*shader, ShaderStage::Fragment);
            if (!vsInfo || !fsInfo) {
                logError("Gfx: Pipeline failed. Shader doesn't have vs/fs stages: %s", shader->name);
                return;
            }

            VkPipelineShaderStageCreateInfo shaderStages[] = {
                gfxCreateShaderStageVk(*vsInfo, gfxCreateShaderModuleVk(shader->name, vsInfo->data.Get(), vsInfo->dataSize)),
                gfxCreateShaderStageVk(*fsInfo, gfxCreateShaderModuleVk(shader->name, fsInfo->data.Get(), fsInfo->dataSize))
            };

            memcpy((void*)pipData.gfxCreateInfo->pStages, shaderStages, 
                sizeof(VkPipelineShaderStageCreateInfo)*pipData.gfxCreateInfo->stageCount);

            VkPipeline pipeline;
            if (VK_FAILED(vkCreateGraphicsPipelines(gVk.device, VK_NULL_HANDLE, 1, pipData.gfxCreateInfo, &gVk.allocVk, &pipeline))) {
                logError("Gfx: Creating graphics pipeline failed");
                return;
            }

            if (pipData.pipeline) {
                MutexScope mtxGarbage(gVk.garbageMtx);
                gVk.garbage.Push(GfxGarbage {
                    .type = GfxGarbage::Type::Pipeline,
                    .frameIdx = engineFrameIndex(),
                    .pipeline = pipData.pipeline
                });
            }

            for (uint32 sidx = 0; sidx < CountOf(shaderStages); sidx++) 
                vkDestroyShaderModule(gVk.device, shaderStages[sidx].module, &gVk.allocVk);

            AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::PIPELINES]);
            gVk.pools.pipelines.Data(pipelineList[i]).pipeline = pipeline;
        }   // For each pipeline in the list
    }
}

const GfxPhysicalDeviceProperties& gfxGetPhysicalDeviceProperties()
{
    static GfxPhysicalDeviceProperties props;
    static bool propsInit = false;

    if (!propsInit) {
        props = GfxPhysicalDeviceProperties {
            .limits = {
                .timestampPeriod = gVk.deviceProps.limits.timestampPeriod,
                .minTexelBufferOffsetAlignment = uint32(gVk.deviceProps.limits.minTexelBufferOffsetAlignment),
                .minUniformBufferOffsetAlignment = uint32(gVk.deviceProps.limits.minUniformBufferOffsetAlignment),
                .minStorageBufferOffsetAlignment = uint32(gVk.deviceProps.limits.minStorageBufferOffsetAlignment)
            }
        };
    }

    return props;
}

void* GfxHeapAllocator::Malloc(size_t size, uint32 align)
{
    void* ptr = gVk.runtimeAlloc.Malloc(size, align);
    TracyCAllocN(ptr, size, kGfxAllocName);
    return ptr;
}

void* GfxHeapAllocator::Realloc(void* ptr, size_t size, uint32 align)
{
    [[maybe_unused]] void* freePtr = ptr;

    ptr = gVk.runtimeAlloc.Realloc(ptr, size, align);

    #ifdef TRACY_ENABLE
        if (freePtr) {
            TracyCFreeN(freePtr, kGfxAllocName);
        }
        TracyCAllocN(ptr, size, kGfxAllocName);
    #endif

    return ptr;
}

void  GfxHeapAllocator::Free(void* ptr, uint32 align)
{
    gVk.runtimeAlloc.Free(ptr, align);
    TracyCFreeN(ptr, kGfxAllocName);
}
    
void* GfxHeapAllocator::vkAlloc(void*, size_t size, size_t align, VkSystemAllocationScope)
{
    // Align to minimum of 32 bytes 
    // because we don't know the size of alignment on free, we need to always force alignment!
    if (gVk.tlsfAlloc.IsDebugMode()) {
        uint32 minAlign = CONFIG_MACHINE_ALIGNMENT << 1;
        align = Max(minAlign, uint32(align));
    }
    void* ptr = gVk.runtimeAlloc.Malloc(size, uint32(align));
    TracyCAllocN(ptr, size, kVulkanAllocName);
    return ptr;
}

void* GfxHeapAllocator::vkRealloc(void*, void* pOriginal, size_t size, size_t align, VkSystemAllocationScope)
{
    [[maybe_unused]] void* freePtr = pOriginal;
    if (gVk.tlsfAlloc.IsDebugMode()) {
        uint32 minAlign = CONFIG_MACHINE_ALIGNMENT << 1;
        align = Max(minAlign, uint32(align));
    }
    void* ptr = gVk.runtimeAlloc.Realloc(pOriginal, size, uint32(align));

    #ifdef TRACY_ENABLE
        if (freePtr) {
            TracyCFreeN(freePtr, kVulkanAllocName);
        }
        TracyCAllocN(ptr, size, kVulkanAllocName);
    #endif

    return ptr;
}

void GfxHeapAllocator::vkFree(void*, void* pPtr)
{
    // TODO: we have to know the alignment here, this is not exactly the best approach
    if (gVk.tlsfAlloc.IsDebugMode())
        gVk.runtimeAlloc.Free(pPtr, CONFIG_MACHINE_ALIGNMENT << 1);
    else 
        gVk.runtimeAlloc.Free(pPtr);
    TracyCFreeN(pPtr, kVulkanAllocName);
}

void GfxHeapAllocator::vkInternalAllocFn(void*, size_t, VkInternalAllocationType, VkSystemAllocationScope)
{
    // TODO
}

void GfxHeapAllocator::vkInternalFreeFn(void*, size_t, VkInternalAllocationType, VkSystemAllocationScope)
{
    // TODO
}

void gfxGetBudgetStats(GfxBudgetStats* stats)
{
    stats->maxBuffers = _limits::kGfxMaxBuffers;
    stats->maxImages = _limits::kGfxMaxImages;
    stats->maxDescriptorSets = _limits::kGfxMaxDescriptorSets;
    stats->maxPipelines = _limits::kGfxMaxPipelines;
    stats->maxPipelineLayouts = _limits::kGfxMaxPipelineLayouts;
    stats->maxGarbage = _limits::kGfxMaxGarbage;

    stats->numBuffers = gVk.pools.buffers.Count();
    stats->numImages = gVk.pools.images.Count();
    stats->numDescriptorSets = gVk.pools.descriptorSets.Count();
    stats->numPipelines = gVk.pools.pipelines.Count();
    stats->numPipelineLayouts = gVk.pools.pipelineLayouts.Count();
    stats->numGarbage = gVk.garbage.Count();

    stats->initHeapStart = gVk.initHeapStart;
    stats->initHeapSize = gVk.initHeapSize;

    stats->runtimeHeapSize = gVk.tlsfAlloc.GetAllocatedSize();
    stats->runtimeHeapMax = _limits::kGfxRuntimeSize;
    
    stats->runtimeHeap = &gVk.tlsfAlloc;

    memcpy(&stats->descriptors, &gVk.descriptorStats, sizeof(gVk.descriptorStats));
}

Mat4 gfxGetClipspaceTransform()
{
    switch (appGetFramebufferTransform()) {
    case AppFramebufferTransform::None:           return kMat4Ident;
    case AppFramebufferTransform::Rotate90:       return mat4RotateZ(kPIHalf);
    case AppFramebufferTransform::Rotate180:      return mat4RotateZ(kPI);
    case AppFramebufferTransform::Rotate270:      return mat4RotateZ(kPI + kPIHalf);
    }

    return kMat4Ident;
}

bool gfxIsRenderingToSwapchain()
{
    return gCmdBufferThreadData.renderingToSwapchain;
}

float gfxGetRenderTimeNs()
{
    if (!gVk.deviceProps.limits.timestampComputeAndGraphics)
        return 0;

    uint64 frameTimestamps[2];
    // Try getting results for the last successfully queried frame
    for (uint32 i = kMaxFramesInFlight; i-- > 0;) {
        uint32 frame = (gVk.currentFrameIdx + i)%kMaxFramesInFlight;
        if (vkGetQueryPoolResults(gVk.device, gVk.queryPool[frame], 0, 2, sizeof(frameTimestamps), frameTimestamps, 
                                  sizeof(uint64), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
        {
            return float(frameTimestamps[1] - frameTimestamps[0]) * gVk.deviceProps.limits.timestampPeriod;
        }
    }
    
    return 0;
}

#endif // __GRAPHICS_VK_CPP__
