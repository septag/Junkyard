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
#include "../Core/MathTypes.h"

#include "../Common/Application.h"
#include "../Common/JunkyardSettings.h"
#include "../Common/CommonTypes.h"

#include "../Engine.h"

#ifndef __10X__
    #include "../External/volk/volk.h"
#endif

static constexpr uint32 GFXBACKEND_MAX_SWAP_CHAIN_IMAGES = 3;
static constexpr uint32 GFXBACKEND_MAX_FRAMES_IN_FLIGHT = 2;
static constexpr uint32 GFXBACKEND_MAX_GARBAGE_COLLECT_PER_FRAME = 16;
static constexpr uint32 GFXBACKEND_BACKBUFFER_COUNT = 3;
static constexpr uint32 GFXBACKEND_FRAMES_IN_FLIGHT = 2;

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
    VkImage images[GFXBACKEND_BACKBUFFER_COUNT];
    VkImageView views[GFXBACKEND_BACKBUFFER_COUNT];
    VkSemaphore swapchainSemaphores[GFXBACKEND_BACKBUFFER_COUNT];
    VkSemaphore presentSemaphores[GFXBACKEND_BACKBUFFER_COUNT];
    VkExtent2D extent;

    void GoNext() { backbufferIdx = (backbufferIdx + 1) % GFXBACKEND_BACKBUFFER_COUNT; }
};

struct GfxBackendSwapchainInfo
{
    VkSurfaceCapabilitiesKHR caps;
    uint32 numFormats;
    uint32 numPresentModes;
    VkSurfaceFormatKHR* formats;
    VkPresentModeKHR* presentModes;
};

enum class GfxBackendQueueType : uint32
{
    None = 0,
    Graphics = 0x1,
    Compute = 0x2,
    Transfer = 0x4,
    Present = 0x8
};
ENABLE_BITMASK(GfxBackendQueueType);

struct GfxBackendQueueFamily
{
    GfxBackendQueueType type;
    uint32 count;
};

struct GfxBackendQueue
{
    VkQueue handle;
    GfxBackendQueueType type;
    uint32 familyIdx;
    float priority;
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

struct GfxBackendVk
{
    MemProxyAllocator parentAlloc;
    MemProxyAllocator runtimeAlloc;
    MemProxyAllocator driverAlloc;
    GfxBackendAllocator runtimeAllocBase;
    GfxBackendAllocator driverAllocBase;
    GfxBackendVkAllocator vkAlloc;
    
    GfxBackendInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    GfxBackendGpu gpu;
    VkDevice device;
    VkSurfaceKHR surface;

    GfxBackendVkExtensions extApi;

    GfxBackendQueueFamily* queueFamilies;
    uint32 numQueueFamilies;

    GfxBackendQueue* queues;
    uint32 numQueues;

    GfxBackendSwapchainInfo swapchainInfo;
    GfxBackendSwapchain swapchain;

    // TEMP 
    VkFence renderFences[GFXBACKEND_FRAMES_IN_FLIGHT];
    uint32 renderIdx;
};

static GfxBackendVk gBackendVk;

namespace GfxBackend
{
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

            // TODO: How can we know we have VK_Validation_Features ? 
            //       Because it is only enabled when debug layer is activated
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
            // Prefer discrete GPUs over integrated ones
            for (uint32 i = 0; i < numGPUs; i++) {
                VkPhysicalDeviceProperties deviceProps {};
                vkGetPhysicalDeviceProperties(gpus[i], &deviceProps);
                if (deviceProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
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

        // Estimate memory
        // TODO: We can extend this with VK_EXT_memory_budget 
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
        const GfxBackendGpu& gpu = gBackendVk.gpu;

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

        if (!gpu.features4.synchronization2) {
            LOG_ERROR("Gfx: Synchronization2 feature is required (VK_KHR_synchronization2)");
            return false;
        }

        if (!gpu.features3.descriptorIndexing) {
            LOG_ERROR("Gfx: descriptorIndexing feature is required (VK_EXT_descriptor_indexing)");
            return false;
        }

        if (!gpu.features3.uniformBufferStandardLayout) {
            LOG_ERROR("Gfx: Standard uniform buffer layout feature is required (VK_KHR_uniform_buffer_standard_layout)");
            return false;
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
            for (const char* ext : enabledExtensions) {
                LOG_VERBOSE("\t%s", ext);
            }
        }

        // Gather Queues
        GfxBackendQueue* queues = gBackendVk.queues;
        StaticArray<VkDeviceQueueCreateInfo, 4> queueCreateInfos;
        for (uint32 i = 0; i < gBackendVk.numQueues; i++) {
            if (settings.graphics.headless && IsBitsSet(queues[i].type, GfxBackendQueueType::Graphics|GfxBackendQueueType::Present))
                continue;

            VkDeviceQueueCreateInfo createInfo {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = queues[i].familyIdx,
                .queueCount = 1,
                .pQueuePriorities = &queues[i].priority
            };
            queueCreateInfos.Push(createInfo);
        }

        // Create device (logical)
        VkDeviceCreateInfo devCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = gBackendVk.numQueues,
            .pQueueCreateInfos = queueCreateInfos.Ptr(),
            .enabledExtensionCount = enabledExtensions.Count(),
            .ppEnabledExtensionNames = enabledExtensions.Ptr()
        };

        // Enable extension features
        void** deviceNext = const_cast<void**>(&devCreateInfo.pNext);
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

        // Fetch queues
        for (uint32 i = 0; i < gBackendVk.numQueues; i++) {
            ASSERT(queues[i].handle == nullptr);

            vkGetDeviceQueue(gBackendVk.device, queues[i].familyIdx, 0, &queues[i].handle);
            ASSERT_ALWAYS(queues[i].handle, "Something went wrong! Cannot fetch device queue. Invalid queue family");
        }

        return true;
    }

    static void _ReleaseDevice()
    {
        MemAllocator* alloc = &gBackendVk.parentAlloc;

        if (gBackendVk.device) 
            vkDestroyDevice(gBackendVk.device, gBackendVk.vkAlloc);
        
        Mem::Free(gBackendVk.gpu.extensions, alloc);
    }

    static void _InitializeQueueFamilies()
    {
        ASSERT(gBackendVk.gpu.handle);

        MemAllocator* alloc = &gBackendVk.parentAlloc;
        MemTempAllocator tempAlloc;

        vkGetPhysicalDeviceQueueFamilyProperties(gBackendVk.gpu.handle, &gBackendVk.numQueueFamilies, nullptr);
        ASSERT_ALWAYS(gBackendVk.numQueueFamilies, "There should be at least 1 queue family on this hardware");
        gBackendVk.queueFamilies = Mem::AllocTyped<GfxBackendQueueFamily>(gBackendVk.numQueueFamilies, alloc);
        
        VkQueueFamilyProperties* families = tempAlloc.MallocTyped<VkQueueFamilyProperties>(gBackendVk.numQueueFamilies);
        vkGetPhysicalDeviceQueueFamilyProperties(gBackendVk.gpu.handle, &gBackendVk.numQueueFamilies, families);
        
        for (uint32 i = 0; i < gBackendVk.numQueueFamilies; i++) {
            GfxBackendQueueFamily& fam = gBackendVk.queueFamilies[i];
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
                vkGetPhysicalDeviceSurfaceSupportKHR(gBackendVk.gpu.handle, i, gBackendVk.surface, &supportsPresentation);
                if (supportsPresentation)
                    fam.type |= GfxBackendQueueType::Present;
            }
        }

        LOG_VERBOSE("(init) Found %u queue families", gBackendVk.numQueueFamilies);
    }

    static uint32 _AssignQueueFamily(GfxBackendQueueType type, GfxBackendQueueType preferNotHave = GfxBackendQueueType::None)
    {
        ASSERT(gBackendVk.numQueueFamilies);

        uint32 familyIndex = uint32(-1);
        for (uint32 i = 0; i < gBackendVk.numQueueFamilies; i++) {
            if (IsBitsSet<GfxBackendQueueType>(gBackendVk.queueFamilies[i].type, type) && gBackendVk.queueFamilies[i].count) {
                if (preferNotHave != GfxBackendQueueType::None) {
                    if (!IsBitsSet(gBackendVk.queueFamilies[i].type, preferNotHave)) {
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

        if (familyIndex != -1)
            --gBackendVk.queueFamilies[familyIndex].count;

        if (familyIndex == -1 && preferNotHave != GfxBackendQueueType::None) 
            return _AssignQueueFamily(type);
        else
            return familyIndex;
    }

    static bool _SetupQueues()
    {
        MemAllocator* alloc = &gBackendVk.parentAlloc;

        // TODO: change the scheme for Discrete and Integrated GPUs
        // Discrete GPUs:
        //  (1) Graphics + Present 
        //  (1) Transfer: Preferebly exclusive 
        //  (1) Compute: Preferebly exclusive
        gBackendVk.numQueues = 3;
        gBackendVk.queues = Mem::AllocZeroTyped<GfxBackendQueue>(gBackendVk.numQueues, alloc);
        
        {
            gBackendVk.queues[0] = {
                .type = GfxBackendQueueType::Graphics|GfxBackendQueueType::Present,
                .familyIdx = _AssignQueueFamily(GfxBackendQueueType::Graphics|GfxBackendQueueType::Present),
                .priority = 1.0f
            }; 

            if (gBackendVk.queues[0].familyIdx != -1) {
                LOG_VERBOSE("\tGraphics queue from index: %u", gBackendVk.queues[0].familyIdx);
            }
            else {
                LOG_ERROR("Gfx: Graphics queue not found");
                return false;
            }
        }

        {
            gBackendVk.queues[1] = {
                .type = GfxBackendQueueType::Transfer,
                .familyIdx = _AssignQueueFamily(GfxBackendQueueType::Transfer, GfxBackendQueueType::Graphics|GfxBackendQueueType::Compute),
                .priority = 1.0f
            };

            if (gBackendVk.queues[1].familyIdx != -1) {
                LOG_VERBOSE("\tTransfer queue from index: %u", gBackendVk.queues[1].familyIdx);
            }
            else {
                LOG_ERROR("Gfx: Transfer queue not found");
                return false;
            }
        }

        {
            gBackendVk.queues[2] = {
                .type = GfxBackendQueueType::Compute,
                .familyIdx = _AssignQueueFamily(GfxBackendQueueType::Compute, GfxBackendQueueType::Graphics|GfxBackendQueueType::Transfer),
                .priority = 1.0f
            };

            if (gBackendVk.queues[2].familyIdx != -1) {
                LOG_VERBOSE("\tCompute queue from index: %u", gBackendVk.queues[2].familyIdx);
            }
            else {
                LOG_ERROR("Gfx: Compute queue not found");
                return false;
            }
        }

        return true;
    }

    static bool _InitializeSwapchain(GfxBackendSwapchain* swapchain, VkSurfaceKHR surface, Int2 size)
    {
        VkSurfaceFormatKHR chosenFormat {};
        const GfxBackendSwapchainInfo& info = gBackendVk.swapchainInfo;

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
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, // TODO: VK_IMAGE_USAGE_TRANSFER_DST_BIT if we are postprocessing
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
        VkImageViewCreateInfo viewCreateInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = chosenFormat.format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1
            }
        };

        for (uint32 i = 0; i < numActualImages; i++) {
            viewCreateInfo.image = swapchain->images[i];
            if (vkCreateImageView(gBackendVk.device, &viewCreateInfo, gBackendVk.vkAlloc, &swapchain->views[i]) != VK_SUCCESS) {
                LOG_ERROR("Gfx: Create Swapchain view failed for image %u", i);
                return false;
            }
        }

        // Semaphores
        VkSemaphoreCreateInfo semCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        for (uint32 i = 0; i < GFXBACKEND_BACKBUFFER_COUNT; i++) {
            vkCreateSemaphore(gBackendVk.device, &semCreateInfo, gBackendVk.vkAlloc, &swapchain->swapchainSemaphores[i]);
            vkCreateSemaphore(gBackendVk.device, &semCreateInfo, gBackendVk.vkAlloc, &swapchain->presentSemaphores[i]);
        }

        return true;
    }

    void _ReleaseSwapchain(GfxBackendSwapchain* swapchain)
    {
        ASSERT(swapchain);

        for (uint32 i = 0; i < swapchain->numImages; i++)  {
            if (swapchain->views[i])
                vkDestroyImageView(gBackendVk.device, swapchain->views[i], gBackendVk.vkAlloc);
        }

        if (swapchain->handle) 
            vkDestroySwapchainKHR(gBackendVk.device, swapchain->handle, gBackendVk.vkAlloc);

        for (uint32 i = 0; i < GFXBACKEND_BACKBUFFER_COUNT; i++) {
            vkDestroySemaphore(gBackendVk.device, swapchain->swapchainSemaphores[i], gBackendVk.vkAlloc);
            vkDestroySemaphore(gBackendVk.device, swapchain->presentSemaphores[i], gBackendVk.vkAlloc);
        }

        memset(swapchain, 0x0, sizeof(*swapchain));
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

    _InitializeQueueFamilies();

    if (!_SetupQueues())
        return false;

    if (!_InitializeDevice(settings))
        return false;

    // Swapchain and it's capabilities
    // We can only create this after device is created. 
    if (!settings.graphics.headless) {
        uint32 numFormats;
        uint32 numPresentModes;
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

        vkGetPhysicalDeviceSurfaceFormatsKHR(gBackendVk.gpu.handle, gBackendVk.surface, &numFormats, nullptr);
        gBackendVk.swapchainInfo.numFormats = numFormats;
        gBackendVk.swapchainInfo.formats = Mem::AllocTyped<VkSurfaceFormatKHR>(numFormats, &gBackendVk.parentAlloc);
        vkGetPhysicalDeviceSurfaceFormatsKHR(gBackendVk.gpu.handle, gBackendVk.surface, &numFormats, 
                                             gBackendVk.swapchainInfo.formats);

        vkGetPhysicalDeviceSurfacePresentModesKHR(gBackendVk.gpu.handle, gBackendVk.surface, &numPresentModes, nullptr);
        gBackendVk.swapchainInfo.numPresentModes = numPresentModes;
        gBackendVk.swapchainInfo.presentModes = Mem::AllocTyped<VkPresentModeKHR>(numPresentModes, &gBackendVk.parentAlloc);
        vkGetPhysicalDeviceSurfacePresentModesKHR(gBackendVk.gpu.handle, gBackendVk.surface, &numPresentModes, 
                                                  gBackendVk.swapchainInfo.presentModes);

        if (!_InitializeSwapchain(&gBackendVk.swapchain, gBackendVk.surface, Int2(App::GetFramebufferWidth(), App::GetFramebufferHeight())))
            return false;
    }

    // TEMP
    {
        VkFenceCreateInfo fenceInfo {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT
        };

        for (uint32 i = 0; i < GFXBACKEND_FRAMES_IN_FLIGHT; i++) 
            vkCreateFence(gBackendVk.device, &fenceInfo, gBackendVk.vkAlloc, &gBackendVk.renderFences[i]);

    }

    return true;
}

void GfxBackend::Begin()
{
    uint32 renderIdx = gBackendVk.renderIdx;
    vkWaitForFences(gBackendVk.device, 1, &gBackendVk.renderFences[renderIdx], TRUE, UINT64_MAX);
    vkResetFences(gBackendVk.device, 1, &gBackendVk.renderFences[renderIdx]);

    {
        uint32 backbufferIndex = gBackendVk.swapchain.backbufferIdx;
        uint32 imageIdx;
        VkResult r = vkAcquireNextImageKHR(gBackendVk.device, gBackendVk.swapchain.handle, UINT64_MAX, 
                                           gBackendVk.swapchain.swapchainSemaphores[backbufferIndex], nullptr, 
                                           &imageIdx);
        ASSERT(r == VK_SUCCESS);
    }


}

void GfxBackend::End()
{
    gBackendVk.renderIdx = (gBackendVk.renderIdx + 1) % GFXBACKEND_FRAMES_IN_FLIGHT;
    gBackendVk.swapchain.GoNext();
}


void GfxBackend::Release()
{
    MemAllocator* alloc = &gBackendVk.parentAlloc;

    for (uint32 i = 0; i < GFXBACKEND_FRAMES_IN_FLIGHT; i++) {
        if (gBackendVk.renderFences[i])
            vkDestroyFence(gBackendVk.device, gBackendVk.renderFences[i], gBackendVk.vkAlloc);
    }

    _ReleaseSwapchain(&gBackendVk.swapchain);
    _ReleaseDevice();


    if (gBackendVk.surface)
        vkDestroySurfaceKHR(gBackendVk.instance.handle, gBackendVk.surface, gBackendVk.vkAlloc);
    if (gBackendVk.debugMessenger) 
        vkDestroyDebugUtilsMessengerEXT(gBackendVk.instance.handle, gBackendVk.debugMessenger, gBackendVk.vkAlloc);

    Mem::Free(gBackendVk.swapchainInfo.formats, alloc);
    Mem::Free(gBackendVk.swapchainInfo.presentModes, alloc);

    _ReleaseInstance();

    Mem::Free(gBackendVk.queueFamilies, alloc);
    Mem::Free(gBackendVk.queues, alloc);

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
