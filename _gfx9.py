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


# --- Acquire reports failure instead of pretending it worked
rep("""static void _AcquireSwapchainOnce(GfxSwapchainHandle handle)
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
}""",
    """// Returns false when no image could be acquired. In that case the ready semaphore is NOT signalled,
// so nothing may wait on it and the swapchain must not be presented, otherwise the submission
// blocks forever and the next frame times out waiting on its fence
static bool _AcquireSwapchainOnce(GfxSwapchainHandle handle)
{
    ASSERT(handle.IsValid());

    SpinLockMutexScope lock(gBackendVk.acquiredSwapchainsMtx);
    for (uint32 i = 0; i < gBackendVk.numAcquiredSwapchains; i++) {
        if (gBackendVk.acquiredSwapchains[i] == handle)
            return true;
    }

    ASSERT_MSG(gBackendVk.numAcquiredSwapchains < GFXBACKEND_MAX_SWAPCHAIN_TARGETS,
               "Too many swapchains presented in a single frame");

    GfxBackendSwapchain& swapchain = gBackendVk.swapchains.Data(handle);
    VkResult r = vkAcquireNextImageKHR(gBackendVk.device, swapchain.handle, UINT64_MAX,
                                       swapchain.GetSwapchainReadySemaphore(gBackendVk.queueMan.GetFrameIndex()),
                                       nullptr, &swapchain.imageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        // Rebuilt at the start of the next frame, this one is dropped for that swapchain
        swapchain.resize = true;
        return false;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        ASSERT_ALWAYS(0, "Gfx: AcquireSwapchain failed");

    gBackendVk.acquiredSwapchains[gBackendVk.numAcquiredSwapchains++] = handle;
    return true;
}

static bool _IsSwapchainAcquired(GfxSwapchainHandle handle)
{
    SpinLockMutexScope lock(gBackendVk.acquiredSwapchainsMtx);
    for (uint32 i = 0; i < gBackendVk.numAcquiredSwapchains; i++) {
        if (gBackendVk.acquiredSwapchains[i] == handle)
            return true;
    }
    return false;
}""")

# --- Maintenance moves out of End() into its own function
rep("""    // Deferred swapchain maintenance. Submissions are drained by now (see frameSyncSignal above),
    // so vkDeviceWaitIdle here does not race the submission thread
    {
        bool forceSRGB""",
    """    __PLACEHOLDER_MAINTENANCE__
    {
        bool forceSRGB""")

m = re.search(r'    __PLACEHOLDER_MAINTENANCE__\r\n    \{\r\n.*?\r\n    \}\r\n', s, re.S)
assert m, 'maintenance block not found'
block = m.group(0)
body = block.replace('    __PLACEHOLDER_MAINTENANCE__\r\n', '')
# strip one level of indentation from the extracted body
body_lines = body.split('\r\n')
inner = '\r\n'.join(l[4:] if l.startswith('    ') else l for l in body_lines[1:-2])
s = s[:m.start()] + s[m.end():]

func = ('// Deferred swapchain resize/destroy. Only safe where the submission thread is idle, which is why\r\n'
        '// this runs at the start of a frame (after queueMan.BeginFrame waits on the in-flight fences)\r\n'
        '// and never inline from a window event\r\n'
        'static void _ServicePendingSwapchainWork()\r\n'
        '{\r\n' + inner + '\r\n}\r\n\r\n')

anchor = 'static bool _AcquireSwapchainOnce(GfxSwapchainHandle handle)'
i = s.find('// Returns false when no image could be acquired')
assert i > 0
s = s[:i] + func + s[i:]

# --- Begin(): service maintenance before acquiring anything
rep("""    gBackendVk.numAcquiredSwapchains = 0;
    _AcquireSwapchainOnce(gBackendVk.mainSwapchain);""",
    """    // Must precede the acquire: acquiring from a swapchain that is pending a resize returns
    // OUT_OF_DATE and leaves the ready semaphore unsignalled
    _ServicePendingSwapchainWork();

    gBackendVk.numAcquiredSwapchains = 0;
    _AcquireSwapchainOnce(gBackendVk.mainSwapchain);""")

io.open(path, 'w', encoding='utf-8', newline='').write(s)
print('acquire + maintenance reorder ok')
