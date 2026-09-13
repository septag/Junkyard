import io
import re

path = 'code/Graphics/GfxBackend.cpp'
s = io.open(path, encoding='utf-8', newline='').read()


def rep(a, b, n=1):
    global s
    pat = re.compile(r'[ \t]*\r\n'.join(re.escape(x) for x in a.split('\n')))
    b = b.replace('\n', '\r\n')
    found = len(pat.findall(s))
    assert found == n, (found, n, a[:110])
    s = pat.sub(lambda m: b, s, count=n)


# --- Surface caps/formats/present-modes are per-surface, so they move onto the swapchain
rep("""    VkSurfaceKHR surface;
    uint32 imageIndex;""",
    """    VkSurfaceKHR surface;
    GfxBackendSwapchainInfo info;
    uint32 imageIndex;""")

rep("""    GfxBackendSwapchainInfo swapchainInfo;
    HandlePool<GfxSwapchainHandle, GfxBackendSwapchain> swapchains;""",
    """    HandlePool<GfxSwapchainHandle, GfxBackendSwapchain> swapchains;""")

rep("gBackendVk.swapchainInfo.caps", "swapchain->info.caps", 2)
rep("        const GfxBackendSwapchainInfo& info = gBackendVk.swapchainInfo;",
    "        const GfxBackendSwapchainInfo& info = swapchain->info;")

# --- Query helper, used by both the main swapchain and any created later
rep("""    static bool _ResizeSwapchain(GfxBackendSwapchain* swapchain, Int2 size, bool forceSRGB)""",
    """    static void _QuerySwapchainInfo(GfxBackendSwapchain* swapchain)
    {
        ASSERT(swapchain->surface);
        GfxBackendSwapchainInfo& info = swapchain->info;

        uint32 numFormats = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(gBackendVk.gpu.handle, swapchain->surface, &numFormats, nullptr);
        info.numFormats = numFormats;
        info.formats = Mem::AllocTyped<VkSurfaceFormatKHR>(numFormats, &gBackendVk.parentAlloc);
        vkGetPhysicalDeviceSurfaceFormatsKHR(gBackendVk.gpu.handle, swapchain->surface, &numFormats, info.formats);

        uint32 numPresentModes = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(gBackendVk.gpu.handle, swapchain->surface, &numPresentModes, nullptr);
        info.numPresentModes = numPresentModes;
        info.presentModes = Mem::AllocTyped<VkPresentModeKHR>(numPresentModes, &gBackendVk.parentAlloc);
        vkGetPhysicalDeviceSurfacePresentModesKHR(gBackendVk.gpu.handle, swapchain->surface, &numPresentModes, info.presentModes);
    }

    static bool _ResizeSwapchain(GfxBackendSwapchain* swapchain, Int2 size, bool forceSRGB)""")

# --- Release frees the per-swapchain surface info too
rep("""    static void _ReleaseSwapchain(GfxBackendSwapchain* swapchain)
    {
        ASSERT(swapchain);
""",
    """    static void _ReleaseSwapchain(GfxBackendSwapchain* swapchain)
    {
        ASSERT(swapchain);

        Mem::Free(swapchain->info.formats, &gBackendVk.parentAlloc);
        Mem::Free(swapchain->info.presentModes, &gBackendVk.parentAlloc);
        swapchain->info = GfxBackendSwapchainInfo {};
""")

# --- Initialize: replace the inlined format/present-mode query
rep("""    // Swapchain and it's capabilities
    // We can only create this after device is created.
    if (!settings.graphics.headless) {
        uint32 numFormats;
        uint32 numPresentModes;

        // TODO: Maybe also take these into InitializeSwapchain and use different data structuring for swapchains
        vkGetPhysicalDeviceSurfaceFormatsKHR(gBackendVk.gpu.handle, _MainSwapchain().surface, &numFormats, nullptr);
        gBackendVk.swapchainInfo.numFormats = numFormats;
        gBackendVk.swapchainInfo.formats = Mem::AllocTyped<VkSurfaceFormatKHR>(numFormats, &gBackendVk.parentAlloc);
        vkGetPhysicalDeviceSurfaceFormatsKHR(gBackendVk.gpu.handle, _MainSwapchain().surface, &numFormats, gBackendVk.swapchainInfo.formats);

        vkGetPhysicalDeviceSurfacePresentModesKHR(gBackendVk.gpu.handle, _MainSwapchain().surface, &numPresentModes, nullptr);
        gBackendVk.swapchainInfo.numPresentModes = numPresentModes;
        gBackendVk.swapchainInfo.presentModes = Mem::AllocTyped<VkPresentModeKHR>(numPresentModes, &gBackendVk.parentAlloc);
        vkGetPhysicalDeviceSurfacePresentModesKHR(gBackendVk.gpu.handle, _MainSwapchain().surface, &numPresentModes, gBackendVk.swapchainInfo.presentModes);

        if (!_InitializeSwapchain(&_MainSwapchain(), Int2(App::GetFramebufferWidth(), App::GetFramebufferHeight())))
            return false;
    }""",
    """    // Swapchain and it's capabilities
    // We can only create this after device is created.
    if (!settings.graphics.headless) {
        _QuerySwapchainInfo(&_MainSwapchain());

        if (!_InitializeSwapchain(&_MainSwapchain(), Int2(App::GetFramebufferWidth(), App::GetFramebufferHeight())))
            return false;
    }""")

rep("""    Mem::Free(gBackendVk.swapchainInfo.formats, alloc);
    Mem::Free(gBackendVk.swapchainInfo.presentModes, alloc);
""", "")

# --- Resolve an optional handle: invalid means "the main swapchain"
rep("""static inline GfxBackendSwapchain& _MainSwapchain()
{
    return gBackendVk.swapchains.Data(gBackendVk.mainSwapchain);
}""",
    """static inline GfxBackendSwapchain& _MainSwapchain()
{
    return gBackendVk.swapchains.Data(gBackendVk.mainSwapchain);
}

// An invalid handle means the main swapchain, so existing single-window call sites need no change
static inline GfxBackendSwapchain& _GetSwapchain(GfxSwapchainHandle handle)
{
    return handle.IsValid() ? gBackendVk.swapchains.Data(handle) : _MainSwapchain();
}""")

# --- Public getters take an optional swapchain
rep("""GfxFormat GfxBackend::GetSwapchainFormat()
{
    return GfxFormat(_MainSwapchain().format.format);
}

Int2 GfxBackend::GetSwapchainExtent()
{
    return Int2(int(_MainSwapchain().extent.width), int(_MainSwapchain().extent.height));
}""",
    """GfxFormat GfxBackend::GetSwapchainFormat(GfxSwapchainHandle handle)
{
    return GfxFormat(_GetSwapchain(handle).format.format);
}

Int2 GfxBackend::GetSwapchainExtent(GfxSwapchainHandle handle)
{
    const GfxBackendSwapchain& swapchain = _GetSwapchain(handle);
    return Int2(int(swapchain.extent.width), int(swapchain.extent.height));
}

GfxSwapchainHandle GfxBackend::GetMainSwapchain()
{
    return gBackendVk.mainSwapchain;
}

// Secondary swapchains exist only to present ImGui viewports: no MSAA, no depth, no resolve
GfxSwapchainHandle GfxBackend::CreateSwapchain(void* windowHandle, Int2 size)
{
    ASSERT(windowHandle);
    ASSERT_MSG(!SettingsJunkyard::Get().graphics.headless, "Cannot create a swapchain in headless mode");

    GfxBackendSwapchain swapchain {};
    swapchain.surface = GfxBackend::_CreateWindowSurface(windowHandle);
    if (!swapchain.surface) {
        LOG_ERROR("Gfx: Creating window surface for secondary swapchain failed");
        return GfxSwapchainHandle();
    }

    GfxSwapchainHandle handle = gBackendVk.swapchains.Add(swapchain);
    GfxBackendSwapchain& sc = gBackendVk.swapchains.Data(handle);

    GfxBackend::_QuerySwapchainInfo(&sc);
    if (!GfxBackend::_InitializeSwapchain(&sc, size)) {
        LOG_ERROR("Gfx: Creating secondary swapchain failed");
        GfxBackend::_ReleaseSwapchain(&sc);
        vkDestroySurfaceKHR(gBackendVk.instance.handle, sc.surface, gBackendVk.vkAlloc);
        gBackendVk.swapchains.Remove(handle);
        return GfxSwapchainHandle();
    }

    return handle;
}

void GfxBackend::DestroySwapchain(GfxSwapchainHandle& handle)
{
    if (!handle.IsValid())
        return;
    ASSERT_MSG(handle != gBackendVk.mainSwapchain, "Main swapchain is owned by the backend and cannot be destroyed");

    // Presentation may still be in flight for this surface
    vkDeviceWaitIdle(gBackendVk.device);

    GfxBackendSwapchain& swapchain = gBackendVk.swapchains.Data(handle);
    GfxBackend::_ReleaseSwapchain(&swapchain);
    if (swapchain.surface)
        vkDestroySurfaceKHR(gBackendVk.instance.handle, swapchain.surface, gBackendVk.vkAlloc);

    gBackendVk.swapchains.Remove(handle);
    handle = GfxSwapchainHandle();
}

void GfxBackend::ResizeSwapchain(GfxSwapchainHandle handle, Int2 size)
{
    GfxBackendSwapchain& swapchain = _GetSwapchain(handle);
    vkDeviceWaitIdle(gBackendVk.device);
    GfxBackend::_ResizeSwapchain(&swapchain, size, SettingsJunkyard::Get().graphics.surfaceSRGB);
}""")

io.open(path, 'w', encoding='utf-8', newline='').write(s)
print('gfx step2 cpp ok')
