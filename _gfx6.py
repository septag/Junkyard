import io
import re

path = 'code/Graphics/GfxBackend.cpp'
s = io.open(path, encoding='utf-8', newline='').read()


def rep(a, b, n=1):
    global s
    pat = re.compile(r'[ \t]*\r\n'.join(re.escape(x) for x in a.split('\n')))
    b2 = b.replace('\n', '\r\n')
    found = len(pat.findall(s))
    assert found == n, (found, n, a[:110])
    s = pat.sub(lambda m: b2, s, count=n)


GFX_MAX_SWAPCHAIN_TARGETS = 8

# --- 1) A submission can now be tied to several swapchains
rep("""struct GfxBackendQueueSubmitRequest
{
    GfxQueueType type;
    GfxQueueType dependents;
    VkCommandBuffer* cmdBuffers;
    VkFence fence;
    VkSemaphore semaphore;
    uint32 numCmdBuffers;
};""",
    """// Swapchains presented from a single queue submission. One per ImGui viewport plus the main window
inline constexpr uint32 GFXBACKEND_MAX_SWAPCHAIN_TARGETS = 8;

struct GfxBackendQueueSubmitRequest
{
    GfxQueueType type;
    GfxQueueType dependents;
    VkCommandBuffer* cmdBuffers;
    VkFence fence;
    VkSemaphore semaphore;
    uint32 numCmdBuffers;
    GfxSwapchainHandle swapchainTargets[GFXBACKEND_MAX_SWAPCHAIN_TARGETS];
    uint32 numSwapchainTargets;
};""")

# --- 2) Queues accumulate their swapchain targets while recording
rep("""    GfxQueueType internalDependents;
    AtomicUint32 numCmdBuffersInRecording;""",
    """    GfxQueueType internalDependents;
    // Filled while recording (alongside internalDependents), drained into the submit request
    GfxSwapchainHandle swapchainTargets[GFXBACKEND_MAX_SWAPCHAIN_TARGETS];
    uint32 numSwapchainTargets;
    AtomicUint32 numCmdBuffersInRecording;""")

# --- 3) Frame-level acquire bookkeeping
rep("""    HandlePool<GfxSwapchainHandle, GfxBackendSwapchain> swapchains;
    GfxSwapchainHandle mainSwapchain;""",
    """    HandlePool<GfxSwapchainHandle, GfxBackendSwapchain> swapchains;
    GfxSwapchainHandle mainSwapchain;
    // Swapchains whose image was acquired this frame. Every one of them must be presented in End()
    GfxSwapchainHandle acquiredSwapchains[GFXBACKEND_MAX_SWAPCHAIN_TARGETS];
    uint32 numAcquiredSwapchains;
    SpinLockMutex acquiredSwapchainsMtx;""")

# --- 4) Helpers: acquire on demand and register the target on the recording queue
rep("""// An invalid handle means the main swapchain, so existing single-window call sites need no change
static inline GfxBackendSwapchain& _GetSwapchain(GfxSwapchainHandle handle)
{
    return handle.IsValid() ? gBackendVk.swapchains.Data(handle) : _MainSwapchain();
}""",
    """// An invalid handle means the main swapchain, so existing single-window call sites need no change
static inline GfxBackendSwapchain& _GetSwapchain(GfxSwapchainHandle handle)
{
    return handle.IsValid() ? gBackendVk.swapchains.Data(handle) : _MainSwapchain();
}

// Acquires an image if this swapchain has not been acquired yet in the current frame.
// The main swapchain is acquired eagerly by Begin(), secondary ones lazily on first use
static void _AcquireSwapchainOnce(GfxSwapchainHandle handle)
{
    ASSERT(handle.IsValid());

    SpinLockMutexScope lock(gBackendVk.acquiredSwapchainsMtx);
    for (uint32 i = 0; i < gBackendVk.numAcquiredSwapchains; i++) {
        if (gBackendVk.acquiredSwapchains[i] == handle)
            return;
    }

    ASSERT_MSG(gBackendVk.numAcquiredSwapchains < GFXBACKEND_MAX_SWAPCHAIN_TARGETS,
               "Too many swapchains presented in a single frame");

    GfxBackendSwapchain& swapchain = gBackendVk.swapchains.Data(handle);
    VkResult r = vkAcquireNextImageKHR(gBackendVk.device, swapchain.handle, UINT64_MAX,
                                       swapchain.GetSwapchainReadySemaphore(gBackendVk.queueMan.GetFrameIndex()),
                                       nullptr, &swapchain.imageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR)
        swapchain.resize = true;
    else if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        ASSERT_ALWAYS(0, "Gfx: AcquireSwapchain failed");

    gBackendVk.acquiredSwapchains[gBackendVk.numAcquiredSwapchains++] = handle;
}

// Records that the queue currently being recorded into will present to this swapchain
static void _AddQueueSwapchainTarget(GfxBackendQueue& queue, GfxSwapchainHandle handle)
{
    for (uint32 i = 0; i < queue.numSwapchainTargets; i++) {
        if (queue.swapchainTargets[i] == handle)
            return;
    }

    ASSERT_MSG(queue.numSwapchainTargets < GFXBACKEND_MAX_SWAPCHAIN_TARGETS,
               "Too many swapchains targeted by a single queue submission");
    queue.swapchainTargets[queue.numSwapchainTargets++] = handle;
}""")

# --- 5) Begin(): reset the frame list, main is acquired eagerly as before
rep("""    {
        GfxBackendSwapchain& swapchain = _MainSwapchain();

        VkResult r = vkAcquireNextImageKHR(gBackendVk.device, swapchain.handle, UINT64_MAX,
                                           swapchain.GetSwapchainReadySemaphore(gBackendVk.queueMan.GetFrameIndex()),
                                           nullptr, &swapchain.imageIndex);
        if (r == VK_ERROR_OUT_OF_DATE_KHR)
            swapchain.resize = true;
        else if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
            ASSERT_ALWAYS(0, "Gfx: AcquireSwapchain failed");
    }""",
    """    gBackendVk.numAcquiredSwapchains = 0;
    _AcquireSwapchainOnce(gBackendVk.mainSwapchain);""")

# --- 6) Register targets where Present is flagged
rep("""        gBackendVk.queueMan.GetQueue(mQueueIndex).internalDependents |= GfxQueueType::Present;

        // Records the resolved target, so EndCommandBuffer transitions the right image to PRESENT""",
    """        GfxBackendQueue& passQueue = gBackendVk.queueMan.GetQueue(mQueueIndex);
        passQueue.internalDependents |= GfxQueueType::Present;
        _AcquireSwapchainOnce(targetHandle);
        _AddQueueSwapchainTarget(passQueue, targetHandle);

        // Records the resolved target, so EndCommandBuffer transitions the right image to PRESENT""")

rep("    gBackendVk.queueMan.GetQueue(mQueueIndex).internalDependents |= GfxQueueType::Present;",
    """    {
        GfxBackendQueue& swapchainQueue = gBackendVk.queueMan.GetQueue(mQueueIndex);
        swapchainQueue.internalDependents |= GfxQueueType::Present;
        _AddQueueSwapchainTarget(swapchainQueue, gBackendVk.mainSwapchain);
    }""", 2)

# --- 7) Drain the targets into the submit request
rep("""    req->dependents = dependentQueues | queue.internalDependents;
    queue.internalDependents = GfxQueueType::None;""",
    """    req->dependents = dependentQueues | queue.internalDependents;
    queue.internalDependents = GfxQueueType::None;

    req->numSwapchainTargets = queue.numSwapchainTargets;
    for (uint32 i = 0; i < queue.numSwapchainTargets; i++)
        req->swapchainTargets[i] = queue.swapchainTargets[i];
    queue.numSwapchainTargets = 0;""")

# --- 8) Sync against every swapchain this submission presents to
rep("""        ASSERT(req.type == GfxQueueType::Graphics);
        // Notify the queue that the next Submit is gonna depend on swapchain
        queue.waitSemaphores.Push({_MainSwapchain().GetSwapchainReadySemaphore(gBackendVk.queueMan.GetFrameIndex()), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT});
        queue.signalSemaphores.Push(_MainSwapchain().GetSwapchainRenderFinishedSemaphore());""",
    """        ASSERT(req.type == GfxQueueType::Graphics);
        // Notify the queue that the next Submit is gonna depend on the swapchains it renders into
        for (uint32 i = 0; i < req.numSwapchainTargets; i++) {
            GfxBackendSwapchain& swapchain = gBackendVk.swapchains.Data(req.swapchainTargets[i]);
            queue.waitSemaphores.Push({swapchain.GetSwapchainReadySemaphore(gBackendVk.queueMan.GetFrameIndex()),
                                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT});
            queue.signalSemaphores.Push(swapchain.GetSwapchainRenderFinishedSemaphore());
        }""")

io.open(path, 'w', encoding='utf-8', newline='').write(s)
print('gfx step4 part A ok')
