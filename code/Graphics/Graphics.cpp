#include "Graphics.h"

// define VULKAN_API_WORKAROUND_10XEDITOR for the 10x cpp parser workaround
#ifndef __10X__
    #define VK_NO_PROTOTYPES
#endif

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

#ifndef __10X__
    #define VOLK_VULKAN_H_PATH "../External/vulkan/include/vulkan.h"
    #include "../External/volk/volk.h"
#endif

#include "../Core/StringUtil.h"
#include "../Core/System.h"
#include "../Core/Hash.h"
#include "../Core/Settings.h"
#include "../Core/Atomic.h"
#include "../Core/Log.h"
#include "../Core/BlitSort.h"
#include "../Core/TracyHelper.h"
#include "../Core/MathAll.h"
#include "../Core/TracyHelper.h"
#include "../Core/Debug.h"

#include "../Common/VirtualFS.h"
#include "../Common/Application.h"
#include "../Common/JunkyardSettings.h"

#include "ValidateEnumsVk.inl"

#include "../Engine.h"


//    ██╗   ██╗███╗   ███╗ █████╗ 
//    ██║   ██║████╗ ████║██╔══██╗
//    ██║   ██║██╔████╔██║███████║
//    ╚██╗ ██╔╝██║╚██╔╝██║██╔══██║
//     ╚████╔╝ ██║ ╚═╝ ██║██║  ██║
//      ╚═══╝  ╚═╝     ╚═╝╚═╝  ╚═╝
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4100)    // unreferenced formal parameter
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4189)    // local variable is initialized but not referenced
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4127)    // conditional expression is constant
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4324)    // structure was padded due to alignment specifier
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


//     ██████╗ ██╗      ██████╗ ██████╗  █████╗ ██╗     ███████╗
//    ██╔════╝ ██║     ██╔═══██╗██╔══██╗██╔══██╗██║     ██╔════╝
//    ██║  ███╗██║     ██║   ██║██████╔╝███████║██║     ███████╗
//    ██║   ██║██║     ██║   ██║██╔══██╗██╔══██║██║     ╚════██║
//    ╚██████╔╝███████╗╚██████╔╝██████╔╝██║  ██║███████╗███████║
//     ╚═════╝ ╚══════╝ ╚═════╝ ╚═════╝ ╚═╝  ╚═╝╚══════╝╚══════╝
static constexpr uint32 MAX_SWAP_CHAIN_IMAGES = 3;
static constexpr uint32 MAX_FRAMES_IN_FLIGHT = 4;
static constexpr uint32 kMaxDescriptorSetLayoutPerPipeline = 3;

#ifdef TRACY_ENABLE
static constexpr const char* kGfxAllocName = "Graphics";
static constexpr const char* kVulkanAllocName = "Vulkan";
#endif

namespace _limits
{
    static constexpr uint32 kGfxMaxBuffers = 2048;
    static constexpr uint32 kGfxMaxImages = 2048;
    static constexpr uint32 kGfxMaxDescriptorSets = 256;
    static constexpr uint32 kGfxMaxDescriptorSetLayouts = 256;
    static constexpr uint32 kGfxMaxPipelines = 256;
    static constexpr uint32 kGfxMaxPipelineLayouts = 256;
    static constexpr uint32 kGfxMaxGarbage = 4096;
    static constexpr size_t kGfxRuntimeSize = 64*SIZE_MB;
}

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
    VkImage images[MAX_SWAP_CHAIN_IMAGES];               // count: numImages
    VkImageView imageViews[MAX_SWAP_CHAIN_IMAGES];       // count: numImages
    VkFramebuffer framebuffers[MAX_SWAP_CHAIN_IMAGES];   // count: numImages
    VkExtent2D extent;
    VkFormat colorFormat;
    VkRenderPass renderPass;
    GfxImageHandle depthImage;
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
    GfxDescriptorSetLayoutHandle     descriptorSetLayouts[kMaxDescriptorSetLayoutPerPipeline];
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
    GfxPipelineLayoutHandle pipelineLayout;
    VkGraphicsPipelineCreateInfo* gfxCreateInfo;    // Keep this to be able recreate pipelines anytime
    uint32 shaderHash;
    uint32 numShaderParams;
    GfxShaderParameterInfo* shaderParams;

#if !CONFIG_FINAL_BUILD
    void*          stackframes[8];
    uint16         numStackframes;
#endif
};

struct GfxDescriptorSetData
{
    GfxDescriptorSetLayoutHandle layout;
    VkDescriptorSet descriptorSet;

#if !CONFIG_FINAL_BUILD
    void*          stackframes[8];
    uint16         numStackframes;
#endif
};

struct GfxCommandBufferThreadData
{
    uint64 lastResetFrame;
    VkCommandPool commandPools[MAX_FRAMES_IN_FLIGHT];
    VkCommandBuffer curCmdBuffer;
    Array<VkCommandBuffer> freeLists[MAX_FRAMES_IN_FLIGHT];
    Array<VkCommandBuffer> cmdBuffers[MAX_FRAMES_IN_FLIGHT];
    bool initialized;
    bool deferredCmdBuffer;
    bool renderingToSwapchain;
};

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

    ReadWriteMutex locks[POOL_COUNT];

    HandlePool<GfxBufferHandle, GfxBufferData> buffers;
    HandlePool<GfxImageHandle, GfxImageData> images;
    HandlePool<GfxPipelineLayoutHandle, GfxPipelineLayoutData> pipelineLayouts;
    HandlePool<GfxPipelineHandle, GfxPipelineData> pipelines;
    HandlePool<GfxDescriptorSetHandle, GfxDescriptorSetData> descriptorSets;
    HandlePool<GfxDescriptorSetLayoutHandle, GfxDescriptorSetLayoutData> descriptorSetLayouts;

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

struct GfxHeapAllocator final : MemAllocator
{
    void* Malloc(size_t size, uint32 align) override;
    void* Realloc(void* ptr, size_t size, uint32 align) override;
    void  Free(void* ptr, uint32 align) override;
    MemAllocatorType GetType() const override { return MemAllocatorType::Heap; }
    
    static void* vkAlloc(void* pUserData, size_t size, size_t align, VkSystemAllocationScope allocScope);
    static void* vkRealloc(void* pUserData, void* pOriginal, size_t size, size_t align, VkSystemAllocationScope allocScope);
    static void vkFree(void* pUserData, void* pPtr);
    static void vkInternalAllocFn(void* pUserData, size_t size, VkInternalAllocationType allocType, VkSystemAllocationScope allocScope);
    static void vkInternalFreeFn(void* pUserData, size_t size, VkInternalAllocationType allocType, VkSystemAllocationScope allocScope); 
};

struct GfxContext
{
    MemTlsfAllocator tlsfAlloc;
    uint8 _padding1[alignof(MemThreadSafeAllocator) - sizeof(MemTlsfAllocator)];
    MemThreadSafeAllocator runtimeAlloc;   // All allocations during runtime are allocated from this
    GfxHeapAllocator alloc;
    VkAllocationCallbacks allocVk;

    VkInstance instance;
    GfxApiVersion apiVersion;
    uint32 numInstanceExtensions;
    uint32 numDeviceExtensions;
    uint32 numLayers;
    VkExtensionProperties* instanceExtensions;
    VkExtensionProperties* deviceExtensions;
    VkLayerProperties* layers;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkDebugReportCallbackEXT debugReportCallback;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceProperties deviceProps;
    VkPhysicalDeviceVulkan11Properties deviceProps11;
    VkPhysicalDeviceVulkan12Properties deviceProps12;
    VkPhysicalDeviceFeatures deviceFeatures;
    uint32 _padding2;
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

    VkQueryPool queryPool[MAX_FRAMES_IN_FLIGHT];
    AtomicUint32 queryFirstCall;
    uint32 _padding3;

    VkSemaphore imageAvailSemaphores[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore renderFinishedSemaphores[MAX_FRAMES_IN_FLIGHT];
    VkFence inflightFences[MAX_FRAMES_IN_FLIGHT];
    VkFence inflightImageFences[MAX_SWAP_CHAIN_IMAGES];  // count: Swapchain.numImages
    Array<GfxGarbage> garbage;

    AtomicUint32 currentFrameIdx;
    uint32       prevFrameIdx;
    VmaAllocator vma;
    GfxObjectPools pools;

    Mutex shaderPipelinesTableMtx;
    Mutex garbageMtx;
    
    SpinLockMutex pendingCmdBuffersLock;
    StaticArray<VkCommandBuffer, 32> pendingCmdBuffers;
    HashTable<Array<GfxPipelineHandle>> shaderPipelinesTable;
    Array<GfxDeferredCommand> deferredCmds;

    size_t initHeapStart;
    size_t initHeapSize;

    SpinLockMutex threadDataLock;
    StaticArray<GfxCommandBufferThreadData*, 32> initializedThreadData;

    Blob deferredCmdBuffer;
    Mutex deferredCommandsMtx;

    GfxBudgetStats::DescriptorBudgetStats descriptorStats;

    _private::GfxUpdateImageDescriptorCallback updateImageDescCallback;

    bool hasAstcDecodeMode;     // VK_EXT_astc_decode_mode extension is available. use it for ImageViews
    bool hasDebugUtils;
    bool hasPipelineExecutableProperties;
    bool hasMemoryBudget;
    bool hasHostQueryReset;
    bool hasFloat16Support;
    bool hasDescriptorIndexing;
    bool hasPushDescriptor;
    bool hasNonSemanticInfo;
    bool initialized;
};

static GfxContext gVk;
static thread_local GfxCommandBufferThreadData gCmdBufferThreadData;

#define GFX_LOCK_POOL_TEMP(_type) ReadWriteMutexReadScope CONCAT(mtx, __LINE__)(gVk.pools.locks[GfxObjectPools::_type])
// #define GFX_LOCK_POOL_TEMP(_type)

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

//----------------------------------------------------------------------------------------------------------------------
// @fwd decls

// Debug callbacks
static VKAPI_ATTR VkBool32 VKAPI_CALL gfxDebugUtilsMessageFn(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData);

static VkBool32 gfxDebugReportFn(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
                                 uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage,
                                 void* pUserData);

static VkSurfaceKHR gfxCreateWindowSurface(void* windowHandle);

static GfxSwapchain gfxCreateSwapchain(VkSurfaceKHR surface, uint16 width, uint16 height, 
                                       VkSwapchainKHR oldSwapChain = VK_NULL_HANDLE, bool depth = false);


//    ██╗███╗   ██╗██╗████████╗
//    ██║████╗  ██║██║╚══██╔══╝
//    ██║██╔██╗ ██║██║   ██║   
//    ██║██║╚██╗██║██║   ██║   
//    ██║██║ ╚████║██║   ██║   
//    ╚═╝╚═╝  ╚═══╝╚═╝   ╚═╝   
bool _private::gfxInitialize()
{
    TimerStopWatch stopwatch;

    if (volkInitialize() != VK_SUCCESS) {
        LOG_ERROR("Volk failed to initialize. Possibly VulkanSDK is not installed (or MoltenVK dll is missing on Mac)");
        return false;
    }

    MemBumpAllocatorBase* initHeap = Engine::GetInitHeap();
    gVk.initHeapStart = initHeap->GetOffset();
    
    {
        size_t bufferSize = MemTlsfAllocator::GetMemoryRequirement(_limits::kGfxRuntimeSize);
        gVk.tlsfAlloc.Initialize(_limits::kGfxRuntimeSize, initHeap->Malloc(bufferSize), bufferSize, SettingsJunkyard::Get().engine.debugAllocations);
        gVk.runtimeAlloc.SetAllocator(&gVk.tlsfAlloc);
    }

    const SettingsGraphics& settings = SettingsJunkyard::Get().graphics;

    gVk.allocVk = VkAllocationCallbacks {
        .pUserData = &gVk.alloc,
        .pfnAllocation = GfxHeapAllocator::vkAlloc,
        .pfnReallocation = GfxHeapAllocator::vkRealloc,
        .pfnFree = GfxHeapAllocator::vkFree,
        .pfnInternalAllocation = GfxHeapAllocator::vkInternalAllocFn,
        .pfnInternalFree = GfxHeapAllocator::vkInternalFreeFn
    };

    gVk.pools.Initialize();

    //------------------------------------------------------------------------------------------------------------------
    // Layers
    uint32 numLayers = 0;
    vkEnumerateInstanceLayerProperties(&numLayers, nullptr);
    if (numLayers) {
        gVk.layers = Mem::AllocTyped<VkLayerProperties>(numLayers, initHeap);
        gVk.numLayers = numLayers;

        vkEnumerateInstanceLayerProperties(&numLayers, gVk.layers);
    }

    //------------------------------------------------------------------------------------------------------------------
    // Instance Extensions
    uint32 numInstanceExtensions = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &numInstanceExtensions, nullptr);
    if (numInstanceExtensions) {
        gVk.instanceExtensions = Mem::AllocTyped<VkExtensionProperties>(numInstanceExtensions, initHeap);
        gVk.numInstanceExtensions = numInstanceExtensions;

        vkEnumerateInstanceExtensionProperties(nullptr, &numInstanceExtensions, gVk.instanceExtensions);

        if (settings.listExtensions) {
            LOG_VERBOSE("Instance Extensions (%u):", gVk.numInstanceExtensions);
            for (uint32 i = 0; i < numInstanceExtensions; i++) {
                LOG_VERBOSE("\t%s", gVk.instanceExtensions[i].extensionName);
            }
        }
    }

    //------------------------------------------------------------------------------------------------------------------
    // Instance
    VkApplicationInfo appInfo {};

    auto HasLayer = [](const char* layerName)
    {
        for (uint32 i = 0; i < gVk.numLayers; i++) {
            if (Str::IsEqual(gVk.layers[i].layerName, layerName))
                return true;
        }
        return false;
    };

    // To set our maximum API version, we need to query for VkEnumerateInstanceVersion (vk1.1)
    // As it states in the link below, only vulkan-1.0 implementations throw error if the requested API is greater than 1.0
    // But implementations 1.1 and higher doesn't care for vkCreateInstance
    // https://www.khronos.org/registry/vulkan/specs/1.3-extensions/html/vkspec.html#VkApplicationInfo

    // vkApiVersion is actually the API supported by the Vulkan dll, not the driver itself
    // For driver, we fetch that from DeviceProperties
    uint32 vkApiVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) 
        vkEnumerateInstanceVersion(&vkApiVersion);

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
        if (HasLayer("VK_LAYER_KHRONOS_validation")) {
            enabledLayers.Push("VK_LAYER_KHRONOS_validation");
        }
        else {
            LOG_ERROR("Gfx: Vulkan backend doesn't have validation layer support. Turn it off in the settings.");
            return false;
        }
    }

    instCreateInfo.enabledLayerCount = enabledLayers.Count();
    instCreateInfo.ppEnabledLayerNames = enabledLayers.Ptr();
   
    //------------------------------------------------------------------------------------------------------------------
    // Instance extensions
    StaticArray<const char*, 32> enabledInstanceExtensions;
    for (uint32 i = 0; i < sizeof(kGfxVkExtensions)/sizeof(const char*); i++)
        enabledInstanceExtensions.Push(kGfxVkExtensions[i]);
    
    // Enable Validation
    VkValidationFeaturesEXT validationFeatures;
    StaticArray<VkValidationFeatureEnableEXT, 5> validationFeatureFlags;
    if constexpr (!CONFIG_FINAL_BUILD) {
        if (gfxHasInstanceExtension("VK_EXT_debug_utils"))
            enabledInstanceExtensions.Push("VK_EXT_debug_utils");
        else if (gfxHasInstanceExtension("VK_EXT_debug_report"))
            enabledInstanceExtensions.Push("VK_EXT_debug_report");

        bool validateFeatures = settings.validateBestPractices || settings.validateSynchronization;
        // TODO: How can we know we have VK_Validation_Features ? 
        //       Because it is only enabled when debug layer is activated
        if (validateFeatures/* && gfxHasInstanceExtension("VK_EXT_validation_features")*/) {
            enabledInstanceExtensions.Push("VK_EXT_validation_features");
            
            if (settings.validateBestPractices)
                validationFeatureFlags.Push(VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
            if (settings.validateSynchronization)
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
    
    // physical device properties is always enabled if exists
    if (gfxHasInstanceExtension("VK_KHR_get_physical_device_properties2"))
        enabledInstanceExtensions.Push("VK_KHR_get_physical_device_properties2");

    instCreateInfo.enabledExtensionCount = enabledInstanceExtensions.Count();
    instCreateInfo.ppEnabledExtensionNames = enabledInstanceExtensions.Ptr();
    
    if (enabledLayers.Count()) {
        LOG_VERBOSE("Enabled instance layers:");
        for (const char* layer: enabledLayers)
            LOG_VERBOSE("\t%s", layer);
    }

    if (enabledInstanceExtensions.Count()) {
        LOG_VERBOSE("Enabled instance extensions:");
        for (const char* ext: enabledInstanceExtensions) 
            LOG_VERBOSE("\t%s", ext);
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
        LOG_ERROR("Gfx: Creating vulkan instance failed: %s", errorCode);
        return false;
    }
    LOG_INFO("(init) Vulkan instance created");

    volkLoadInstance(gVk.instance);

    //------------------------------------------------------------------------------------------------------------------
    // Validation layer and callbacks
    if constexpr (!CONFIG_FINAL_BUILD) {
        if (gfxHasInstanceExtension("VK_EXT_debug_utils")) {
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

            if (vkCreateDebugUtilsMessengerEXT(gVk.instance, &debugUtilsInfo, &gVk.allocVk, &gVk.debugMessenger) != VK_SUCCESS) {
                LOG_ERROR("Gfx: vkCreateDebugUtilsMessengerEXT failed");
                return false;
            }

            gVk.hasDebugUtils = true;
        }
        else if (gfxHasInstanceExtension("VK_EXT_debug_report")) {
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
            
            if (vkCreateDebugReportCallbackEXT(gVk.instance, &debugReportInfo, &gVk.allocVk, &gVk.debugReportCallback) != VK_SUCCESS) {
                LOG_ERROR("Gfx: vkCreateDebugReportCallbackEXT failed");
                return false;
            }
        }
    }

    //------------------------------------------------------------------------------------------------------------------
    // Surface (Implementation is platform dependent)
    if (!settings.headless) {
        gVk.surface = gfxCreateWindowSurface(App::GetNativeWindowHandle());
        if (!gVk.surface) {
            LOG_ERROR("Gfx: Creating window surface failed");
            return false;
        }
    }

    //------------------------------------------------------------------------------------------------------------------
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
            LOG_ERROR("Gfx: No compatible vulkan device found");
            return false;
        }

    }
    else {
        LOG_ERROR("Gfx: No compatible vulkan device found");
        return false;
    }

    //------------------------------------------------------------------------------------------------------------------
    // Physical device is created, gather information about driver/hardware and show it before we continue initialization other stuff
    auto HasVulkanVersion = [](GfxApiVersion version)->bool
    {
        return uint32(gVk.apiVersion) >= uint32(version) && 
            uint32(gVk.apiVersion) < uint32(GfxApiVersion::_Vulkan);
    };

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
        LOG_INFO("(init) GPU: %s (%s)", gVk.deviceProps.deviceName, gpuType);
        LOG_INFO("(init) GPU memory: %_$$$llu", heapSize);

        uint32 major = VK_API_VERSION_MAJOR(gVk.deviceProps.apiVersion);
        uint32 minor = VK_API_VERSION_MINOR(gVk.deviceProps.apiVersion);
        LOG_INFO("(init) GPU driver vulkan version: %u.%u", major, minor);

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
        if (HasVulkanVersion(GfxApiVersion::Vulkan_1_2) && gfxHasInstanceExtension("VK_KHR_get_physical_device_properties2")) {
            gVk.deviceProps11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
            VkPhysicalDeviceProperties2 props2 {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                .pNext = &gVk.deviceProps11
            };

            gVk.deviceProps12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
            gVk.deviceProps11.pNext = &gVk.deviceProps12;

            vkGetPhysicalDeviceProperties2KHR(gVk.physicalDevice, &props2);
            
            LOG_INFO("(init) GPU driver: %s - %s", gVk.deviceProps12.driverName, gVk.deviceProps12.driverInfo);
            LOG_INFO("(init) GPU driver conformance version: %d.%d.%d-%d", 
                gVk.deviceProps12.conformanceVersion.major,
                gVk.deviceProps12.conformanceVersion.minor,
                gVk.deviceProps12.conformanceVersion.subminor,
                gVk.deviceProps12.conformanceVersion.patch);
        }

        // Get device features based on the vulkan API
        if (HasVulkanVersion(GfxApiVersion::Vulkan_1_1)) {
            gVk.deviceFeatures11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
            VkPhysicalDeviceFeatures2 features2 {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                .pNext = &gVk.deviceFeatures11
            };
            
            if (HasVulkanVersion(GfxApiVersion::Vulkan_1_2)) {
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

    //------------------------------------------------------------------------------------------------------------------
    // Device extensions
    uint32 numDevExtensions;
    vkEnumerateDeviceExtensionProperties(gVk.physicalDevice, nullptr, &numDevExtensions, nullptr);
    if (numDevExtensions > 0) {
        gVk.numDeviceExtensions = numDevExtensions;
        gVk.deviceExtensions = Mem::AllocTyped<VkExtensionProperties>(numDevExtensions, initHeap);
        vkEnumerateDeviceExtensionProperties(gVk.physicalDevice, nullptr, &numDevExtensions, gVk.deviceExtensions);

        if (settings.listExtensions) {
            LOG_VERBOSE("Device Extensions (%u):", gVk.numDeviceExtensions);
            for (uint32 i = 0; i < numDevExtensions; i++) {
                LOG_VERBOSE("\t%s", gVk.deviceExtensions[i].extensionName);
            }
        }
    }

    //------------------------------------------------------------------------------------------------------------------
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
                queueCreateInfos.Push(queueCreateInfo);
            }
        }
    }

    //------------------------------------------------------------------------------------------------------------------
    // Device Extensions that we need
    gVk.hasAstcDecodeMode = gfxHasDeviceExtension("VK_EXT_astc_decode_mode");
    gVk.hasMemoryBudget = gfxHasDeviceExtension("VK_EXT_memory_budget");

    gVk.hasHostQueryReset = gfxHasDeviceExtension("VK_EXT_host_query_reset");
    if (HasVulkanVersion(GfxApiVersion::Vulkan_1_2) && !gVk.deviceFeatures12.hostQueryReset)
        gVk.hasHostQueryReset = false;

    gVk.hasFloat16Support = gfxHasDeviceExtension("VK_KHR_shader_float16_int8");
    if (HasVulkanVersion(GfxApiVersion::Vulkan_1_2) && !gVk.deviceFeatures12.shaderFloat16)
        gVk.hasFloat16Support = false;

    gVk.hasNonSemanticInfo = gfxHasDeviceExtension("VK_KHR_shader_non_semantic_info");
    gVk.hasDescriptorIndexing = gfxHasDeviceExtension("VK_EXT_descriptor_indexing");
    gVk.hasPushDescriptor = gfxHasDeviceExtension("VK_KHR_push_descriptor");

    StaticArray<const char*, 32> enabledDeviceExtensions;
    if (!settings.headless) {
        if (gfxHasDeviceExtension("VK_KHR_swapchain"))
            enabledDeviceExtensions.Push("VK_KHR_swapchain");
        if (gVk.hasAstcDecodeMode)
            enabledDeviceExtensions.Push("VK_EXT_astc_decode_mode");
    }

    #ifdef TRACY_ENABLE
        if (gfxHasDeviceExtension(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME)) {
            enabledDeviceExtensions.Push(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
        }
    #endif

    if (settings.shaderDumpProperties && 
        gfxHasDeviceExtension("VK_KHR_pipeline_executable_properties") &&
        gfxHasInstanceExtension("VK_KHR_get_physical_device_properties2"))
    {
        gVk.hasPipelineExecutableProperties = true;
        enabledDeviceExtensions.Push("VK_KHR_pipeline_executable_properties");
    }

    if (gVk.hasMemoryBudget)
        enabledDeviceExtensions.Push("VK_EXT_memory_budget");
    if (gVk.hasHostQueryReset)
        enabledDeviceExtensions.Push("VK_EXT_host_query_reset");

    if (gVk.hasFloat16Support)
        enabledDeviceExtensions.Push("VK_KHR_shader_float16_int8");
    if (gVk.hasNonSemanticInfo)
        enabledDeviceExtensions.Push("VK_KHR_shader_non_semantic_info");
    if (gVk.hasDescriptorIndexing)
        enabledDeviceExtensions.Push("VK_EXT_descriptor_indexing");
    if (gVk.hasPushDescriptor)
        enabledDeviceExtensions.Push("VK_KHR_push_descriptor");

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
        LOG_VERBOSE("Enabled device extensions:");
        for (const char* ext : enabledDeviceExtensions) {
            LOG_VERBOSE("\t%s", ext);
        }
    }

    if (vkCreateDevice(gVk.physicalDevice, &devCreateInfo, &gVk.allocVk, &gVk.device) != VK_SUCCESS) {
        LOG_ERROR("Gfx: vkCreateDevice failed");
        return false;
    }

    LOG_INFO("(init) Vulkan device created");
    volkLoadDevice(gVk.device);

    //------------------------------------------------------------------------------------------------------------------
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
            LOG_ERROR("Gfx: Creating VMA allocator failed");
            return false;
        }
    }

    //-------------------------------------------------------------------------------------------------------------------
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

    //------------------------------------------------------------------------------------------------------------------
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
                App::AndroidSetFramebufferTransform(AppFramebufferTransform::Rotate90);
            if (swapchainCaps.currentTransform & VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR)
                App::AndroidSetFramebufferTransform(AppFramebufferTransform::Rotate180);
            if (swapchainCaps.currentTransform & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR)
                App::AndroidSetFramebufferTransform(AppFramebufferTransform::Rotate270);
        #endif
        
        vkGetPhysicalDeviceSurfaceFormatsKHR(gVk.physicalDevice, gVk.surface, &numFormats, nullptr);
        gVk.swapchainSupport.numFormats = numFormats;
        gVk.swapchainSupport.formats = Mem::AllocTyped<VkSurfaceFormatKHR>(numFormats, initHeap);
        vkGetPhysicalDeviceSurfaceFormatsKHR(gVk.physicalDevice, gVk.surface, &numFormats, gVk.swapchainSupport.formats);

        vkGetPhysicalDeviceSurfacePresentModesKHR(gVk.physicalDevice, gVk.surface, &numPresentModes, nullptr);
        gVk.swapchainSupport.numPresentModes = numPresentModes;
        gVk.swapchainSupport.presentModes = Mem::AllocTyped<VkPresentModeKHR>(numPresentModes, initHeap);
        vkGetPhysicalDeviceSurfacePresentModesKHR(gVk.physicalDevice, gVk.surface, &numPresentModes, 
            gVk.swapchainSupport.presentModes);

        gVk.swapchain = gfxCreateSwapchain(gVk.surface, App::GetFramebufferWidth(), App::GetFramebufferHeight(), nullptr, true);
    }

    //------------------------------------------------------------------------------------------------------------------
    // Synchronization
    VkSemaphoreCreateInfo semaphoreCreateInfo {};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceCreateInfo {};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (VK_FAILED(vkCreateSemaphore(gVk.device, &semaphoreCreateInfo, &gVk.allocVk, &gVk.imageAvailSemaphores[i])) ||
            VK_FAILED(vkCreateSemaphore(gVk.device, &semaphoreCreateInfo, &gVk.allocVk, &gVk.renderFinishedSemaphores[i])))
        {
            LOG_ERROR("Gfx: vkCreateSemaphore failed");
            return false;
        }

        if (VK_FAILED(vkCreateFence(gVk.device, &fenceCreateInfo, &gVk.allocVk, &gVk.inflightFences[i]))) {
            LOG_ERROR("Gfx: vkCreateFence failed");
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
            LOG_ERROR("Gfx: Create descriptor pool failed");
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
        gVk.garbage.Reserve(_limits::kGfxMaxGarbage, Mem::Alloc(bufferSize, initHeap), bufferSize);
    }

    LOG_INFO("(init) Gfx initialized");

    //------------------------------------------------------------------------------------------------------------------
    // Profiling
    #ifdef TRACY_ENABLE
        if (settings.enableGpuProfile) {
            if (!gfxInitializeProfiler()) {
                LOG_ERROR("Initializing GPU profiler failed");
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
        for (uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateQueryPool(gVk.device, &queryCreateInfo, &gVk.allocVk, &gVk.queryPool[i]) != VK_SUCCESS) {
                LOG_ERROR("Gfx: Creating main query pool failed");
                return false;
            }
            vkResetQueryPoolEXT(gVk.device, gVk.queryPool[i], 0, 2);
        }
    }

    gVk.initHeapSize = initHeap->GetOffset() - gVk.initHeapStart;
    gfxGetPhysicalDeviceProperties();       // call once just to populate the struct
    gVk.initialized = true;

    LOG_VERBOSE("(init) Graphics initialized (%.1f ms)", stopwatch.ElapsedMS());
    return true;
}

void GfxObjectPools::Initialize()
{
    MemAllocator* initHeap = Engine::GetInitHeap();

    {
        size_t poolSize = HandlePool<GfxBufferHandle, GfxBufferData>::GetMemoryRequirement(_limits::kGfxMaxBuffers);
        buffers.Reserve(_limits::kGfxMaxBuffers, Mem::Alloc(poolSize, initHeap), poolSize);
    }

    {
        size_t poolSize = HandlePool<GfxImageHandle, GfxImageData>::GetMemoryRequirement(_limits::kGfxMaxImages);
        images.Reserve(_limits::kGfxMaxImages, Mem::Alloc(poolSize, initHeap), poolSize);
    }

    {
        size_t poolSize = HandlePool<GfxDescriptorSetHandle, GfxDescriptorSetData>::GetMemoryRequirement(_limits::kGfxMaxDescriptorSets);
        descriptorSets.Reserve(_limits::kGfxMaxDescriptorSets, Mem::Alloc(poolSize, initHeap), poolSize);
    }

    {
        size_t poolSize = HandlePool<GfxDescriptorSetLayoutHandle, GfxDescriptorSetLayoutData>::GetMemoryRequirement(_limits::kGfxMaxDescriptorSetLayouts);
        descriptorSetLayouts.Reserve(_limits::kGfxMaxDescriptorSetLayouts, Mem::Alloc(poolSize, initHeap), poolSize);
    }

    {
        size_t poolSize = HandlePool<GfxPipelineHandle, GfxPipelineData>::GetMemoryRequirement(_limits::kGfxMaxPipelines);
        pipelines.Reserve(_limits::kGfxMaxPipelines, Mem::Alloc(poolSize, initHeap), poolSize);
    }

    {
        size_t poolSize = HandlePool<GfxPipelineLayoutHandle, GfxPipelineLayoutData>::GetMemoryRequirement(_limits::kGfxMaxPipelineLayouts);
        pipelineLayouts.Reserve(_limits::kGfxMaxPipelineLayouts, Mem::Alloc(poolSize, initHeap), poolSize);
    }
}

//    ██╗   ██╗████████╗██╗██╗     
//    ██║   ██║╚══██╔══╝██║██║     
//    ██║   ██║   ██║   ██║██║     
//    ██║   ██║   ██║   ██║██║     
//    ╚██████╔╝   ██║   ██║███████╗
//     ╚═════╝    ╚═╝   ╚═╝╚══════╝
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
        if (Str::IsEqual(gVk.deviceExtensions[i].extensionName, extension)) 
            return true;
    }

    return false;
}

bool gfxHasInstanceExtension(const char* extension)
{
    for (uint32 i = 0; i < gVk.numInstanceExtensions; i++) {
        if (Str::IsEqual(gVk.instanceExtensions[i].extensionName, extension)) 
            return true;
    }

    return false;
}

[[maybe_unused]] INLINE bool gfxFormatIsDepthStencil(GfxFormat fmt)
{
    return  fmt == GfxFormat::D32_SFLOAT ||
            fmt == GfxFormat::D16_UNORM_S8_UINT ||
            fmt == GfxFormat::D24_UNORM_S8_UINT ||
            fmt == GfxFormat::D32_SFLOAT_S8_UINT ||
            fmt == GfxFormat::S8_UINT;
}

[[maybe_unused]] INLINE bool gfxFormatHasDepth(GfxFormat fmt)
{
    return  fmt == GfxFormat::D32_SFLOAT ||
            fmt == GfxFormat::D16_UNORM_S8_UINT ||
            fmt == GfxFormat::D24_UNORM_S8_UINT ||
            fmt == GfxFormat::D32_SFLOAT_S8_UINT;
}

[[maybe_unused]] INLINE bool gfxFormatHasStencil(GfxFormat fmt)
{
    return  fmt == GfxFormat::D24_UNORM_S8_UINT ||
            fmt == GfxFormat::D16_UNORM_S8_UINT ||
            fmt == GfxFormat::D32_SFLOAT_S8_UINT ||
            fmt == GfxFormat::S8_UINT;
}

INLINE const GfxShaderStageInfo* gfxShaderGetStage(const GfxShader& info, GfxShaderStage stage)
{
    for (uint32 i = 0; i < info.numStages; i++) {
        if (info.stages[i].stage == stage)
            return &info.stages[i];
    }
    return nullptr;
}

INLINE const GfxShaderParameterInfo* gfxShaderGetParam(const GfxShader& info, const char* name)
{
    for (uint32 i = 0; i < info.numParams; i++) {
        if (Str::IsEqual(info.params[i].name, name))
            return &info.params[i];
    }
    return nullptr;
}

// https://android-developers.googleblog.com/2020/02/handling-device-orientation-efficiently.html
static Pair<Int2, Int2> gfxTransformRectangleBasedOnOrientation(int x, int y, int w, int h, bool isSwapchain)
{
    int bufferWidth = App::GetFramebufferWidth();
    int bufferHeight = App::GetFramebufferHeight();

    if (!isSwapchain)
        return Pair<Int2, Int2>(Int2(x, y), Int2(w, h));

    switch (App::GetFramebufferTransform()) {
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

//    ██████╗ ███████╗██████╗ ██╗   ██╗ ██████╗ 
//    ██╔══██╗██╔════╝██╔══██╗██║   ██║██╔════╝ 
//    ██║  ██║█████╗  ██████╔╝██║   ██║██║  ███╗
//    ██║  ██║██╔══╝  ██╔══██╗██║   ██║██║   ██║
//    ██████╔╝███████╗██████╔╝╚██████╔╝╚██████╔╝
//    ╚═════╝ ╚══════╝╚═════╝  ╚═════╝  ╚═════╝ 
static VKAPI_ATTR VkBool32 VKAPI_CALL gfxDebugUtilsMessageFn(
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

static VkBool32 gfxDebugReportFn(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
    uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage,
    void* pUserData)
{
    UNUSED(object);
    UNUSED(location);
    UNUSED(messageCode);
    UNUSED(objectType);
    UNUSED(pUserData);

    if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
        LOG_DEBUG("Gfx: [%s] %s", pLayerPrefix, pMessage);
    }
    else if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
        LOG_INFO("Gfx: [%s] %s", pLayerPrefix, pMessage);
    }
    else if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
        LOG_WARNING("Gfx: [%s] %s", pLayerPrefix, pMessage);
    }
    else if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
        LOG_WARNING("Gfx: [%s] (PERFORMANCE) %s", pLayerPrefix, pMessage);
    }
    else if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
        LOG_ERROR("Gfx: [%s] %s", pLayerPrefix, pMessage);
    }

    return VK_FALSE;
}

//     ██████╗███╗   ███╗██████╗     ██████╗ ██╗   ██╗███████╗███████╗███████╗██████╗ 
//    ██╔════╝████╗ ████║██╔══██╗    ██╔══██╗██║   ██║██╔════╝██╔════╝██╔════╝██╔══██╗
//    ██║     ██╔████╔██║██║  ██║    ██████╔╝██║   ██║█████╗  █████╗  █████╗  ██████╔╝
//    ██║     ██║╚██╔╝██║██║  ██║    ██╔══██╗██║   ██║██╔══╝  ██╔══╝  ██╔══╝  ██╔══██╗
//    ╚██████╗██║ ╚═╝ ██║██████╔╝    ██████╔╝╚██████╔╝██║     ██║     ███████╗██║  ██║
//     ╚═════╝╚═╝     ╚═╝╚═════╝     ╚═════╝  ╚═════╝ ╚═╝     ╚═╝     ╚══════╝╚═╝  ╚═╝
static VkCommandBuffer gfxGetNewCommandBuffer()
{
    PROFILE_ZONE();

    uint32 frameIdx = Atomic::LoadExplicit(&gVk.currentFrameIdx, AtomicMemoryOrder::Acquire);
    
    if (!gCmdBufferThreadData.initialized) {
        VkCommandPoolCreateInfo poolCreateInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = 0,
            .queueFamilyIndex = gVk.gfxQueueFamilyIndex
        };

        for (uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vkCreateCommandPool(gVk.device, &poolCreateInfo, &gVk.allocVk, &gCmdBufferThreadData.commandPools[i]) != VK_SUCCESS) {
                ASSERT_MSG(0, "Creating command-pool failed");
                return VK_NULL_HANDLE;
            }

            gCmdBufferThreadData.freeLists[i].SetAllocator(&gVk.alloc);
            gCmdBufferThreadData.cmdBuffers[i].SetAllocator(&gVk.alloc);
        }
        
        gCmdBufferThreadData.lastResetFrame = Engine::GetFrameIndex();
        
        gCmdBufferThreadData.initialized = true;

        // Add to thread data collection for later house-cleaning
        SpinLockMutexScope lock(gVk.threadDataLock);
        gVk.initializedThreadData.Push(&gCmdBufferThreadData);
    }
    else {
        PROFILE_ZONE_NAME("ResetCommandPool");
        // Check if we need to reset command-pools
        // We only reset the command-pools after new frame is started. 
        uint64 engineFrame = Engine::GetFrameIndex();
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

bool gfxBeginCommandBuffer()
{
    ASSERT(gCmdBufferThreadData.curCmdBuffer == VK_NULL_HANDLE);
    ASSERT(!gCmdBufferThreadData.deferredCmdBuffer);
    PROFILE_ZONE();

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
        if (Atomic::CompareExchange_Weak(&gVk.queryFirstCall, &expectedValue, 1)) {
            if (gVk.hasHostQueryReset)
                vkResetQueryPoolEXT(gVk.device, gVk.queryPool[gVk.currentFrameIdx], 0, 2);

            vkCmdWriteTimestamp(gCmdBufferThreadData.curCmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, gVk.queryPool[gVk.currentFrameIdx], 0);
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
    SpinLockMutexScope lock(gVk.pendingCmdBuffersLock);
    gVk.pendingCmdBuffers.Push(gCmdBufferThreadData.curCmdBuffer);
    gCmdBufferThreadData.curCmdBuffer = VK_NULL_HANDLE;
}


//     ██████╗ ██████╗ ███╗   ███╗███╗   ███╗ █████╗ ███╗   ██╗██████╗ ███████╗
//    ██╔════╝██╔═══██╗████╗ ████║████╗ ████║██╔══██╗████╗  ██║██╔══██╗██╔════╝
//    ██║     ██║   ██║██╔████╔██║██╔████╔██║███████║██╔██╗ ██║██║  ██║███████╗
//    ██║     ██║   ██║██║╚██╔╝██║██║╚██╔╝██║██╔══██║██║╚██╗██║██║  ██║╚════██║
//    ╚██████╗╚██████╔╝██║ ╚═╝ ██║██║ ╚═╝ ██║██║  ██║██║ ╚████║██████╔╝███████║
//     ╚═════╝ ╚═════╝ ╚═╝     ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝╚═╝  ╚═══╝╚═════╝ ╚══════╝
static void gfxCmdCopyBufferToImage(VkBuffer buffer, VkImage image, uint32 width, uint32 height, uint32 numMips, 
                                    const uint32* mipOffsets)
{
    VkBufferImageCopy regions[GFX_MAX_MIPS];
    
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
                VkBufferImageCopy regions[GFX_MAX_MIPS];
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

void gfxCmdUpdateBuffer(GfxBufferHandle buffer, const void* data, uint32 size)
{
    ASSERT(data);
    ASSERT(size);

    GfxBufferData bufferData;
    {
        GFX_LOCK_POOL_TEMP(BUFFERS);
        bufferData = gVk.pools.buffers.Data(buffer);
    }
    ASSERT(size <= bufferData.size);
    ASSERT_MSG(bufferData.memUsage != GfxBufferUsage::Immutable, "Immutable buffers cannot be updated");
    ASSERT(bufferData.mappedBuffer);

    if (bufferData.memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        memcpy(bufferData.mappedBuffer, data, size);
        vmaFlushAllocation(gVk.vma, bufferData.allocation, 0, size);
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

void gfxCmdPushConstants(GfxPipelineHandle pipeline, GfxShaderStage stage, const void* data, uint32 size)
{
    VkPipelineLayout pipLayoutVk;
    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

    { 
        ReadWriteMutexReadScope lk1(gVk.pools.locks[GfxObjectPools::PIPELINES]);
        const GfxPipelineData& pipData = gVk.pools.pipelines.Data(pipeline);
        ReadWriteMutexReadScope lk2(gVk.pools.locks[GfxObjectPools::PIPELINE_LAYOUTS]);
        pipLayoutVk = gVk.pools.pipelineLayouts.Data(pipData.pipelineLayout).layout;
    }

    vkCmdPushConstants(cmdBufferVk, pipLayoutVk, static_cast<VkShaderStageFlags>(stage), 0, size, data);
}

void gfxCmdBeginSwapchainRenderPass(Color bgColor)
{
    ASSERT_MSG(gVk.swapchain.imageIdx != UINT32_MAX, "This function must be called within during frame rendering");

    PROFILE_ZONE();

    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

    uint32 imageIdx = gVk.swapchain.imageIdx;

    Float4 bgColor4f = Color::ToFloat4(bgColor);
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
        Atomic::StoreExplicit(&gVk.queryFirstCall, 0, AtomicMemoryOrder::Relaxed);
    }
}

void gfxCmdBindDescriptorSets(GfxPipelineHandle pipeline, uint32 numDescriptorSets, const GfxDescriptorSetHandle* descriptorSets, 
                              const uint32* dynOffsets, uint32 dynOffsetCount)
{
    ASSERT(numDescriptorSets > 0);
    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;

    MemTempAllocator tempAlloc;
    VkDescriptorSet* descriptorSetsVk = tempAlloc.MallocTyped<VkDescriptorSet>(numDescriptorSets);
    VkPipelineLayout pipLayoutVk;

    {
        GFX_LOCK_POOL_TEMP(DESCRIPTOR_SETS);
        for (uint32 i = 0; i < numDescriptorSets; i++) {
            const GfxDescriptorSetData& dsData = gVk.pools.descriptorSets.Data(descriptorSets[i]);
            descriptorSetsVk[i] = dsData.descriptorSet;
        }
    }

    {
        GFX_LOCK_POOL_TEMP(PIPELINES);
        GFX_LOCK_POOL_TEMP(PIPELINE_LAYOUTS);
        pipLayoutVk = gVk.pools.pipelineLayouts.Data(gVk.pools.pipelines.Data(pipeline).pipelineLayout).layout;
    }
    
    vkCmdBindDescriptorSets(cmdBufferVk, VK_PIPELINE_BIND_POINT_GRAPHICS, pipLayoutVk, 
                            0, numDescriptorSets, descriptorSetsVk, dynOffsetCount, dynOffsets);
}

void gfxCmdBindPipeline(GfxPipelineHandle pipeline)
{
    VkPipeline pipVk;
    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

    {
        GFX_LOCK_POOL_TEMP(PIPELINES);
        pipVk = gVk.pools.pipelines.Data(pipeline).pipeline;
    }

    vkCmdBindPipeline(cmdBufferVk, VK_PIPELINE_BIND_POINT_GRAPHICS, pipVk);
}

void gfxCmdSetScissors(uint32 firstScissor, uint32 numScissors, const RectInt* scissors, bool isSwapchain)
{
    ASSERT(numScissors);

    MemTempAllocator tmpAlloc;
    VkRect2D* scissorsVk = tmpAlloc.MallocTyped<VkRect2D>(numScissors);
    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

    for (uint32 i = 0; i < numScissors; i++) {
        const RectInt& scissor = scissors[i];
        Pair<Int2, Int2> transformed = 
            gfxTransformRectangleBasedOnOrientation(scissor.xmin, scissor.ymin, 
                                                    scissor.Width(), scissor.Height(), isSwapchain);
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

void gfxCmdBindVertexBuffers(uint32 firstBinding, uint32 numBindings, const GfxBufferHandle* vertexBuffers, const uint64* offsets)
{
    static_assert(sizeof(uint64) == sizeof(VkDeviceSize));

    VkBuffer* buffersVk = (VkBuffer*)alloca(sizeof(VkBuffer)*numBindings);
    ASSERT_ALWAYS(buffersVk, "Out of stack memory");

    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

    { 
        GFX_LOCK_POOL_TEMP(BUFFERS);
        for (uint32 i = 0; i < numBindings; i++) {
            const GfxBufferData& vb = gVk.pools.buffers.Data(vertexBuffers[i]);
            buffersVk[i] = vb.buffer;
        }
    }
    
    vkCmdBindVertexBuffers(cmdBufferVk, firstBinding, numBindings, buffersVk, reinterpret_cast<const VkDeviceSize*>(offsets));
}

void gfxCmdBindIndexBuffer(GfxBufferHandle indexBuffer, uint64 offset, GfxIndexType indexType)
{
    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

    VkBuffer bufferVk;
    {
        GFX_LOCK_POOL_TEMP(BUFFERS);
        bufferVk = gVk.pools.buffers.Data(indexBuffer).buffer;
    }

    vkCmdBindIndexBuffer(cmdBufferVk, bufferVk, static_cast<VkDeviceSize>(offset), static_cast<VkIndexType>(indexType));
}


//    ███████╗██╗    ██╗ █████╗ ██████╗  ██████╗██╗  ██╗ █████╗ ██╗███╗   ██╗
//    ██╔════╝██║    ██║██╔══██╗██╔══██╗██╔════╝██║  ██║██╔══██╗██║████╗  ██║
//    ███████╗██║ █╗ ██║███████║██████╔╝██║     ███████║███████║██║██╔██╗ ██║
//    ╚════██║██║███╗██║██╔══██║██╔═══╝ ██║     ██╔══██║██╔══██║██║██║╚██╗██║
//    ███████║╚███╔███╔╝██║  ██║██║     ╚██████╗██║  ██║██║  ██║██║██║ ╚████║
//    ╚══════╝ ╚══╝╚══╝ ╚═╝  ╚═╝╚═╝      ╚═════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝╚═╝  ╚═══╝
static VkSurfaceKHR gfxCreateWindowSurface(void* windowHandle)
{
    VkSurfaceKHR surface = nullptr;
    #if PLATFORM_WINDOWS
        VkWin32SurfaceCreateInfoKHR surfaceCreateInfo {
            .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
            .hinstance = (HMODULE)App::GetNativeAppHandle(),
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

static VkRenderPass gfxCreateRenderPassVk(VkFormat format, VkFormat depthFormat = VK_FORMAT_UNDEFINED)
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
    attachments.Push(VkAttachmentDescription {
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
        attachments.Push(VkAttachmentDescription {
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
        LOG_ERROR("Gfx: vkCreateRenderPass failed");
        return VK_NULL_HANDLE;
    }

    return renderPass;
}

static GfxSwapchain gfxCreateSwapchain(VkSurfaceKHR surface, uint16 width, uint16 height, VkSwapchainKHR oldSwapChain, bool depth)
{
    VkSurfaceFormatKHR format {};
    for (uint32 i = 0; i < gVk.swapchainSupport.numFormats; i++) {
        VkFormat fmt = gVk.swapchainSupport.formats[i].format;
        if (SettingsJunkyard::Get().graphics.surfaceSRGB) {
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

    VkPresentModeKHR presentMode = SettingsJunkyard::Get().graphics.enableVsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;

    // Verify that SwapChain has support for this present mode
    bool presentModeIsSupported = false;
    for (uint32 i = 0; i < gVk.swapchainSupport.numPresentModes; i++) {
        if (gVk.swapchainSupport.presentModes[i] == presentMode) {
            presentModeIsSupported = true;
            break;
        }
    }

    if (!presentModeIsSupported) {
        LOG_WARNING("Gfx: PresentMode: %u is not supported by device, choosing default: %u", presentMode, 
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
    if (App::GetFramebufferTransform() == AppFramebufferTransform::Rotate90 || 
        App::GetFramebufferTransform() == AppFramebufferTransform::Rotate270)
    {
       Swap(extent.width, extent.height);
    }

    uint32 minImages = Min(Clamp(gVk.swapchainSupport.caps.minImageCount + 1, 1u, 
                                 gVk.swapchainSupport.caps.maxImageCount), MAX_SWAP_CHAIN_IMAGES);
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
        LOG_ERROR("Gfx: CreateSwapchain failed");
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
            LOG_ERROR("Gfx: Creating Swapchain image views failed");
            gfxDestroySwapchain(&newSwapchain);
            return GfxSwapchain {};
        }
    }

    VkFormat depthFormat = gfxFindDepthFormat();
    if (depth) {
        GfxImageHandle depthImage = gfxCreateImage(GfxImageDesc {
            .width = uint32(extent.width),
            .height = uint32(extent.height),
            .format = static_cast<GfxFormat>(depthFormat),
            .frameBuffer = true
        });

        if (!depthImage.IsValid()) {
            LOG_ERROR("Gfx: Creating Swapchain depth image failed");
            gfxDestroySwapchain(&newSwapchain);
            return GfxSwapchain {};
        }

        newSwapchain.depthImage = depthImage;
    }

    // RenderPasses
    newSwapchain.renderPass = gfxCreateRenderPassVk(format.format, depth ? depthFormat : VK_FORMAT_UNDEFINED);
    if (newSwapchain.renderPass == VK_NULL_HANDLE) {
        gfxDestroySwapchain(&newSwapchain);
        return GfxSwapchain {};
    }

    // Framebuffers
    GFX_LOCK_POOL_TEMP(IMAGES);
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

void gfxResizeSwapchain(uint16 width, uint16 height)
{
    if (gVk.device)
        vkDeviceWaitIdle(gVk.device);
    
    gfxDestroySwapchain(&gVk.swapchain);

    uint32 oldWidth = gVk.swapchain.extent.width;
    uint32 oldHeight = gVk.swapchain.extent.height;
    
    gVk.swapchain = gfxCreateSwapchain(gVk.surface, width, height, nullptr, true);
    LOG_DEBUG("Swapchain resized from %ux%u to %ux%u", oldWidth, oldHeight, width, height);

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
    
    gVk.surface = gfxCreateWindowSurface(App::GetNativeWindowHandle());
    ASSERT(gVk.surface);

    gfxDestroySwapchain(&gVk.swapchain);
    gVk.swapchain = gfxCreateSwapchain(gVk.surface, App::GetFramebufferWidth(), App::GetFramebufferHeight(), nullptr, true);

    if (gVk.device)
        vkDeviceWaitIdle(gVk.device);

    LOG_DEBUG("Window surface (Handle = 0x%x) and swapchain (%ux%u) recreated.", 
             App::GetNativeWindowHandle(), App::GetFramebufferWidth(), App::GetFramebufferHeight());
}


//    ██████╗ ██╗██████╗ ███████╗██╗     ██╗███╗   ██╗███████╗
//    ██╔══██╗██║██╔══██╗██╔════╝██║     ██║████╗  ██║██╔════╝
//    ██████╔╝██║██████╔╝█████╗  ██║     ██║██╔██╗ ██║█████╗  
//    ██╔═══╝ ██║██╔═══╝ ██╔══╝  ██║     ██║██║╚██╗██║██╔══╝  
//    ██║     ██║██║     ███████╗███████╗██║██║ ╚████║███████╗
//    ╚═╝     ╚═╝╚═╝     ╚══════╝╚══════╝╚═╝╚═╝  ╚═══╝╚══════╝
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
        LOG_ERROR("Gfx: vkCreateShaderModule failed: %s", name);
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}

static inline VkPipelineShaderStageCreateInfo gfxCreateShaderStageVk(const GfxShaderStageInfo& shaderStage, VkShaderModule shaderModule)
{
    VkPipelineShaderStageCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = (VkShaderStageFlagBits)shaderStage.stage,
        .module = shaderModule,
        .pName = "main"
    };
        
    return createInfo;
}

static GfxPipelineLayoutHandle gfxCreatePipelineLayout(const GfxShader& shader,
                                                 const GfxDescriptorSetLayoutHandle* descriptorSetLayouts,
                                                 uint32 numDescriptorSetLayouts,
                                                 const GfxPushConstantDesc* pushConstants,
                                                 uint32 numPushConstants,
                                                 VkPipelineLayout* layoutOut)
{
    ASSERT_MSG(numDescriptorSetLayouts <= kMaxDescriptorSetLayoutPerPipeline, "Too many descriptor set layouts per-pipeline");

    // hash the layout bindings and look in cache
    // TODO: cleanup the data for pipeline layout. maybe we can also remove the bindings (?)
    HashMurmur32Incremental hasher(0x5eed1);
    uint32 hash = hasher.Add<GfxDescriptorSetLayoutHandle>(descriptorSetLayouts, numDescriptorSetLayouts)
                        .Add<GfxPushConstantDesc>(pushConstants, numPushConstants)
                        .Hash();

    gVk.pools.locks[GfxObjectPools::PIPELINE_LAYOUTS].EnterRead();
    if (GfxPipelineLayoutHandle pipLayout = gVk.pools.pipelineLayouts.FindIf(
        [hash](const GfxPipelineLayoutData& item)->bool { return item.hash == hash; }); pipLayout.IsValid())
    {
        GfxPipelineLayoutData& item = gVk.pools.pipelineLayouts.Data(pipLayout);
        ++item.refCount;
        gVk.pools.locks[GfxObjectPools::PIPELINE_LAYOUTS].ExitRead();
        if (layoutOut)
            *layoutOut = item.layout;
        return pipLayout;
    }
    else {
        gVk.pools.locks[GfxObjectPools::PIPELINE_LAYOUTS].ExitRead();
    
        MemTempAllocator tempAlloc;
        
        VkDescriptorSetLayout* vkDescriptorSetLayouts = nullptr;
        if (numDescriptorSetLayouts) {   
            GFX_LOCK_POOL_TEMP(DESCRIPTOR_SET_LAYOUTS);
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
                [[maybe_unused]] const GfxShaderParameterInfo* paramInfo = gfxShaderGetParam(shader, pushConstants[i].name);
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
            LOG_ERROR("Gfx: Failed to create pipeline layout");
            return GfxPipelineLayoutHandle();
        }
        
        GFX_LOCK_POOL_TEMP(PIPELINE_LAYOUTS);
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
        if (SettingsJunkyard::Get().graphics.trackResourceLeaks)
            pipLayoutData.numStackframes = Debug::CaptureStacktrace(pipLayoutData.stackframes, (uint16)CountOf(pipLayoutData.stackframes), 2);
        #endif

        pipLayout = gVk.pools.pipelineLayouts.Add(pipLayoutData);
        if (layoutOut)
            *layoutOut = pipelineLayoutVk;
        return pipLayout;
    }
}

static void gfxDestroyPipelineLayout(GfxPipelineLayoutHandle layout)
{
    GfxPipelineLayoutData& layoutData = gVk.pools.pipelineLayouts.Data(layout);
    ASSERT(layoutData.refCount > 0);
    if (--layoutData.refCount == 0) {
        if (layoutData.layout) 
            vkDestroyPipelineLayout(gVk.device, layoutData.layout, &gVk.allocVk);
        memset(&layoutData, 0x0, sizeof(layoutData));

        ReadWriteMutexWriteScope lk(gVk.pools.locks[GfxObjectPools::PIPELINE_LAYOUTS]);
        gVk.pools.pipelineLayouts.Remove(layout);
    }
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
    if (vkGetPipelineExecutablePropertiesKHR(gVk.device, &pipInfo, &numExecutables, NULL) == VK_SUCCESS && numExecutables) {
        VkPipelineExecutablePropertiesKHR* executableProperties = (VkPipelineExecutablePropertiesKHR*)
            alloca(numExecutables*sizeof(VkPipelineExecutablePropertiesKHR));
        ASSERT(executableProperties);
        memset(executableProperties, 0x0, sizeof(VkPipelineExecutablePropertiesKHR)*numExecutables);
        for (uint32 i = 0; i < numExecutables; i++)
            executableProperties[i].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR;

        vkGetPipelineExecutablePropertiesKHR(gVk.device, &pipInfo, &numExecutables, executableProperties);
        for (uint32 i = 0; i < numExecutables; i++) {
            const VkPipelineExecutablePropertiesKHR& ep = executableProperties[i];

            Str::PrintFmt(lineStr, sizeof(lineStr), "%s - %s:\n", ep.name, ep.description);
            info.Write(lineStr, Str::Len(lineStr));

            VkPipelineExecutableInfoKHR pipExecInfo {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR,
                .pipeline = pip,
                .executableIndex = i
            };

            uint32 numStats;
            if (vkGetPipelineExecutableStatisticsKHR(gVk.device, &pipExecInfo, &numStats, nullptr) == VK_SUCCESS && numStats) {
                VkPipelineExecutableStatisticKHR* stats = (VkPipelineExecutableStatisticKHR*)
                    alloca(sizeof(VkPipelineExecutableStatisticKHR)*numStats);
                ASSERT(stats);
                memset(stats, 0x0, sizeof(VkPipelineExecutableStatisticKHR)*numStats);
                for (uint32 statIdx = 0; statIdx < numStats; statIdx++)
                    stats[statIdx].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR;
                vkGetPipelineExecutableStatisticsKHR(gVk.device, &pipExecInfo, &numStats, stats);
                for (uint32 statIdx = 0; statIdx < numStats; statIdx++) {
                    const VkPipelineExecutableStatisticKHR& stat = stats[statIdx];                    

                    char valueStr[32];
                    switch (stat.format) {
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:    
                        Str::Copy(valueStr, sizeof(valueStr), stat.value.b32 ? "True" : "False"); 
                        break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:     
                        Str::PrintFmt(valueStr, sizeof(valueStr), "%lld", stat.value.i64); 
                        break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:    
                        Str::PrintFmt(valueStr, sizeof(valueStr), "%llu", stat.value.u64); 
                        break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:   
                        Str::PrintFmt(valueStr, sizeof(valueStr), "%.3f", stat.value.f64); 
                        break;
                    default: ASSERT(0); break;
                    }

                    Str::PrintFmt(lineStr, sizeof(lineStr), "\t%s = %s\n", stat.name, valueStr);
                    info.Write(lineStr, Str::Len(lineStr));
                }
            }

            // TODO: we don't seem to be getting here, at least for nvidia drivers 
            uint32 numRepr;
            if (vkGetPipelineExecutableInternalRepresentationsKHR && 
                vkGetPipelineExecutableInternalRepresentationsKHR(gVk.device, &pipExecInfo, &numRepr, nullptr) == VK_SUCCESS)
            {
                if (numRepr) {
                    VkPipelineExecutableInternalRepresentationKHR* reprs = (VkPipelineExecutableInternalRepresentationKHR*)
                    alloca(sizeof(VkPipelineExecutableInternalRepresentationKHR)*numRepr);
                    ASSERT(reprs);
                    vkGetPipelineExecutableInternalRepresentationsKHR(gVk.device, &pipExecInfo, &numRepr, reprs);
                    for (uint32 ri = 0; ri < numRepr; ri++) {
                        const VkPipelineExecutableInternalRepresentationKHR& repr = reprs[ri];
                        LOG_DEBUG(repr.name);
                    }
                }
            }
        } // Foreach executable
    } 

    if (info.Size()) {
        // TODO: use async write 
        Path filepath(name);
        filepath.Append(".txt");
        Vfs::WriteFileAsync(filepath.CStr(), info, VfsFlags::AbsolutePath|VfsFlags::TextFile, 
                          [](const char* path, size_t, Blob&, void*) { LOG_VERBOSE("Written shader information to file: %s", path); },
                          nullptr);
    }
}

static VkGraphicsPipelineCreateInfo* gfxDuplicateGraphicsPipelineCreateInfo(const VkGraphicsPipelineCreateInfo& pipelineInfo)
{
    // Child POD members with arrays inside
    MemSingleShotMalloc<VkPipelineVertexInputStateCreateInfo> pallocVertexInputInfo;
    pallocVertexInputInfo.AddMemberArray<VkVertexInputBindingDescription>(
        offsetof(VkPipelineVertexInputStateCreateInfo, pVertexBindingDescriptions), 
        pipelineInfo.pVertexInputState->vertexBindingDescriptionCount);
    pallocVertexInputInfo.AddMemberArray<VkVertexInputAttributeDescription>(
        offsetof(VkPipelineVertexInputStateCreateInfo, pVertexAttributeDescriptions), 
        pipelineInfo.pVertexInputState->vertexAttributeDescriptionCount);

    MemSingleShotMalloc<VkPipelineColorBlendStateCreateInfo> pallocColorBlendState;
    pallocColorBlendState.AddMemberArray<VkPipelineColorBlendAttachmentState>(
        offsetof(VkPipelineColorBlendStateCreateInfo, pAttachments),
        pipelineInfo.pColorBlendState->attachmentCount);

    MemSingleShotMalloc<VkPipelineDynamicStateCreateInfo> pallocDynamicState;
    pallocDynamicState.AddMemberArray<VkDynamicState>(
        offsetof(VkPipelineDynamicStateCreateInfo, pDynamicStates),
        pipelineInfo.pDynamicState->dynamicStateCount);

    // Main fields
    MemSingleShotMalloc<VkGraphicsPipelineCreateInfo, 12> mallocator;

    mallocator.AddMemberArray<VkPipelineShaderStageCreateInfo>(
        offsetof(VkGraphicsPipelineCreateInfo, pStages),
        pipelineInfo.stageCount);
        
    mallocator.AddChildStructSingleShot(pallocVertexInputInfo, offsetof(VkGraphicsPipelineCreateInfo, pVertexInputState), 1);

    mallocator.AddMemberArray<VkPipelineInputAssemblyStateCreateInfo>(
        offsetof(VkGraphicsPipelineCreateInfo, pInputAssemblyState), 1);

    // skip pTessellationState

    mallocator.AddMemberArray<VkPipelineViewportStateCreateInfo>(
        offsetof(VkGraphicsPipelineCreateInfo, pViewportState), 1);
        
    mallocator.AddMemberArray<VkPipelineRasterizationStateCreateInfo>(
        offsetof(VkGraphicsPipelineCreateInfo, pRasterizationState), 1);

    mallocator.AddMemberArray<VkPipelineMultisampleStateCreateInfo>(
        offsetof(VkGraphicsPipelineCreateInfo, pMultisampleState), 1);

    mallocator.AddMemberArray<VkPipelineDepthStencilStateCreateInfo>(
        offsetof(VkGraphicsPipelineCreateInfo, pDepthStencilState), 1);

    mallocator.AddChildStructSingleShot(pallocColorBlendState, offsetof(VkGraphicsPipelineCreateInfo, pColorBlendState), 1);
    mallocator.AddChildStructSingleShot(pallocDynamicState, offsetof(VkGraphicsPipelineCreateInfo, pDynamicState), 1);

    VkGraphicsPipelineCreateInfo* pipInfoNew = mallocator.Calloc(&gVk.alloc);

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

GfxPipelineHandle gfxCreatePipeline(const GfxPipelineDesc& desc)
{
    MemTempAllocator tempAlloc;

    // Shaders
    GfxShader* shaderInfo = desc.shader;
    ASSERT(shaderInfo);

    const GfxShaderStageInfo* vsInfo = gfxShaderGetStage(*shaderInfo, GfxShaderStage::Vertex);
    const GfxShaderStageInfo* fsInfo = gfxShaderGetStage(*shaderInfo, GfxShaderStage::Fragment);
    if (!vsInfo || !fsInfo) {
        LOG_ERROR("Gfx: Pipeline failed. Shader doesn't have vs/fs stages: %s", shaderInfo->name);
        return GfxPipelineHandle();
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
    GfxPipelineLayoutHandle pipelineLayout = gfxCreatePipelineLayout(*shaderInfo, 
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
    const GfxBlendAttachmentDesc* blendAttachmentDescs = !desc.blend.attachments ? GfxBlendAttachmentDesc::GetDefault() : desc.blend.attachments;
        
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
        LOG_ERROR("Gfx: Creating graphics pipeline failed");
        return GfxPipelineHandle();
    }

    if (gVk.hasPipelineExecutableProperties)
        gfxSavePipelineBinaryProperties(desc.shader->name, pipeline);

    for (uint32 i = 0; i < CountOf(shaderStages); i++)
        vkDestroyShaderModule(gVk.device, shaderStages[i].module, &gVk.allocVk);

    GfxPipelineData pipData {
        .pipeline = pipeline,
        .pipelineLayout = pipelineLayout,
        .gfxCreateInfo = gfxDuplicateGraphicsPipelineCreateInfo(pipelineInfo),
        .shaderHash = shaderInfo->hash,
        .numShaderParams = shaderInfo->numParams,
        .shaderParams = Mem::AllocCopy<GfxShaderParameterInfo>(shaderInfo->params.Get(), shaderInfo->numParams, &gVk.alloc)
    };

    #if !CONFIG_FINAL_BUILD
    if (SettingsJunkyard::Get().graphics.trackResourceLeaks)
        pipData.numStackframes = Debug::CaptureStacktrace(pipData.stackframes, (uint16)CountOf(pipData.stackframes), 2);
    #endif
    
    GfxPipelineHandle pip;
    {
        GFX_LOCK_POOL_TEMP(PIPELINES);
        pip = gVk.pools.pipelines.Add(pipData);
    }

    { // Add to shader's used piplines list, so later we could iterate over them to recreate the pipelines
        MutexScope pipTableMtx(gVk.shaderPipelinesTableMtx);
        uint32 index = gVk.shaderPipelinesTable.Find(shaderInfo->hash);
        if (index != -1) {
            gVk.shaderPipelinesTable.GetMutable(index).Push(pip);
        }
        else {
            Array<GfxPipelineHandle>* arr = PLACEMENT_NEW(gVk.shaderPipelinesTable.Add(shaderInfo->hash), Array<GfxPipelineHandle>);
            arr->Push(pip);
        }
    }

    return pip;   
}

void gfxDestroyPipeline(GfxPipelineHandle pipeline)
{
    if (!pipeline.IsValid())
        return;

    GfxPipelineData& pipData = gVk.pools.pipelines.Data(pipeline);

    {   // Remove from shader <-> pipeline table
        MutexScope pipTableMtx(gVk.shaderPipelinesTableMtx);
        uint32 index = gVk.shaderPipelinesTable.Find(pipData.shaderHash);
        if (index != UINT32_MAX) {
            Array<GfxPipelineHandle>& pipList = gVk.shaderPipelinesTable.GetMutable(index);
            uint32 pipIdx = pipList.FindIf([pipeline](const GfxPipelineHandle& pip)->bool { return pip == pipeline; });
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

    Mem::Free(pipData.shaderParams, &gVk.alloc);

    if (pipData.pipelineLayout.IsValid()) 
        gfxDestroyPipelineLayout(pipData.pipelineLayout);
    if (pipData.pipeline)
        vkDestroyPipeline(gVk.device, pipData.pipeline, &gVk.allocVk);

    ReadWriteMutexWriteScope lk(gVk.pools.locks[GfxObjectPools::PIPELINES]);
    gVk.pools.pipelines.Remove(pipeline);
}

void _private::gfxRecreatePipelinesWithNewShader(uint32 shaderHash, GfxShader* shader)
{
    MutexScope mtx(gVk.shaderPipelinesTableMtx);
    uint32 index = gVk.shaderPipelinesTable.Find(shaderHash);
    if (index != UINT32_MAX) {
        const Array<GfxPipelineHandle>& pipelineList = gVk.shaderPipelinesTable.Get(index);

        MemTempAllocator tmpAlloc;
        GfxPipelineData* pipDatas;
        {
            GFX_LOCK_POOL_TEMP(PIPELINES);
            pipDatas = tmpAlloc.MallocTyped<GfxPipelineData>(pipelineList.Count());
            for (uint32 i = 0; i < pipelineList.Count(); i++) {
                const GfxPipelineData& srcData = gVk.pools.pipelines.Data(pipelineList[i]);
                pipDatas[i] = srcData;
                pipDatas[i].gfxCreateInfo = Mem::AllocCopy<VkGraphicsPipelineCreateInfo>(srcData.gfxCreateInfo, 1, &tmpAlloc);
            }
        }
        
        for (uint32 i = 0; i < pipelineList.Count(); i++) {
            const GfxPipelineData& pipData = pipDatas[i];
           
            // Recreate shaders only
            const GfxShaderStageInfo* vsInfo = gfxShaderGetStage(*shader, GfxShaderStage::Vertex);
            const GfxShaderStageInfo* fsInfo = gfxShaderGetStage(*shader, GfxShaderStage::Fragment);
            if (!vsInfo || !fsInfo) {
                LOG_ERROR("Gfx: Pipeline failed. Shader doesn't have vs/fs stages: %s", shader->name);
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
                LOG_ERROR("Gfx: Creating graphics pipeline failed");
                return;
            }

            if (pipData.pipeline) {
                MutexScope mtxGarbage(gVk.garbageMtx);
                gVk.garbage.Push(GfxGarbage {
                    .type = GfxGarbage::Type::Pipeline,
                    .frameIdx = Engine::GetFrameIndex(),
                    .pipeline = pipData.pipeline
                });
            }

            for (uint32 sidx = 0; sidx < CountOf(shaderStages); sidx++) 
                vkDestroyShaderModule(gVk.device, shaderStages[sidx].module, &gVk.allocVk);

            GFX_LOCK_POOL_TEMP(PIPELINES);
            gVk.pools.pipelines.Data(pipelineList[i]).pipeline = pipeline;
        }   // For each pipeline in the list
    }
}

//    ██████╗ ██╗   ██╗███████╗███████╗███████╗██████╗ 
//    ██╔══██╗██║   ██║██╔════╝██╔════╝██╔════╝██╔══██╗
//    ██████╔╝██║   ██║█████╗  █████╗  █████╗  ██████╔╝
//    ██╔══██╗██║   ██║██╔══╝  ██╔══╝  ██╔══╝  ██╔══██╗
//    ██████╔╝╚██████╔╝██║     ██║     ███████╗██║  ██║
//    ╚═════╝  ╚═════╝ ╚═╝     ╚═╝     ╚══════╝╚═╝  ╚═╝

// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html#usage_patterns_advanced_data_uploading
GfxBufferHandle gfxCreateBuffer(const GfxBufferDesc& desc)
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

    if (memUsage == GfxBufferUsage::Stream) {
        bufferCreateInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |  VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    else {
        bufferCreateInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    VmaAllocationInfo allocInfo;
    if (vmaCreateBuffer(gVk.vma, &bufferCreateInfo, &allocCreateInfo, &bufferData.buffer, 
                        &bufferData.allocation, &allocInfo) != VK_SUCCESS)
    {
        ASSERT_MSG(0, "Create buffer failed");
        return GfxBufferHandle();
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
                return GfxBufferHandle();
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
                .frameIdx = Engine::GetFrameIndex(),
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
                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO,
            };
            
            if (vmaCreateBuffer(gVk.vma, &stageBufferCreateInfo, &stageAllocCreateInfo, &bufferData.stagingBuffer, 
                                &bufferData.stagingAllocation, &allocInfo) != VK_SUCCESS)
            {
                vmaDestroyBuffer(gVk.vma, bufferData.buffer, bufferData.allocation);
                ASSERT_MSG(0, "Create staging buffer failed");
                return GfxBufferHandle();
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
    if (SettingsJunkyard::Get().graphics.trackResourceLeaks)
        bufferData.numStackframes = Debug::CaptureStacktrace(bufferData.stackframes, (uint16)CountOf(bufferData.stackframes), 2);
    #endif

    ReadWriteMutexWriteScope lk(gVk.pools.locks[GfxObjectPools::BUFFERS]);
    return gVk.pools.buffers.Add(bufferData);
}

void gfxDestroyBuffer(GfxBufferHandle buffer)
{
    if (!buffer.IsValid())
        return;
    
    GfxBufferData bufferData;
    { 
        GFX_LOCK_POOL_TEMP(BUFFERS);
        bufferData = gVk.pools.buffers.Data(buffer);
    }

    vmaDestroyBuffer(gVk.vma, bufferData.buffer, bufferData.allocation);
    if (bufferData.stagingBuffer) 
        vmaDestroyBuffer(gVk.vma, bufferData.stagingBuffer, bufferData.stagingAllocation);

    { 
        ReadWriteMutexWriteScope lk(gVk.pools.locks[GfxObjectPools::BUFFERS]);
        gVk.pools.buffers.Remove(buffer);
    }
}

//    ██╗███╗   ███╗ █████╗  ██████╗ ███████╗
//    ██║████╗ ████║██╔══██╗██╔════╝ ██╔════╝
//    ██║██╔████╔██║███████║██║  ███╗█████╗  
//    ██║██║╚██╔╝██║██╔══██║██║   ██║██╔══╝  
//    ██║██║ ╚═╝ ██║██║  ██║╚██████╔╝███████╗
//    ╚═╝╚═╝     ╚═╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝
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
        LOG_ERROR("Gfx: CreateImageView failed");
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
        LOG_ERROR("Gfx: CreateSampler failed");
        return VK_NULL_HANDLE;
    }

    return sampler;
}

GfxImageHandle gfxCreateImage(const GfxImageDesc& desc)
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
        return GfxImageHandle();
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
                return GfxImageHandle();
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
                .frameIdx = Engine::GetFrameIndex(),
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
    if (SettingsJunkyard::Get().graphics.trackResourceLeaks)
        imageData.numStackframes = Debug::CaptureStacktrace(imageData.stackframes, (uint16)CountOf(imageData.stackframes), 2);
#endif

    ReadWriteMutexWriteScope lk(gVk.pools.locks[GfxObjectPools::IMAGES]);
    return gVk.pools.images.Add(imageData);
}

void gfxDestroyImage(GfxImageHandle image)
{
    if (!image.IsValid())
        return;

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

    ReadWriteMutexWriteScope lk(gVk.pools.locks[GfxObjectPools::IMAGES]);
    gVk.pools.images.Remove(image);
}


//    ██████╗ ███████╗███████╗ ██████╗██████╗ ██╗██████╗ ████████╗ ██████╗ ██████╗     ███████╗███████╗████████╗
//    ██╔══██╗██╔════╝██╔════╝██╔════╝██╔══██╗██║██╔══██╗╚══██╔══╝██╔═══██╗██╔══██╗    ██╔════╝██╔════╝╚══██╔══╝
//    ██║  ██║█████╗  ███████╗██║     ██████╔╝██║██████╔╝   ██║   ██║   ██║██████╔╝    ███████╗█████╗     ██║   
//    ██║  ██║██╔══╝  ╚════██║██║     ██╔══██╗██║██╔═══╝    ██║   ██║   ██║██╔══██╗    ╚════██║██╔══╝     ██║   
//    ██████╔╝███████╗███████║╚██████╗██║  ██║██║██║        ██║   ╚██████╔╝██║  ██║    ███████║███████╗   ██║   
//    ╚═════╝ ╚══════╝╚══════╝ ╚═════╝╚═╝  ╚═╝╚═╝╚═╝        ╚═╝    ╚═════╝ ╚═╝  ╚═╝    ╚══════╝╚══════╝   ╚═╝   
GfxDescriptorSetLayoutHandle gfxCreateDescriptorSetLayout(const GfxShader& shader, const GfxDescriptorSetLayoutBinding* bindings, 
                                                          uint32 numBindings, GfxDescriptorSetLayoutFlags flags)
{
    ASSERT(numBindings);
    ASSERT(bindings);

    MemTempAllocator tmpAlloc;

    // Construct Vulkan-specific structs for bindings and their names
    VkDescriptorSetLayoutBinding* descriptorSetBindings = tmpAlloc.MallocTyped<VkDescriptorSetLayoutBinding>(numBindings);
    const char** names = tmpAlloc.MallocTyped<const char*>(numBindings);
    
    for (uint32 i = 0; i < numBindings; i++) {
        const GfxDescriptorSetLayoutBinding& dsLayoutBinding = bindings[i];
        ASSERT(dsLayoutBinding.arrayCount > 0);

        const GfxShaderParameterInfo* shaderParam = gfxShaderGetParam(shader, dsLayoutBinding.name);
        ASSERT_MSG(shaderParam != nullptr, "Shader parameter '%s' does not exist in shader '%s'", dsLayoutBinding.name, shader.name);
        ASSERT_MSG(!shaderParam->isPushConstant, "Shader parameter '%s' is a push-constant in shader '%s'. cannot be used as regular uniform", dsLayoutBinding.name, shader.name);

        names[i] = shaderParam->name;    // Set the pointer to the field in ShaderParameterInfo because it is garuanteed to stay in mem
        descriptorSetBindings[i] = VkDescriptorSetLayoutBinding {
            .binding = shaderParam->bindingIdx,
            .descriptorType = static_cast<VkDescriptorType>(dsLayoutBinding.type),
            .descriptorCount = dsLayoutBinding.arrayCount,
            .stageFlags = static_cast<VkShaderStageFlags>(dsLayoutBinding.stages)
        };
    }

    // Search in existing descriptor set layouts and try to find a match. 
    HashMurmur32Incremental hasher(0x5eed1);
    uint32 hash = hasher.Add<VkDescriptorSetLayoutBinding>(descriptorSetBindings, numBindings)
                        .AddCStringArray(names, numBindings)
                        .Hash();

    gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SET_LAYOUTS].EnterRead();
    if (GfxDescriptorSetLayoutHandle layout = gVk.pools.descriptorSetLayouts.FindIf(
        [hash](const GfxDescriptorSetLayoutData& item)->bool { return item.hash == hash; }); layout.IsValid())
    {
        GfxDescriptorSetLayoutData& item = gVk.pools.descriptorSetLayouts.Data(layout);
        ++item.refCount;
        gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SET_LAYOUTS].ExitRead();
        return layout;
    }
    else {
        gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SET_LAYOUTS].ExitRead();

        bool isPushDescriptor = (flags & GfxDescriptorSetLayoutFlags::PushDescriptor) == GfxDescriptorSetLayoutFlags::PushDescriptor;
        ASSERT_ALWAYS((isPushDescriptor && gVk.hasPushDescriptor) || !isPushDescriptor, "VK_KHR_push_descriptor extension is not supported");

        VkDescriptorSetLayoutCreateInfo layoutCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .flags = isPushDescriptor ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR : 0u,
            .bindingCount = numBindings,
            .pBindings = descriptorSetBindings
        };

        // VK_EXT_descriptor_indexing
        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT layoutBindingFlags {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT,
            .bindingCount = numBindings,
        };
        if (gVk.hasDescriptorIndexing) {
            VkDescriptorBindingFlagsEXT* bindingFlags = tmpAlloc.MallocTyped<VkDescriptorBindingFlagsEXT>(numBindings);
            for (uint32 i = 0; i < numBindings; i++) {
                bindingFlags[i] = (bindings[i].arrayCount > 1) ? VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT : 0;
            }
            layoutBindingFlags.pBindingFlags = bindingFlags;
            layoutCreateInfo.pNext = &layoutBindingFlags;
        }
        
        VkDescriptorSetLayout dsLayout;
        if (vkCreateDescriptorSetLayout(gVk.device, &layoutCreateInfo, &gVk.allocVk, &dsLayout) != VK_SUCCESS) {
            LOG_ERROR("Gfx: CreateDescriptorSetLayout failed");
            return GfxDescriptorSetLayoutHandle();
        }

        // Copy layout bindings (for validation and lazy binding) and add descriptor set layout as Gfx object
        GfxDescriptorSetLayoutData dsLayoutData {
            .hash = hash,
            .layout = dsLayout,
            .numBindings = numBindings,
            .refCount = 1,
            .bindings = Mem::AllocTyped<GfxDescriptorSetLayoutData::Binding>(numBindings, &gVk.alloc)
        };

        for (uint32 i = 0; i < numBindings; i++) {
            ASSERT(names[i]);
            dsLayoutData.bindings[i].name = names[i];
            dsLayoutData.bindings[i].nameHash = Hash::Fnv32Str(names[i]);
            dsLayoutData.bindings[i].variableDescCount = bindings[i].arrayCount;
            memcpy(&dsLayoutData.bindings[i].vkBinding, &descriptorSetBindings[i], sizeof(VkDescriptorSetLayoutBinding));
        }

        #if !CONFIG_FINAL_BUILD
        if (SettingsJunkyard::Get().graphics.trackResourceLeaks)
            dsLayoutData.numStackframes = Debug::CaptureStacktrace(dsLayoutData.stackframes, (uint16)CountOf(dsLayoutData.stackframes), 2);
        #endif

        ReadWriteMutexWriteScope mtx(gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SET_LAYOUTS]);
        GfxDescriptorSetLayoutData prevLayout;
        layout = gVk.pools.descriptorSetLayouts.Add(dsLayoutData, &prevLayout);

        Mem::Free(prevLayout.bindings, &gVk.alloc);
        return layout;
    }

}

void gfxDestroyDescriptorSetLayout(GfxDescriptorSetLayoutHandle layout)
{
    if (!layout.IsValid())
        return;

    GfxDescriptorSetLayoutData& layoutData = gVk.pools.descriptorSetLayouts.Data(layout);
    ASSERT(layoutData.refCount > 0);
    if (--layoutData.refCount == 0) {
        if (layoutData.layout)
            vkDestroyDescriptorSetLayout(gVk.device, layoutData.layout, &gVk.allocVk);
        if (layoutData.bindings)
            Mem::Free(layoutData.bindings, &gVk.alloc);
        memset(&layoutData, 0x0, sizeof(layoutData));

        ReadWriteMutexWriteScope lk(gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SET_LAYOUTS]);
        gVk.pools.descriptorSetLayouts.Remove(layout);
    }
}

GfxDescriptorSetHandle gfxCreateDescriptorSet(GfxDescriptorSetLayoutHandle layout)
{
    MemTempAllocator tempAlloc;
    VkDescriptorSetLayout vkLayout;

    uint32* variableDescCounts = nullptr;
    uint32 numVariableDescCounts = 0;

    {
        GFX_LOCK_POOL_TEMP(DESCRIPTOR_SET_LAYOUTS);
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
        LOG_ERROR("Gfx: AllocateDescriptorSets failed");
        return GfxDescriptorSetHandle();
    }

    #if !CONFIG_FINAL_BUILD
    if (SettingsJunkyard::Get().graphics.trackResourceLeaks)
        descriptorSetData.numStackframes = Debug::CaptureStacktrace(descriptorSetData.stackframes, (uint16)CountOf(descriptorSetData.stackframes), 2);
    #endif

    ReadWriteMutexWriteScope lk(gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SETS]);
    return gVk.pools.descriptorSets.Add(descriptorSetData);
}

void gfxDestroyDescriptorSet(GfxDescriptorSetHandle dset)
{
    if (!dset.IsValid())
        return;

    GfxDescriptorSetData dsetData;
    {
        GFX_LOCK_POOL_TEMP(DESCRIPTOR_SETS);
        dsetData = gVk.pools.descriptorSets.Data(dset);
    }

    {
        GFX_LOCK_POOL_TEMP(DESCRIPTOR_SET_LAYOUTS);
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

    ReadWriteMutexWriteScope lk(gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SETS]);
    gVk.pools.descriptorSets.Remove(dset);
}

void gfxCmdPushDescriptorSet(GfxPipelineHandle pipeline, GfxPipelineBindPoint bindPoint, uint32 setIndex, uint32 numDescriptorBindings, 
                             const GfxDescriptorBindingDesc* descriptorBindings)
{
    ASSERT_ALWAYS(gVk.hasPushDescriptor, "VK_KHR_push_descriptor extension is not supported for this function");
    ASSERT(numDescriptorBindings);
    ASSERT(descriptorBindings);
    ASSERT(pipeline.IsValid());

    VkCommandBuffer cmdBufferVk = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBufferVk, "CmdXXX functions must come between Begin/End CommandBuffer calls");

    const GfxPipelineData* pipData = nullptr;
    VkPipelineLayout pipLayout = nullptr;
    
    {   
        ReadWriteMutexReadScope lk1(gVk.pools.locks[GfxObjectPools::PIPELINES]);
        pipData = &gVk.pools.pipelines.Data(pipeline);
    }
    ASSERT(pipData);

    MemTempAllocator tempAlloc;

    {
        ReadWriteMutexReadScope lk2(gVk.pools.locks[GfxObjectPools::PIPELINE_LAYOUTS]);
        pipLayout = gVk.pools.pipelineLayouts.Data(pipData->pipelineLayout).layout;
    }
    ASSERT(pipLayout);

    bool hasImage = false;

    VkWriteDescriptorSet* dsWrites = tempAlloc.MallocTyped<VkWriteDescriptorSet>(numDescriptorBindings);
    VkDescriptorBufferInfo* bufferInfos = tempAlloc.MallocTyped<VkDescriptorBufferInfo>(numDescriptorBindings);
    VkDescriptorImageInfo* imageInfos = tempAlloc.MallocTyped<VkDescriptorImageInfo>(numDescriptorBindings);

    auto FindBindingIndex = [&pipData](const char* name)->uint32
    {
        for (uint32 i = 0; i < pipData->numShaderParams; i++) {
            if (Str::IsEqual(pipData->shaderParams[i].name, name))
                return pipData->shaderParams[i].bindingIdx;
        }
        return uint32(-1);
    };

    for (uint32 i = 0; i < numDescriptorBindings; i++) {
        const GfxDescriptorBindingDesc& binding = descriptorBindings[i];

        uint32 bindingIdx = FindBindingIndex(binding.name);
        if (bindingIdx == -1) {
            ASSERT_ALWAYS(0, "Descriptor layout binding '%s' not found", binding.name);
        }
            
        VkDescriptorBufferInfo* pBufferInfo = nullptr;
        VkDescriptorImageInfo* pImageInfo = nullptr;
        uint32 descriptorCount = 1;

        switch (binding.type) {
        case GfxDescriptorType::UniformBuffer: 
        case GfxDescriptorType::UniformBufferDynamic:
        {
            GFX_LOCK_POOL_TEMP(BUFFERS);
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
            GFX_LOCK_POOL_TEMP(IMAGES);
            imageInfos[i] = {
                .sampler = binding.image.IsValid() ? gVk.pools.images.Data(binding.image).sampler : VK_NULL_HANDLE,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
            pImageInfo = &imageInfos[i];
            break;
        }
        case GfxDescriptorType::CombinedImageSampler:
        {
            GFX_LOCK_POOL_TEMP(IMAGES);
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
            GFX_LOCK_POOL_TEMP(IMAGES);
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

        dsWrites[i] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = bindingIdx,
            .dstArrayElement = 0,
            .descriptorCount = descriptorCount,
            .descriptorType = VkDescriptorType(binding.type),
            .pImageInfo = pImageInfo,
            .pBufferInfo = pBufferInfo,
            .pTexelBufferView = nullptr
        };
    } // foreach descriptor binding

    vkCmdPushDescriptorSetKHR(cmdBufferVk, VkPipelineBindPoint(bindPoint), pipLayout, setIndex, numDescriptorBindings, dsWrites);
}

void gfxUpdateDescriptorSet(GfxDescriptorSetHandle dset, uint32 numBindings, const GfxDescriptorBindingDesc* bindings)
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
        ReadWriteMutexReadScope lk1(gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SETS]);
        dsetData = gVk.pools.descriptorSets.Data(dset);
    }

    MemTempAllocator tempAlloc;

    ReadWriteMutexReadScope lk2(gVk.pools.locks[GfxObjectPools::DESCRIPTOR_SET_LAYOUTS]);
    GfxDescriptorSetLayoutData& layoutData = gVk.pools.descriptorSetLayouts.Data(dsetData.layout);
    bool hasImage = false;

    VkWriteDescriptorSet* dsWrites = tempAlloc.MallocTyped<VkWriteDescriptorSet>(layoutData.numBindings);
    ASSERT(numBindings == layoutData.numBindings); // can be removed in case we wanted to update sets partially

    VkDescriptorBufferInfo* bufferInfos = tempAlloc.MallocTyped<VkDescriptorBufferInfo>(numBindings);
    VkDescriptorImageInfo* imageInfos = tempAlloc.MallocTyped<VkDescriptorImageInfo>(numBindings);

    for (uint32 i = 0; i < numBindings; i++) {
        const GfxDescriptorBindingDesc& binding = bindings[i];
            
        // TODO: match binding names. if they don't match, try to find it in the list
        uint32 nameHash = Hash::Fnv32Str(binding.name);
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
            GFX_LOCK_POOL_TEMP(BUFFERS);
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
            GFX_LOCK_POOL_TEMP(IMAGES);
            imageInfos[i] = {
                .sampler = binding.image.IsValid() ? gVk.pools.images.Data(binding.image).sampler : VK_NULL_HANDLE,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
            pImageInfo = &imageInfos[i];
            break;
        }
        case GfxDescriptorType::CombinedImageSampler:
        {
            GFX_LOCK_POOL_TEMP(IMAGES);
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
            GFX_LOCK_POOL_TEMP(IMAGES);
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
    } // foreach descriptor binding

    vkUpdateDescriptorSets(gVk.device, layoutData.numBindings, dsWrites, 0, nullptr);

    // Save descriptor set bindings for texture management (reloads)
    if (hasImage && gVk.updateImageDescCallback)
        gVk.updateImageDescCallback(dset, numBindings, bindings);
}

void _private::gfxSetUpdateImageDescriptorCallback(_private::GfxUpdateImageDescriptorCallback callback)
{
    gVk.updateImageDescCallback = callback;
}


//     ██████╗  ██████╗
//    ██╔════╝ ██╔════╝
//    ██║  ███╗██║     
//    ██║   ██║██║     
//    ╚██████╔╝╚██████╗
//     ╚═════╝  ╚═════╝
static void gfxCollectGarbage(bool force)
{
    uint64 frameIdx = Engine::GetFrameIndex();
    const uint32 numFramesToWait = MAX_FRAMES_IN_FLIGHT;

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

void GfxObjectPools::DetectAndReleaseLeaks()
{
    auto PrintStacktrace = [](const char* resourceName, void* ptr, void* const* stackframes, uint16 numStackframes) 
    {
        DebugStacktraceEntry entries[8];
        Debug::ResolveStacktrace(numStackframes, stackframes, entries);
        LOG_DEBUG("\t%s: 0x%llx", resourceName, ptr);
        for (uint16 si = 0; si < numStackframes; si++) 
            LOG_DEBUG("\t\t- %s(%u)", entries[si].filename, entries[si].line);
    };

    [[maybe_unused]] bool trackResourceLeaks = SettingsJunkyard::Get().graphics.trackResourceLeaks;

    if (gVk.pools.buffers.Count()) {
        LOG_WARNING("Gfx: Total %u buffers are not released. cleaning up...", gVk.pools.buffers.Count());
        for (uint32 i = 0; i < gVk.pools.buffers.Count(); i++) {
            GfxBufferHandle handle = gVk.pools.buffers.HandleAt(i);
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
        LOG_WARNING("Gfx: Total %u images are not released. cleaning up...", gVk.pools.images.Count());
        for (uint32 i = 0; i < gVk.pools.images.Count(); i++) {
            GfxImageHandle handle = gVk.pools.images.HandleAt(i);
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
        LOG_WARNING("Gfx: Total %u pipeline layout are not released. cleaning up...", gVk.pools.pipelineLayouts.Count());
        for (uint32 i = 0; i < gVk.pools.pipelineLayouts.Count(); i++) {
            GfxPipelineLayoutHandle handle = gVk.pools.pipelineLayouts.HandleAt(i);
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
        LOG_WARNING("Gfx: Total %u pipelines are not released. cleaning up...", gVk.pools.pipelines.Count());
        for (uint32 i = 0; i < gVk.pools.pipelines.Count(); i++) {
            GfxPipelineHandle handle = gVk.pools.pipelines.HandleAt(i);
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
        LOG_WARNING("Gfx: Total %u descriptor sets are not released. cleaning up...", gVk.pools.descriptorSets.Count());
        for (uint32 i = 0; i < gVk.pools.descriptorSets.Count(); i++) {
            GfxDescriptorSetHandle handle = gVk.pools.descriptorSets.HandleAt(i);
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
        LOG_WARNING("Gfx: Total %u descriptor sets layouts are not released. cleaning up...", gVk.pools.descriptorSetLayouts.Count());
        for (uint32 i = 0; i < gVk.pools.descriptorSetLayouts.Count(); i++) {
            GfxDescriptorSetLayoutHandle handle = gVk.pools.descriptorSetLayouts.HandleAt(i);
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

//    ██████╗ ███████╗██╗███╗   ██╗██╗████████╗
//    ██╔══██╗██╔════╝██║████╗  ██║██║╚══██╔══╝
//    ██║  ██║█████╗  ██║██╔██╗ ██║██║   ██║   
//    ██║  ██║██╔══╝  ██║██║╚██╗██║██║   ██║   
//    ██████╔╝███████╗██║██║ ╚████║██║   ██║   
//    ╚═════╝ ╚══════╝╚═╝╚═╝  ╚═══╝╚═╝   ╚═╝   
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
    for (uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
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
            for (uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
                vkDestroyCommandPool(gVk.device, threadData->commandPools[i], &gVk.allocVk);
                threadData->freeLists[i].Free();
                threadData->cmdBuffers[i].Free();
            }
            memset(threadData, 0x0, sizeof(*threadData));
        }

        for (uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
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
        vkDestroyDebugUtilsMessengerEXT(gVk.instance, gVk.debugMessenger, &gVk.allocVk);
    if (gVk.debugReportCallback) 
        vkDestroyDebugReportCallbackEXT(gVk.instance, gVk.debugReportCallback, &gVk.allocVk);

    vkDestroyInstance(gVk.instance, &gVk.allocVk);

    gVk.pools.Release();
    gVk.tlsfAlloc.Release();
    gVk.runtimeAlloc.SetAllocator(nullptr);
}


void GfxObjectPools::Release()
{
    for (GfxDescriptorSetLayoutData& layout : gVk.pools.descriptorSetLayouts) 
        Mem::Free(layout.bindings, &gVk.alloc);

    gVk.pools.buffers.Free();
    gVk.pools.images.Free();
    gVk.pools.pipelineLayouts.Free();
    gVk.pools.pipelines.Free();
    gVk.pools.descriptorSets.Free();
    gVk.pools.descriptorSetLayouts.Free();
}

//    ███████╗██████╗  █████╗ ███╗   ███╗███████╗    ███████╗██╗   ██╗███╗   ██╗ ██████╗
//    ██╔════╝██╔══██╗██╔══██╗████╗ ████║██╔════╝    ██╔════╝╚██╗ ██╔╝████╗  ██║██╔════╝
//    █████╗  ██████╔╝███████║██╔████╔██║█████╗      ███████╗ ╚████╔╝ ██╔██╗ ██║██║     
//    ██╔══╝  ██╔══██╗██╔══██║██║╚██╔╝██║██╔══╝      ╚════██║  ╚██╔╝  ██║╚██╗██║██║     
//    ██║     ██║  ██║██║  ██║██║ ╚═╝ ██║███████╗    ███████║   ██║   ██║ ╚████║╚██████╗
//    ╚═╝     ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝    ╚══════╝   ╚═╝   ╚═╝  ╚═══╝ ╚═════╝
void _private::gfxBeginFrame()
{
    PROFILE_ZONE();

    if (gVk.hasMemoryBudget) {
        ASSERT(Engine::GetFrameIndex() < UINT32_MAX);
        vmaSetCurrentFrameIndex(gVk.vma, uint32(Engine::GetFrameIndex()));
    }

    { PROFILE_ZONE_NAME("WaitForFence");
        vkWaitForFences(gVk.device, 1, &gVk.inflightFences[gVk.currentFrameIdx], VK_TRUE, UINT64_MAX);
    }

    // Submit deferred commands
    { 
        MutexScope mtx(gVk.deferredCommandsMtx);
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

    uint32 frameIdx = gVk.currentFrameIdx;
    uint32 imageIdx;

    { PROFILE_ZONE_NAME("AcquireNextImage");
        VkResult nextImageResult = vkAcquireNextImageKHR(gVk.device, gVk.swapchain.swapchain, UINT64_MAX,
                                                        gVk.imageAvailSemaphores[frameIdx],
                                                        VK_NULL_HANDLE, &imageIdx);
        if (nextImageResult == VK_ERROR_OUT_OF_DATE_KHR) {
            LOG_DEBUG("Out-of-date swapchain: Recreating");
            gfxResizeSwapchain(App::GetFramebufferWidth(), App::GetFramebufferHeight());
        }
        else if (nextImageResult != VK_SUCCESS && nextImageResult != VK_SUBOPTIMAL_KHR) {
            ASSERT_MSG(false, "Gfx: Acquire swapchain failed: %d", nextImageResult);
            return;
        }
    }

    gVk.swapchain.imageIdx = imageIdx;
}

void _private::gfxEndFrame()
{
    ASSERT_MSG(gVk.swapchain.imageIdx != UINT32_MAX, "gfxBeginFrame is not called");
    ASSERT_MSG(gCmdBufferThreadData.curCmdBuffer == VK_NULL_HANDLE, "Graphics should not be in recording state");
    PROFILE_ZONE();

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
    if (!gVk.pendingCmdBuffers.IsEmpty()) {
        SpinLockMutexScope lock(gVk.pendingCmdBuffersLock);
        cmdBuffersVk = Mem::AllocCopy<VkCommandBuffer>(gVk.pendingCmdBuffers.Ptr(), gVk.pendingCmdBuffers.Count(), &tmpAlloc);
        numCmdBuffers = gVk.pendingCmdBuffers.Count();
        gVk.pendingCmdBuffers.Clear();
    }

    gVk.prevFrameIdx = frameIdx;
    Atomic::StoreExplicit(&gVk.currentFrameIdx, (frameIdx + 1) % MAX_FRAMES_IN_FLIGHT, AtomicMemoryOrder::Release);

    //------------------------------------------------------------------------
    // Submit last command-buffers + draw to swpachain framebuffer
    { PROFILE_ZONE_NAME("SubmitLast"); 
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
    { PROFILE_ZONE_NAME("Present");
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
            LOG_DEBUG("Resized/Invalidated swapchain: Recreate");
            gfxResizeSwapchain(App::GetFramebufferWidth(), App::GetFramebufferHeight());
        }
        else if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR) {
            ASSERT_ALWAYS(false, "Gfx: Present swapchain failed");
            return;
        }
    }

    gVk.swapchain.imageIdx = UINT32_MAX;
    gfxCollectGarbage(false);
}

void gfxWaitForIdle()
{
    if (gVk.gfxQueue)
        vkQueueWaitIdle(gVk.gfxQueue);
}

//    ██╗  ██╗███████╗ █████╗ ██████╗      █████╗ ██╗     ██╗      ██████╗  ██████╗
//    ██║  ██║██╔════╝██╔══██╗██╔══██╗    ██╔══██╗██║     ██║     ██╔═══██╗██╔════╝
//    ███████║█████╗  ███████║██████╔╝    ███████║██║     ██║     ██║   ██║██║     
//    ██╔══██║██╔══╝  ██╔══██║██╔═══╝     ██╔══██║██║     ██║     ██║   ██║██║     
//    ██║  ██║███████╗██║  ██║██║         ██║  ██║███████╗███████╗╚██████╔╝╚██████╗
//    ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝╚═╝         ╚═╝  ╚═╝╚══════╝╚══════╝ ╚═════╝  ╚═════╝
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


//    ███████╗████████╗ █████╗ ████████╗███████╗
//    ██╔════╝╚══██╔══╝██╔══██╗╚══██╔══╝██╔════╝
//    ███████╗   ██║   ███████║   ██║   ███████╗
//    ╚════██║   ██║   ██╔══██║   ██║   ╚════██║
//    ███████║   ██║   ██║  ██║   ██║   ███████║
//    ╚══════╝   ╚═╝   ╚═╝  ╚═╝   ╚═╝   ╚══════╝
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
    switch (App::GetFramebufferTransform()) {
    case AppFramebufferTransform::None:           return MAT4_IDENT;
    case AppFramebufferTransform::Rotate90:       return Mat4::RotateZ(M_HALFPI);
    case AppFramebufferTransform::Rotate180:      return Mat4::RotateZ(M_PI);
    case AppFramebufferTransform::Rotate270:      return Mat4::RotateZ(M_PI + M_HALFPI);
    }

    return MAT4_IDENT;
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
    for (uint32 i = MAX_FRAMES_IN_FLIGHT; i-- > 0;) {
        uint32 frame = (gVk.currentFrameIdx + i)%MAX_FRAMES_IN_FLIGHT;
        if (vkGetQueryPoolResults(gVk.device, gVk.queryPool[frame], 0, 2, sizeof(frameTimestamps), frameTimestamps, 
                                  sizeof(uint64), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
        {
            return float(frameTimestamps[1] - frameTimestamps[0]) * gVk.deviceProps.limits.timestampPeriod;
        }
    }
    
    return 0;
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

GfxImageInfo gfxGetImageInfo(GfxImageHandle img)
{
    GFX_LOCK_POOL_TEMP(IMAGES);
    const GfxImageData& data = gVk.pools.images.Data(img);
    return GfxImageInfo {
        .width = data.width,
        .height = data.height,
        .memUsage = data.memUsage,
        .sizeBytes = data.sizeBytes
    };
}

//    ██████╗ ██╗   ██╗███╗   ██╗ █████╗ ███╗   ███╗██╗ ██████╗    ██╗   ██╗██████╗  ██████╗ 
//    ██╔══██╗╚██╗ ██╔╝████╗  ██║██╔══██╗████╗ ████║██║██╔════╝    ██║   ██║██╔══██╗██╔═══██╗
//    ██║  ██║ ╚████╔╝ ██╔██╗ ██║███████║██╔████╔██║██║██║         ██║   ██║██████╔╝██║   ██║
//    ██║  ██║  ╚██╔╝  ██║╚██╗██║██╔══██║██║╚██╔╝██║██║██║         ██║   ██║██╔══██╗██║   ██║
//    ██████╔╝   ██║   ██║ ╚████║██║  ██║██║ ╚═╝ ██║██║╚██████╗    ╚██████╔╝██████╔╝╚██████╔╝
//    ╚═════╝    ╚═╝   ╚═╝  ╚═══╝╚═╝  ╚═╝╚═╝     ╚═╝╚═╝ ╚═════╝     ╚═════╝ ╚═════╝  ╚═════╝ 
GfxDynamicUniformBuffer gfxCreateDynamicUniformBuffer(uint32 count, uint32 stride)
{
    ASSERT_MSG(count > 1, "Why not just use a regular uniform buffer ?");
    ASSERT(stride);
    ASSERT(gVk.deviceProps.limits.minUniformBufferOffsetAlignment);

    stride = AlignValue(stride, uint32(gVk.deviceProps.limits.minUniformBufferOffsetAlignment));

    GfxBufferHandle bufferHandle = gfxCreateBuffer(GfxBufferDesc {
        .size = stride * count,
        .type = GfxBufferType::Uniform,
        .usage = GfxBufferUsage::Stream
    });

    if (!bufferHandle.IsValid())
        return GfxDynamicUniformBuffer {};

    GFX_LOCK_POOL_TEMP(BUFFERS);
    GfxBufferData& bufferData = gVk.pools.buffers.Data(bufferHandle);

    GfxDynamicUniformBuffer r {
        .mBufferHandle = bufferHandle,
        .mBufferPtr = (uint8*)bufferData.mappedBuffer,
        .mStride = stride,
        .mCount = count
    };

    return r;
}

void gfxDestroyDynamicUniformBuffer(GfxDynamicUniformBuffer& buffer)
{
    gfxDestroyBuffer(buffer.mBufferHandle);
    memset(&buffer, 0x0, sizeof(buffer));
}

bool GfxDynamicUniformBuffer::IsValid() const
{
    return mBufferHandle.IsValid() && gVk.pools.buffers.IsValid(mBufferHandle);
}

void GfxDynamicUniformBuffer::Flush(const GfxDyanmicUniformBufferRange* ranges, uint32 numRanges)
{
    VmaAllocation allocation;
    {
        GFX_LOCK_POOL_TEMP(BUFFERS);
        GfxBufferData& bufferData = gVk.pools.buffers.Data(mBufferHandle);
        allocation = bufferData.allocation;

        // TODO: we currently only assume that uniform buffers are created with HOST_VISIBLE bit.
        //       For none-host visible, mappedBuffer is still available, but it's for the staging buffer.
        //       So we have to take care of a copy op and a barrier
        ASSERT(bufferData.memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    }

    MemTempAllocator tmpAlloc;
    VkDeviceSize* offsets = tmpAlloc.MallocTyped<VkDeviceSize>(numRanges);
    VkDeviceSize* sizes = tmpAlloc.MallocTyped<VkDeviceSize>(numRanges);

    for (uint32 i = 0; i < numRanges; i++) {
        offsets[i] = ranges[i].index * mStride;
        sizes[i] = ranges[i].count * mStride;
    }
    
    [[maybe_unused]] VkResult r = vmaFlushAllocations(gVk.vma, 1, &allocation, offsets, sizes);
    ASSERT(r == VK_SUCCESS);
}

//    ██████╗ ██████╗  ██████╗ ███████╗██╗██╗     ██╗███╗   ██╗ ██████╗ 
//    ██╔══██╗██╔══██╗██╔═══██╗██╔════╝██║██║     ██║████╗  ██║██╔════╝ 
//    ██████╔╝██████╔╝██║   ██║█████╗  ██║██║     ██║██╔██╗ ██║██║  ███╗
//    ██╔═══╝ ██╔══██╗██║   ██║██╔══╝  ██║██║     ██║██║╚██╗██║██║   ██║
//    ██║     ██║  ██║╚██████╔╝██║     ██║███████╗██║██║ ╚████║╚██████╔╝
//    ╚═╝     ╚═╝  ╚═╝ ╚═════╝ ╚═╝     ╚═╝╚══════╝╚═╝╚═╝  ╚═══╝ ╚═════╝ 
#ifdef TRACY_ENABLE

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
    SpinLockMutex queueLock;
    VkQueryPool queryPool;
    
    uint64 deviation;
    int64 prevCalibration;
    int64 qpcToNs;

    uint32 queryCount;
    uint32 head;
    uint32 tail;
    uint32 oldCount;
    int64* res;
    uint8 id;
};

struct GfxProfileState
{
    GfxProfileQueryContext gfxQueries[MAX_FRAMES_IN_FLIGHT];
    VkTimeDomainEXT timeDomain;
    uint8 uniqueIdGenerator;
    bool initialized;
};

static GfxProfileState gGfxProfile;

INLINE uint16 gfxProfileGetNextQueryId(GfxProfileQueryContext* ctx)
{
    SpinLockMutexScope lock(ctx->queueLock);
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
        vkGetCalibratedTimestampsEXT(gVk.device, 2, spec, ts, &deviation);
    } while(deviation > ctx.deviation);

    #if PLATFORM_WINDOWS
        *tGpu = ts[0];
        *tCpu = Tracy::_private::__tracy_get_time() * ctx.qpcToNs;
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
        LOG_ERROR("Gfx: Creating Query pool failed");
        return false;
    }

    ctx->queryPool = queryPool;
    ctx->queryCount = queryCount;
    ctx->res = Mem::AllocZeroTyped<int64>(queryCount, Mem::GetDefaultAlloc()); // TODO
    
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
            vkGetCalibratedTimestampsEXT(gVk.device, 2, spec, ts, &deviation[i]);

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
    Mem::Free(ctx->res, Mem::GetDefaultAlloc());
}

static bool gfxInitializeProfiler()
{
    VkTimeDomainEXT timeDomain = VK_TIME_DOMAIN_DEVICE_EXT;

    // TODO: VK_EXT_calibrated_timestamps implementation is incorrect. Revisit this 
    #if 0
    if (gfxHasDeviceExtension("VK_EXT_calibrated_timestamps")) {
        if (!vkGetPhysicalDeviceCalibrateableTimeDomainsEXT) {
            vkGetPhysicalDeviceCalibrateableTimeDomainsEXT = 
                (PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT)vkGetInstanceProcAddr(gVk.instance, "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT");
        }
        if (!vkGetCalibratedTimestampsEXT) {
            vkGetCalibratedTimestampsEXT = 
                (PFN_vkGetCalibratedTimestampsEXT)vkGetInstanceProcAddr(gVk.instance, "vkGetCalibratedTimestampsEXT");
        }
    
        uint32_t num;
        vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(gVk.physicalDevice, &num, nullptr);
        if (num > 4) 
            num = 4;
        VkTimeDomainEXT data[4];
        vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(gVk.physicalDevice, &num, data);
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
    #endif

    gGfxProfile.timeDomain = timeDomain;

    //------------------------------------------------------------------------------------------------------------------
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

    //------------------------------------------------------------------------------------------------------------------
    //
    const char* name = "GfxQueue";
    for (uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        GfxProfileQueryContext* ctx = &gGfxProfile.gfxQueries[i];
        if (!gfxInitializeProfileQueryContext(ctx, gGfxProfile.uniqueIdGenerator++, cmdPool)) {
            vkDestroyCommandPool(gVk.device, cmdPool, nullptr);
            ASSERT(0);
            return false;
        }

        ___tracy_emit_gpu_context_name_serial(___tracy_gpu_context_name_data {
            .context = ctx->id,
            .name = name,
            .len = static_cast<uint16>(Str::Len(name))
        });
    }

    vkDestroyCommandPool(gVk.device, cmdPool, nullptr);

    gGfxProfile.initialized = true;
    return true;
}

static void gfxReleaseProfiler()
{
    if (gGfxProfile.initialized) {
        for (uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            gfxReleaseProfileQueryContext(&gGfxProfile.gfxQueries[i]);
        }
    }
}

void Tracy::_private::gfxProfileZoneBegin(uint64 srcloc)
{
    if (!gGfxProfile.initialized)
        return;

    VkCommandBuffer cmdBuffer = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBuffer != VK_NULL_HANDLE, "GPU profile zone must be inside command-buffer recording");

    uint32 frameIdx = Atomic::LoadExplicit(&gVk.currentFrameIdx, AtomicMemoryOrder::Acquire);
    GfxProfileQueryContext* ctx = &gGfxProfile.gfxQueries[frameIdx];

    uint16 queryId = gfxProfileGetNextQueryId(ctx);
    vkCmdWriteTimestamp(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, ctx->queryPool, queryId);

    ___tracy_emit_gpu_zone_begin_alloc_serial(___tracy_gpu_zone_begin_data {
        .srcloc = srcloc,
        .queryId = queryId,
        .context = ctx->id
    });
}

void Tracy::_private::gfxProfileZoneEnd()
{
    if (!gGfxProfile.initialized)
        return;

    VkCommandBuffer cmdBuffer = gCmdBufferThreadData.curCmdBuffer;
    ASSERT_MSG(cmdBuffer != VK_NULL_HANDLE, "GPU profile zone must be inside command-buffer recording");

    uint32 frameIdx = Atomic::LoadExplicit(&gVk.currentFrameIdx, AtomicMemoryOrder::Acquire);
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
    PROFILE_ZONE_COLOR_OPT(0xff0000, !isVoid);

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
        const int64 refCpu = Tracy::_private::__tracy_get_time();
        const int64 delta = tcpu - ctx->prevCalibration;
        if(delta > 0) {
            ctx->prevCalibration = tcpu;
            ___tracy_emit_gpu_calibrate_serial(Tracy::_private::___tracy_gpu_calibrate_data {
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


