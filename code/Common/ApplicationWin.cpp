#include "Application.h"

#if PLATFORM_WINDOWS

#include "VirtualFS.h"
#include "RemoteServices.h"
#include "JunkyardSettings.h"

#include "../Core/StringUtil.h"
#include "../Core/System.h"
#include "../Core/Settings.h"
#include "../Core/IncludeWin.h"
#include "../Core/Log.h"
#include "../Core/Debug.h"
#include "../Core/Arrays.h"
#include "../Core/Allocators.h"

#include "../Core/External/mgustavsson/ini.h"

// from <windowsx.h>
#ifndef GET_X_LPARAM
    #define GET_X_LPARAM(lp)                        ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
    #define GET_Y_LPARAM(lp)                        ((int)(short)HIWORD(lp))
#endif

inline constexpr uint32 kMaxKeycodes = 512;

#ifndef WM_MOUSEHWHEEL
    #define WM_MOUSEHWHEEL (0x020E)
#endif

#ifndef DPI_ENUMS_DECLARED
    typedef enum PROCESS_DPI_AWARENESS
    {
        PROCESS_DPI_UNAWARE = 0,
        PROCESS_SYSTEM_DPI_AWARE = 1,
        PROCESS_PER_MONITOR_DPI_AWARE = 2
    } PROCESS_DPI_AWARENESS;
            
    typedef enum MONITOR_DPI_TYPE {
        MDT_EFFECTIVE_DPI = 0,
        MDT_ANGULAR_DPI = 1,
        MDT_RAW_DPI = 2,
        MDT_DEFAULT = MDT_EFFECTIVE_DPI
    } MONITOR_DPI_TYPE;
#endif // DPI_ENUMS_DECLARED

struct AppEventCallbackPair
{
    appOnEventCallback callback;
    void*              userData;
};

struct AppWindowsState
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
    uint64 frameCount;
    fl32 mouseX;
    fl32 mouseY;
    AppEvent ev;
    AppDesc desc;
    InputKeycode keycodes[kMaxKeycodes];
    char* clipboard;
    Array<AppEventCallbackPair> eventCallbacks;
    AppMouseCursor mouseCursor;

    HWND hwnd;
    uint16 displayWidth;
    uint16 displayHeight;
    uint16 displayRefreshRate;
    HMONITOR wndMonitor;
    RECT mainRect;          // Actual window dimensions that is serialized. Different than windowWidth/windowHeight above
    RECT consoleRect;       // Actual console window dimensions

    HANDLE hStdin;
    HANDLE hStdOut;

    float dpiScale;
    float windowScale;
    float contentScale;
    float mouseScale;

    bool windowModified;
    bool mouseTracked;
    bool firstFrame;
    bool initCalled;
    bool cleanupCalled;
    bool quitRequested;
    bool quitOrdered;
    bool eventConsumed;
    bool dpiAware;
    bool inCreateWindow;
    bool clipboardEnabled;
    bool iconified;
    bool keysPressed[kMaxKeycodes];
};

static AppWindowsState gApp;

static void appWinInitKeyTable()
{
    gApp.keycodes[0x00B] = InputKeycode::NUM0;
    gApp.keycodes[0x002] = InputKeycode::NUM1;
    gApp.keycodes[0x003] = InputKeycode::NUM2;
    gApp.keycodes[0x004] = InputKeycode::NUM3;
    gApp.keycodes[0x005] = InputKeycode::NUM4;
    gApp.keycodes[0x006] = InputKeycode::NUM5;
    gApp.keycodes[0x007] = InputKeycode::NUM6;
    gApp.keycodes[0x008] = InputKeycode::NUM7;
    gApp.keycodes[0x009] = InputKeycode::NUM8;
    gApp.keycodes[0x00A] = InputKeycode::NUM9;
    gApp.keycodes[0x01E] = InputKeycode::A;
    gApp.keycodes[0x030] = InputKeycode::B;
    gApp.keycodes[0x02E] = InputKeycode::C;
    gApp.keycodes[0x020] = InputKeycode::D;
    gApp.keycodes[0x012] = InputKeycode::E;
    gApp.keycodes[0x021] = InputKeycode::F;
    gApp.keycodes[0x022] = InputKeycode::G;
    gApp.keycodes[0x023] = InputKeycode::H;
    gApp.keycodes[0x017] = InputKeycode::I;
    gApp.keycodes[0x024] = InputKeycode::J;
    gApp.keycodes[0x025] = InputKeycode::K;
    gApp.keycodes[0x026] = InputKeycode::L;
    gApp.keycodes[0x032] = InputKeycode::M;
    gApp.keycodes[0x031] = InputKeycode::N;
    gApp.keycodes[0x018] = InputKeycode::O;
    gApp.keycodes[0x019] = InputKeycode::P;
    gApp.keycodes[0x010] = InputKeycode::Q;
    gApp.keycodes[0x013] = InputKeycode::R;
    gApp.keycodes[0x01F] = InputKeycode::S;
    gApp.keycodes[0x014] = InputKeycode::T;
    gApp.keycodes[0x016] = InputKeycode::U;
    gApp.keycodes[0x02F] = InputKeycode::V;
    gApp.keycodes[0x011] = InputKeycode::W;
    gApp.keycodes[0x02D] = InputKeycode::X;
    gApp.keycodes[0x015] = InputKeycode::Y;
    gApp.keycodes[0x02C] = InputKeycode::Z;
    gApp.keycodes[0x028] = InputKeycode::Apostrophe;
    gApp.keycodes[0x02B] = InputKeycode::Backslash;
    gApp.keycodes[0x033] = InputKeycode::Comma;
    gApp.keycodes[0x00D] = InputKeycode::Equal;
    gApp.keycodes[0x029] = InputKeycode::GraveAccent;
    gApp.keycodes[0x01A] = InputKeycode::LeftBracket;
    gApp.keycodes[0x00C] = InputKeycode::Minus;
    gApp.keycodes[0x034] = InputKeycode::Period;
    gApp.keycodes[0x01B] = InputKeycode::RightBracket;
    gApp.keycodes[0x027] = InputKeycode::Semicolon;
    gApp.keycodes[0x035] = InputKeycode::Slash;
    gApp.keycodes[0x056] = InputKeycode::World2;
    gApp.keycodes[0x00E] = InputKeycode::Backspace;
    gApp.keycodes[0x153] = InputKeycode::Delete;
    gApp.keycodes[0x14F] = InputKeycode::End;
    gApp.keycodes[0x01C] = InputKeycode::Enter;
    gApp.keycodes[0x001] = InputKeycode::Escape;
    gApp.keycodes[0x147] = InputKeycode::Home;
    gApp.keycodes[0x152] = InputKeycode::Insert;
    gApp.keycodes[0x15D] = InputKeycode::Menu;
    gApp.keycodes[0x151] = InputKeycode::PageDown;
    gApp.keycodes[0x149] = InputKeycode::PageUp;
    gApp.keycodes[0x045] = InputKeycode::Pause;
    gApp.keycodes[0x146] = InputKeycode::Pause;
    gApp.keycodes[0x039] = InputKeycode::Space;
    gApp.keycodes[0x00F] = InputKeycode::Tab;
    gApp.keycodes[0x03A] = InputKeycode::CapsLock;
    gApp.keycodes[0x145] = InputKeycode::NumLock;
    gApp.keycodes[0x046] = InputKeycode::ScrollLock;
    gApp.keycodes[0x03B] = InputKeycode::F1;
    gApp.keycodes[0x03C] = InputKeycode::F2;
    gApp.keycodes[0x03D] = InputKeycode::F3;
    gApp.keycodes[0x03E] = InputKeycode::F4;
    gApp.keycodes[0x03F] = InputKeycode::F5;
    gApp.keycodes[0x040] = InputKeycode::F6;
    gApp.keycodes[0x041] = InputKeycode::F7;
    gApp.keycodes[0x042] = InputKeycode::F8;
    gApp.keycodes[0x043] = InputKeycode::F9;
    gApp.keycodes[0x044] = InputKeycode::F10;
    gApp.keycodes[0x057] = InputKeycode::F11;
    gApp.keycodes[0x058] = InputKeycode::F12;
    gApp.keycodes[0x064] = InputKeycode::F13;
    gApp.keycodes[0x065] = InputKeycode::F14;
    gApp.keycodes[0x066] = InputKeycode::F15;
    gApp.keycodes[0x067] = InputKeycode::F16;
    gApp.keycodes[0x068] = InputKeycode::F17;
    gApp.keycodes[0x069] = InputKeycode::F18;
    gApp.keycodes[0x06A] = InputKeycode::F19;
    gApp.keycodes[0x06B] = InputKeycode::F20;
    gApp.keycodes[0x06C] = InputKeycode::F21;
    gApp.keycodes[0x06D] = InputKeycode::F22;
    gApp.keycodes[0x06E] = InputKeycode::F23;
    gApp.keycodes[0x076] = InputKeycode::F24;
    gApp.keycodes[0x038] = InputKeycode::LeftAlt;
    gApp.keycodes[0x01D] = InputKeycode::LeftControl;
    gApp.keycodes[0x02A] = InputKeycode::LeftShift;
    gApp.keycodes[0x15B] = InputKeycode::LeftSuper;
    gApp.keycodes[0x137] = InputKeycode::PrintScreen;
    gApp.keycodes[0x138] = InputKeycode::RightAlt;
    gApp.keycodes[0x11D] = InputKeycode::RightControl;
    gApp.keycodes[0x036] = InputKeycode::RightShift;
    gApp.keycodes[0x15C] = InputKeycode::RightSuper;
    gApp.keycodes[0x150] = InputKeycode::Down;
    gApp.keycodes[0x14B] = InputKeycode::Left;
    gApp.keycodes[0x14D] = InputKeycode::Right;
    gApp.keycodes[0x148] = InputKeycode::Up;
    gApp.keycodes[0x052] = InputKeycode::KP0;
    gApp.keycodes[0x04F] = InputKeycode::KP1;
    gApp.keycodes[0x050] = InputKeycode::KP2;
    gApp.keycodes[0x051] = InputKeycode::KP3;
    gApp.keycodes[0x04B] = InputKeycode::KP4;
    gApp.keycodes[0x04C] = InputKeycode::KP5;
    gApp.keycodes[0x04D] = InputKeycode::KP6;
    gApp.keycodes[0x047] = InputKeycode::KP7;
    gApp.keycodes[0x048] = InputKeycode::KP8;
    gApp.keycodes[0x049] = InputKeycode::KP9;
    gApp.keycodes[0x04E] = InputKeycode::KPAdd;
    gApp.keycodes[0x053] = InputKeycode::KPDecimal;
    gApp.keycodes[0x135] = InputKeycode::KPDivide;
    gApp.keycodes[0x11C] = InputKeycode::KPEnter;
    gApp.keycodes[0x037] = InputKeycode::KPMultiply;
    gApp.keycodes[0x04A] = InputKeycode::KPSubtract;
}

static void appWinLoadInitRects()
{
    ini_t* windowsIni = nullptr;
    char iniFilename[64];
    strPrintFmt(iniFilename, sizeof(iniFilename), "%s_windows.ini", appGetName());

    Blob data = vfsReadFile(iniFilename, VfsFlags::TextFile|VfsFlags::AbsolutePath);
    if (data.IsValid()) {
        windowsIni = ini_load((const char*)data.Data(), memDefaultAlloc());
        data.Free();
    }
    
    auto GetWindowData = [](ini_t* ini, const char* name, RECT* rc) {
        int id = ini_find_section(ini, name, strLen(name));
        if (id != -1) {
           int topId = ini_find_property(ini, id, "top", 0);
           int bottomId = ini_find_property(ini, id, "bottom", 0);
           int leftId = ini_find_property(ini, id, "left", 0);
           int rightId = ini_find_property(ini, id, "right", 0);

           if (topId != -1)
               rc->top = strToInt(ini_property_value(ini, id, topId));
           if (bottomId != -1)
               rc->bottom = strToInt(ini_property_value(ini, id, bottomId));
           if (leftId != -1)
               rc->left = strToInt(ini_property_value(ini, id, leftId));
           if (rightId != -1)
               rc->right = strToInt(ini_property_value(ini, id, rightId));
        }
        return rc;
    };

    gApp.mainRect = RECT {0, 0, -1, -1};
    gApp.consoleRect = RECT {1, 1, -1, -1};     // empty (leave it as it is)
    if (windowsIni) {
        GetWindowData(windowsIni, "Main", &gApp.mainRect);
        GetWindowData(windowsIni, "Console", &gApp.consoleRect);
        ini_destroy(windowsIni);
    }
}

static void appWinSaveInitRects()
{
    auto PutWindowData = [](ini_t* ini, const char* name, const RECT& rc) {
        int id = ini_section_add(ini, name, strLen(name));
        char value[32];
        strPrintFmt(value, sizeof(value), "%d", rc.top);
        ini_property_add(ini, id, "top", 0, value, strLen(value));

        strPrintFmt(value, sizeof(value), "%d", rc.bottom);
        ini_property_add(ini, id, "bottom", 0, value, strLen(value));

        strPrintFmt(value, sizeof(value), "%d", rc.left);
        ini_property_add(ini, id, "left", 0, value, strLen(value));

        strPrintFmt(value, sizeof(value), "%d", rc.right);
        ini_property_add(ini, id, "right", 0, value, strLen(value));
    };

    if (gApp.windowModified && gApp.hwnd) {
        ini_t* windowsIni = ini_create(memDefaultAlloc());
        char iniFilename[64];
        strPrintFmt(iniFilename, sizeof(iniFilename), "%s_windows.ini", appGetName());

        RECT mainRect, consoleRect;
        if (GetWindowRect(gApp.hwnd, &mainRect))
            PutWindowData(windowsIni, "Main", mainRect);
        if (GetWindowRect(GetConsoleWindow(), &consoleRect))
            PutWindowData(windowsIni, "Console", consoleRect);

        int size = ini_save(windowsIni, nullptr, 0);
        if (size > 0) {
            char* data = memAllocTyped<char>(size);
            ini_save(windowsIni, data, size);
    
            File f;
            if (f.Open(iniFilename, FileOpenFlags::Write)) {
                f.Write<char>(data, static_cast<size_t>(size));
                f.Close();
            }
            memFree(data);
        }
    
        ini_destroy(windowsIni);
    }
}

// Returns true if window monitor has changed
static bool appWinUpdateDisplayInfo()
{
    HMONITOR hm = gApp.hwnd ? 
        MonitorFromWindow(gApp.hwnd, MONITOR_DEFAULTTONEAREST) : 
        MonitorFromPoint({ 1, 1 }, MONITOR_DEFAULTTONEAREST);
    if (hm == gApp.wndMonitor)
        return false;

    gApp.wndMonitor = hm;

    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*);
    GetDpiForMonitorFn GetDpiForMonitor = nullptr;
    HINSTANCE shcore = LoadLibraryA("shcore.dll");
    if (shcore)
        GetDpiForMonitor = (GetDpiForMonitorFn)GetProcAddress(shcore, "GetDpiForMonitor");

    // Dpi settings
    if (GetDpiForMonitor) {
        UINT dpix, dpiy;
        [[maybe_unused]] HRESULT hr = GetDpiForMonitor(hm, MDT_EFFECTIVE_DPI, &dpix, &dpiy);
        ASSERT(SUCCEEDED(hr));
        gApp.windowScale = static_cast<float>(dpix) / 96.0f;
    }
    else {
        gApp.windowScale = 1.0f;
    }
    
    if (gApp.desc.highDPI) {
        gApp.contentScale = gApp.windowScale;
        gApp.mouseScale = 1.0f / gApp.windowScale;
    }
    else {
        gApp.contentScale = 1.0f;
        gApp.mouseScale = 1.0f / gApp.windowScale;
    }
    
    gApp.dpiScale = gApp.contentScale;

    // Display settings
    MONITORINFOEX monitorInfo { sizeof(MONITORINFOEX) };
    GetMonitorInfoA(hm, &monitorInfo);
    DEVMODEA mode { sizeof(DEVMODEA) };
    EnumDisplaySettingsA(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &mode);
    gApp.displayWidth = static_cast<uint16>(mode.dmPelsWidth);
    gApp.displayHeight = static_cast<uint16>(mode.dmPelsHeight);
    gApp.displayRefreshRate = static_cast<uint16>(mode.dmDisplayFrequency);
    
    if (shcore)
        FreeLibrary(shcore);

    return true;
}

static void appWinInitDpi()
{
    using SetProcessDpiAwareFn = BOOL(WINAPI*)(void);
    using SetProcessDpiAwarenessFn = HRESULT(WINAPI*)(PROCESS_DPI_AWARENESS);
    
    SetProcessDpiAwareFn SetProcessDpiAware = nullptr;
    SetProcessDpiAwarenessFn SetProcessDpiAwareness = nullptr;
    
    HINSTANCE user32 = LoadLibraryA("user32.dll");
    if (user32)
        SetProcessDpiAware = (SetProcessDpiAwareFn)GetProcAddress(user32, "SetProcessDPIAware");
    
    HINSTANCE shcore = LoadLibraryA("shcore.dll");
    if (shcore)
        SetProcessDpiAwareness = (SetProcessDpiAwarenessFn)GetProcAddress(shcore, "SetProcessDpiAwareness");
    
    if (SetProcessDpiAwareness) {
        // if the app didn't request HighDPI rendering, let Windows do the upscaling
        PROCESS_DPI_AWARENESS processDpiAwareness = PROCESS_SYSTEM_DPI_AWARE;
        gApp.dpiAware = true;
        if (!gApp.desc.highDPI) {
            processDpiAwareness = PROCESS_DPI_UNAWARE;
            gApp.dpiAware = false;
        }
        SetProcessDpiAwareness(processDpiAwareness);
    } else if (SetProcessDpiAware) {
        SetProcessDpiAware();
        gApp.dpiAware = true;
    }

    appWinUpdateDisplayInfo();

    if (user32)
        FreeLibrary(user32);
    
    if (shcore)
        FreeLibrary(shcore);
}

bool appSetClipboardString(const char* str)
{
    if (!gApp.clipboardEnabled) {
        return false;
    }

    ASSERT(str);
    ASSERT(gApp.hwnd);
    ASSERT(gApp.desc.clipboardSizeBytes > 0);
    
    wchar_t* wcharBuff = 0;
    const size_t wcharBuffSize = gApp.desc.clipboardSizeBytes * sizeof(wchar_t);
    HANDLE object = GlobalAlloc(GMEM_MOVEABLE, wcharBuffSize);
    if (!object)
        goto error;

    wcharBuff = (wchar_t*)GlobalLock(object);
    if (!wcharBuff)
        goto error;
    if (!strUt8ToWide(str, wcharBuff, wcharBuffSize))
        goto error;

    GlobalUnlock(wcharBuff);
    wcharBuff = 0;
    if (!OpenClipboard(gApp.hwnd)) {
        goto error;
    }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, object);
    CloseClipboard();

    strCopy(gApp.clipboard, (uint32)gApp.desc.clipboardSizeBytes, str);
    return true;
    
    error:
        if (wcharBuff)
            GlobalUnlock(object);
        if (object) 
            GlobalFree(object);
        return false;
}

static inline bool appWinEventsEnabled()
{
    // only send events when an event callback is set, and the init function was called
    return gApp.desc.callbacks && gApp.initCalled;
}

static bool appWinCallEvent(const AppEvent& ev)
{
    if (!gApp.cleanupCalled) {
        if (gApp.desc.callbacks)
            gApp.desc.callbacks->OnEvent(ev);

        // Call extra registered event callbacks
        for (auto c : gApp.eventCallbacks)
            c.callback(ev, c.userData);
    }
    if (gApp.eventConsumed) {
        gApp.eventConsumed = false;
        return true;
    }
    else {
        return false;
    }
}

static void appWinInitEvent(AppEventType type)
{
    memset(&gApp.ev, 0, sizeof(gApp.ev));
    gApp.ev.type = type;
    gApp.ev.mouseButton = InputMouseButton::Invalid;
    gApp.ev.windowWidth = gApp.windowWidth;
    gApp.ev.windowHeight = gApp.windowHeight;
    gApp.ev.framebufferWidth = gApp.framebufferWidth;
    gApp.ev.framebufferHeight = gApp.framebufferHeight;
}


static void appWinDispatchEvent(AppEventType type)
{
    if (appWinEventsEnabled()) {
        appWinInitEvent(type);
        appWinCallEvent(gApp.ev);
    }
}

InputKeyModifiers appGetKeyMods()
{
    InputKeyModifiers mods = InputKeyModifiers::None;
    if (GetKeyState(VK_SHIFT) & (1<<15))
        mods |= InputKeyModifiers::Shift;
    if (GetKeyState(VK_CONTROL) & (1<<15))
        mods |= InputKeyModifiers::Ctrl;
    if (GetKeyState(VK_MENU) & (1<<15))
        mods |= InputKeyModifiers::Alt;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & (1<<15))
        mods |= InputKeyModifiers::Super;
    return mods;
}

static void appWinDispatchMouseEvent(AppEventType type, InputMouseButton btn)
{
    if (appWinEventsEnabled()) {
        appWinInitEvent(type);
        gApp.ev.keyMods = appGetKeyMods();
        gApp.ev.mouseButton = btn;
        gApp.ev.mouseX = gApp.mouseX;
        gApp.ev.mouseY = gApp.mouseY;
        appWinCallEvent(gApp.ev);
    }
}

static void appWinDispatchScrollEvent(fl32 x, fl32 y)
{
    if (appWinEventsEnabled()) {
        appWinInitEvent(AppEventType::MouseScroll);
        gApp.ev.keyMods = appGetKeyMods();
        gApp.ev.scrollX = -x / 30.0f;
        gApp.ev.scrollY = y / 30.0f;
        appWinCallEvent(gApp.ev);
    }
}

static void appWinDispatchKeyEvent(AppEventType type, int vk, bool repeat)
{
    if (appWinEventsEnabled() && (vk < kMaxKeycodes)) {
        appWinInitEvent(type);
        gApp.ev.keyMods = appGetKeyMods();
        gApp.ev.keycode = gApp.keycodes[vk];
        gApp.ev.keyRepeat = repeat;
        gApp.keysPressed[uint32(gApp.keycodes[vk])] = (type == AppEventType::KeyDown);

        appWinCallEvent(gApp.ev);
        // check if a CLIPBOARDPASTED event must be sent too
        if (gApp.clipboardEnabled &&
            (type == AppEventType::KeyDown) &&
            (gApp.ev.keyMods == InputKeyModifiers::Ctrl) &&
            (gApp.ev.keycode == InputKeycode::V)) 
        {
            appWinInitEvent(AppEventType::ClipboardPasted);
            appWinCallEvent(gApp.ev);
        }
    }
}

static void appWinDispatchCharEvent(uint32 c, bool repeat)
{
    if (appWinEventsEnabled() && (c >= 32)) {
        appWinInitEvent(AppEventType::Char);
        gApp.ev.keyMods = appGetKeyMods();
        gApp.ev.charcode = c;
        gApp.ev.keyRepeat = repeat;
        appWinCallEvent(gApp.ev);
    }
}

static LRESULT CALLBACK appWinProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // TODO: refresh rendering during resize with a WM_TIMER event
    if (!gApp.inCreateWindow) {
        switch (uMsg) {
            case WM_CLOSE:
                // only give user a chance to intervene when sapp_quit() wasn't already called 
                if (!gApp.quitOrdered) {
                    // if window should be closed and event handling is enabled, give user code
                    // a change to intervene via sapp_cancel_quit()
                    gApp.quitRequested = true;
                    // if user code hasn't intervened, quit the app
                    if (gApp.quitRequested)
                        gApp.quitOrdered = true;
                }
                if (gApp.quitOrdered) {
                    PostQuitMessage(0);
                }
                return 0;
            case WM_SYSCOMMAND:
                switch (wParam & 0xFFF0) {
                    case SC_SCREENSAVE:
                    case SC_MONITORPOWER:
                        if (gApp.desc.fullscreen) {
                            // disable screen saver and blanking in fullscreen mode 
                            return 0;
                        }
                        break;
                    case SC_KEYMENU:
                        // user trying to access menu via ALT
                        return 0;
            }
                break;
            case WM_ERASEBKGND:
                return 1;
            case WM_SIZE:
            {
                const bool iconified = wParam == SIZE_MINIMIZED;
                if (iconified != gApp.iconified) {
                    gApp.iconified = iconified;
                    if (iconified) {
                        appWinDispatchEvent(AppEventType::Iconified);
                    }
                    else {
                        appWinDispatchEvent(AppEventType::Restored);
                    }
                }
            }
            break;
            case WM_MOVE:
                if (appWinUpdateDisplayInfo())
                    appWinDispatchEvent(AppEventType::DisplayUpdated);
                appWinDispatchEvent(AppEventType::Moved);
                gApp.windowModified = true;
                break;
            case WM_SETCURSOR:
                if (gApp.desc.userCursor) {
                    if (LOWORD(lParam) == HTCLIENT) {
                        appWinDispatchEvent(AppEventType::UpdateCursor);
                        return 1;
                    }
                }
                break;
            case WM_LBUTTONDOWN:
                appWinDispatchMouseEvent(AppEventType::MouseDown, InputMouseButton::Left);
                break;
            case WM_RBUTTONDOWN:
                appWinDispatchMouseEvent(AppEventType::MouseDown, InputMouseButton::Right);
                break;
            case WM_MBUTTONDOWN:
                appWinDispatchMouseEvent(AppEventType::MouseDown, InputMouseButton::Middle);
                break;
            case WM_LBUTTONUP:
                appWinDispatchMouseEvent(AppEventType::MouseUp, InputMouseButton::Left);
                break;
            case WM_RBUTTONUP:
                appWinDispatchMouseEvent(AppEventType::MouseUp, InputMouseButton::Right);
                break;
            case WM_MBUTTONUP:
                appWinDispatchMouseEvent(AppEventType::MouseUp, InputMouseButton::Middle);
                break;
            case WM_MOUSEMOVE:
                gApp.mouseX = (fl32)GET_X_LPARAM(lParam) * gApp.mouseScale;
                gApp.mouseY = (fl32)GET_Y_LPARAM(lParam) * gApp.mouseScale;
                if (!gApp.mouseTracked) {
                    gApp.mouseTracked = true;
                    TRACKMOUSEEVENT tme;
                    memset(&tme, 0, sizeof(tme));
                    tme.cbSize = sizeof(tme);
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = gApp.hwnd;
                    TrackMouseEvent(&tme);
                    appWinDispatchMouseEvent(AppEventType::MouseEnter, InputMouseButton::Invalid);
                }
                appWinDispatchMouseEvent(AppEventType::MouseMove, InputMouseButton::Invalid);
                break;
            case WM_MOUSEHOVER:
                if (gApp.mouseCursor == AppMouseCursor::None)
                    appSetCursor(AppMouseCursor::Arrow);
                break;
            case WM_MOUSELEAVE:
                gApp.mouseTracked = false;
                gApp.mouseCursor = AppMouseCursor::None;
                appWinDispatchMouseEvent(AppEventType::MouseLeave, InputMouseButton::Invalid);
                break;
            case WM_MOUSEWHEEL:
                appWinDispatchScrollEvent(0.0f, (fl32)((SHORT)HIWORD(wParam)));
                break;
            case WM_MOUSEHWHEEL:
                appWinDispatchScrollEvent((fl32)((SHORT)HIWORD(wParam)), 0.0f);
                break;
            case WM_CHAR:
                appWinDispatchCharEvent((uint32)wParam, !!(lParam & 0x40000000));
                break;
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                appWinDispatchKeyEvent(AppEventType::KeyDown, (int)(HIWORD(lParam) & 0x1FF), !!(lParam & 0x40000000));
                break;
            case WM_KEYUP:
            case WM_SYSKEYUP:
                appWinDispatchKeyEvent(AppEventType::KeyUp, (int)(HIWORD(lParam) & 0x1FF), false);
                break;
            case WM_DISPLAYCHANGE:
                appWinUpdateDisplayInfo();
                appWinDispatchEvent(AppEventType::DisplayUpdated);
                break;

            default:
                break;
        }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static bool appWinUpdateDimensions()
{
    RECT rect;
    if (GetClientRect(gApp.hwnd, &rect)) {
        gApp.windowWidth = uint16(float(rect.right - rect.left) / gApp.windowScale);
        gApp.windowHeight = uint16(float(rect.bottom - rect.top) / gApp.windowScale);
        const uint16 fbWidth = uint16(float(gApp.windowWidth) * gApp.contentScale);
        const uint16 fbHeight = uint16(float(gApp.windowHeight) * gApp.contentScale);
        if ((fbWidth != gApp.framebufferWidth) || (fbHeight != gApp.framebufferHeight)) {
            gApp.framebufferWidth = uint16(float(gApp.windowWidth) * gApp.contentScale);
            gApp.framebufferHeight = uint16(float(gApp.windowHeight) * gApp.contentScale);
            // prevent a framebuffer size of 0 when window is minimized
            if (gApp.framebufferWidth == 0)
                gApp.framebufferWidth = 1;
            if (gApp.framebufferHeight == 0)
                gApp.framebufferHeight = 1;
            return true;
        }
    }
    else {
        gApp.windowWidth = gApp.windowHeight = 1;
        gApp.framebufferWidth = gApp.framebufferHeight = 1;
    }
    return false;
}

static bool appWinCreateWindow()
{
    WNDCLASSW wndclassw;
    memset(&wndclassw, 0, sizeof(wndclassw));
    wndclassw.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wndclassw.lpfnWndProc = (WNDPROC)appWinProc;
    wndclassw.hInstance = GetModuleHandleW(NULL);
    wndclassw.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndclassw.hIcon = LoadIcon(NULL, IDI_WINLOGO);
    wndclassw.lpszClassName = L"JunkyardApp";
    RegisterClassW(&wndclassw);
    
    DWORD winStyle;
    const DWORD winExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
    RECT rect = gApp.mainRect;
    if (gApp.desc.fullscreen) {
        winStyle = WS_POPUP | WS_SYSMENU | WS_VISIBLE;
        rect = RECT {-1, -1, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    else {
        winStyle = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SIZEBOX;
    }

    if (rect.right == -1 || rect.bottom == -1) {
        rect = {0, 0, uint16(float(gApp.windowWidth)*gApp.windowScale) , uint16(float(gApp.windowHeight)*gApp.windowScale) };
        AdjustWindowRectEx(&rect, winStyle, FALSE, winExStyle);
        gApp.windowModified = true;
    }

    const int winWidth = uint16(rect.right - rect.left);
    const int winHeight = uint16(rect.bottom - rect.top);
    
    static wchar_t winTitleWide[128];
    strUt8ToWide(gApp.windowTitle, winTitleWide, sizeof(winTitleWide));

    gApp.inCreateWindow = true;
    gApp.hwnd = CreateWindowExW(
        winExStyle,               	/* dwExStyle */
        L"JunkyardApp", 	        /* lpClassName */
        winTitleWide,             	/* lpWindowName */
        winStyle,                 	/* dwStyle */
        rect.left > 0 ? rect.left : CW_USEDEFAULT, /* X */
        rect.top > 0 ? rect.top : CW_USEDEFAULT,   /* Y */
        winWidth,                  	/* nWidth */
        winHeight,                 	/* nHeight */
        NULL,                       /* hWndParent */
        NULL,                       /* hMenu */
        GetModuleHandle(NULL),      /* hInstance */
        NULL);                      /* lParam */
    if (!gApp.hwnd)
        return false;
    
    ShowWindow(gApp.hwnd, settingsGet().app.launchMinimized ? SW_MINIMIZE : SW_SHOW);
    gApp.inCreateWindow = false;
    
    appWinUpdateDimensions();

    // Adjust console window
    RECT conRc = gApp.consoleRect;
    if (conRc.right > conRc.left && conRc.bottom > conRc.top) {
        MoveWindow(GetConsoleWindow(), conRc.left, conRc.top, conRc.right - conRc.left, conRc.bottom - conRc.top, FALSE);
    }
    return true;
}

static bool appWinFrame(fl32 dt)
{
    if (gApp.firstFrame) {
        gApp.firstFrame = false;
        if (gApp.desc.callbacks) {
            if (!gApp.desc.callbacks->Initialize()) {
                return false;
            }
        }
        gApp.initCalled = true;
    }
    
    if (gApp.desc.callbacks)
        gApp.desc.callbacks->Update(dt);

    gApp.frameCount++;
    return true;
}

bool appInitialize(const AppDesc& desc)
{
    TimerStopWatch stopwatch;

    gApp.desc = desc;

    gApp.firstFrame = true;
    gApp.windowWidth = gApp.desc.width;
    gApp.windowHeight = gApp.desc.height;
    gApp.framebufferWidth = gApp.desc.width;
    gApp.framebufferHeight = gApp.desc.height;
    gApp.dpiScale = 1.0f;
    gApp.clipboardEnabled = desc.enableClipboard;
    gApp.mouseCursor = AppMouseCursor::None;
    if (desc.enableClipboard)
        gApp.clipboard = memAllocZeroTyped<char>((uint32)gApp.desc.clipboardSizeBytes);

    if (desc.windowTitle)
        strCopy(gApp.windowTitle, sizeof(gApp.windowTitle), desc.windowTitle);
    else
        strCopy(gApp.windowTitle, sizeof(gApp.windowTitle), "Junkyard");

    char moduleFilename[128];
    pathGetMyPath(moduleFilename, sizeof(moduleFilename));
    pathFileName(moduleFilename, moduleFilename, sizeof(moduleFilename));
    strCopy(gApp.name, sizeof(gApp.name), moduleFilename);

    if (settingsGet().app.launchMinimized)
        ShowWindow(GetConsoleWindow(), SW_MINIMIZE);

    gApp.hStdin = GetStdHandle(STD_INPUT_HANDLE);
    gApp.hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

    {
        DWORD consoleMode = 0;
        GetConsoleMode(gApp.hStdOut, &consoleMode);
        consoleMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT;
        [[maybe_unused]] BOOL r = SetConsoleMode(gApp.hStdOut, consoleMode);
        ASSERT(r);
    }

    // Default console handler
    SetConsoleCtrlHandler([](DWORD type)->BOOL { 
        if (type == CTRL_C_EVENT) {
            appQuit();
            return TRUE; 
        } 
        return FALSE;
    }, TRUE);

    // Initialize settings if not initialied before
    // Since this is not a recommended way, we also throw an assert
    if (!settingsIsInitializedJunkyard()) {
        ASSERT_MSG(0, "Settings must be initialized before this call. See settingsInitialize() function");
        settingsInitializeJunkyard({}); // initialize with default settings
    }

    // Set some initial settings
    memEnableMemPro(settingsGet().engine.enableMemPro);
    memTempSetCaptureStackTrace(settingsGet().debug.captureStacktraceForTempAllocator);
    debugSetCaptureStacktraceForFiberProtector(settingsGet().debug.captureStacktraceForFiberProtector);
    logSetSettings(static_cast<LogLevel>(settingsGet().engine.logLevel), settingsGet().engine.breakOnErrors, settingsGet().engine.treatWarningsAsErrors);

    // RemoteServices
    if (!_private::remoteInitialize()) {
        ASSERT_MSG(0, "Initializing Server failed");
        return false;
    }

    // VirutalFS -> Depends on RemoteServices for some functionality
    if (!_private::vfsInitialize()) {
        ASSERT_MSG(0, "Initializing VirtualFS failed");
        return false;
    }

    appWinLoadInitRects();  // may modify window/framebuffer dimensions 
    appWinInitKeyTable();

    bool headless = settingsGet().graphics.headless || !settingsGet().graphics.enable;
    if (!headless) {
        appWinInitDpi();
        if (!appWinCreateWindow()) {
            ASSERT_MSG(0, "Creating win32 window failed");
            return false;
        }
        appWinUpdateDisplayInfo();
    }
    gApp.valid = true;

    logVerbose("(init) App initialized (%.1f ms). Entering event loop ...", stopwatch.ElapsedMS());

    // Main loop
    uint64 tmPrev = 0;
    bool done = false;
    while (!(done || gApp.quitOrdered)) {
        if (!headless) {
            MSG msg;
            
            // Get window messages
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (WM_QUIT == msg.message) {
                    done = true;
                    continue;
                }
                else {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }

            // When minimized, bring down update frequency
            if (gApp.iconified) {
                Sleep(16);
                continue;
            }
            else {
                // check for window resized, this cannot happen in WM_SIZE as it explodes memory usage
                if (appWinUpdateDimensions()) {
                    appWinDispatchEvent(AppEventType::Resized);
                    gApp.windowModified = true;
                }
            }
            
            uint64 tmNow = timerGetTicks();
            float dt = !gApp.firstFrame ? static_cast<fl32>(timerToSec(timerDiff(tmNow, tmPrev))) : 0;
            if (!appWinFrame(dt))
                appQuit();
            tmPrev = tmNow;
            
            if (gApp.quitRequested)
                PostMessage(gApp.hwnd, WM_CLOSE, 0, 0);
        }
        else {
            // Get console messages
            if (gApp.hStdin != INVALID_HANDLE_VALUE) {
                INPUT_RECORD inputBuff[16];
                DWORD numInputs;
                while (GetNumberOfConsoleInputEvents(gApp.hStdin, &numInputs) && numInputs &&
                       ReadConsoleInput(gApp.hStdin, inputBuff, CountOf(inputBuff), &numInputs)) 
                {
                    for (DWORD inIdx = 0; inIdx < numInputs; inIdx++) {
                        if (inputBuff[inIdx].EventType == KEY_EVENT) {
                            const KEY_EVENT_RECORD& keyEvent = inputBuff[inIdx].Event.KeyEvent;
                            if (keyEvent.uChar.AsciiChar >= 32 && keyEvent.uChar.AsciiChar < 128) 
                                appWinDispatchCharEvent((char)keyEvent.uChar.AsciiChar, keyEvent.wRepeatCount > 1);

                            AppEventType eventType = keyEvent.bKeyDown ? AppEventType::KeyDown : AppEventType::KeyUp;
                            appWinDispatchKeyEvent(eventType, keyEvent.wVirtualScanCode, keyEvent.wRepeatCount > 1);
                        }
                    }
                }
            }

            uint64 tmNow = timerGetTicks();
            float dt = !gApp.firstFrame ? static_cast<fl32>(timerToSec(timerDiff(tmNow, tmPrev))) : 0;
            if (!appWinFrame(dt))
                appQuit();
            tmPrev = tmNow;
        }
    }
    
    // Cleanup
    appWinSaveInitRects();

    if (gApp.desc.callbacks) {
        gApp.desc.callbacks->Cleanup();
    }

    _private::remoteRelease();
    _private::vfsRelease();
    
    if (!headless) {
        DestroyWindow(gApp.hwnd);
        UnregisterClassW(L"JunkyardApp", GetModuleHandleW(NULL));
    }
    gApp.hwnd = nullptr;
    
    if (gApp.clipboardEnabled) {
        ASSERT(gApp.clipboard);
        memFree(gApp.clipboard);
    }

    gApp.eventCallbacks.Free();

    return true;
}

void appShowMouse(bool visible)
{
    ShowCursor((BOOL)visible);
}

bool appIsMouseShown(void)
{
    CURSORINFO cursorInfo;
    memset(&cursorInfo, 0, sizeof(CURSORINFO));
    cursorInfo.cbSize = sizeof(CURSORINFO);
    GetCursorInfo(&cursorInfo);
    return (cursorInfo.flags & CURSOR_SHOWING) != 0;
}

const char* appGetClipboardString()
{
    ASSERT(gApp.clipboardEnabled && gApp.clipboard);
    ASSERT(gApp.hwnd);
    
    if (!OpenClipboard(gApp.hwnd)) {
        // silently ignore any errors and just return the current content of the local clipboard buffer
        return gApp.clipboard;
    }
    
    HANDLE object = GetClipboardData(CF_UNICODETEXT);
    if (!object) {
        CloseClipboard();
        return gApp.clipboard;
    }
    
    wchar_t* wcharBuff = (wchar_t*)GlobalLock(object);
    if (!wcharBuff) {
        CloseClipboard();
        return gApp.clipboard;
    }
    
    strWideToUtf8(wcharBuff, gApp.clipboard, gApp.desc.clipboardSizeBytes);
    GlobalUnlock(object);
    CloseClipboard();
    
    return gApp.clipboard;
}

void* appGetNativeWindowHandle()
{
    return gApp.hwnd;
}

void appRequestQuit()
{
    gApp.quitRequested = true;
}

void appCancelQuit()
{
    gApp.quitRequested = false;
}

void appQuit()
{
    gApp.quitOrdered = true;
}

uint16 appGetWindowWidth()
{
    return gApp.windowWidth;
}

uint16 appGetWindowHeight()
{
    return gApp.windowHeight;
}

uint16 appGetFramebufferWidth()
{
    return gApp.framebufferWidth;
}

uint16 appGetFramebufferHeight()
{
    return gApp.framebufferHeight;
}

AppDisplayInfo appGetDisplayInfo()
{
    return AppDisplayInfo {
        .width = gApp.displayWidth,
        .height = gApp.displayHeight,
        .refreshRate = gApp.displayRefreshRate,
        .dpiScale = gApp.dpiScale
    };
}

void appRegisterEventsCallback(appOnEventCallback callback, void* userData)
{
    bool alreadyExist = false;
    for (uint32 i = 0; i < gApp.eventCallbacks.Count(); i++) {
        if (callback == gApp.eventCallbacks[i].callback) {
            alreadyExist = true;
            break;
        }
    }
    
    ASSERT_MSG(!alreadyExist, "Callback function already exists in event callbacks");
    if (!alreadyExist) {
        gApp.eventCallbacks.Push({callback, userData});
    }    
}

void appUnregisterEventsCallback(appOnEventCallback callback)
{
    if (uint32 index = gApp.eventCallbacks.FindIf([callback](const AppEventCallbackPair& p)->bool 
        {return p.callback == callback;});
        index != UINT32_MAX)
    {
        gApp.eventCallbacks.RemoveAndSwap(index);
    }
}

const char* appGetName(void)
{
    return gApp.name;
}

void appSetCursor(AppMouseCursor cursor)
{
    if (cursor == gApp.mouseCursor)
        return;

    switch (cursor) {
    case AppMouseCursor::None:          SetCursor(NULL);							  break; 
    case AppMouseCursor::Arrow:         SetCursor(LoadCursor(NULL, IDC_ARROW));       break;
    case AppMouseCursor::TextInput:     SetCursor(LoadCursor(NULL, IDC_IBEAM));       break;
    case AppMouseCursor::ResizeAll:     SetCursor(LoadCursor(NULL, IDC_SIZEALL));     break;
    case AppMouseCursor::ResizeNS:      SetCursor(LoadCursor(NULL, IDC_SIZENS));      break;
    case AppMouseCursor::ResizeWE:      SetCursor(LoadCursor(NULL, IDC_SIZEWE));      break;
    case AppMouseCursor::ResizeNESW:    SetCursor(LoadCursor(NULL, IDC_SIZENESW));    break;
    case AppMouseCursor::ResizeNWSE:    SetCursor(LoadCursor(NULL, IDC_SIZENWSE));    break;
    case AppMouseCursor::Hand:          SetCursor(LoadCursor(NULL, IDC_HAND));        break;
    case AppMouseCursor::NotAllowed:    SetCursor(LoadCursor(NULL, IDC_NO));          break;
    }

    gApp.mouseCursor = cursor;
}

void* appGetNativeAppHandle(void)
{
    return ::GetModuleHandleA(NULL);
}

bool appIsKeyDown(InputKeycode keycode)
{
    return gApp.keysPressed[uint32(keycode)];
}

bool appIsAnyKeysDown(const InputKeycode* keycodes, uint32 numKeycodes)
{
    bool down = false;
    for (uint32 i = 0; i < numKeycodes; i++)
        down |= gApp.keysPressed[uint32(keycodes[i])];
    return down;
}

AppFramebufferTransform appGetFramebufferTransform()
{
    return AppFramebufferTransform::None;
}

void appCaptureMouse()
{
    appSetCursor(AppMouseCursor::None);
    SetCapture(gApp.hwnd);
}

void appReleaseMouse()
{
    appSetCursor(AppMouseCursor::Arrow);
    ReleaseCapture();
}

#endif // PLATFORM_WINDOWS
