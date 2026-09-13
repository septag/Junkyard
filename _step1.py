import io
import re

CRLF = '\r\n'


class Patcher:
    def __init__(self, path):
        self.path = path
        self.s = io.open(path, encoding='utf-8', newline='').read()

    def rep(self, a, b, n=1):
        # Tolerates trailing-whitespace differences at line ends.
        pat = re.compile(r'[ \t]*\r\n'.join(re.escape(x) for x in a.split('\n')))
        b = b.replace('\n', CRLF)
        found = len(pat.findall(self.s))
        assert found == n, (self.path, found, n, a[:110])
        self.s = pat.sub(lambda m: b, self.s, count=n)

    def save(self):
        io.open(self.path, 'w', encoding='utf-8', newline='').write(self.s)


# ===========================================================================
# Application.h
# ===========================================================================
h = Patcher('code/Common/Application.h')

h.rep("""enum class AppMouseCursor
{""",
      """// Handle to a platform window. The main window is created by App::Run and can be fetched with App::GetMainWindow.
// Note: This is deliberately not DEFINE_HANDLE, because Core/Pools.h hides Handle<> from ObjectiveC translation
//       units (Apple's MacTypes.h declares its own 'Handle') and ApplicationMac.mm includes this header.
struct AppWindowHandle
{
    uint32 mId = 0;

    bool IsValid() const { return mId != 0; }
    bool operator==(const AppWindowHandle& v) const { return mId == v.mId; }
    bool operator!=(const AppWindowHandle& v) const { return mId != v.mId; }
};

enum class AppMouseCursor
{""")

h.rep("""    API uint16 GetWindowWidth();""",
      """    API AppWindowHandle GetMainWindow();

    API uint16 GetWindowWidth();""")

h.save()

# ===========================================================================
# ApplicationWin.cpp
# ===========================================================================
w = Patcher('code/Common/ApplicationWin.cpp')

w.rep("""#include "../Core/Arrays.h"
#include "../Core/Allocators.h\"""",
      """#include "../Core/Arrays.h"
#include "../Core/Allocators.h"
#include "../Core/Pools.h\"""")

# --- Per-window state split out of the global app state
w.rep("""struct AppWindowsState
{
    bool valid;
    char name[32];
    // Window dimensions are logical and does not include DPI scaling. They also present Client area, excluding the border
    uint16 windowWidth;
    uint16 windowHeight;
    // Framebuffer dimensions equals window dimensions on HighDPI, but scaled down on non-HighDPI
    uint16 framebufferWidth;
    uint16 framebufferHeight;
    char windowTitle[128];
    fl32 mouseX;
    fl32 mouseY;
    AppDesc desc;""",
      """DEFINE_HANDLE(AppWindowInternalHandle);

// Per-window state. The main window is registered by Run() before it actually gets created,
// so `hwnd` stays null until _CreateMainWindow fills it in.
struct AppWindow
{
    HWND hwnd;
    // Window dimensions are logical and does not include DPI scaling. They also present Client area, excluding the border
    uint16 windowWidth;
    uint16 windowHeight;
    // Framebuffer dimensions equals window dimensions on HighDPI, but scaled down on non-HighDPI
    uint16 framebufferWidth;
    uint16 framebufferHeight;
    char title[128];
    fl32 mouseX;
    fl32 mouseY;
    HMONITOR monitor;
    float windowScale;
    float contentScale;
    float mouseScale;
    float dpiScale;
    bool iconified;
    bool mouseTracked;
    bool isMain;
};

struct AppWindowsState
{
    bool valid;
    char name[32];
    AppDesc desc;""")

w.rep("""    HWND hwnd;
    uint16 displayWidth;""",
      """    HandlePool<AppWindowInternalHandle, AppWindow> windows;
    AppWindowHandle mainWindow;
    uint16 displayWidth;""")

w.rep("""    HMONITOR wndMonitor;
    RECT mainRect;""",
      """    RECT mainRect;""")

w.rep("""    float dpiScale;
    float windowScale;
    float contentScale;
    float mouseScale;

    bool quitFromConsole;
    bool windowModified;
    bool mouseTracked;
    bool dpiAware;
    bool clipboardEnabled;
    bool iconified;
    bool keysPressed[APP_MAX_KEY_CODES];""",
      """    bool quitFromConsole;
    bool windowModified;
    bool dpiAware;
    bool clipboardEnabled;
    bool keysPressed[APP_MAX_KEY_CODES];""")

# --- Registry helpers
w.rep("""static AppWindowsState gApp;
""",
      """static AppWindowsState gApp;

namespace App
{
    static inline AppWindowInternalHandle _ToInternal(AppWindowHandle handle)
    {
        return AppWindowInternalHandle(handle.mId);
    }

    static inline AppWindowHandle _ToPublic(AppWindowInternalHandle handle)
    {
        return AppWindowHandle { uint32(handle) };
    }

    static AppWindow& _GetWindow(AppWindowHandle handle)
    {
        ASSERT(handle.IsValid());
        return gApp.windows.Data(_ToInternal(handle));
    }

    // Every call site that predates multi-window support resolves to the main window
    static AppWindow& _MainWindow()
    {
        return _GetWindow(gApp.mainWindow);
    }

    static AppWindowHandle _RegisterWindow(bool isMain)
    {
        AppWindow wnd {};
        wnd.isMain = isMain;
        wnd.windowScale = 1.0f;
        wnd.contentScale = 1.0f;
        wnd.mouseScale = 1.0f;
        wnd.dpiScale = 1.0f;
        return _ToPublic(gApp.windows.Add(wnd));
    }
} // App
""")

w.save()
print('step1 part A ok')
