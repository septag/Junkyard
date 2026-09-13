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


# _ResizeSwapchain / _InitializeSwapchain already take the swapchain, the surface now rides along on it
rep("    static bool _ResizeSwapchain(GfxBackendSwapchain* swapchain, VkSurfaceKHR surface, Int2 size, bool forceSRGB)",
    "    static bool _ResizeSwapchain(GfxBackendSwapchain* swapchain, Int2 size, bool forceSRGB)")
rep("    static bool _InitializeSwapchain(GfxBackendSwapchain* swapchain, VkSurfaceKHR surface, Int2 size)",
    "    static bool _InitializeSwapchain(GfxBackendSwapchain* swapchain, Int2 size)")

# Registration happens before the surface exists, mirroring how the main window is registered in App::Run
rep("""    if (!_InitializeGPU(settings))
        return false;

    // Window surface
    if (!settings.graphics.headless) {
        gBackendVk.surface = _CreateWindowSurface(App::GetNativeWindowHandle());
        if (!gBackendVk.surface) {""",
    """    if (!_InitializeGPU(settings))
        return false;

    // Register the main swapchain up front so _MainSwapchain() is valid everywhere below.
    // In headless mode it stays a shell with a null surface and no VkSwapchainKHR
    {
        GfxBackendSwapchain mainSwapchain {};
        mainSwapchain.isMain = true;
        gBackendVk.swapchains.SetAllocator(&gBackendVk.parentAlloc);
        gBackendVk.mainSwapchain = gBackendVk.swapchains.Add(mainSwapchain);
    }

    // Window surface
    if (!settings.graphics.headless) {
        _MainSwapchain().surface = _CreateWindowSurface(App::GetNativeWindowHandle());
        if (!_MainSwapchain().surface) {""")

rep("        if (!_InitializeSwapchain(&gBackendVk.swapchain, gBackendVk.surface, Int2(App::GetFramebufferWidth(), App::GetFramebufferHeight())))",
    "        if (!_InitializeSwapchain(&_MainSwapchain(), Int2(App::GetFramebufferWidth(), App::GetFramebufferHeight())))")

# Resize call sites
rep("""            GfxBackend::_ResizeSwapchain(&gBackendVk.swapchain, gBackendVk.surface,
                                         Int2(App::GetFramebufferWidth(), App::GetFramebufferHeight()),
                                         SettingsJunkyard::Get().graphics.surfaceSRGB);""",
    """            GfxBackend::_ResizeSwapchain(&_MainSwapchain(),
                                         Int2(App::GetFramebufferWidth(), App::GetFramebufferHeight()),
                                         SettingsJunkyard::Get().graphics.surfaceSRGB);""")

rep("""        GfxBackend::_ResizeSwapchain(&gBackendVk.swapchain, gBackendVk.surface,
                                     Int2(App::GetFramebufferWidth(), App::GetFramebufferHeight()),
                                     SettingsJunkyard::Get().graphics.surfaceSRGB);""",
    """        GfxBackend::_ResizeSwapchain(&_MainSwapchain(),
                                     Int2(App::GetFramebufferWidth(), App::GetFramebufferHeight()),
                                     SettingsJunkyard::Get().graphics.surfaceSRGB);""")

# Release: free the pool after the swapchain and surface are gone
rep("    _ReleaseSwapchain(&gBackendVk.swapchain);", "    _ReleaseSwapchain(&_MainSwapchain());")
rep("""    if (gBackendVk.surface)
        vkDestroySurfaceKHR(gBackendVk.instance.handle, gBackendVk.surface, gBackendVk.vkAlloc);""",
    """    if (_MainSwapchain().surface)
        vkDestroySurfaceKHR(gBackendVk.instance.handle, _MainSwapchain().surface, gBackendVk.vkAlloc);
    gBackendVk.swapchains.Free();
    gBackendVk.mainSwapchain = GfxSwapchainHandle();""")

# Everything else is a straight swap onto the main swapchain
plain = [
    ("gBackendVk.swapchain.GetImageView()", "_MainSwapchain().GetImageView()", 2),
    ("gBackendVk.swapchain.GetImage()", "_MainSwapchain().GetImage()", 4),
    ("gBackendVk.swapchain.GetImageState()", "_MainSwapchain().GetImageState()", 4),
    ("gBackendVk.swapchain.GetSwapchainRenderFinishedSemaphore()", "_MainSwapchain().GetSwapchainRenderFinishedSemaphore()", 2),
    ("gBackendVk.swapchain.GetSwapchainReadySemaphore(gBackendVk.queueMan.GetFrameIndex())",
     "_MainSwapchain().GetSwapchainReadySemaphore(gBackendVk.queueMan.GetFrameIndex())", 1),
    ("GfxBackendSwapchain& swapchain = gBackendVk.swapchain;", "GfxBackendSwapchain& swapchain = _MainSwapchain();", 1),
    ("gBackendVk.swapchain.extent.width", "_MainSwapchain().extent.width", 1),
    ("gBackendVk.swapchain.extent.height", "_MainSwapchain().extent.height", 1),
    ("&gBackendVk.swapchain.handle", "&_MainSwapchain().handle", 1),
    ("&gBackendVk.swapchain.imageIndex", "&_MainSwapchain().imageIndex", 1),
    ("gBackendVk.swapchain.resize = true;", "_MainSwapchain().resize = true;", 1),
    ("if (gBackendVk.swapchain.resize) {", "if (_MainSwapchain().resize) {", 1),
    ("gBackendVk.gpu.handle, gBackendVk.surface", "gBackendVk.gpu.handle, _MainSwapchain().surface", 6),
    ("if (gBackendVk.surface) {", "if (_MainSwapchain().surface) {", 1),
    ("gpu.handle, i, gBackendVk.surface", "gpu.handle, i, _MainSwapchain().surface", 1),
]
for a, b, n in plain:
    cnt = s.count(a)
    assert cnt >= 1, ('no match', a)
    s = s.replace(a, b)

io.open(path, 'w', encoding='utf-8', newline='').write(s)
print('gfx step1 part B ok')
print('remaining gBackendVk.swapchain/surface refs:',
      len(re.findall(r'gBackendVk\.(swapchain\b|surface\b)', s)))
