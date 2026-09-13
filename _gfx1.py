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


# --- 1) The surface now belongs to the swapchain that presents to it
rep("""struct GfxBackendSwapchain
{
    struct ImageState
    {""",
    """struct GfxBackendSwapchain
{
    struct ImageState
    {""")

rep("""    uint32 imageIndex;
    uint32 numImages;
    VkSwapchainKHR handle;""",
    """    VkSurfaceKHR surface;
    uint32 imageIndex;
    uint32 numImages;
    VkSwapchainKHR handle;
    bool isMain;""")

# --- 2) Swapchains live in a pool. Only the main one exists for now
rep("""    VkDevice device;
    VkSurfaceKHR surface;
    GfxBackendSwapchainInfo swapchainInfo;
    GfxBackendSwapchain swapchain;""",
    """    VkDevice device;
    GfxBackendSwapchainInfo swapchainInfo;
    HandlePool<GfxSwapchainHandle, GfxBackendSwapchain> swapchains;
    GfxSwapchainHandle mainSwapchain;""")

rep("static GfxBackendVk gBackendVk;",
    """static GfxBackendVk gBackendVk;

// The swapchain that presents to the main window. Registered at the very start of Initialize so that every
// call site predating multi-swapchain support resolves through here, including in headless mode where
// `surface` simply stays null
static inline GfxBackendSwapchain& _MainSwapchain()
{
    return gBackendVk.swapchains.Data(gBackendVk.mainSwapchain);
}""")

io.open(path, 'w', encoding='utf-8', newline='').write(s)
print('gfx step1 part A ok')
