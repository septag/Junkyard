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
#elif PLATFORM_LINUX
    #include <vulkan/vulkan.h>
    #include <vulkan/vulkan_xlib.h>
    #include <GLFW/glfw3.h>
#else
    #error "Not implemented"
#endif

// Always include volk after vulkan.h
#define VOLK_IMPLEMENTATION
#if PLATFORM_WINDOWS
    #define VK_USE_PLATFORM_WIN32_KHR
#elif PLATFORM_LINUX
    #define VK_USE_PLATFORM_XLIB_KHR
#elif PLATFORM_ANDROID
    #define VK_USE_PLATFORM_ANDROID_KHR
#elif PLATFORM_OSX
    #define VK_USE_PLATFORM_MACOS_MVK
#elif PLATFORM_IOS
    #define VK_USE_PLATFORM_IOS_MVK
#endif
#ifndef __10X__
#include "../External/volk/volk.h"
#endif

// Validate our enums matching Vulkan's
#include "ValidateEnumsVk.inl"

#include "../Core/Allocators.h"
#include "../Core/System.h"
#include "../Core/Log.h"
#include "../Core/MathAll.h"
#include "../Core/Arrays.h"
#include "../Core/StringUtil.h"
#include "../Core/Atomic.h"
#include "../Core/Pools.h"
#include "../Core/Hash.h"

#include "../Common/Application.h"
#include "../Common/JunkyardSettings.h"
#include "../Common/CommonTypes.h"
#include "../Common/Profiler.h"

#include "../Engine.h"

#define OFFSET_ALLOCATOR_API_DECL static
#define OFFSET_ALLOCATOR_ASSERT(x) ASSERT(x)
#define OFFSET_ALLOCATOR_IMPLEMENT
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4505)    // unreferenced shader linkage removed
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wunused-function")
#include "../External/OffsetAllocator/offsetAllocator.h"
PRAGMA_DIAGNOSTIC_POP()

static constexpr uint32 GFXBACKEND_MAX_GARBAGE_COLLECT_PER_FRAME = 32;
static constexpr uint32 GFXBACKEND_BACKBUFFER_COUNT = 3;
static constexpr uint32 GFXBACKEND_FRAMES_IN_FLIGHT = 2;
static constexpr uint32 GFXBACKEND_MAX_SETS_PER_PIPELINE = 4;
static constexpr uint32 GFXBACKEND_MAX_ENTRIES_IN_OFFSET_ALLOCATOR = 64*1024;
static constexpr uint32 GFXBACKEND_MAX_QUEUES = 4;

#if PLATFORM_WINDOWS
static const char* GFXBACKEND_DEFAULT_INSTANCE_EXTENSIONS[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
#elif PLATFORM_ANDROID 
static const char* GFXBACKEND_DEFAULT_INSTANCE_EXTENSIONS[] = {"VK_KHR_surface", "VK_KHR_android_surface"};
#elif PLATFORM_APPLE
static const char* GFXBACKEND_DEFAULT_INSTANCE_EXTENSIONS[] = {"VK_KHR_surface", "VK_EXT_metal_surface"};
#elif PLATFORM_LINUX
static const char* GFXBACKEND_DEFAULT_INSTANCE_EXTENSIONS[] = {"VK_KHR_Surface", "VK_KHR_xlib_surface"};
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
    struct ImageState
    {
        VkPipelineStageFlags2 lastStage;
        VkImageLayout lastLayout;
        VkAccessFlags2 lastAccessFlags;
    };

    uint32 backbufferIdx;
    uint32 numImages;
    VkSwapchainKHR handle;
    VkSurfaceFormatKHR format;
    VkImage images[GFXBACKEND_BACKBUFFER_COUNT];
    VkImageView imageViews[GFXBACKEND_BACKBUFFER_COUNT];
    VkSemaphore imageReadySemaphores[GFXBACKEND_BACKBUFFER_COUNT];
    VkSemaphore presentSemaphores[GFXBACKEND_BACKBUFFER_COUNT];
    ImageState imageStates[GFXBACKEND_BACKBUFFER_COUNT];
    VkExtent2D extent;
    uint32 imageIndex;

    bool resize;

    void GoNext() { backbufferIdx = (backbufferIdx + 1) % GFXBACKEND_BACKBUFFER_COUNT; }
    VkSemaphore GetSwapchainSemaphore() { return imageReadySemaphores[backbufferIdx]; }
    VkSemaphore GetPresentSemaphore() { return presentSemaphores[backbufferIdx]; }
    VkImage GetImage() { return images[imageIndex]; }
    VkImageView GetImageView() { return imageViews[imageIndex]; }
    ImageState& GetImageState() { return imageStates[imageIndex]; }
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
    GfxQueueType type;
    uint32 count;
};

struct GfxBackendQueueSubmitRequest
{
    GfxQueueType type;
    GfxQueueType dependents;
    VkCommandBuffer* cmdBuffers;
    VkFence fence;
    VkSemaphore semaphore;
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

struct GfxBackendQueueSemaphoreBank
{
    SpinLockMutex mMutex;
    Array<VkSemaphore> mSemaphores;
    Array<VkSemaphore> mSemaphoreFreeList;

    void Initialize();
    void Release();
    VkSemaphore GetSemaphore();
    void Reset();
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
        uint32 targetQueueIndex;
        GfxResourceTransferCallback resourceTransferedCallback;
        void* resourceTransferedUserData;

        union {
            GfxBufferHandle bufferHandle;
            GfxImageHandle imageHandle;
        };

        union {
            VkBufferMemoryBarrier2 bufferBarrier;
            VkImageMemoryBarrier2 imageBarrier;
        };
    };

    ReadWriteMutex cmdBufferCtxMutex;   
    SpinLockMutex barriersMutex;

    VkQueue handle;
    GfxQueueType type;
    uint32 familyIdx;
    float priority;
    bool supportsTransfer;
    GfxBackendCommandBufferContext cmdBufferContexts[GFXBACKEND_FRAMES_IN_FLIGHT];
    GfxBackendQueueSemaphoreBank semaphoreBanks[GFXBACKEND_FRAMES_IN_FLIGHT];
    Array<WaitSemaphore> waitSemaphores;
    Array<VkSemaphore> signalSemaphores;
    Array<PendingBarrier> pendingBarriers;  // Buffers transfers coming into this queue
    Array<PendingBarrier> dependentBarriers; // Barriers that needs to be submitted for dependent queues (after current submission)
    GfxQueueType internalDependents;
    AtomicUint32 numCmdBuffersInRecording;
    AtomicUint32 numPendingCmdBuffers;
};

struct GfxBackendQueueManager
{
    bool Initialize();
    void PostInitialize();   // Calls after vkDevice is created (See GfxBackend::Initialize)
    void Release();

    void BeginFrame();

    void SubmitQueue(GfxQueueType queueType, GfxQueueType dependentQueues);

    inline uint32 FindQueue(GfxQueueType type) const;
    inline uint32 GetQueueCount() const { return mNumQueues; }
    inline GfxBackendQueue& GetQueue(uint32 index) const { ASSERT(index < mNumQueues); return mQueues[index]; }
    inline VkCommandBuffer GetCommandBufferHandle(const GfxCommandBuffer& cmdBuffer);
    inline uint32 GetGeneration() const { return mGeneration; }
    inline uint32 GetFrameIndex() const { return mFrameIndex; }

private:
    static int SubmitThread(void* userData);
    bool SubmitQueueInternal(GfxBackendQueueSubmitRequest& req);
    void SetupQueuesForDiscreteDevice();
    void SetupQueuesForIntegratedDevice();
    uint32 AssignQueueFamily(GfxQueueType type, GfxQueueType preferNotHave = GfxQueueType::None, 
                             uint32 numExcludes = 0, const uint32* excludeList = nullptr);
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
    OffsetAllocatorNodeIndex offsetAllocMetaData = OFFSET_ALLOCATOR_NO_SPACE;
    uint16 offsetAllocPadding;      // We need this calculate the offset returned by the OffsetAllocator. Main offset is aligned.
    GfxMemoryArena arena;

    uint8 isHeapDeviceLocal : 1;   // Accessible by GPU (fast) 
    uint8 isCpuVisible : 1;        // Can be written by CPU
    uint8 isCached : 1;            // Faster for small frequent updates
    uint8 isCoherent : 1;          // No need to flush/map (potentially slower)
    uint8 isLazilyAlloc : 1;       // TBR

    bool IsValid() { return handle != nullptr || offset == -1; }
};
static_assert(sizeof(GfxBackendDeviceMemory) <= 32);

struct GfxBackendMemoryBumpAllocator
{
    bool Initialize(VkDeviceSize blockSize, uint32 memoryTypeIndex);
    void Release();
    GfxBackendDeviceMemory Malloc(const VkMemoryRequirements& memReq);
    void Reset();

private:
    struct Block
    {
        VkDeviceMemory deviceMem;
        VkDeviceSize offset;
        void* mappedData;      // Only for HOST_VISIBLE memory where we can map the entire buffer upfront
    };

    bool CreateBlock(Block* block);
    void DestroyBlock(Block* block);

    SpinLockMutex mMutex;
    VkDeviceSize mBlockSize;
    VkDeviceSize mCapacity;
    uint32 mMemTypeIndex;
    VkMemoryPropertyFlags mTypeFlags;
    VkMemoryHeapFlags mHeapFlags;
    Array<Block> mBlocks;
};

struct GfxBackendMemoryOffsetAllocator
{
    bool Initialize(VkDeviceSize blockSize, uint32 memoryTypeIndex);
    void Release();
    void Reset();

    GfxBackendDeviceMemory Malloc(const VkMemoryRequirements& memReq);
    void Free(GfxBackendDeviceMemory mem);

private:
    struct Block
    {
        VkDeviceMemory deviceMem;
        void* mappedData;
        OffsetAllocator* offsetAlloc;
    };

    Block* CreateBlock();
    void DestroyBlock(Block* block);

    SpinLockMutex mMutex;
    VkDeviceSize mCapacity;
    uint32 mBlockSize;
    uint32 mMemTypeIndex;
    VkMemoryPropertyFlags mTypeFlags;
    VkMemoryHeapFlags mHeapFlags;
    Array<Block*> mBlocks;
};

// TODO: For memory management, we can improve the initial allocation methods
//       - Use total percent of GPU memory for each areana instead of size
//       - Use Budget info to get available memory, and fallback to total memory if the extension is not available
//       - Make allocators growable with large pages. So basically we have a large "Reserved" like VM defined by Percentages
//         Then add pages for each arena until we reach to that point
struct GfxBackendDeviceMemoryManager
{
    bool Initialize();
    void Release();

    GfxBackendDeviceMemory Malloc(const VkMemoryRequirements& memReq, GfxMemoryArena arena);
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
    GfxBackendMemoryOffsetAllocator mDynamicImageGPU;
    GfxBackendMemoryOffsetAllocator mDynamicBufferGPU;

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
    GfxBackendDeviceMemory mem;

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
    GfxImageDesc desc;
    GfxBackendDeviceMemory mem;
    VkImageLayout layout;
    VkPipelineStageFlags2 transitionedStage;
    VkAccessFlagBits2 transitionedAccess;
};

struct GfxBackendBuffer
{
    VkBuffer handle;
    GfxBufferDesc desc;
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
    uint32 shaderHash;

    union 
    {
        VkGraphicsPipelineCreateInfo* graphics;
        VkComputePipelineCreateInfo* compute;
    } createInfo;
};

struct GfxBackendSampler
{
    VkSampler handle;
    GfxSamplerDesc desc;
};

struct GfxBackendVk
{
    ReadWriteMutex objectPoolsMutex;
    Mutex garbageMtx;
    MemProxyAllocator parentAlloc;
    MemProxyAllocator runtimeAlloc;
    MemProxyAllocator driverAlloc;
    GfxBackendAllocator runtimeAllocBase;
    GfxBackendAllocator driverAllocBase;
    GfxBackendVkAllocator vkAlloc;
    Signal frameSyncSignal;
    Signal externalFrameSyncSignal;
    AtomicUint32 numTransientResroucesInUse;
    AtomicUint32 numOpenExternalFrameSyncs;
    
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
    HandlePool<GfxSamplerHandle, GfxBackendSampler> samplers;
    
    Array<GfxBackendGarbage> garbage;
    uint64 presentFrame;

    VkSampler samplerDefault;
    VkPipelineCache pipelineCache;
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
    static inline VkPipelineStageFlags2 _GetBufferDestStageFlags(GfxQueueType type, GfxShaderStage dstStages, 
                                                                 GfxBufferUsageFlags usageFlags)
    {
        VkPipelineStageFlags2 flags = 0;
        if (type == GfxQueueType::Graphics) {
            if (IsBitsSet<GfxShaderStage>(dstStages, GfxShaderStage::Vertex)) {
                if (IsBitsSet<GfxBufferUsageFlags>(usageFlags, GfxBufferUsageFlags::Vertex)) {
                    flags |= VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
                }
                else if (IsBitsSet<GfxBufferUsageFlags>(usageFlags, GfxBufferUsageFlags::Index)) {
                    flags |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
                }
                else {
                    flags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
                }
            }
            if (IsBitsSet<GfxShaderStage>(dstStages, GfxShaderStage::Fragment)) {
                flags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            }
        }
        else if (type == GfxQueueType::Compute) {
            flags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        }

        return flags;
    }
    
    static inline VkPipelineStageFlags2 _GetImageDestStageFlags(GfxQueueType type, GfxShaderStage dstStages)
    {
        VkPipelineStageFlags2 flags = 0;
        if (type == GfxQueueType::Graphics) {
            if (IsBitsSet<GfxShaderStage>(dstStages, GfxShaderStage::Vertex)) 
                flags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
            if (IsBitsSet<GfxShaderStage>(dstStages, GfxShaderStage::Fragment)) 
                flags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        }
        else if (type == GfxQueueType::Compute) {
            flags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        }

        return flags;
    }

    static inline VkAccessFlags2 _GetImageReadAccessFlags(VkImageUsageFlags usageFlags)
    {
        VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT|
            VK_ACCESS_2_SHADER_READ_BIT|
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT|
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT|
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT|
            VK_ACCESS_2_SHADER_BINDING_TABLE_READ_BIT_KHR;

        VkAccessFlags2 accessFlags = 0;
        if (usageFlags & VK_IMAGE_USAGE_SAMPLED_BIT) 
            accessFlags |= VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        if (usageFlags & VK_IMAGE_USAGE_STORAGE_BIT)
            accessFlags |= VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        if (usageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
            accessFlags |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
        if (usageFlags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
            accessFlags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        if (usageFlags & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
            accessFlags |= VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT;
        if (usageFlags == 0)
            accessFlags = VK_ACCESS_2_MEMORY_READ_BIT;
        return accessFlags;
    }

    static inline const GfxShaderParameterInfo* _FindShaderParam(const GfxShader& shader, const char* paramName)
    {
        for (uint32 i = 0; i < shader.numParams; i++) {
            if (Str::IsEqual(shader.params[i].name, paramName))
                return &shader.params[i];
        }
        return nullptr;
    }

    inline VkCommandBuffer _GetCommandBufferHandle(const GfxCommandBuffer& cmdBuffer)
    {
        GfxBackendQueue& queue = gBackendVk.queueMan.GetQueue(cmdBuffer.mQueueIndex);
        ASSERT_MSG(cmdBuffer.mGeneration == gBackendVk.queueMan.GetGeneration(), 
                   "EndCommandBuffer must be called before resetting the queue");
        
        const GfxBackendCommandBufferContext& cmdBufferMan = queue.cmdBufferContexts[gBackendVk.queueMan.GetFrameIndex()];

        ReadWriteMutexReadScope lock(queue.cmdBufferCtxMutex);
        return cmdBufferMan.cmdBuffers[cmdBuffer.mCmdBufferIndex];
    }

    // https://android-developers.googleblog.com/2020/02/handling-device-orientation-efficiently.html
    static Pair<Int2, Int2> _TransformRectangleBasedOnOrientation(int x, int y, int w, int h, bool isSwapchain)
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
                    gBackendVk.memMan.Free(garbage.mem);
                    break;
                case GfxBackendGarbage::Type::Image:
                    vkDestroyImage(gBackendVk.device, garbage.image, gBackendVk.vkAlloc);
                    gBackendVk.memMan.Free(garbage.mem);
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

                gBackendVk.garbage.Pop(i);
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

        auto HasLayer = [](const char* layerName)
        {
            for (uint32 i = 0; i < gBackendVk.instance.numLayers; i++) {
                if (Str::IsEqual(gBackendVk.instance.layers[i].layerName, layerName))
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
        #elif PLATFORM_LINUX
        glfwCreateWindowSurface(gBackendVk.instance.handle, (GLFWwindow*)App::GetNativeWindowHandle(), 
                                gBackendVk.vkAlloc, &surface);
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
        LOG_INFO("(init) GPU RAM: %_$$$llu", heapSize);
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

        auto CheckAddExtension = [&enabledExtensions](const char* name, bool required = false)->bool
        {
            if (_HasExtension(gBackendVk.gpu.extensions, gBackendVk.gpu.numExtensions, name)) {
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
        gBackendVk.extApi.hasNonSemanticInfo = (gpu.props.apiVersion < VK_API_VERSION_1_3) ? CheckAddExtension("VK_KHR_shader_non_semantic_info") : true;
        gBackendVk.extApi.hasMemoryBudget = CheckAddExtension("VK_EXT_memory_budget");
        if constexpr (PLATFORM_MOBILE)
            gBackendVk.extApi.hasAstcDecodeMode = CheckAddExtension("VK_EXT_astc_decode_mode");
        gBackendVk.extApi.hasPipelineExecutableProperties = settings.graphics.shaderDumpProperties ? CheckAddExtension("VK_KHR_pipeline_executable_properties") : false;

        if (enabledExtensions.Count()) {
            LOG_VERBOSE("Enabled device extensions (%u):", enabledExtensions.Count());
            for (const char* ext : enabledExtensions)
                LOG_VERBOSE("\t%s", ext);
        }

        // Gather Queues
        StaticArray<VkDeviceQueueCreateInfo, GFXBACKEND_MAX_QUEUES> queueCreateInfos;
        for (uint32 i = 0; i < gBackendVk.queueMan.GetQueueCount(); i++) {
            const GfxBackendQueue& queue = gBackendVk.queueMan.GetQueue(i);
            if (settings.graphics.headless && IsBitsSet(queue.type, GfxQueueType::Graphics|GfxQueueType::Present))
                continue;

            bool isDuplicate = false;
            for (uint32 k = 0; k < i; k++) {
                if (gBackendVk.queueMan.GetQueue(k).familyIdx == queue.familyIdx) {
                    isDuplicate = true;
                    break;
                }                    
            }
            if (isDuplicate)
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
            .queueCreateInfoCount = queueCreateInfos.Count(),
            .pQueueCreateInfos = queueCreateInfos.Ptr(),
            .enabledExtensionCount = enabledExtensions.Count(),
            .ppEnabledExtensionNames = enabledExtensions.Ptr()
        };

        // Enable extensions and features
        void** deviceNext = const_cast<void**>(&devCreateInfo.pNext);
        // We already queried all the features in InitializeGPU
        // Just use all the existing features. Unless we explicitly want to turn something off
        // TODO: Can turn on selected set of features
        VkPhysicalDeviceFeatures2 features {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &gpu.features2,
            .features = gpu.features
        };

        *deviceNext = &features;
        deviceNext = &gpu.features4.pNext;

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

    static VkGraphicsPipelineCreateInfo* _DuplicateGraphicsPipelineCreateInfo(const VkGraphicsPipelineCreateInfo& pipelineInfo)
    {
        // Child POD members with arrays inside
        const VkPipelineRenderingCreateInfo* srcRenderingCreateInfo = (VkPipelineRenderingCreateInfo*)pipelineInfo.pNext;
        MemSingleShotMalloc<VkPipelineRenderingCreateInfo> pallocRenderingCreateInfo;
        pallocRenderingCreateInfo.AddMemberArray<VkFormat>(
            offsetof(VkPipelineRenderingCreateInfo, pColorAttachmentFormats), 
            srcRenderingCreateInfo->colorAttachmentCount);
            
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

        mallocator.AddMemberArray<VkPipelineViewportStateCreateInfo>(
            offsetof(VkGraphicsPipelineCreateInfo, pViewportState), 1);
        
        mallocator.AddMemberArray<VkPipelineRasterizationStateCreateInfo>(
            offsetof(VkGraphicsPipelineCreateInfo, pRasterizationState), 1);

        mallocator.AddMemberArray<VkPipelineMultisampleStateCreateInfo>(
            offsetof(VkGraphicsPipelineCreateInfo, pMultisampleState), 1);

        mallocator.AddMemberArray<VkPipelineDepthStencilStateCreateInfo>(
            offsetof(VkGraphicsPipelineCreateInfo, pDepthStencilState), 1);

        mallocator.AddChildStructSingleShot(pallocRenderingCreateInfo, offsetof(VkGraphicsPipelineCreateInfo, pNext), 1);
        mallocator.AddChildStructSingleShot(pallocColorBlendState, offsetof(VkGraphicsPipelineCreateInfo, pColorBlendState), 1);
        mallocator.AddChildStructSingleShot(pallocDynamicState, offsetof(VkGraphicsPipelineCreateInfo, pDynamicState), 1);

        VkGraphicsPipelineCreateInfo* pipInfoNew = mallocator.Calloc(&gBackendVk.runtimeAlloc);

        pipInfoNew->sType = pipelineInfo.sType;
        pipInfoNew->flags = pipelineInfo.flags;
        pipInfoNew->stageCount = pipelineInfo.stageCount;
        memcpy((void*)pipInfoNew->pStages, pipelineInfo.pStages, sizeof(VkPipelineShaderStageCreateInfo)*pipelineInfo.stageCount);
        memcpy((void*)pipInfoNew->pInputAssemblyState, pipelineInfo.pInputAssemblyState, sizeof(VkPipelineInputAssemblyStateCreateInfo));
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

        { // VkPipelineRenderingCreateInfo
            VkPipelineRenderingCreateInfo* renderingCreateInfo = (VkPipelineRenderingCreateInfo*)pipInfoNew->pNext;
            renderingCreateInfo->sType = srcRenderingCreateInfo->sType;
            renderingCreateInfo->viewMask = srcRenderingCreateInfo->viewMask;
            renderingCreateInfo->colorAttachmentCount = srcRenderingCreateInfo->colorAttachmentCount;
            memcpy((void*)renderingCreateInfo->pColorAttachmentFormats, srcRenderingCreateInfo->pColorAttachmentFormats, 
                   sizeof(VkFormat)*srcRenderingCreateInfo->colorAttachmentCount);
            renderingCreateInfo->depthAttachmentFormat = srcRenderingCreateInfo->depthAttachmentFormat;
        }
        
        return pipInfoNew;
    }

    static VkComputePipelineCreateInfo* _DuplicateComputePipelineCreateInfo(const VkComputePipelineCreateInfo& createInfo)
    {
        return Mem::AllocCopy<VkComputePipelineCreateInfo>(&createInfo, 1, &gBackendVk.runtimeAlloc);
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

    gBackendVk.runtimeAllocBase.Initialize(&gBackendVk.parentAlloc, 16*SIZE_MB, debugAllocs);
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
    gBackendVk.pipelines.SetAllocator(&gBackendVk.runtimeAlloc);
    gBackendVk.samplers.SetAllocator(&gBackendVk.runtimeAlloc);
    gBackendVk.buffers.SetAllocator(&gBackendVk.runtimeAlloc);
    gBackendVk.objectPoolsMutex.Initialize();

    gBackendVk.garbage.SetAllocator(&gBackendVk.runtimeAlloc);
    gBackendVk.garbageMtx.Initialize();

    gBackendVk.frameSyncSignal.Initialize();
    gBackendVk.externalFrameSyncSignal.Initialize();
    gBackendVk.externalFrameSyncSignal.Increment();

    // Make a trilinear sampler as default sampler
    // TODO: Make a better sampler system
    {
        VkSamplerCreateInfo samplerInfo {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .mipLodBias = 0.0f,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1.0f,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0,
            .maxLod = VK_LOD_CLAMP_NONE,
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            .unnormalizedCoordinates = VK_FALSE, 
        };

        if (vkCreateSampler(gBackendVk.device, &samplerInfo, gBackendVk.vkAlloc, &gBackendVk.samplerDefault) != VK_SUCCESS) {
            LOG_ERROR("Gfx: CreateSampler failed");
        }
    }

    // Pipeline Cache
    // TODO: Serialize the cache
    VkPipelineCacheCreateInfo pipelineCacheCreateInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
    };
    vkCreatePipelineCache(gBackendVk.device, &pipelineCacheCreateInfo, gBackendVk.vkAlloc, &gBackendVk.pipelineCache);

    return true;
}

void GfxBackend::Begin()
{
    PROFILE_ZONE_COLOR(PROFILE_COLOR_GFX1);

    ASSERT_MSG(Engine::IsMainThread(), "Update can only be called in the main thread");

    // GPU -> CPU sync
    gBackendVk.queueMan.BeginFrame();

    // Unlock external systems to use and submit command-buffers
    gBackendVk.externalFrameSyncSignal.Decrement();
    gBackendVk.externalFrameSyncSignal.Raise();

    gBackendVk.swapchain.AcquireImage();
}

void GfxCommandBuffer::ClearImageColor(GfxImageHandle imgHandle, Color4u color)
{
    ClearImageColor(imgHandle, Color4u::ToFloat4(color));
}

void GfxCommandBuffer::ClearImageColor(GfxImageHandle imgHandle, Float4 color)
{
    mShouldSubmit = true;

    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);

    gBackendVk.objectPoolsMutex.EnterRead();
    GfxBackendImage& image = gBackendVk.images.Data(imgHandle);
    ASSERT_MSG(image.layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL || image.layout == VK_IMAGE_LAYOUT_GENERAL, 
               "Image should be already transitioned to TRANSFER_DST_OPTIMAL or GENERAL layout");
    VkImageLayout imageLayout = image.layout;
    VkImage imageHandle = image.handle;
    gBackendVk.objectPoolsMutex.ExitRead();

    VkClearColorValue clearVal = {{color.x, color.y, color.z, color.w}};
    VkImageSubresourceRange clearRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .layerCount = VK_REMAINING_ARRAY_LAYERS
    };

    vkCmdClearColorImage(cmdVk, imageHandle, imageLayout, &clearVal, 1, &clearRange);
}

void GfxCommandBuffer::ClearSwapchainColor(Float4 color)
{
    ASSERT(!mIsInRenderPass);

    mShouldSubmit = true;

    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);

    VkImage imageVk = gBackendVk.swapchain.GetImage();

    {
        VkImageMemoryBarrier2 imageBarrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = imageVk,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .layerCount = VK_REMAINING_ARRAY_LAYERS
            }
        };

        VkDependencyInfo depInfo {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &imageBarrier
        };

        vkCmdPipelineBarrier2(cmdVk, &depInfo);
    }

    VkClearColorValue clearVal = {{color.x, color.y, color.z, color.w}};
    VkImageSubresourceRange clearRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .layerCount = VK_REMAINING_ARRAY_LAYERS
    };
    vkCmdClearColorImage(cmdVk, imageVk, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearVal, 1, &clearRange);

    GfxBackendSwapchain::ImageState& state = gBackendVk.swapchain.GetImageState();
    state.lastStage = VK_PIPELINE_STAGE_2_CLEAR_BIT;
    state.lastLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    state.lastAccessFlags = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mDrawsToSwapchain = true;
    gBackendVk.queueMan.GetQueue(mQueueIndex).internalDependents |= GfxQueueType::Present;
}

void GfxCommandBuffer::CopyImageToSwapchain(GfxImageHandle imgHandle)
{
    ASSERT(!mIsInRenderPass);
    ASSERT(mIsRecording);

    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);

    gBackendVk.objectPoolsMutex.EnterRead();
    GfxBackendImage& image = gBackendVk.images.Data(imgHandle);
    ASSERT_MSG(image.layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, "Image should be already transitioned to TRANSFER_SRC_OPTIMAL layout");
    VkImage imageHandle = image.handle;
    int imageWidth = int(image.desc.width);
    int imageHeight = int(image.desc.height);
    gBackendVk.objectPoolsMutex.ExitRead();

    VkImage swapchainImage = gBackendVk.swapchain.GetImage();
    
    {
        VkImageMemoryBarrier2 imageBarrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = swapchainImage,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .layerCount = VK_REMAINING_ARRAY_LAYERS
            }
        };

        VkDependencyInfo depInfo {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &imageBarrier
        };

        vkCmdPipelineBarrier2(cmdVk, &depInfo);
    }

    {
        VkImageBlit2 blitRegion {
            .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1
            },
            .srcOffsets = {
                {0, 0, 0},
                {imageWidth, imageHeight, 1}
            },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1
            },
            .dstOffsets = {
                {0, 0, 0},
                {int(gBackendVk.swapchain.extent.width), int(gBackendVk.swapchain.extent.height), 1}
            },
        };

        VkBlitImageInfo2 blitInfo {
            .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
            .srcImage = imageHandle,
            .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .dstImage = swapchainImage,
            .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .regionCount = 1,
            .pRegions = &blitRegion,
            .filter = VK_FILTER_LINEAR
        };

        vkCmdBlitImage2(cmdVk, &blitInfo);
    }

    GfxBackendSwapchain::ImageState& state = gBackendVk.swapchain.GetImageState();
    state.lastStage = VK_PIPELINE_STAGE_2_COPY_BIT;
    state.lastLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    state.lastAccessFlags = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mDrawsToSwapchain = true;
    mShouldSubmit = true;

    gBackendVk.queueMan.GetQueue(mQueueIndex).internalDependents |= GfxQueueType::Present;
}

void GfxBackend::End()
{
    PROFILE_ZONE_COLOR(PROFILE_COLOR_GFX1);

    // Lock external systems to wait until Begin() call ends
    gBackendVk.externalFrameSyncSignal.Increment();

    // CPU <-> CPU sync: Transient memory and CommandBuffers
    // Before we move on to the next frame, we must make sure that no transient memory allocation or CommandBuffer creation is left out and leaked to next frame
    // Locked when a CommandBuffer is created or Transient memory is created
    // Unlocked when all CommandBuffers are submitted and objects binded to Transient memory are destroyed
    if (!gBackendVk.frameSyncSignal.WaitOnCondition([](int value, int ref) { return value > ref; }, 0, 500)) {
        for (uint32 i = 0; i < gBackendVk.queueMan.GetQueueCount(); i++) {
            GfxBackendQueue& queue = gBackendVk.queueMan.GetQueue(i);
            ASSERT_MSG(Atomic::Load(&queue.numPendingCmdBuffers) == 0, 
                       "Queue index %u still has %u pending CommandBuffers that aren't submitted", i, queue.numPendingCmdBuffers);
        }

        ASSERT_MSG(Atomic::Load(&gBackendVk.numOpenExternalFrameSyncs) == 0, 
                   "There are %u BeginRenderFrameSync() calls that are not closed with EndRenderFrameSync()", 
                   gBackendVk.numOpenExternalFrameSyncs);

        ASSERT_MSG(Atomic::Load(&gBackendVk.numTransientResroucesInUse) == 0,
                   "There are %u Transient resources (Buffer/Image) that are not Destroyed in the frame yet", 
                   gBackendVk.numTransientResroucesInUse);

        LOG_WARNING("Gfx: Waiting too long for backend CPU syncing. Enforcing device wait");
        vkDeviceWaitIdle(gBackendVk.device);
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

        uint32 queueIndex = gBackendVk.queueMan.FindQueue(GfxQueueType::Present);
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

    if (gBackendVk.samplerDefault)
        vkDestroySampler(gBackendVk.device, gBackendVk.samplerDefault, gBackendVk.vkAlloc);        

    _CollectGarbage(true);

    // TODO: Save the cache to disk
    if (gBackendVk.pipelineCache) 
        vkDestroyPipelineCache(gBackendVk.device, gBackendVk.pipelineCache, gBackendVk.vkAlloc);

    gBackendVk.pipelineLayouts.Free();
    gBackendVk.images.Free();
    gBackendVk.samplers.Free();
    gBackendVk.buffers.Free();
    gBackendVk.pipelines.Free();
    gBackendVk.objectPoolsMutex.Release();

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
    gBackendVk.externalFrameSyncSignal.Release();

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

GfxCommandBuffer GfxBackend::BeginCommandBuffer(GfxQueueType queueType)
{
    gBackendVk.frameSyncSignal.Increment();

    uint32 queueIndex = gBackendVk.queueMan.FindQueue(queueType);
    ASSERT(queueIndex != -1);
    GfxBackendQueue& queue = gBackendVk.queueMan.GetQueue(queueIndex);
    ASSERT(queue.handle);
    GfxBackendCommandBufferContext& cmdBufferCtx = queue.cmdBufferContexts[gBackendVk.queueMan.GetFrameIndex()];
    VkCommandBuffer cmdVk;

    queue.cmdBufferCtxMutex.EnterWrite();
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
    queue.cmdBufferCtxMutex.ExitWrite();

    VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT 
    };

    [[maybe_unused]] VkResult r = vkBeginCommandBuffer(cmdVk, &beginInfo);
    ASSERT(r == VK_SUCCESS);

    GfxCommandBuffer cmdBuffer {
        .mGeneration = gBackendVk.queueMan.GetGeneration(),
        .mCmdBufferIndex = uint16(cmdBufferIndex),
        .mQueueIndex = uint8(queueIndex)
    };

    // Record all pending buffer memory barriers
    queue.barriersMutex.Enter();
    if (!queue.pendingBarriers.IsEmpty()) {
        using ResourceTransferCallbackPair = Pair<GfxResourceTransferCallback, void*>;

        MemTempAllocator tempAlloc;
        uint32 numPendingBarriers = queue.pendingBarriers.Count();
        GfxBackendQueue::PendingBarrier* pendingBarriers = 
            Mem::AllocCopy<GfxBackendQueue::PendingBarrier>(queue.pendingBarriers.Ptr(), numPendingBarriers, &tempAlloc);
        queue.pendingBarriers.Clear();
        queue.barriersMutex.Exit();

        VkBufferMemoryBarrier2* bufferBarriers = tempAlloc.MallocTyped<VkBufferMemoryBarrier2>(numPendingBarriers);
        VkImageMemoryBarrier2* imageBarriers = tempAlloc.MallocTyped<VkImageMemoryBarrier2>(numPendingBarriers);
        Array<ResourceTransferCallbackPair> transferFinishedCallbacks(&tempAlloc);
        transferFinishedCallbacks.Reserve(numPendingBarriers);
        uint32 numBufferBarriers = 0;
        uint32 numImageBarriers = 0;

        gBackendVk.objectPoolsMutex.EnterRead();
        for (uint32 i = 0; i < numPendingBarriers; i++) {
            const GfxBackendQueue::PendingBarrier& b = pendingBarriers[i];
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
                img.layout = imageBarriers[index].newLayout;
                img.transitionedStage = imageBarriers[index].dstStageMask;
                img.transitionedAccess = imageBarriers[index].dstAccessMask;
            }

            if (b.resourceTransferedCallback)
                transferFinishedCallbacks.Push(ResourceTransferCallbackPair(b.resourceTransferedCallback, b.resourceTransferedUserData));
        }
        gBackendVk.objectPoolsMutex.ExitRead();

        VkDependencyInfo depInfo {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = numBufferBarriers,
            .pBufferMemoryBarriers = bufferBarriers,
            .imageMemoryBarrierCount = numImageBarriers,
            .pImageMemoryBarriers = imageBarriers
        };
        
        vkCmdPipelineBarrier2(cmdVk, &depInfo);

        // Trigger all the resource finished uploading callbacks
        for (ResourceTransferCallbackPair& c : transferFinishedCallbacks) 
            c.first(c.second);
    }
    else {
        queue.barriersMutex.Exit();
    }

    cmdBuffer.mIsRecording = true;

    Atomic::FetchAdd(&queue.numCmdBuffersInRecording, 1);
    Atomic::FetchAdd(&queue.numPendingCmdBuffers, 1);
    return cmdBuffer;
}

void GfxBackend::EndCommandBuffer(GfxCommandBuffer& cmdBuffer)
{
    ASSERT(cmdBuffer.mIsRecording && !cmdBuffer.mIsInRenderPass);
    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(cmdBuffer);

    // Transition the swapchain to PRESENT layout if we have drawn to it
    if (cmdBuffer.mDrawsToSwapchain) {
        GfxBackendSwapchain::ImageState& state = gBackendVk.swapchain.GetImageState();

        VkImageMemoryBarrier2 imageBarrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = state.lastStage,
            .srcAccessMask = state.lastAccessFlags,
            .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            .oldLayout = state.lastLayout,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = gBackendVk.swapchain.GetImage(),
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .layerCount = VK_REMAINING_ARRAY_LAYERS
            }
        };

        VkDependencyInfo depInfo {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &imageBarrier
        };

        vkCmdPipelineBarrier2(cmdVk, &depInfo);

        state.lastStage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        state.lastLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        state.lastAccessFlags = 0;
    }

    [[maybe_unused]] VkResult r = vkEndCommandBuffer(cmdVk);
    ASSERT(r == VK_SUCCESS);
    cmdBuffer.mIsRecording = false;

    GfxBackendQueue& queue = gBackendVk.queueMan.GetQueue(cmdBuffer.mQueueIndex);
    Atomic::FetchSub(&queue.numCmdBuffersInRecording, 1);
}

void GfxBackendSwapchain::AcquireImage()
{
    PROFILE_ZONE_COLOR(PROFILE_COLOR_GFX2);

    [[maybe_unused]] VkResult r = vkAcquireNextImageKHR(gBackendVk.device, handle, UINT64_MAX, imageReadySemaphores[backbufferIdx], 
                                                        nullptr, &imageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR)
        resize = true;
    else if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) 
        ASSERT_ALWAYS(0, "Gfx: AcquireSwapchain failed");
}

bool GfxBackendMemoryBumpAllocator::Initialize(VkDeviceSize blockSize, uint32 memoryTypeIndex)
{
    ASSERT(memoryTypeIndex != -1);
    ASSERT(gBackendVk.device);
    ASSERT(blockSize);

    mMemTypeIndex = memoryTypeIndex;
    mBlockSize = blockSize;
    mBlocks.SetAllocator(&gBackendVk.runtimeAlloc);

    const VkMemoryType& memType = gBackendVk.memMan.GetProps().memoryTypes[memoryTypeIndex];
    mTypeFlags = memType.propertyFlags;
    mHeapFlags = gBackendVk.memMan.GetProps().memoryHeaps[memType.heapIndex].flags;

    return CreateBlock(mBlocks.Push());
}

void GfxBackendMemoryBumpAllocator::Release()
{
    for (GfxBackendMemoryBumpAllocator::Block& block : mBlocks)
        DestroyBlock(&block);

    mBlocks.Free();    
    mCapacity = 0;
    mBlockSize = 0;
    mMemTypeIndex = 0;
}

bool GfxBackendMemoryBumpAllocator::CreateBlock(Block* block)
{
    *block = {};

    if (gBackendVk.extApi.hasMemoryBudget) {
        ASSERT_MSG(gBackendVk.memMan.GetDeviceMemoryBudget(mMemTypeIndex) >= mBlockSize, 
                   "Not enough GPU memory available in the specified heap");
    }

    VkMemoryAllocateInfo allocInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mBlockSize,
        .memoryTypeIndex = mMemTypeIndex
    };
    VkResult r = vkAllocateMemory(gBackendVk.device, &allocInfo, gBackendVk.vkAlloc, &block->deviceMem);
    if (r != VK_SUCCESS) {
        MEM_FAIL();
        return false;
    }

    if (mTypeFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        r = vkMapMemory(gBackendVk.device, block->deviceMem, 0, VK_WHOLE_SIZE, 0, &block->mappedData);
        ASSERT(r == VK_SUCCESS);
    }

    mCapacity += mBlockSize;
    return true;
}

void GfxBackendMemoryBumpAllocator::DestroyBlock(Block* block)
{
    if (block->deviceMem) {
        if (block->mappedData)
            vkUnmapMemory(gBackendVk.device, block->deviceMem);
        vkFreeMemory(gBackendVk.device, block->deviceMem, gBackendVk.vkAlloc);
    }
    *block = {};
}

GfxBackendDeviceMemory GfxBackendMemoryBumpAllocator::Malloc(const VkMemoryRequirements& memReq)
{
    if (!((memReq.memoryTypeBits >> mMemTypeIndex) & 0x1)) {
        ASSERT_ALWAYS(0, "Allocation for this resource is not supported by this memory type");
        return GfxBackendDeviceMemory {};
    }

    ASSERT(memReq.alignment);

    if (memReq.size > mBlockSize) {
        ASSERT_MSG(0, "GpuMemoryAllocator block size (%_$$$llu) is smaller than requested size (%_$$$llu)", mBlockSize, memReq.size);
        MEM_FAIL();
        return GfxBackendDeviceMemory {};
    }

    SpinLockMutexScope lock(mMutex);

    // Start trying from the last block to first
    // So there's a higher chance that we hit what we want earlier
    Block* block = nullptr;
    VkDeviceSize offset = 0;
    for (uint32 i = mBlocks.Count(); i-- > 0;) {
        Block& b = mBlocks[i];
        offset = b.offset;
        if (offset % memReq.alignment != 0)
            offset = AlignValue<VkDeviceSize>(offset, memReq.alignment);
        if (offset + memReq.size <= mBlockSize) {
            block = &b;
            break;
        }         
    }

    if (!block) {
        if (!CreateBlock(mBlocks.Push()))
            return GfxBackendDeviceMemory {};
        block = &mBlocks.Last();
        offset = 0;
    }

    block->offset = offset + memReq.size;

    GfxBackendDeviceMemory mem {
        .handle = block->deviceMem,
        .offset = offset,
        .mappedData = block->mappedData ? ((uint8*)block->mappedData + offset) : nullptr,
        .isHeapDeviceLocal = (mHeapFlags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
        .isCpuVisible = (mTypeFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        .isCached = (mTypeFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) == VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        .isCoherent = (mTypeFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        .isLazilyAlloc = (mTypeFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) == VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT
    };

    return mem;
}

void GfxBackendMemoryBumpAllocator::Reset()
{
    for (Block& block : mBlocks)
        block.offset = 0;
}

GfxImageHandle GfxBackend::CreateImage(const GfxImageDesc& desc)
{
    GfxImageHandle handle {};
    GfxBackend::BatchCreateImage(1, &desc, &handle);
    return handle;    
}

void GfxBackend::BatchCreateImage(uint32 numImages, const GfxImageDesc* descs, GfxImageHandle* outHandles)
{
    ASSERT(numImages);
    ASSERT(descs);
    ASSERT(outHandles);

    MemTempAllocator tempAlloc;
    GfxBackendImage* images = tempAlloc.MallocTyped<GfxBackendImage>(numImages);
    uint32 numTransientIncrements = 0;
    for (uint32 i = 0; i < numImages; i++) {
        const GfxImageDesc& desc = descs[i];
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
        [[maybe_unused]] VkResult r = vkCreateImage(gBackendVk.device, &imageCreateInfo, gBackendVk.vkAlloc, &imageVk);
        ASSERT_ALWAYS(r == VK_SUCCESS, "vkCreateImage failed");
 
        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(gBackendVk.device, imageVk, &memReq);
        GfxBackendDeviceMemory mem = gBackendVk.memMan.Malloc(memReq, desc.arena);
        vkBindImageMemory(gBackendVk.device, imageVk, mem.handle, mem.offset);

        if (desc.arena == GfxMemoryArena::TransientCPU)
            ++numTransientIncrements;

        // View
        VkImageAspectFlags aspect = 0;
        if (GfxBackend::_FormatHasDepth(desc.format))
            aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (GfxBackend::_FormatHasStencil(desc.format))
            aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        if (aspect == 0) 
            aspect = VK_IMAGE_ASPECT_COLOR_BIT;

        VkImageView imageViewVk = nullptr;
        {
            // TEMP: view type can be cube / array / etc.
            VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
            switch (desc.type) {
            case GfxImageType::Image1D: viewType = VK_IMAGE_VIEW_TYPE_1D; break;
            case GfxImageType::Image2D: viewType = VK_IMAGE_VIEW_TYPE_2D; break;
            case GfxImageType::Image3D: viewType = VK_IMAGE_VIEW_TYPE_3D; break;
            }

            VkImageViewCreateInfo viewCreateInfo {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = imageVk,
                .viewType = viewType,
                .format = VkFormat(desc.format),
                .subresourceRange {
                    .aspectMask = aspect,
                    .levelCount = desc.numMips,
                    .layerCount = desc.numArrayLayers
                }
            };

            r = vkCreateImageView(gBackendVk.device, &viewCreateInfo, gBackendVk.vkAlloc, &imageViewVk);
            ASSERT_ALWAYS(r == VK_SUCCESS, "vkCreateImageView failed");
        }

        images[i] = {
            .handle = imageVk,
            .viewHandle = imageViewVk,
            .desc = desc,
            .mem = mem
        };
    }

    if (numTransientIncrements) {
        gBackendVk.frameSyncSignal.Increment(numTransientIncrements);
        Atomic::FetchAdd(&gBackendVk.numTransientResroucesInUse, numTransientIncrements);
    }

    ReadWriteMutexWriteScope objPoolLock(gBackendVk.objectPoolsMutex);
    for (uint32 i = 0; i < numImages; i++)
        outHandles[i] = gBackendVk.images.Add(images[i]);
}

void GfxBackend::DestroyImage(GfxImageHandle& handle)
{
    if (handle.IsValid()) 
        GfxBackend::BatchDestroyImage(1, &handle);
}

void GfxBackend::BatchDestroyImage(uint32 numImages, GfxImageHandle* handles)
{
    ASSERT(numImages);
    ASSERT(handles);

    MemTempAllocator tempAlloc;
    Array<GfxBackendGarbage> garbages(&tempAlloc);
    uint32 numTransientDecrements = 0;

    {
        ReadWriteMutexWriteScope objPoolLock(gBackendVk.objectPoolsMutex);

        for (uint32 i = 0; i < numImages; i++) {
            GfxImageHandle handle = handles[i];
            if (handle.IsValid()) {
                GfxBackendImage& image = gBackendVk.images.Data(handle);

                {
                    if (image.handle) {
                        garbages.Push({
                            .type = GfxBackendGarbage::Type::Image,
                            .frameIdx = gBackendVk.presentFrame,
                            .mem = image.mem,
                            .image = image.handle
                        });
                    }

                    if (image.viewHandle) {
                        garbages.Push({
                            .type = GfxBackendGarbage::Type::ImageView,
                            .frameIdx = gBackendVk.presentFrame,
                            .imageView = image.viewHandle
                        });
                    }
                }

                gBackendVk.images.Remove(handle);

                if (image.mem.arena == GfxMemoryArena::TransientCPU)
                    numTransientDecrements++;

                handles[i] = {};
            }
        }
    }

    if (numTransientDecrements) {
        Atomic::FetchSub(&gBackendVk.numTransientResroucesInUse, numTransientDecrements);

        gBackendVk.frameSyncSignal.Decrement(numTransientDecrements);
        gBackendVk.frameSyncSignal.Raise();
    }

    MutexScope lock(gBackendVk.garbageMtx);
    gBackendVk.garbage.Extend(garbages);
}

const GfxImageDesc& GfxBackend::GetImageDesc(GfxImageHandle handle)
{
    ReadWriteMutexReadScope objPoolLock(gBackendVk.objectPoolsMutex);
    const GfxBackendImage& image = gBackendVk.images.Data(handle);
    return image.desc;
}

GfxPipelineLayoutHandle GfxBackend::CreatePipelineLayout(const GfxShader& shader, const GfxPipelineLayoutDesc& desc)
{
    struct DescriptorSetRef
    {
        uint32 startIndex;
        uint32 count;
    };

    MemTempAllocator tempAlloc;
    
    // Construct Vulkan-specific structs for bindings and their names
    VkDescriptorSetLayoutBinding* bindingsVk = nullptr;
    Array<const char*> names(&tempAlloc);
    Array<GfxPipelineLayoutDesc::Binding> bindings(&tempAlloc);
    StaticArray<DescriptorSetRef, GFXBACKEND_MAX_SETS_PER_PIPELINE> sets;

    if (desc.numBindings) {
        bindingsVk = tempAlloc.MallocTyped<VkDescriptorSetLayoutBinding>(desc.numBindings);
        bindings.Reserve(desc.numBindings);
        names.Reserve(desc.numBindings + desc.numPushConstants);

        // Sort all bindings by their set index
        for (uint32 i = 0; i < desc.numBindings; i++)  {
            ASSERT(desc.bindings[i].setIndex < GFXBACKEND_MAX_SETS_PER_PIPELINE);
            bindings.PushAndSort(desc.bindings[i], 
                                 [](const GfxPipelineLayoutDesc::Binding& a, GfxPipelineLayoutDesc::Binding& b) 
            {
                return int(a.setIndex) - int(b.setIndex);
            });
        }

        // Create descriptor sets
        uint32 setBindingStartIndex = 0;
        uint32 setBindingCount = 0;
        uint8 setIndex = bindings[0].setIndex;
        for (uint32 i = 0; i < bindings.Count(); i++) {
            const GfxPipelineLayoutDesc::Binding& binding = bindings[i];
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
    }    


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

    {
        ReadWriteMutexReadScope objPoolLock(gBackendVk.objectPoolsMutex);
        if (GfxPipelineLayoutHandle layoutHandle = gBackendVk.pipelineLayouts.FindIf(
            [hash](const GfxBackendPipelineLayout* item)->bool { return item->hash == hash; }); 
            layoutHandle.IsValid())
        {
            GfxBackendPipelineLayout* item = gBackendVk.pipelineLayouts.Data(layoutHandle);
            ++item->refCount;
            return layoutHandle;
        }
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
        const GfxPipelineLayoutDesc::Binding& srcBinding = bindings[i];
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

    ReadWriteMutexWriteScope objPoolLock(gBackendVk.objectPoolsMutex);
    return gBackendVk.pipelineLayouts.Add(layout);
}

void GfxBackend::DestroyPipelineLayout(GfxPipelineLayoutHandle& handle)
{
    if (handle.IsValid()) {
        ReadWriteMutexReadScope objPoolLock(gBackendVk.objectPoolsMutex);
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

        handle = {};
    }
}

void GfxBackend::ReloadShaderPipelines(const GfxShader& shader)
{
    ReadWriteMutexReadScope objPoolLock(gBackendVk.objectPoolsMutex);

    for (GfxBackendPipeline& pipeline : gBackendVk.pipelines) {
        if (pipeline.shaderHash != shader.hash)
            continue;
        
        // Reload the shaders by only reloading the modules
        VkPipeline oldPipeline = nullptr;
        if (pipeline.type == GfxBackendPipeline::PipelineTypeGraphics) {
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
                    return;
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
                    return;
                }
            }

            const_cast<VkPipelineShaderStageCreateInfo*>(&pipeline.createInfo.graphics->pStages[0])->module = vsShaderModule;
            const_cast<VkPipelineShaderStageCreateInfo*>(&pipeline.createInfo.graphics->pStages[1])->module = psShaderModule;

            VkPipeline pipelineVk;
            if (vkCreateGraphicsPipelines(gBackendVk.device, nullptr, 1, pipeline.createInfo.graphics, gBackendVk.vkAlloc, &pipelineVk) != VK_SUCCESS) 
            {
                LOG_ERROR("Gfx: Failed to create graphics pipeline for shader '%s'", shader.name);
                return;
            }

            vkDestroyShaderModule(gBackendVk.device, vsShaderModule, gBackendVk.vkAlloc);
            vkDestroyShaderModule(gBackendVk.device, psShaderModule, gBackendVk.vkAlloc);

            oldPipeline = pipeline.handle;
            pipeline.handle = pipelineVk;
        }
        else if (pipeline.type == GfxBackendPipeline::PipelineTypeCompute) {
            const GfxShaderStageInfo* csInfo = nullptr; 

            for (uint32 i = 0; i < shader.numStages; i++) {
                if (shader.stages[i].stage == GfxShaderStage::Compute)
                    csInfo = &shader.stages[i];
            }
            ASSERT_MSG(csInfo, "Shader '%s' is missing Compute shader program", shader.name);

            VkShaderModule csShaderModule;
            VkShaderModuleCreateInfo shaderStageCreateInfo {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = csInfo->dataSize,
                .pCode = (const uint32*)csInfo->data.Get()
            };

            if (vkCreateShaderModule(gBackendVk.device, &shaderStageCreateInfo, gBackendVk.vkAlloc, &csShaderModule) != VK_SUCCESS) {
                LOG_ERROR("Gfx: Failed to compile Compute module for shader '%s'", shader.name);
                return;
            }
            const_cast<VkPipelineShaderStageCreateInfo*>(&pipeline.createInfo.compute->stage)->module = csShaderModule;

            VkPipeline pipelineVk;
            if (vkCreateComputePipelines(gBackendVk.device, nullptr, 1, pipeline.createInfo.compute, gBackendVk.vkAlloc, &pipelineVk) != VK_SUCCESS) 
            {
                LOG_ERROR("Gfx: Failed to create compute pipeline for shader '%s'", shader.name);
                return;
            }

            vkDestroyShaderModule(gBackendVk.device, csShaderModule, gBackendVk.vkAlloc);

            oldPipeline = pipeline.handle;
            pipeline.handle = pipelineVk;
        }

        ASSERT(oldPipeline);
        MutexScope lock(gBackendVk.garbageMtx);
        gBackendVk.garbage.Push({
            .type = GfxBackendGarbage::Type::Pipeline,
            .frameIdx = gBackendVk.presentFrame,
            .pipeline = oldPipeline
        });
    }
}

GfxPipelineHandle GfxBackend::CreateGraphicsPipeline(const GfxShader& shader, GfxPipelineLayoutHandle layoutHandle, 
                                                     const GfxGraphicsPipelineDesc& desc)
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

    VkPipelineLayout layoutVk;
    
    {
        ReadWriteMutexReadScope objPoolLock(gBackendVk.objectPoolsMutex);
        layoutVk = gBackendVk.pipelineLayouts.Data(layoutHandle)->handle;
    }

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

    ASSERT(desc.numColorAttachments);
    VkPipelineRenderingCreateInfo renderCreateInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = desc.numColorAttachments,
        .pColorAttachmentFormats = (const VkFormat*)desc.colorAttachmentFormats,
        .depthAttachmentFormat = VkFormat(desc.depthAttachmentFormat),
        .stencilAttachmentFormat = VkFormat(desc.stencilAttachmentFormat)
    };
    
    VkGraphicsPipelineCreateInfo pipelineInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderCreateInfo,
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

    // TODO: implement hasPipelineExecutableProperties (shaderDumpProperties option):
    //  Dump shader properties into text files
    //  https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_pipeline_executable_properties.html

    VkPipeline pipelineVk;
    if (vkCreateGraphicsPipelines(gBackendVk.device, gBackendVk.pipelineCache, 1, &pipelineInfo, gBackendVk.vkAlloc, &pipelineVk) != VK_SUCCESS) {
        LOG_ERROR("Gfx: Failed to create graphics pipeline for shader '%s'", shader.name);
        return GfxPipelineHandle();
    }

    // Should we keep these shader modules ?
    vkDestroyShaderModule(gBackendVk.device, vsShaderModule, gBackendVk.vkAlloc);
    vkDestroyShaderModule(gBackendVk.device, psShaderModule, gBackendVk.vkAlloc);

    GfxBackendPipeline pipeline {
        .handle = pipelineVk,
        .type = GfxBackendPipeline::PipelineTypeGraphics,
        .shaderHash = shader.hash,
        .createInfo = {
            .graphics = _DuplicateGraphicsPipelineCreateInfo(pipelineInfo)
        }
    };

    ReadWriteMutexWriteScope objPoolLock(gBackendVk.objectPoolsMutex);
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

    VkPipelineLayout layoutVk;
    {
        ReadWriteMutexReadScope objPoolLock(gBackendVk.objectPoolsMutex);
        layoutVk = gBackendVk.pipelineLayouts.Data(layoutHandle)->handle;
    }
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
    if (vkCreateComputePipelines(gBackendVk.device, gBackendVk.pipelineCache, 1, &pipelineCreateInfo, gBackendVk.vkAlloc, &pipelineVk) != VK_SUCCESS) {
        LOG_ERROR("Gfx: Failed to create compute pipeline for shader '%s'", shader.name);
        return GfxPipelineHandle();
    }

    // Should we keep the shader module ?
    vkDestroyShaderModule(gBackendVk.device, csShaderModule, gBackendVk.vkAlloc);

    // TODO: gfxSavePipelineBinaryProperties()
    GfxBackendPipeline pipeline {
        .handle = pipelineVk,
        .type = GfxBackendPipeline::PipelineTypeCompute,
        .shaderHash = shader.hash,
        .createInfo = {
            .compute = _DuplicateComputePipelineCreateInfo(pipelineCreateInfo)
        }
    };

    ReadWriteMutexWriteScope objPoolLock(gBackendVk.objectPoolsMutex);
    return gBackendVk.pipelines.Add(pipeline);
}


void GfxBackend::DestroyPipeline(GfxPipelineHandle& handle)
{
    if (handle.IsValid()) {
        ReadWriteMutexWriteScope objPoolLock(gBackendVk.objectPoolsMutex);
        GfxBackendPipeline& pipeline = gBackendVk.pipelines.Data(handle);
        if (pipeline.handle) {
            MutexScope lock(gBackendVk.garbageMtx);
            gBackendVk.garbage.Push({
                .type = GfxBackendGarbage::Type::Pipeline,
                .frameIdx = gBackendVk.presentFrame,
                .pipeline = pipeline.handle
            });
        }

        if (pipeline.type == GfxBackendPipeline::PipelineTypeGraphics)
            Mem::Free(pipeline.createInfo.graphics, &gBackendVk.runtimeAlloc);
        else if (pipeline.type == GfxBackendPipeline::PipelineTypeCompute)
            Mem::Free(pipeline.createInfo.compute, &gBackendVk.runtimeAlloc);

        gBackendVk.pipelines.Remove(handle);
        handle = {};
    }
}

void GfxCommandBuffer::PushConstants(GfxPipelineLayoutHandle layoutHandle, const char* name, const void* data, uint32 dataSize)
{
    ASSERT(mIsRecording);
    ASSERT(data);
    ASSERT(dataSize);
    ASSERT(name);

    ReadWriteMutexReadScope objPoolLock(gBackendVk.objectPoolsMutex);
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

void GfxCommandBuffer::PushBindings(GfxPipelineLayoutHandle layoutHandle, uint32 numBindings, const GfxBindingDesc* bindings)
{
    ASSERT(mIsRecording);
    ASSERT(numBindings);
    ASSERT(bindings);

    ReadWriteMutexReadScope objPoolLock(gBackendVk.objectPoolsMutex);
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
        const GfxBindingDesc& binding = bindings[i];
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
            const GfxBindingDesc& binding = bindings[idx];
            const VkDescriptorSetLayoutBinding& bindingVk = layout.bindingsVk[layoutBindingIdx];

            VkDescriptorImageInfo* pImageInfo = nullptr;
            VkImageLayout imgLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            VkDescriptorBufferInfo* pBufferInfo = nullptr;

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
                if (!imgLayout)
                    imgLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                pImageInfo = imageInfos.Push();
                *pImageInfo = {
                    .sampler = gBackendVk.samplerDefault,
                    .imageView = binding.image.IsValid() ? gBackendVk.images.Data(binding.image).viewHandle : VK_NULL_HANDLE,
                    .imageLayout = imgLayout
                };
                break;
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
                .pBufferInfo = pBufferInfo,
                .pTexelBufferView = nullptr
            };
        } // foreach binding

        vkCmdPushDescriptorSetKHR(cmdVk, bindPoint, layoutVk, setIdx, numSetBindings, descriptorWrites);
    } // foreach descriptor set
}

void GfxCommandBuffer::BindPipeline(GfxPipelineHandle pipeHandle)
{
    ASSERT(mIsRecording);
    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);

    ReadWriteMutexReadScope objPoolLock(gBackendVk.objectPoolsMutex);
    GfxBackendPipeline& pipe = gBackendVk.pipelines.Data(pipeHandle);

    VkPipelineBindPoint bindPoint = pipe.type == GfxBackendPipeline::PipelineTypeCompute ? 
        VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    vkCmdBindPipeline(cmdVk, bindPoint, pipe.handle);
}

void GfxCommandBuffer::Dispatch(uint32 groupCountX, uint32 groupCountY, uint32 groupCountZ)
{
    ASSERT(mIsRecording);
    mShouldSubmit = true;

    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    vkCmdDispatch(cmdVk, groupCountX, groupCountY, groupCountZ);
}

GfxBufferHandle GfxBackend::CreateBuffer(const GfxBufferDesc& desc)
{
    GfxBufferHandle handle;
    GfxBackend::BatchCreateBuffer(1, &desc, &handle);
    return handle;
}

void GfxBackend::BatchCreateBuffer(uint32 numBuffers, const GfxBufferDesc* descs, GfxBufferHandle* outHandles)
{
    ASSERT(numBuffers);
    ASSERT(descs);
    ASSERT(outHandles);

    MemTempAllocator tempAlloc;
    GfxBackendBuffer* buffers = tempAlloc.MallocTyped<GfxBackendBuffer>(numBuffers); 
    uint32 numTransientIncrements = 0;
    for (uint32 i = 0; i < numBuffers; i++) {
        const GfxBufferDesc& desc = descs[i];
        ASSERT(desc.sizeBytes);

        VkBufferCreateInfo bufferCreateInfo {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = desc.sizeBytes,
            .usage = VkBufferUsageFlags(desc.usageFlags)
        };

        VkBuffer bufferVk;
        [[maybe_unused]] VkResult r = vkCreateBuffer(gBackendVk.device, &bufferCreateInfo, gBackendVk.vkAlloc, &bufferVk);
        ASSERT_ALWAYS(r == VK_SUCCESS, "vkCreateBuffer failed");

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(gBackendVk.device, bufferVk, &memReq);
        GfxBackendDeviceMemory mem = gBackendVk.memMan.Malloc(memReq, desc.arena);
        vkBindBufferMemory(gBackendVk.device, bufferVk, mem.handle, mem.offset);

        if (desc.arena == GfxMemoryArena::TransientCPU)
            ++numTransientIncrements;

        buffers[i] = {
            .handle = bufferVk,
            .desc = desc,
            .mem = mem
        };
    }

    if (numTransientIncrements) {
        gBackendVk.frameSyncSignal.Increment(numTransientIncrements);
        Atomic::FetchAdd(&gBackendVk.numTransientResroucesInUse, numTransientIncrements);
    }

    ReadWriteMutexWriteScope objPoolLock(gBackendVk.objectPoolsMutex);
    for (uint32 i = 0; i < numBuffers; i++) 
        outHandles[i] = gBackendVk.buffers.Add(buffers[i]);
}

void GfxBackend::DestroyBuffer(GfxBufferHandle& handle)
{
    if (handle.IsValid())
        GfxBackend::BatchDestroyBuffer(1, &handle);
}

void GfxBackend::BatchDestroyBuffer(uint32 numBuffers, GfxBufferHandle* handles)
{
    ASSERT(numBuffers);
    ASSERT(handles);

    MemTempAllocator tempAlloc;
    Array<GfxBackendGarbage> garbages(&tempAlloc);
    uint32 numTransientDecrements = 0;

    {
        ReadWriteMutexWriteScope objPoolLock(gBackendVk.objectPoolsMutex);
        for (uint32 i = 0; i < numBuffers; i++) {
            GfxBufferHandle handle = handles[i];
            if (!handle.IsValid())
                continue;

            GfxBackendBuffer& buffer = gBackendVk.buffers.Data(handle);

            garbages.Push({
                .type = GfxBackendGarbage::Type::Buffer,
                .frameIdx = gBackendVk.presentFrame,
                .mem = buffer.mem,
                .buffer = buffer.handle
            });

            gBackendVk.buffers.Remove(handle);

            if (buffer.mem.arena == GfxMemoryArena::TransientCPU)
                numTransientDecrements++;

            handles[i] = {};
        }
    }

    if (numTransientDecrements) {
        Atomic::FetchSub(&gBackendVk.numTransientResroucesInUse, numTransientDecrements);

        gBackendVk.frameSyncSignal.Decrement(numTransientDecrements);
        gBackendVk.frameSyncSignal.Raise();
    }

    MutexScope lock(gBackendVk.garbageMtx);
    gBackendVk.garbage.Extend(garbages);
}

GfxBackendDeviceMemory GfxBackendDeviceMemoryManager::Malloc(const VkMemoryRequirements& memReq, GfxMemoryArena arena)
{
    GfxBackendDeviceMemory mem {
        .arena = arena
    };

    switch (arena) {
        case GfxMemoryArena::PersistentGPU:
            mem = mPersistentGPU.Malloc(memReq);
            break;

        case GfxMemoryArena::PersistentCPU:
            mem = mPersistentCPU.Malloc(memReq);
            break;

        case GfxMemoryArena::TransientCPU:
            mem = mTransientCPU[mStagingIndex].Malloc(memReq);
            break;

        case GfxMemoryArena::DynamicImageGPU:
            mem = mDynamicImageGPU.Malloc(memReq);
            break;

        case GfxMemoryArena::DynamicBufferGPU:
            mem = mDynamicBufferGPU.Malloc(memReq);
            break;

        default:
            ASSERT_MSG(0, "Not implemented");
    }

    mem.arena = arena;
    return mem;
}

void GfxBackendDeviceMemoryManager::Free(GfxBackendDeviceMemory mem)
{
    switch (mem.arena) {
    case GfxMemoryArena::DynamicImageGPU:
        mDynamicImageGPU.Free(mem);
        break;
    case GfxMemoryArena::DynamicBufferGPU:
        mDynamicBufferGPU.Free(mem);
        break;
    default:
        break;
    }
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

    {
        mDynamicImageGPU.Initialize(128*SIZE_MB, FindDeviceMemoryType(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true));
        mDynamicBufferGPU.Initialize(128*SIZE_MB, FindDeviceMemoryType(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true));
    }

    return true;
}

void GfxBackendDeviceMemoryManager::Release()
{
    mPersistentGPU.Release();
    mPersistentCPU.Release();
    mDynamicImageGPU.Release();
    mDynamicBufferGPU.Release();
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
            fam.type |= GfxQueueType::Graphics;
        if (props.queueFlags & VK_QUEUE_COMPUTE_BIT)
            fam.type |= GfxQueueType::Compute;
        if (props.queueFlags & VK_QUEUE_TRANSFER_BIT)
            fam.type |= GfxQueueType::Transfer;

        fam.count = props.queueCount;

        if (gBackendVk.surface) {
            VkBool32 supportsPresentation = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(gpu.handle, i, gBackendVk.surface, &supportsPresentation);
            if (supportsPresentation)
                fam.type |= GfxQueueType::Present;
        }
    }

    LOG_VERBOSE("(init) Found total %u queue families", mNumQueueFamilies);

    if (gpu.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        SetupQueuesForDiscreteDevice();
    else 
        SetupQueuesForIntegratedDevice();

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

    // Fetch queues from the device and initialize other data structures 
    for (uint32 i = 0; i < mNumQueues; i++) {
        GfxBackendQueue& queue = mQueues[i];

        queue.cmdBufferCtxMutex.Initialize();

        ASSERT(queue.handle == nullptr);
        vkGetDeviceQueue(gBackendVk.device, queue.familyIdx, 0, &queue.handle);
        ASSERT_ALWAYS(queue.handle, "Something went wrong! Cannot fetch device queue. Invalid queue family");

        queue.waitSemaphores.SetAllocator(&gBackendVk.runtimeAlloc);
        queue.signalSemaphores.SetAllocator(&gBackendVk.runtimeAlloc);
        queue.pendingBarriers.SetAllocator(&gBackendVk.runtimeAlloc);
        queue.dependentBarriers.SetAllocator(&gBackendVk.runtimeAlloc);

        for (uint32 k = 0; k < GFXBACKEND_FRAMES_IN_FLIGHT; k++) {
            if (!InitializeCommandBufferContext(queue.cmdBufferContexts[k], queue.familyIdx)) {
                LOG_WARNING("Gfx: CommandBuffer manager init failed for queue %u", i);
                ASSERT(0);
            }

            queue.semaphoreBanks[k].Initialize();
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

        for (uint32 k = 0; k < GFXBACKEND_FRAMES_IN_FLIGHT; k++) {
            ReleaseCommandBufferContext(queue.cmdBufferContexts[k]);
            queue.semaphoreBanks[k].Release();            
        }

        queue.waitSemaphores.Free();
        queue.signalSemaphores.Free();
        queue.pendingBarriers.Free();
        queue.dependentBarriers.Free();
        queue.cmdBufferCtxMutex.Release();
    }

    Mem::Free(mQueueFamilies, &gBackendVk.parentAlloc);
    Mem::Free(mQueues, &gBackendVk.parentAlloc);
}

void GfxBackendQueueManager::SetupQueuesForDiscreteDevice()
{
    MemAllocator* alloc = &gBackendVk.parentAlloc;

    // Discrete GPUs:
    //  Graphics + Present + Compute. We also have an implicit Transfer to do frequent buffer updates and whatnot
    //  Transfer: Preferebly exclusive 
    //  ComputeAsync: Preferebly exclusive
    mQueues = Mem::AllocZeroTyped<GfxBackendQueue>(GFXBACKEND_MAX_QUEUES, alloc);
    StaticArray<uint32, GFXBACKEND_MAX_QUEUES> queueFamilyIndices;

    if (SettingsJunkyard::Get().graphics.IsGraphicsEnabled()) {
        uint32 familyIdx = AssignQueueFamily(GfxQueueType::Graphics|GfxQueueType::Present|GfxQueueType::Transfer|GfxQueueType::Compute);
        mQueues[mNumQueues++] = {
            .type = GfxQueueType::Graphics|GfxQueueType::Present|GfxQueueType::Compute,
            .familyIdx = familyIdx,
            .priority = 1.0f,
            .supportsTransfer = true
        }; 

        if (familyIdx != -1) {
            LOG_VERBOSE("\tGraphics/Compute queue from index: %u", familyIdx);
            queueFamilyIndices.Push(familyIdx);
        }
        else {
            LOG_ERROR("Gfx: Graphics queue not found");
            ASSERT_MSG(0, "Cannot continue without a valid Graphics|Trasnfer|Compute queue");
        }
    }

    {
        uint32 familyIdx = AssignQueueFamily(GfxQueueType::Transfer, GfxQueueType::Graphics|GfxQueueType::Compute, 
                                             queueFamilyIndices.Count(), queueFamilyIndices.Ptr());
        if (familyIdx == -1) {
            familyIdx = AssignQueueFamily(GfxQueueType::Transfer, GfxQueueType::Graphics, 
                                          queueFamilyIndices.Count(), queueFamilyIndices.Ptr());

            if (familyIdx == -1) {
                familyIdx = AssignQueueFamily(GfxQueueType::Transfer, GfxQueueType::None, 
                                              queueFamilyIndices.Count(), queueFamilyIndices.Ptr());
            }
        }

        if (familyIdx != -1) {
            mQueues[mNumQueues++] = {
                .type = GfxQueueType::Transfer,
                .familyIdx = familyIdx,
                .priority = 1.0f,
                .supportsTransfer = true
            };
            LOG_VERBOSE("\tTransfer queue from index: %u", familyIdx);
            queueFamilyIndices.Push(familyIdx);
        }
        else {
            // Assign the first one to TRANSFER as well
            if (mNumQueues)
                mQueues[0].type |= GfxQueueType::Transfer;
            LOG_WARNING("Gfx: Performance warning: Separate transfer queue not found. Using unified queue family (%u) for transfers", mQueues[0].familyIdx);
        }
    }

    {
        uint32 familyIdx = AssignQueueFamily(GfxQueueType::Compute|GfxQueueType::Transfer, GfxQueueType::Graphics,
                                             queueFamilyIndices.Count(), queueFamilyIndices.Ptr());
        if (familyIdx == -1) {
            familyIdx = AssignQueueFamily(GfxQueueType::Compute|GfxQueueType::Transfer, GfxQueueType::Graphics,
                                          queueFamilyIndices.Count(), queueFamilyIndices.Ptr());
        }

        if (familyIdx != -1) {
            GfxQueueType extraCompute = SettingsJunkyard::Get().graphics.IsGraphicsEnabled() ? GfxQueueType::None : GfxQueueType::Compute;

            mQueues[mNumQueues++] = {
                .type = GfxQueueType::ComputeAsync | extraCompute,
                .familyIdx = familyIdx,
                .priority = 1.0f
            };

            LOG_VERBOSE("\tComputeAsync queue from index: %u", familyIdx);
        }
        else if (mNumQueues && IsBitsSet<GfxQueueType>(mQueues[0].type, GfxQueueType::Compute)) {
            mQueues[0].type |= GfxQueueType::ComputeAsync;
            LOG_WARNING("Gfx: Performance warning: Separate compute queue not found. Using unified queue family (%d) for async compute", mQueues[0].familyIdx);
        }
        else {
            familyIdx = AssignQueueFamily(GfxQueueType::Compute|GfxQueueType::Transfer);
            if (familyIdx != -1) {
                mQueues[mNumQueues++] = {
                    .type = GfxQueueType::ComputeAsync | GfxQueueType::Compute,
                    .familyIdx = familyIdx,
                    .priority = 1.0f
                };

                LOG_WARNING("Gfx: Performance warning: Separate compute queue not found. Using unified queue family (%d) for async compute", familyIdx);
            }
            else {
                LOG_ERROR("Gfx: Cannot find Compute|Transfer queue on this GPU");
                ASSERT(0);
            }
        }
    }
}

void GfxBackendQueueManager::SetupQueuesForIntegratedDevice()
{
    MemAllocator* alloc = &gBackendVk.parentAlloc;

    // Discrete GPUs:
    //  Graphics + Present + Compute. We also have an implicit Transfer to do frequent buffer updates and whatnot
    //  ComputeAsync: Preferebly exclusive
    mQueues = Mem::AllocZeroTyped<GfxBackendQueue>(GFXBACKEND_MAX_QUEUES, alloc);
    StaticArray<uint32, GFXBACKEND_MAX_QUEUES> queueFamilyIndices;

    if (SettingsJunkyard::Get().graphics.IsGraphicsEnabled()) {
        uint32 familyIdx = AssignQueueFamily(GfxQueueType::Graphics|GfxQueueType::Present|GfxQueueType::Transfer|GfxQueueType::Compute);
        mQueues[mNumQueues++] = {
            .type = GfxQueueType::Graphics|GfxQueueType::Present|GfxQueueType::Compute,
            .familyIdx = familyIdx,
            .priority = 1.0f,
            .supportsTransfer = true
        }; 

        if (familyIdx != -1) {
            LOG_VERBOSE("\tGraphics/Compute/Transfer queue from index: %u", familyIdx);
            queueFamilyIndices.Push(familyIdx);
        }
        else {
            LOG_ERROR("Gfx: Graphics queue not found");
            ASSERT_MSG(0, "Cannot continue without a valid Graphics|Trasnfer|Compute queue");
        }
    }

    {
        uint32 familyIdx = AssignQueueFamily(GfxQueueType::Transfer, GfxQueueType::Graphics|GfxQueueType::Compute);
        if (familyIdx == -1) 
            familyIdx = AssignQueueFamily(GfxQueueType::Transfer);

        if (familyIdx != -1) {
            mQueues[mNumQueues++] = {
                .type = GfxQueueType::Transfer,
                .familyIdx = familyIdx,
                .priority = 1.0f,
                .supportsTransfer = true
            };
            LOG_VERBOSE("\tTransfer queue from index: %u", familyIdx);
            queueFamilyIndices.Push(familyIdx);
        }
        else {
            LOG_ERROR("Gfx: Transfer queue not found");
            ASSERT(0);
        }
    }

    {
        uint32 familyIdx = AssignQueueFamily(GfxQueueType::Compute|GfxQueueType::Transfer, GfxQueueType::Graphics,
                                             queueFamilyIndices.Count(), queueFamilyIndices.Ptr());
        if (familyIdx == -1) {
            familyIdx = AssignQueueFamily(GfxQueueType::Compute|GfxQueueType::Transfer, GfxQueueType::Graphics,
                                          queueFamilyIndices.Count(), queueFamilyIndices.Ptr());
        }

        if (familyIdx != -1) {
            GfxQueueType extraCompute = SettingsJunkyard::Get().graphics.IsGraphicsEnabled() ? GfxQueueType::None : GfxQueueType::Compute;

            mQueues[mNumQueues++] = {
                .type = GfxQueueType::ComputeAsync | extraCompute,
                .familyIdx = familyIdx,
                .priority = 1.0f
            };

            LOG_VERBOSE("\tComputeAsync queue from index: %u", familyIdx);
        }
        else if (mNumQueues && IsBitsSet<GfxQueueType>(mQueues[0].type, GfxQueueType::Compute)) {
            mQueues[0].type |= GfxQueueType::ComputeAsync;
            LOG_WARNING("Gfx: Performance warning: Separate compute queue not found. Using unified queue family (%d) for async compute", mQueues[0].familyIdx);
        }
        else {
            familyIdx = AssignQueueFamily(GfxQueueType::Compute|GfxQueueType::Transfer);
            if (familyIdx != -1) {
                mQueues[mNumQueues++] = {
                    .type = GfxQueueType::ComputeAsync | GfxQueueType::Compute,
                    .familyIdx = familyIdx,
                    .priority = 1.0f
                };

                LOG_WARNING("Gfx: Performance warning: Separate compute queue not found. Using unified queue family (%d) for async compute", familyIdx);
            }
            else {
                LOG_ERROR("Gfx: Cannot find Compute|Transfer queue on this GPU");
                ASSERT(0);
            }
        }
    }
}

uint32 GfxBackendQueueManager::AssignQueueFamily(GfxQueueType type, GfxQueueType preferNotHave, 
                                                 uint32 numExcludes, const uint32* excludes)
{
    ASSERT(mNumQueueFamilies);

    uint32 familyIndex = uint32(-1);

    for (uint32 i = 0; i < mNumQueueFamilies; i++) {
        if (IsBitsSet<GfxQueueType>(mQueueFamilies[i].type, type) && mQueueFamilies[i].count) {
            bool isExcluded = false;
            for (uint32 k = 0; k < numExcludes; k++) {
                if (excludes[k] == i) {
                    isExcluded = true;
                    break;
                }
            }

            if (isExcluded)
                continue;

            if (preferNotHave != GfxQueueType::None) {
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

    return familyIndex;
}

inline uint32 GfxBackendQueueManager::FindQueue(GfxQueueType type) const
{
    for (uint32 i = 0; i < mNumQueues; i++) {
        if (IsBitsSet(mQueues[i].type, type))
            return i;
    }
        
    return uint32(-1);
}

void GfxBackend::SubmitQueue(GfxQueueType queueType, GfxQueueType dependentQueues)
{
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
            if (req->type != GfxQueueType::None)
                self->SubmitQueueInternal(*req);

            MemSingleShotMalloc<GfxBackendQueueSubmitRequest>::Free(req, &gBackendVk.runtimeAlloc);
        }
    }

    return 0;
}

void GfxBackendQueueManager::SubmitQueue(GfxQueueType queueType, GfxQueueType dependentQueues)
{
    uint32 queueIndex = FindQueue(queueType);
    ASSERT(queueIndex != -1);
    GfxBackendQueue& queue = GetQueue(queueIndex);

    ASSERT_MSG(Atomic::Load(&queue.numCmdBuffersInRecording) == 0, "Cannot submit: CommandBuffers are still in recording");

    // Take all the command-buffers since last Submit call and pass it to the submission thread
    GfxBackendCommandBufferContext& cmdBufferCtx = queue.cmdBufferContexts[mFrameIndex];

    queue.cmdBufferCtxMutex.EnterWrite();
    uint32 numCmdBuffers = cmdBufferCtx.cmdBuffers.Count() - cmdBufferCtx.cmdBufferIndex;
    if (!numCmdBuffers) {
        queue.cmdBufferCtxMutex.ExitWrite();
        return;
    }

    MemSingleShotMalloc<GfxBackendQueueSubmitRequest> mallocator;
    mallocator.AddMemberArray<VkCommandBuffer>(offsetof(GfxBackendQueueSubmitRequest, cmdBuffers), numCmdBuffers);
    GfxBackendQueueSubmitRequest* req = mallocator.Calloc(&gBackendVk.runtimeAlloc);
    req->type = queueType;
    req->numCmdBuffers = numCmdBuffers;
    for (uint32 i = cmdBufferCtx.cmdBufferIndex; i < cmdBufferCtx.cmdBuffers.Count(); i++) {
        req->cmdBuffers[i - cmdBufferCtx.cmdBufferIndex] = cmdBufferCtx.cmdBuffers[i];
    }

    cmdBufferCtx.cmdBufferIndex = cmdBufferCtx.cmdBuffers.Count();

    // Also add injected dependent queues
    req->dependents = dependentQueues | queue.internalDependents;
    queue.internalDependents = GfxQueueType::None;

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
    queue.cmdBufferCtxMutex.ExitWrite();

    req->semaphore = queue.semaphoreBanks[mFrameIndex].GetSemaphore();

    {
        SpinLockMutexScope lock(mRequestMutex);
        mSubmitRequests.Push(req);
    }

    mRequestsSemaphore.Post();
    Atomic::StoreExplicit(&queue.numPendingCmdBuffers, 0, AtomicMemoryOrder::Release);
}

bool GfxBackendQueueManager::SubmitQueueInternal(GfxBackendQueueSubmitRequest& req)
{
    uint32 queueIndex = FindQueue(req.type);
    ASSERT(queueIndex != -1);
    GfxBackendQueue& queue = mQueues[queueIndex];

    // Connect dependencies
    // Each queue has it's own signal semaphore
    // When we have dependents, then add the current queue's signal semaphore to the dependent's wait semaphore
    // This forms a dependency chain

    // TODO: We can have tune this to be more specific
    auto GetStageFlag = [](GfxQueueType type)->VkPipelineStageFlags
    {
        switch (type) {
        case GfxQueueType::Graphics: return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        case GfxQueueType::Compute:  return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        case GfxQueueType::Transfer: return VK_PIPELINE_STAGE_TRANSFER_BIT;
        default: return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        }
    };

    // Check for swapchain draw within command-buffers
    if (IsBitsSet<GfxQueueType>(req.dependents, GfxQueueType::Present)) 
    {
        ASSERT(req.type == GfxQueueType::Graphics);
        // Notify the queue that the next Submit is gonna depend on swapchain
        queue.waitSemaphores.Push({gBackendVk.swapchain.GetSwapchainSemaphore(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT});
        queue.signalSemaphores.Push(gBackendVk.swapchain.GetPresentSemaphore());
    }

    if (IsBitsSet<GfxQueueType>(req.dependents, GfxQueueType::Graphics)) {
        ASSERT(req.type != GfxQueueType::Graphics);
        GfxBackendQueue& graphicsQueue = mQueues[FindQueue(GfxQueueType::Graphics)];
        graphicsQueue.waitSemaphores.Push({req.semaphore, GetStageFlag(req.type)});
        queue.signalSemaphores.Push(req.semaphore);
    }

    if (IsBitsSet<GfxQueueType>(req.dependents, GfxQueueType::Compute)) {
        ASSERT(req.type != GfxQueueType::Compute);
        GfxBackendQueue& computeQueue = mQueues[FindQueue(GfxQueueType::Compute)];
        computeQueue.waitSemaphores.Push({req.semaphore, GetStageFlag(req.type)});
        queue.signalSemaphores.Push(req.semaphore);
    }

    if (IsBitsSet<GfxQueueType>(req.dependents, GfxQueueType::Transfer)) {
        ASSERT(req.type != GfxQueueType::Transfer);
        GfxBackendQueue& transferQueue = mQueues[FindQueue(GfxQueueType::Transfer)];
        transferQueue.waitSemaphores.Push({req.semaphore, GetStageFlag(req.type)});
        queue.signalSemaphores.Push(req.semaphore);
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

    queue.barriersMutex.Enter();
    if (!queue.dependentBarriers.IsEmpty()) {
        uint32 targetQueueIndex = queue.dependentBarriers[0].targetQueueIndex;
        ASSERT(targetQueueIndex != queueIndex);
        for (uint32 i = 1; i < queue.dependentBarriers.Count(); i++) {
            // This just makes sure we are sending all barriers to a single target queue
            // If this is not always the case, then we should implement something else here
            ASSERT(targetQueueIndex == queue.dependentBarriers[i].targetQueueIndex);
        }
        
        {
            GfxBackendQueue& targetQueue = gBackendVk.queueMan.GetQueue(targetQueueIndex);
            SpinLockMutexScope lock(targetQueue.barriersMutex);
            targetQueue.pendingBarriers.Extend(queue.dependentBarriers);
        }
        
        queue.dependentBarriers.Clear();
        queue.barriersMutex.Exit();
    }
    else {
        queue.barriersMutex.Exit();
    }

    queue.waitSemaphores.Clear();
    queue.signalSemaphores.Clear();

    gBackendVk.frameSyncSignal.Decrement(req.numCmdBuffers);
    gBackendVk.frameSyncSignal.Raise();

    return true;
}

void GfxBackendQueueManager::BeginFrame()
{
    PROFILE_ZONE_COLOR(PROFILE_COLOR_GFX2);
    ++mGeneration;
    mFrameIndex = mGeneration % GFXBACKEND_FRAMES_IN_FLIGHT;

    for (uint32 i = 0; i < mNumQueues; i++) {
        GfxBackendQueue& queue = mQueues[i];
        GfxBackendCommandBufferContext& cmdBufferCtx = queue.cmdBufferContexts[mFrameIndex];
    
        // NOTE: We are making sure that no other thread is submitting when BeginFrame() is called (See frameSyncSignal/externalFrameSyncSignal)
        //       So we this mutex should have no contention and is just here for safety
        queue.cmdBufferCtxMutex.EnterWrite();

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

        queue.cmdBufferCtxMutex.ExitWrite();

        queue.semaphoreBanks[mFrameIndex].Reset();
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

void GfxCommandBuffer::MapBuffer(GfxBufferHandle buffHandle, void** outPtr, size_t* outSizeBytes)
{
    ASSERT(outPtr);

    GfxMapResult r;
    BatchMapBuffer(1, &buffHandle, &r);
    *outPtr = r.dataPtr;
    if (outSizeBytes)
        *outSizeBytes = r.dataSize;
}

void GfxCommandBuffer::BatchMapBuffer(uint32 numParams, const GfxBufferHandle* handles, GfxMapResult* mapResults)
{
    ASSERT(mIsRecording);
    ASSERT(handles);
    ASSERT(mapResults);
    ASSERT(numParams);

    ReadWriteMutexReadScope objPoolLock(gBackendVk.objectPoolsMutex);

    for (uint32 i = 0; i < numParams; i++) {
        GfxBackendBuffer& buffer = gBackendVk.buffers.Data(handles[i]);
        ASSERT_MSG(buffer.mem.mappedData, "Buffer is not mappable");

        mapResults[i].dataPtr = buffer.mem.mappedData;
        mapResults[i].dataSize = buffer.desc.sizeBytes;
    }
}

void GfxCommandBuffer::FlushBuffer(GfxBufferHandle buffHandle)
{
    BatchFlushBuffer(1, &buffHandle);
}

void GfxCommandBuffer::BatchFlushBuffer(uint32 numBuffers, const GfxBufferHandle* bufferHandles)
{
    ASSERT(mIsRecording);

    MemTempAllocator tempAlloc;
    Array<VkMappedMemoryRange> memRanges(&tempAlloc);

    gBackendVk.objectPoolsMutex.EnterRead();

    for (uint32 i = 0; i < numBuffers; i++) {
        GfxBackendBuffer& buffer = gBackendVk.buffers.Data(bufferHandles[i]);
        if (!buffer.mem.isCoherent) {
            size_t alignedSize = AlignValue(buffer.desc.sizeBytes, gBackendVk.gpu.props.limits.nonCoherentAtomSize);
            VkMappedMemoryRange memRange {
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = buffer.mem.handle,
                .offset = buffer.mem.offset,
                .size = alignedSize
            };
            memRanges.Push(memRange);
        }
    }

    gBackendVk.objectPoolsMutex.ExitRead();

    if (!memRanges.IsEmpty())
        vkFlushMappedMemoryRanges(gBackendVk.device, memRanges.Count(), memRanges.Ptr());
}

void GfxCommandBuffer::CopyBufferToBuffer(GfxBufferHandle srcHandle, GfxBufferHandle dstHandle, GfxShaderStage stagesUsed, 
                                                 size_t srcOffset, size_t dstOffset, size_t sizeBytes)
{
    GfxCopyBufferToBufferParams param {
        .srcHandle = srcHandle,
        .dstHandle = dstHandle,
        .stagesUsed = stagesUsed,
        .srcOffset = srcOffset,
        .dstOffset = dstOffset,
        .sizeBytes = sizeBytes
    };

    BatchCopyBufferToBuffer(1, &param);
}

void GfxCommandBuffer::BatchCopyBufferToBuffer(uint32 numParams, const GfxCopyBufferToBufferParams* params)
{
    ASSERT(numParams);
    ASSERT(params);
    ASSERT(mIsRecording);
    mShouldSubmit = true;

    struct CopyBufferToBufferData
    {
        VkBuffer srcBuffer;
        VkBuffer dstBuffer;
        VkBufferCopy region;
    };

    GfxBackendQueue& queue = gBackendVk.queueMan.GetQueue(mQueueIndex);
    ASSERT_MSG(IsBitsSet<GfxQueueType>(queue.type, GfxQueueType::Transfer) || queue.supportsTransfer,
               "Cannot do buffer copies on non-Transfer queues");

    MemTempAllocator tempAlloc;
    Array<VkBufferMemoryBarrier2> bufferBarriers(&tempAlloc);
    Array<GfxBackendQueue::PendingBarrier> pendingBarriers(&tempAlloc);
    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);

    gBackendVk.objectPoolsMutex.EnterRead();
    for (uint32 i = 0; i < numParams; i++) {
        const GfxCopyBufferToBufferParams& copyParams = params[i];

        GfxBackendBuffer& srcBuffer = gBackendVk.buffers.Data(copyParams.srcHandle);
        GfxBackendBuffer& dstBuffer = gBackendVk.buffers.Data(copyParams.dstHandle);

        size_t sizeBytes = copyParams.sizeBytes;
        if (sizeBytes == 0)
            sizeBytes = Min(srcBuffer.desc.sizeBytes, dstBuffer.desc.sizeBytes);
        ASSERT(sizeBytes <= srcBuffer.desc.sizeBytes);
        ASSERT(sizeBytes <= dstBuffer.desc.sizeBytes);

        VkBufferCopy copyRegion {
            .srcOffset = copyParams.srcOffset,
            .dstOffset = copyParams.dstOffset,
            .size = sizeBytes
        };
        vkCmdCopyBuffer(cmdVk, srcBuffer.handle, dstBuffer.handle, 1, &copyRegion);

        VkAccessFlags2 accessFlags;
        if (IsBitsSet<GfxBufferUsageFlags>(dstBuffer.desc.usageFlags, GfxBufferUsageFlags::Index))
            accessFlags = VK_ACCESS_2_INDEX_READ_BIT;
        else if (IsBitsSet<GfxBufferUsageFlags>(dstBuffer.desc.usageFlags, GfxBufferUsageFlags::Vertex))
            accessFlags = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        else if (IsBitsSet<GfxBufferUsageFlags>(dstBuffer.desc.usageFlags, GfxBufferUsageFlags::Uniform))
            accessFlags = VK_ACCESS_2_UNIFORM_READ_BIT;
        else 
            accessFlags = VK_ACCESS_2_MEMORY_READ_BIT;

        GfxQueueType dstQueueType = GfxQueueType::None;
        if (IsBitsSet<GfxShaderStage>(copyParams.stagesUsed, GfxShaderStage::Vertex) ||
            IsBitsSet<GfxShaderStage>(copyParams.stagesUsed, GfxShaderStage::Fragment))
        {
            dstQueueType = GfxQueueType::Graphics;
        }
        else if (IsBitsSet<GfxShaderStage>(copyParams.stagesUsed, GfxShaderStage::Compute))
        {
            dstQueueType = GfxQueueType::Compute;
        }
        ASSERT(dstQueueType != GfxQueueType::None);

        uint32 queueFamilyIdx = queue.familyIdx;
        uint32 dstQueueFamilyIdx = gBackendVk.queueMan.GetQueue(gBackendVk.queueMan.FindQueue(dstQueueType)).familyIdx;
        ASSERT(dstQueueFamilyIdx != -1);
        if (queueFamilyIdx == dstQueueFamilyIdx) {
            // Unified queue
            VkBufferMemoryBarrier2 barrier {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                .dstStageMask = GfxBackend::_GetBufferDestStageFlags(dstQueueType, copyParams.stagesUsed, dstBuffer.desc.usageFlags),
                .dstAccessMask = accessFlags,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = dstBuffer.handle,
                .offset = copyParams.dstOffset,
                .size = sizeBytes
            };

            dstBuffer.transitionedStage = barrier.dstStageMask;
            dstBuffer.transitionedAccess = barrier.dstAccessMask;

            bufferBarriers.Push(barrier);

            if (copyParams.resourceTransferedCallback)
                copyParams.resourceTransferedCallback(copyParams.resourceTransferedUserData);
        }
        else {
            // Separate queue
            // We have to do queue ownership transfer first
            VkBufferMemoryBarrier2 barrier {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                .srcQueueFamilyIndex = queueFamilyIdx,
                .dstQueueFamilyIndex = dstQueueFamilyIdx,
                .buffer = dstBuffer.handle,
                .offset = copyParams.dstOffset,
                .size = sizeBytes
            };
            bufferBarriers.Push(barrier);

            // TODO: Assert that dstQueue is not being recorded
            GfxBackendQueue::PendingBarrier dstBarrier {
                .type = GfxBackendQueue::PendingBarrier::BUFFER,
                .targetQueueIndex = dstQueueFamilyIdx,
                .resourceTransferedCallback = copyParams.resourceTransferedCallback,
                .resourceTransferedUserData = copyParams.resourceTransferedUserData,
                .bufferHandle = copyParams.dstHandle,
                .bufferBarrier = {
                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
                    .dstStageMask = GfxBackend::_GetBufferDestStageFlags(dstQueueType, copyParams.stagesUsed, dstBuffer.desc.usageFlags),
                    .dstAccessMask = accessFlags,
                    .srcQueueFamilyIndex = queueFamilyIdx,
                    .dstQueueFamilyIndex = dstQueueFamilyIdx,
                    .offset = copyParams.dstOffset,
                    .size = sizeBytes
                }
            };

            queue.internalDependents |= dstQueueType;
            pendingBarriers.Push(dstBarrier);
        }
    }
    gBackendVk.objectPoolsMutex.ExitRead();

    // Submit actual pipeline barriers
    ASSERT(!bufferBarriers.IsEmpty());
    VkDependencyInfo depInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = bufferBarriers.Count(),
        .pBufferMemoryBarriers = bufferBarriers.Ptr()
    };
    vkCmdPipelineBarrier2(cmdVk, &depInfo);

    // Send all pending barriers to the queue. After the next Submit, all of those barriers will be passed on to target queue
    SpinLockMutexScope lock(queue.barriersMutex);
    queue.dependentBarriers.Extend(pendingBarriers);
}

void GfxCommandBuffer::CopyBufferToImage(GfxBufferHandle srcHandle, GfxImageHandle dstHandle, GfxShaderStage stagesUsed, 
                                                uint16 startMipIndex, uint16 mipCount)
{
    GfxCopyBufferToImageParams params {
        .srcHandle = srcHandle,
        .dstHandle = dstHandle,
        .stagesUsed = stagesUsed,
        .startMipIndex = startMipIndex,
        .mipCount = mipCount
    };

    BatchCopyBufferToImage(1, &params);
}

void GfxCommandBuffer::BatchCopyBufferToImage(uint32 numParams, const GfxCopyBufferToImageParams* params)
{
    ASSERT(numParams);
    ASSERT(params);
    ASSERT(mIsRecording);

    mShouldSubmit = true;

    struct CopyBufferToImageData
    {
        VkBufferImageCopy imageCopies[GFXBACKEND_MAX_MIPS_PER_IMAGE];
        uint32 numMips;
        VkBuffer bufferHandle;
        VkImage imageHandle;
    };

    MemTempAllocator tempAlloc;
    Array<VkImageMemoryBarrier2> preBarriers(&tempAlloc);
    Array<VkImageMemoryBarrier2> barriers(&tempAlloc);
    Array<CopyBufferToImageData> copies(&tempAlloc);
    Array<GfxBackendQueue::PendingBarrier> pendingBarriers(&tempAlloc);

    GfxBackendQueue& queue = gBackendVk.queueMan.GetQueue(mQueueIndex);
    ASSERT_MSG(IsBitsSet<GfxQueueType>(queue.type, GfxQueueType::Transfer) || queue.supportsTransfer,
               "Cannot do buffer copies on non-Transfer queues");

    gBackendVk.objectPoolsMutex.EnterRead();
    for (uint32 i = 0; i < numParams; i++) {
        const GfxCopyBufferToImageParams& copyParams = params[i];

        ASSERT(copyParams.mipCount);
        GfxBackendBuffer& srcBuffer = gBackendVk.buffers.Data(copyParams.srcHandle);
        GfxBackendImage& dstImage = gBackendVk.images.Data(copyParams.dstHandle);

        ASSERT(copyParams.startMipIndex < dstImage.desc.numMips);
        uint16 mipCount = Min<uint16>(copyParams.mipCount, dstImage.desc.numMips - copyParams.startMipIndex);

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
            dstLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        VkImageSubresourceRange subresourceRange {
            .aspectMask = aspect,
            .baseMipLevel = copyParams.startMipIndex,
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

        preBarriers.Push(preCopyBarrier);

        // Perform copy
        CopyBufferToImageData* copy = copies.Push();
        copy->numMips = mipCount;
        copy->bufferHandle = srcBuffer.handle;
        copy->imageHandle = dstImage.handle;
        uint16 endMipIndex = copyParams.startMipIndex + mipCount;
        for (uint16 mipIdx = copyParams.startMipIndex; mipIdx < endMipIndex; mipIdx++) {
            uint16 mipWidth = Max<uint16>(1, dstImage.desc.width >> mipIdx);
            uint16 mipHeight = Max<uint16>(1, dstImage.desc.height >> mipIdx);
            uint16 imageCopyIdx = mipIdx - copyParams.startMipIndex;

            copy->imageCopies[imageCopyIdx] = {
                .bufferOffset = dstImage.desc.mipOffsets[mipIdx],
                .bufferRowLength = 0,   
                .bufferImageHeight = 0,
                .imageSubresource = {
                    .aspectMask = aspect,
                    .mipLevel = mipIdx, 
                    .baseArrayLayer = 0,
                    .layerCount = 1
                },
                .imageOffset = {0, 0, 0},
                .imageExtent = {mipWidth, mipHeight, 1}
            };
        }

        // Put the post barriers
        // Transition the image from Trasnsfer to 
        GfxQueueType dstQueueType = GfxQueueType::None;
        if (IsBitsSet<GfxShaderStage>(copyParams.stagesUsed, GfxShaderStage::Vertex) ||
            IsBitsSet<GfxShaderStage>(copyParams.stagesUsed, GfxShaderStage::Fragment))
        {
            dstQueueType = GfxQueueType::Graphics;
        }
        else if (IsBitsSet<GfxShaderStage>(copyParams.stagesUsed, GfxShaderStage::Compute))
            dstQueueType = GfxQueueType::Compute;
        ASSERT(dstQueueType != GfxQueueType::None);

        uint32 queueFamilyIdx = queue.familyIdx;
        uint32 dstQueueFamilyIdx = gBackendVk.queueMan.GetQueue(gBackendVk.queueMan.FindQueue(dstQueueType)).familyIdx;
        ASSERT(dstQueueFamilyIdx != -1);

        if (queueFamilyIdx == dstQueueFamilyIdx) {
            // Unified Queue
            VkImageMemoryBarrier2 barrier {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = GfxBackend::_GetImageDestStageFlags(dstQueueType, copyParams.stagesUsed),
                .dstAccessMask = GfxBackend::_GetImageReadAccessFlags(VkImageUsageFlags(dstImage.desc.usageFlags)),
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = dstLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = dstImage.handle,
                .subresourceRange = subresourceRange
            };

            dstImage.layout = dstLayout;
            dstImage.transitionedStage = barrier.dstStageMask;
            dstImage.transitionedAccess = barrier.dstAccessMask;
            barriers.Push(barrier);

            if (copyParams.resourceTransferedCallback)
                copyParams.resourceTransferedCallback(copyParams.resourceTransferedUserData);
        }
        else {
            // Separate queue
            VkImageMemoryBarrier2 barrier {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = dstLayout,
                .srcQueueFamilyIndex = queueFamilyIdx,
                .dstQueueFamilyIndex = dstQueueFamilyIdx,
                .image = dstImage.handle,
                .subresourceRange = subresourceRange
            };

            barriers.Push(barrier);

            // TODO: Assert that dstQueue is not being recorded
            GfxBackendQueue::PendingBarrier dstBarrier {
                .type = GfxBackendQueue::PendingBarrier::IMAGE,
                .resourceTransferedCallback = copyParams.resourceTransferedCallback,
                .resourceTransferedUserData = copyParams.resourceTransferedUserData,
                .imageHandle = copyParams.dstHandle,
                .imageBarrier = {
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    .dstStageMask = GfxBackend::_GetImageDestStageFlags(dstQueueType, copyParams.stagesUsed),
                    .dstAccessMask = GfxBackend::_GetImageReadAccessFlags(VkImageUsageFlags(dstImage.desc.usageFlags)),
                    .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .newLayout = dstLayout,
                    .srcQueueFamilyIndex = queueFamilyIdx,
                    .dstQueueFamilyIndex = dstQueueFamilyIdx,
                    .subresourceRange = subresourceRange
                }
            };

            queue.internalDependents |= dstQueueType;
            pendingBarriers.Push(dstBarrier);
        }
    }
    gBackendVk.objectPoolsMutex.ExitRead();

    // Pre barriers
    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    ASSERT(!barriers.IsEmpty());
    ASSERT(!preBarriers.IsEmpty());
    VkDependencyInfo preDepInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = preBarriers.Count(),
        .pImageMemoryBarriers = preBarriers.Ptr()
    };
    vkCmdPipelineBarrier2(cmdVk, &preDepInfo);

    // Copy ops
    for (CopyBufferToImageData& copyParams : copies) {
        vkCmdCopyBufferToImage(cmdVk, copyParams.bufferHandle, copyParams.imageHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
                               copyParams.numMips, copyParams.imageCopies);
    }

    // Post barriers
    VkDependencyInfo postDepInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = barriers.Count(),
        .pImageMemoryBarriers = barriers.Ptr()
    };
    vkCmdPipelineBarrier2(cmdVk, &postDepInfo);

    // Pass on all the barriers that should be submitted to the targetQueue
    // These will be added to target pending after the first submission
    SpinLockMutexScope lock(queue.barriersMutex);
    queue.dependentBarriers.Extend(pendingBarriers);
}

void GfxCommandBuffer::TransitionBuffer(GfxBufferHandle buffHandle, GfxBufferTransition transition)
{
    ASSERT(mIsRecording);
    mShouldSubmit = true;

    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);

    gBackendVk.objectPoolsMutex.EnterRead();
    GfxBackendBuffer& buffer = gBackendVk.buffers.Data(buffHandle);
    GfxBackendQueue& queue = gBackendVk.queueMan.GetQueue(mQueueIndex);

    VkBufferMemoryBarrier2 barrier {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .buffer = buffer.handle,
        .size = VK_WHOLE_SIZE
    };
    
    switch (transition) {
        case GfxBufferTransition::TransferWrite:
            ASSERT_MSG(IsBitsSet<GfxQueueType>(queue.type, GfxQueueType::Transfer) || queue.supportsTransfer,
                       "Cannot do transfer transitions on non-Transfer queues");

            barrier.srcStageMask = buffer.transitionedStage;
            barrier.srcAccessMask = buffer.transitionedAccess;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
            break;
        default:
            break;
    }
    buffer.transitionedStage = barrier.dstStageMask;
    buffer.transitionedAccess = barrier.dstAccessMask;
    gBackendVk.objectPoolsMutex.ExitRead();

    VkDependencyInfo depInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &barrier
    };

    vkCmdPipelineBarrier2(cmdVk, &depInfo);
}

void GfxCommandBuffer::TransitionImage(GfxImageHandle imgHandle, GfxImageTransition transition)
{
    ASSERT(mIsRecording);
    mShouldSubmit = true;

    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);

    gBackendVk.objectPoolsMutex.EnterRead();
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
        case GfxImageTransition::ShaderRead:
            barrier.srcStageMask = image.transitionedStage;
            barrier.srcAccessMask = image.transitionedAccess;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex = 1;    // TEMP
            break;

        case GfxImageTransition::ComputeWrite:
            barrier.srcStageMask = image.transitionedStage;
            barrier.srcAccessMask = image.transitionedAccess;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            break;

        case GfxImageTransition::CopySource:
            barrier.srcStageMask = image.transitionedStage;
            barrier.srcAccessMask = image.transitionedAccess;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            break;

        case GfxImageTransition::RenderTarget:
            {
                VkImageLayout layout;
                VkPipelineStageFlags2 dstStage;
                VkAccessFlags2 accessFlags;
                if (GfxBackend::_FormatIsDepthStencil(image.desc.format)) {
                    layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    dstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
                    accessFlags = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT|VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                }
                else {
                    layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    accessFlags = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                }
                barrier.srcStageMask = image.transitionedStage;
                barrier.srcAccessMask = image.transitionedAccess;
                barrier.dstStageMask = dstStage;
                barrier.dstAccessMask = accessFlags;
                barrier.newLayout = layout;
                break;
            }
        default:
            ASSERT(0);
            break;
    }
    image.layout = barrier.newLayout;
    image.transitionedStage = barrier.dstStageMask;
    image.transitionedAccess = barrier.dstAccessMask;
    gBackendVk.objectPoolsMutex.ExitRead();

    VkDependencyInfo depInfo {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };

    vkCmdPipelineBarrier2(cmdVk, &depInfo);

}

void GfxCommandBuffer::BeginRenderPass(const GfxBackendRenderPass& pass)
{
    ASSERT(mIsRecording);

    auto MakeRenderingAttachmentInfo = [](const GfxRenderPassAttachment& srcAttachment, VkImageView view, VkImageLayout layout)
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
            clearValue.color = {{srcAttachment.clearValue.color.x, srcAttachment.clearValue.color.y, 
                                 srcAttachment.clearValue.color.z, srcAttachment.clearValue.color.w}};
        }
        else if (layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL || 
                 layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
                 layout == VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL)
        {
            clearValue.depthStencil = {.depth = srcAttachment.clearValue.depth, .stencil = srcAttachment.clearValue.stencil};
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

    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);

    uint32 numColorAttachments = !pass.swapchain ? pass.numAttachments : 1;
    ASSERT(numColorAttachments);
    ASSERT(numColorAttachments < GFXBACKEND_MAX_RENDERPASS_COLOR_ATTACHMENTS);
    VkRenderingAttachmentInfo colorAttachments[GFXBACKEND_MAX_RENDERPASS_COLOR_ATTACHMENTS];

    gBackendVk.objectPoolsMutex.EnterRead();
    VkImageView colorViews[GFXBACKEND_MAX_RENDERPASS_COLOR_ATTACHMENTS];
    if (!pass.swapchain) {
        for (uint32 i = 0; i < numColorAttachments; i++) 
            colorViews[i] = gBackendVk.images.Data(pass.colorAttachments[i].image).viewHandle;
    }

    VkImageView depthView = pass.hasDepth ? gBackendVk.images.Data(pass.depthAttachment.image).viewHandle : nullptr;
    [[maybe_unused]] VkImageView stencilView = pass.hasStencil ? gBackendVk.images.Data(pass.stencilAttachment.image).viewHandle : nullptr;
    gBackendVk.objectPoolsMutex.ExitRead();

    uint16 width = 0;
    uint16 height = 0;
    for (uint32 i = 0; i < numColorAttachments; i++) {
        const GfxRenderPassAttachment& srcAttachment = pass.colorAttachments[i];
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

        VkImageView view = pass.swapchain ? gBackendVk.swapchain.GetImageView() : colorViews[i];
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

    ASSERT_MSG(!pass.hasStencil, "Not implemented yet");
    VkRenderingAttachmentInfo depthAttachment;
    if (pass.hasDepth)
        depthAttachment = MakeRenderingAttachmentInfo(pass.depthAttachment, depthView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    // If we are drawing to Swapchain, we have to wait for drawing to finish and also transition the layout to COLOR_ATTACHMENT_OUTPUT
    if (pass.swapchain) {
        GfxBackendSwapchain::ImageState& state = gBackendVk.swapchain.GetImageState();
        VkImageMemoryBarrier2 imageBarrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = state.lastAccessFlags,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = state.lastLayout,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = gBackendVk.swapchain.GetImage(),
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .layerCount = VK_REMAINING_ARRAY_LAYERS
            }
        };

        VkDependencyInfo depInfo {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &imageBarrier
        };

        vkCmdPipelineBarrier2(cmdVk, &depInfo);

        state.lastStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        state.lastAccessFlags = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        state.lastLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        gBackendVk.queueMan.GetQueue(mQueueIndex).internalDependents |= GfxQueueType::Present;
    }

    VkRenderingInfo renderInfo {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = renderArea,
        .layerCount = 1,
        .viewMask = 0,
        .colorAttachmentCount = numColorAttachments,
        .pColorAttachments = colorAttachments,
        .pDepthAttachment = pass.hasDepth ? &depthAttachment : nullptr,
        .pStencilAttachment = nullptr // TODO
    };
    vkCmdBeginRendering(cmdVk, &renderInfo);

    mDrawsToSwapchain |= pass.swapchain;
    mIsInRenderPass = true;
}

void GfxCommandBuffer::EndRenderPass()
{
    ASSERT(mIsRecording);

    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    vkCmdEndRendering(cmdVk);

    mIsInRenderPass = false;
}

void GfxCommandBuffer::Draw(uint32 vertexCount, uint32 instanceCount, uint32 firstVertex, uint32 firstInstance)
{
    ASSERT(mIsRecording);
    mShouldSubmit = true;

    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    vkCmdDraw(cmdVk, vertexCount, instanceCount, firstVertex, firstInstance);
}

void GfxCommandBuffer::DrawIndexed(uint32 indexCount, uint32 instanceCount, uint32 firstIndex, uint32 vertexOffset, uint32 firstInstance)
{
    ASSERT(mIsRecording);
    mShouldSubmit = true;

    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    vkCmdDrawIndexed(cmdVk, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

GfxFormat GfxBackend::GetSwapchainFormat()
{
    return GfxFormat(gBackendVk.swapchain.format.format);
}

Int2 GfxBackend::GetSwapchainExtent()
{
    return Int2(int(gBackendVk.swapchain.extent.width), int(gBackendVk.swapchain.extent.height));
}

void GfxCommandBuffer::SetScissors(uint32 firstScissor, uint32 numScissors, const RectInt* scissors)
{
    ASSERT(mIsRecording);
    ASSERT(numScissors);
    ASSERT(scissors);

    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);

    MemTempAllocator tmpAlloc;
    VkRect2D* scissorsVk = tmpAlloc.MallocTyped<VkRect2D>(numScissors);

    for (uint32 i = 0; i < numScissors; i++) {
        const RectInt& scissor = scissors[i];
        Pair<Int2, Int2> transformed = GfxBackend::_TransformRectangleBasedOnOrientation(scissor.xmin, scissor.ymin, 
                                                                                         scissor.Width(), scissor.Height(), 
                                                                                         mDrawsToSwapchain);
        scissorsVk[i].offset.x = transformed.first.x;
        scissorsVk[i].offset.y = transformed.first.y;
        scissorsVk[i].extent.width = transformed.second.x;
        scissorsVk[i].extent.height = transformed.second.y;
    }

    vkCmdSetScissor(cmdVk, firstScissor, numScissors, scissorsVk);
}

void GfxCommandBuffer::SetViewports(uint32 firstViewport, uint32 numViewports, const GfxViewport* viewports)
{
    ASSERT(mIsRecording);
    ASSERT(numViewports);
    ASSERT(viewports);

    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);

    MemTempAllocator tmpAlloc;
    VkViewport* viewportsVk = tmpAlloc.MallocTyped<VkViewport>(numViewports);

    for (uint32 i = 0; i < numViewports; i++) {
        Pair<Int2, Int2> transformed = GfxBackend::_TransformRectangleBasedOnOrientation(
            int(viewports[i].x), int(viewports[i].y), 
            int(viewports[i].width), int(viewports[i].height), 
            mDrawsToSwapchain);

        viewportsVk[i].x = float(transformed.first.x);
        viewportsVk[i].y = float(transformed.first.y);
        viewportsVk[i].width = float(transformed.second.x);
        viewportsVk[i].height = float(transformed.second.y);
        viewportsVk[i].minDepth = viewports[i].minDepth;
        viewportsVk[i].maxDepth = viewports[i].maxDepth;        
    }

    vkCmdSetViewport(cmdVk, firstViewport, numViewports, viewportsVk);
}

void GfxCommandBuffer::BindVertexBuffers(uint32 firstBinding, uint32 numBindings, const GfxBufferHandle* vertexBuffers, const uint64* offsets)
{
    ASSERT(mIsRecording);
    static_assert(sizeof(uint64) == sizeof(VkDeviceSize));

    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);

    MemTempAllocator tempAlloc;
    VkBuffer* buffersVk = tempAlloc.MallocTyped<VkBuffer>(numBindings);
    for (uint32 i = 0; i < numBindings; i++) 
        buffersVk[i] = gBackendVk.buffers.Data(vertexBuffers[i]).handle;
    
    vkCmdBindVertexBuffers(cmdVk, firstBinding, numBindings, buffersVk, reinterpret_cast<const VkDeviceSize*>(offsets));
}

void GfxCommandBuffer::BindIndexBuffer(GfxBufferHandle indexBuffer, uint64 offset, GfxIndexType indexType)
{
    ASSERT(mIsRecording);
    VkCommandBuffer cmdVk = GfxBackend::_GetCommandBufferHandle(*this);
    GfxBackendBuffer& buffer = gBackendVk.buffers.Data(indexBuffer);

    vkCmdBindIndexBuffer(cmdVk, buffer.handle, VkDeviceSize(offset), VkIndexType(indexType));
}

GfxSamplerHandle GfxBackend::CreateSampler(const GfxSamplerDesc& desc)
{
    VkFilter minMagFilter = VK_FILTER_MAX_ENUM;
    VkSamplerMipmapMode mipFilter = VK_SAMPLER_MIPMAP_MODE_MAX_ENUM;
    VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_MAX_ENUM;
    float anisotropy = desc.anisotropy <= 0 ? 1.0f : desc.anisotropy;

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
        .maxAnisotropy = Min(gBackendVk.gpu.props.limits.maxSamplerAnisotropy, anisotropy),
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f, 
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE, 
    };

    VkSampler samplerVk;
    if (vkCreateSampler(gBackendVk.device, &samplerInfo, gBackendVk.vkAlloc, &samplerVk) != VK_SUCCESS)
        return {};

    GfxBackendSampler sampler {
        .handle = samplerVk,
        .desc = desc
    };

    ReadWriteMutexWriteScope objPoolLock(gBackendVk.objectPoolsMutex);
    return gBackendVk.samplers.Add(sampler);
}

void GfxBackend::DestroySampler(GfxSamplerHandle& handle)
{
    if (handle.IsValid()) {
        ReadWriteMutexWriteScope objPoolLock(gBackendVk.objectPoolsMutex);
        GfxBackendSampler& sampler = gBackendVk.samplers.Data(handle);

        MutexScope lock(gBackendVk.garbageMtx);
        GfxBackendGarbage garbage {
            .type = GfxBackendGarbage::Type::Sampler,
            .frameIdx = gBackendVk.presentFrame,
            .sampler = sampler.handle
        };

        gBackendVk.garbage.Push(garbage);

        gBackendVk.samplers.Remove(handle);

        handle = {};
    }
}

bool GfxBackendMemoryOffsetAllocator::Initialize(VkDeviceSize blockSize, uint32 memoryTypeIndex)
{
    ASSERT(memoryTypeIndex != -1);
    ASSERT(gBackendVk.device);
    ASSERT(blockSize);
    ASSERT_MSG(blockSize < UINT32_MAX, "Our OffsetAllocator doesn't support 64bit address space");

    mMemTypeIndex = memoryTypeIndex;
    mBlockSize = uint32(blockSize);
    mBlocks.SetAllocator(&gBackendVk.runtimeAlloc);

    const VkMemoryType& memType = gBackendVk.memMan.GetProps().memoryTypes[memoryTypeIndex];
    mTypeFlags = memType.propertyFlags;
    mHeapFlags = gBackendVk.memMan.GetProps().memoryHeaps[memType.heapIndex].flags;

    Block* block = CreateBlock();
    if (!block)
        return false;
    mBlocks.Push(block);

    return true;
}

// Calling this will reallocate the buffers in the OffsetAlloc. not recmmended
void GfxBackendMemoryOffsetAllocator::Reset()
{
    for (Block* block : mBlocks)
        OffsetAllocator_Reset(block->offsetAlloc);
}


void GfxBackendMemoryOffsetAllocator::Release()
{
    for (Block* block : mBlocks)
        DestroyBlock(block);
    mBlocks.Free();
    mCapacity = 0;
    mMemTypeIndex = 0;
}

GfxBackendMemoryOffsetAllocator::Block* GfxBackendMemoryOffsetAllocator::CreateBlock()
{
    uint8* offsetAllocMem;
    size_t offsetAllocMemSize = OffsetAllocator_GetRequiredBytes(GFXBACKEND_MAX_ENTRIES_IN_OFFSET_ALLOCATOR);
    MemSingleShotMalloc<GfxBackendMemoryOffsetAllocator::Block> mallocator;
    mallocator.AddExternalPointerField<uint8>(&offsetAllocMem, offsetAllocMemSize);
    Block* block = mallocator.Malloc(&gBackendVk.runtimeAlloc);
    memset(block, 0x0, sizeof(Block));
    block->offsetAlloc = OffsetAllocator_Create(mBlockSize, GFXBACKEND_MAX_ENTRIES_IN_OFFSET_ALLOCATOR, 
                                                offsetAllocMem, offsetAllocMemSize);

    if (gBackendVk.extApi.hasMemoryBudget) {
        ASSERT_MSG(gBackendVk.memMan.GetDeviceMemoryBudget(mMemTypeIndex) >= mBlockSize, 
                   "Not enough GPU memory available in the specified heap");
    }

    VkMemoryAllocateInfo allocInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mBlockSize,
        .memoryTypeIndex = mMemTypeIndex
    };

    VkResult r = vkAllocateMemory(gBackendVk.device, &allocInfo, gBackendVk.vkAlloc, &block->deviceMem);
    if (r != VK_SUCCESS) {
        MEM_FAIL();
        return nullptr;
    }

    if (mTypeFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        r = vkMapMemory(gBackendVk.device, block->deviceMem, 0, VK_WHOLE_SIZE, 0, &block->mappedData);
        ASSERT(r == VK_SUCCESS);
    }

    mCapacity += mBlockSize;
    return block;
}

void GfxBackendMemoryOffsetAllocator::DestroyBlock(Block* block)
{
    if (block->deviceMem) {
        if (block->mappedData)
            vkUnmapMemory(gBackendVk.device, block->deviceMem);
        vkFreeMemory(gBackendVk.device, block->deviceMem, gBackendVk.vkAlloc);
    }

    if (block->offsetAlloc)
        OffsetAllocator_Destroy(block->offsetAlloc);

    MemSingleShotMalloc<GfxBackendMemoryOffsetAllocator::Block>::Free(block, &gBackendVk.runtimeAlloc);
}

GfxBackendDeviceMemory GfxBackendMemoryOffsetAllocator::Malloc(const VkMemoryRequirements& memReq)
{
    ASSERT(memReq.size <= UINT32_MAX);
    ASSERT(memReq.alignment);

    if (!((memReq.memoryTypeBits >> mMemTypeIndex) & 0x1)) {
        ASSERT_ALWAYS(0, "Allocation for this resource is not supported by this memory type");
        return GfxBackendDeviceMemory {};
    }

    if (memReq.size > mBlockSize) {
        ASSERT_MSG(0, "GpuMemoryAllocator block size (%_$$$llu) is smaller than requested size (%_$$$llu)", mBlockSize, memReq.size);
        MEM_FAIL();
        return GfxBackendDeviceMemory {};
    }

    SpinLockMutexScope lock(mMutex);

    // We have to over-allocate then pad to the alignment value
    uint32 totalSize = uint32(memReq.size + memReq.alignment);
    ASSERT(totalSize <= UINT32_MAX);

    // Start trying from the last block to first
    // So there's a higher chance that we hit what we want earlier
    Block* block = nullptr;
    OffsetAllocatorAllocation alloc {OFFSET_ALLOCATOR_NO_SPACE, OFFSET_ALLOCATOR_NO_SPACE};
    for (uint32 i = mBlocks.Count(); i-- > 0;) {
        Block* b = mBlocks[i];
        OffsetAllocator_Allocate(b->offsetAlloc, totalSize, &alloc);
        if (alloc.offset != OFFSET_ALLOCATOR_NO_SPACE) {
            block = b;
            break;
        }
    }

    if (!block) {
        block = CreateBlock();
        if (!block) {
            MEM_FAIL();
            return GfxBackendDeviceMemory {};
        }
        mBlocks.Push(block);

        OffsetAllocator_Allocate(block->offsetAlloc, totalSize, &alloc);
    }

    if (alloc.metadata == OFFSET_ALLOCATOR_NO_SPACE) {
        MEM_FAIL();
        return GfxBackendDeviceMemory {};
    }

    // Align the offset
    uint32 padding = 0;
    uint32 align = uint32(memReq.alignment);
    uint32 alignedOffset = alloc.offset;
    if (alloc.offset % align != 0) {
        alignedOffset = AlignValue<uint32>(alloc.offset, align);
        padding = alignedOffset - alloc.offset;
        ASSERT(padding <= UINT16_MAX);
    }

    GfxBackendDeviceMemory mem {
        .handle = block->deviceMem,
        .offset = alignedOffset,
        .mappedData = block->mappedData ? ((uint8*)block->mappedData + alignedOffset) : nullptr,
        .offsetAllocMetaData = alloc.metadata,
        .offsetAllocPadding = uint16(padding),
        .isHeapDeviceLocal = (mHeapFlags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
        .isCpuVisible = (mTypeFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        .isCached = (mTypeFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) == VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        .isCoherent = (mTypeFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        .isLazilyAlloc = (mTypeFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) == VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT
    };

    return mem;
}

void GfxBackendMemoryOffsetAllocator::Free(GfxBackendDeviceMemory mem)
{
    SpinLockMutexScope lock(mMutex);

    bool freed = false;
    for (Block* block : mBlocks) {
        if (block->deviceMem == mem.handle) {
            OffsetAllocatorAllocation alloc {
                .offset = uint32(mem.offset - mem.offsetAllocPadding),
                .metadata = mem.offsetAllocMetaData
            };

            OffsetAllocator_Free(block->offsetAlloc, &alloc);

            freed = true;
            break;
        }
    }

    ASSERT_MSG(freed, "Doesn't seem to be belonging to this arena ?!");
}

void GfxBackend::BeginRenderFrameSync()
{
    // External CPU <-> CPU sync
    // Used by external systems like AssetManager to wait until it's time to start uploading resources
    if (!gBackendVk.externalFrameSyncSignal.WaitOnCondition([](int value, int ref) { return value > ref; }, 0, UINT32_MAX)) {
        LOG_WARNING("External systems should wait for GfxBackend::BeginFrame");
    }
    
    gBackendVk.frameSyncSignal.Increment();
    Atomic::FetchAdd(&gBackendVk.numOpenExternalFrameSyncs, 1);
}

void GfxBackend::EndRenderFrameSync()
{
    Atomic::FetchSub(&gBackendVk.numOpenExternalFrameSyncs, 1);

    gBackendVk.frameSyncSignal.Decrement();
    gBackendVk.frameSyncSignal.Raise();
}

void GfxBackendQueueSemaphoreBank::Initialize()
{
    mSemaphores.SetAllocator(&gBackendVk.runtimeAlloc);
    mSemaphoreFreeList.SetAllocator(&gBackendVk.runtimeAlloc);
}

void GfxBackendQueueSemaphoreBank::Release()
{
    mSemaphores.Extend(mSemaphoreFreeList);
    for (VkSemaphore sem : mSemaphores)
        vkDestroySemaphore(gBackendVk.device, sem, gBackendVk.vkAlloc);
    mSemaphores.Free();
    mSemaphoreFreeList.Free();            
}

VkSemaphore GfxBackendQueueSemaphoreBank::GetSemaphore()
{
    VkSemaphore sem;

    SpinLockMutexScope lock(mMutex);
    if (!mSemaphoreFreeList.IsEmpty()) {
        sem = mSemaphoreFreeList.PopLast();
    }
    else {
        VkSemaphoreCreateInfo semCreateInfo { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        vkCreateSemaphore(gBackendVk.device, &semCreateInfo, gBackendVk.vkAlloc, &sem);
    }
    mSemaphores.Push(sem);
    return sem;
}

void GfxBackendQueueSemaphoreBank::Reset()
{
    SpinLockMutexScope lock(mMutex);

    mSemaphoreFreeList.Extend(mSemaphores);
    mSemaphores.Clear();
}

Mat4 GfxBackend::GetSwapchainTransformMat()
{
    switch (App::GetFramebufferTransform()) {
    case AppFramebufferTransform::None:           return MAT4_IDENT;
    case AppFramebufferTransform::Rotate90:       return Mat4::RotateZ(M_HALFPI);
    case AppFramebufferTransform::Rotate180:      return Mat4::RotateZ(M_PI);
    case AppFramebufferTransform::Rotate270:      return Mat4::RotateZ(M_PI + M_HALFPI);
    }

    return MAT4_IDENT;
}

float GfxBackend::GetRenderTimeNS()
{
    // TODO
    return 0;
}

const GfxBlendAttachmentDesc* GfxBlendAttachmentDesc::GetDefault()
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

const GfxBlendAttachmentDesc* GfxBlendAttachmentDesc::GetAlphaBlending()
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
