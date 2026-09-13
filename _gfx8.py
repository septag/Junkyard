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


# --- Deferred resize/destroy state
rep("""    VkExtent2D extent;
    bool resize;""",
    """    VkExtent2D extent;
    // Secondary swapchains are resized and destroyed at a safe point in End(), never inline:
    // vkDeviceWaitIdle needs every VkQueue externally synchronized, and the submission thread
    // is only known to be idle after End() drains frameSyncSignal
    Int2 requestedSize;
    bool wantDestroy;
    bool resize;""")

# --- DestroySwapchain / ResizeSwapchain just flag
rep("""void GfxBackend::DestroySwapchain(GfxSwapchainHandle& handle)
{
    if (!handle.IsValid())
        return;
    ASSERT_MSG(handle != gBackendVk.mainSwapchain, "Main swapchain is owned by the backend and cannot be destroyed");

    // Presentation may still be in flight for this surface
    vkDeviceWaitIdle(gBackendVk.device);

    GfxBackendSwapchain& swapchain = gBackendVk.swapchains.Data(handle);
    GfxBackend::_ReleaseSwapchain(&swapchain);

    gBackendVk.swapchains.Remove(handle);
    handle = GfxSwapchainHandle();
}

void GfxBackend::ResizeSwapchain(GfxSwapchainHandle handle, Int2 size)
{
    GfxBackendSwapchain& swapchain = _GetSwapchain(handle);
    vkDeviceWaitIdle(gBackendVk.device);
    GfxBackend::_ResizeSwapchain(&swapchain, size, SettingsJunkyard::Get().graphics.surfaceSRGB);
}""",
    """void GfxBackend::DestroySwapchain(GfxSwapchainHandle& handle)
{
    if (!handle.IsValid())
        return;
    ASSERT_MSG(handle != gBackendVk.mainSwapchain, "Main swapchain is owned by the backend and cannot be destroyed");

    // Flag only. It may still be presenting this frame, and tearing it down here would race the
    // submission thread. End() does the actual destroy once submissions are drained
    gBackendVk.swapchains.Data(handle).wantDestroy = true;
    handle = GfxSwapchainHandle();
}

void GfxBackend::ResizeSwapchain(GfxSwapchainHandle handle, Int2 size)
{
    ASSERT_MSG(handle.IsValid() && handle != gBackendVk.mainSwapchain,
               "Main swapchain is resized by the backend from the window size");

    // Deferred for the same reason as DestroySwapchain
    GfxBackendSwapchain& swapchain = gBackendVk.swapchains.Data(handle);
    swapchain.requestedSize = size;
    swapchain.resize = true;
}""")

# --- End(): service deferred work where the submission thread is known idle
rep("""    // Only the main swapchain is resized here. Secondary ones follow their window, so their owner
    // drives GfxBackend::ResizeSwapchain instead
    if (_MainSwapchain().resize) {
        vkDeviceWaitIdle(gBackendVk.device);
        GfxBackend::_ResizeSwapchain(&_MainSwapchain(),
                                     Int2(App::GetFramebufferWidth(), App::GetFramebufferHeight()),
                                     SettingsJunkyard::Get().graphics.surfaceSRGB);
    }""",
    """    // Deferred swapchain maintenance. Submissions are drained by now (see frameSyncSignal above),
    // so vkDeviceWaitIdle here does not race the submission thread
    {
        bool forceSRGB = SettingsJunkyard::Get().graphics.surfaceSRGB;

        GfxSwapchainHandle pendingDestroy[GFXBACKEND_MAX_SWAPCHAIN_TARGETS];
        uint32 numPendingDestroy = 0;
        bool needsWaitIdle = false;

        for (uint32 i = 0; i < gBackendVk.swapchains.Count(); i++) {
            GfxSwapchainHandle handle = gBackendVk.swapchains.HandleAt(i);
            const GfxBackendSwapchain& swapchain = gBackendVk.swapchains.Data(handle);
            if (swapchain.wantDestroy || swapchain.resize)
                needsWaitIdle = true;
            if (swapchain.wantDestroy && numPendingDestroy < GFXBACKEND_MAX_SWAPCHAIN_TARGETS)
                pendingDestroy[numPendingDestroy++] = handle;
        }

        if (needsWaitIdle)
            vkDeviceWaitIdle(gBackendVk.device);

        for (uint32 i = 0; i < gBackendVk.swapchains.Count(); i++) {
            GfxSwapchainHandle handle = gBackendVk.swapchains.HandleAt(i);
            GfxBackendSwapchain& swapchain = gBackendVk.swapchains.Data(handle);
            if (swapchain.wantDestroy || !swapchain.resize)
                continue;

            // The main swapchain follows the window, secondary ones follow what their owner asked for
            Int2 size = swapchain.isMain ?
                Int2(App::GetFramebufferWidth(), App::GetFramebufferHeight()) :
                swapchain.requestedSize;
            GfxBackend::_ResizeSwapchain(&swapchain, size, forceSRGB);
        }

        for (uint32 i = 0; i < numPendingDestroy; i++) {
            GfxBackend::_ReleaseSwapchain(&gBackendVk.swapchains.Data(pendingDestroy[i]));
            gBackendVk.swapchains.Remove(pendingDestroy[i]);
        }
    }""")

io.open(path, 'w', encoding='utf-8', newline='').write(s)
print('deferred swapchain maintenance ok')
