#include "Application.h"

#if PLATFORM_WINDOWS

#include "External/mgustavsson/ini.h"

#include "Core/Memory.h"
#include "Core/String.h"
#include "Core/System.h"
#include "Core/Settings.h"
#include "Core/IncludeWin.h"
#include "Core/FileIO.h"
#include "Core/Buffers.h"

#include "VirtualFS.h"
#include "RemoteServices.h"

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
    uint16 windowWidth;
    uint16 windowHeight;
    uint16 framebufferWidth;
    uint16 framebufferHeight;
    char windowTitle[128];
    uint64 frameCount;
    fl32 mouseX;
    fl32 mouseY;
    AppEvent ev;
    AppDesc desc;
    AppKeycode keycodes[kMaxKeycodes];
    char* clipboard;
    Array<AppEventCallbackPair> eventCallbacks;
    AppMouseCursor mouseCursor;

    HWND hwnd;
    uint16 displayWidth;
    uint16 displayHeight;
    uint16 displayRefreshRate;
    HMONITOR wndMonitor;
    RECT mainRect;
    RECT consoleRect;

    HANDLE hStdin;
    HANDLE hStdOut;
    DWORD consoleOldMode;

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
    gApp.keycodes[0x00B] = AppKeycode::NUM0;
    gApp.keycodes[0x002] = AppKeycode::NUM1;
    gApp.keycodes[0x003] = AppKeycode::NUM2;
    gApp.keycodes[0x004] = AppKeycode::NUM3;
    gApp.keycodes[0x005] = AppKeycode::NUM4;
    gApp.keycodes[0x006] = AppKeycode::NUM5;
    gApp.keycodes[0x007] = AppKeycode::NUM6;
    gApp.keycodes[0x008] = AppKeycode::NUM7;
    gApp.keycodes[0x009] = AppKeycode::NUM8;
    gApp.keycodes[0x00A] = AppKeycode::NUM9;
    gApp.keycodes[0x01E] = AppKeycode::A;
    gApp.keycodes[0x030] = AppKeycode::B;
    gApp.keycodes[0x02E] = AppKeycode::C;
    gApp.keycodes[0x020] = AppKeycode::D;
    gApp.keycodes[0x012] = AppKeycode::E;
    gApp.keycodes[0x021] = AppKeycode::F;
    gApp.keycodes[0x022] = AppKeycode::G;
    gApp.keycodes[0x023] = AppKeycode::H;
    gApp.keycodes[0x017] = AppKeycode::I;
    gApp.keycodes[0x024] = AppKeycode::J;
    gApp.keycodes[0x025] = AppKeycode::K;
    gApp.keycodes[0x026] = AppKeycode::L;
    gApp.keycodes[0x032] = AppKeycode::M;
    gApp.keycodes[0x031] = AppKeycode::N;
    gApp.keycodes[0x018] = AppKeycode::O;
    gApp.keycodes[0x019] = AppKeycode::P;
    gApp.keycodes[0x010] = AppKeycode::Q;
    gApp.keycodes[0x013] = AppKeycode::R;
    gApp.keycodes[0x01F] = AppKeycode::S;
    gApp.keycodes[0x014] = AppKeycode::T;
    gApp.keycodes[0x016] = AppKeycode::U;
    gApp.keycodes[0x02F] = AppKeycode::V;
    gApp.keycodes[0x011] = AppKeycode::W;
    gApp.keycodes[0x02D] = AppKeycode::X;
    gApp.keycodes[0x015] = AppKeycode::Y;
    gApp.keycodes[0x02C] = AppKeycode::Z;
    gApp.keycodes[0x028] = AppKeycode::Apostrophe;
    gApp.keycodes[0x02B] = AppKeycode::Backslash;
    gApp.keycodes[0x033] = AppKeycode::Comma;
    gApp.keycodes[0x00D] = AppKeycode::Equal;
    gApp.keycodes[0x029] = AppKeycode::GraveAccent;
    gApp.keycodes[0x01A] = AppKeycode::LeftBracket;
    gApp.keycodes[0x00C] = AppKeycode::Minus;
    gApp.keycodes[0x034] = AppKeycode::Period;
    gApp.keycodes[0x01B] = AppKeycode::RightBracket;
    gApp.keycodes[0x027] = AppKeycode::Semicolon;
    gApp.keycodes[0x035] = AppKeycode::Slash;
    gApp.keycodes[0x056] = AppKeycode::World2;
    gApp.keycodes[0x00E] = AppKeycode::Backspace;
    gApp.keycodes[0x153] = AppKeycode::Delete;
    gApp.keycodes[0x14F] = AppKeycode::End;
    gApp.keycodes[0x01C] = AppKeycode::Enter;
    gApp.keycodes[0x001] = AppKeycode::Escape;
    gApp.keycodes[0x147] = AppKeycode::Home;
    gApp.keycodes[0x152] = AppKeycode::Insert;
    gApp.keycodes[0x15D] = AppKeycode::Menu;
    gApp.keycodes[0x151] = AppKeycode::PageDown;
    gApp.keycodes[0x149] = AppKeycode::PageUp;
    gApp.keycodes[0x045] = AppKeycode::Pause;
    gApp.keycodes[0x146] = AppKeycode::Pause;
    gApp.keycodes[0x039] = AppKeycode::Space;
    gApp.keycodes[0x00F] = AppKeycode::Tab;
    gApp.keycodes[0x03A] = AppKeycode::CapsLock;
    gApp.keycodes[0x145] = AppKeycode::NumLock;
    gApp.keycodes[0x046] = AppKeycode::ScrollLock;
    gApp.keycodes[0x03B] = AppKeycode::F1;
    gApp.keycodes[0x03C] = AppKeycode::F2;
    gApp.keycodes[0x03D] = AppKeycode::F3;
    gApp.keycodes[0x03E] = AppKeycode::F4;
    gApp.keycodes[0x03F] = AppKeycode::F5;
    gApp.keycodes[0x040] = AppKeycode::F6;
    gApp.keycodes[0x041] = AppKeycode::F7;
    gApp.keycodes[0x042] = AppKeycode::F8;
    gApp.keycodes[0x043] = AppKeycode::F9;
    gApp.keycodes[0x044] = AppKeycode::F10;
    gApp.keycodes[0x057] = AppKeycode::F11;
    gApp.keycodes[0x058] = AppKeycode::F12;
    gApp.keycodes[0x064] = AppKeycode::F13;
    gApp.keycodes[0x065] = AppKeycode::F14;
    gApp.keycodes[0x066] = AppKeycode::F15;
    gApp.keycodes[0x067] = AppKeycode::F16;
    gApp.keycodes[0x068] = AppKeycode::F17;
    gApp.keycodes[0x069] = AppKeycode::F18;
    gApp.keycodes[0x06A] = AppKeycode::F19;
    gApp.keycodes[0x06B] = AppKeycode::F20;
    gApp.keycodes[0x06C] = AppKeycode::F21;
    gApp.keycodes[0x06D] = AppKeycode::F22;
    gApp.keycodes[0x06E] = AppKeycode::F23;
    gApp.keycodes[0x076] = AppKeycode::F24;
    gApp.keycodes[0x038] = AppKeycode::LeftAlt;
    gApp.keycodes[0x01D] = AppKeycode::LeftControl;
    gApp.keycodes[0x02A] = AppKeycode::LeftShift;
    gApp.keycodes[0x15B] = AppKeycode::LeftSuper;
    gApp.keycodes[0x137] = AppKeycode::PrintScreen;
    gApp.keycodes[0x138] = AppKeycode::RightAlt;
    gApp.keycodes[0x11D] = AppKeycode::RightControl;
    gApp.keycodes[0x036] = AppKeycode::RightShift;
    gApp.keycodes[0x15C] = AppKeycode::RightSuper;
    gApp.keycodes[0x150] = AppKeycode::Down;
    gApp.keycodes[0x14B] = AppKeycode::Left;
    gApp.keycodes[0x14D] = AppKeycode::Right;
    gApp.keycodes[0x148] = AppKeycode::Up;
    gApp.keycodes[0x052] = AppKeycode::KP0;
    gApp.keycodes[0x04F] = AppKeycode::KP1;
    gApp.keycodes[0x050] = AppKeycode::KP2;
    gApp.keycodes[0x051] = AppKeycode::KP3;
    gApp.keycodes[0x04B] = AppKeycode::KP4;
    gApp.keycodes[0x04C] = AppKeycode::KP5;
    gApp.keycodes[0x04D] = AppKeycode::KP6;
    gApp.keycodes[0x047] = AppKeycode::KP7;
    gApp.keycodes[0x048] = AppKeycode::KP8;
    gApp.keycodes[0x049] = AppKeycode::KP9;
    gApp.keycodes[0x04E] = AppKeycode::KPAdd;
    gApp.keycodes[0x053] = AppKeycode::KPDecimal;
    gApp.keycodes[0x135] = AppKeycode::KPDivide;
    gApp.keycodes[0x11C] = AppKeycode::KPEnter;
    gApp.keycodes[0x037] = AppKeycode::KPMultiply;
    gApp.keycodes[0x04A] = AppKeycode::KPSubtract;
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

    gApp.mainRect = RECT {0, 0, gApp.windowWidth, gApp.windowHeight};
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
            PutWindowData(windowsIni, "Main", RECT {mainRect.left, mainRect.top, mainRect.left + gApp.windowWidth, mainRect.top + gApp.windowHeight});
        if (GetWindowRect(GetConsoleWindow(), &consoleRect))
            PutWindowData(windowsIni, "Console", consoleRect);

        int size = ini_save(windowsIni, nullptr, 0);
        if (size > 0) {
            char* data = memAllocTyped<char>(size);
            ini_save(windowsIni, data, size);
    
            if (File f = File(iniFilename, FileIOFlags::Write); f.IsOpen()) {
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
        gApp.mouseScale = 1.0f;
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

static bool appWinUtf8ToWide(const char* src, wchar_t* dst, size_t dstNumBytes) 
{
    ASSERT(src && dst && (dstNumBytes > 1));
    memset(dst, 0, dstNumBytes);
    const int dstChars = (int)(dstNumBytes / sizeof(wchar_t));
    const int dstNeeded = (int)MultiByteToWideChar(CP_UTF8, 0, src, -1, 0, 0);
    if ((dstNeeded > 0) && (dstNeeded < dstChars)) {
        MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, dstChars);
        return true;
    }
    else {
        // input string doesn't fit into destination buffer
        ASSERT(false);
        return false;
    }
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
    if (!object) {
        goto error;
    }
    wcharBuff = (wchar_t*)GlobalLock(object);
    if (!wcharBuff) {
        goto error;
    }
    if (!appWinUtf8ToWide(str, wcharBuff, wcharBuffSize)) {
        goto error;
    }
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
        if (wcharBuff) {
            GlobalUnlock(object);
        }
        if (object) {
            GlobalFree(object);
        }
        return false;
}

static inline bool appWinEventsEnabled(void)
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
    gApp.ev.mouseButton = AppMouseButton::Invalid;
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

AppKeyModifiers appGetKeyMods()
{
    AppKeyModifiers mods = AppKeyModifiers::None;
    if (GetKeyState(VK_SHIFT) & (1<<15))
        mods |= AppKeyModifiers::Shift;
    if (GetKeyState(VK_CONTROL) & (1<<15))
        mods |= AppKeyModifiers::Ctrl;
    if (GetKeyState(VK_MENU) & (1<<15))
        mods |= AppKeyModifiers::Alt;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & (1<<15))
        mods |= AppKeyModifiers::Super;
    return mods;
}

static void appWinDispatchMouseEvent(AppEventType type, AppMouseButton btn)
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
            (gApp.ev.keyMods == AppKeyModifiers::Ctrl) &&
            (gApp.ev.keycode == AppKeycode::V)) 
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
                appWinDispatchMouseEvent(AppEventType::MouseDown, AppMouseButton::Left);
                break;
            case WM_RBUTTONDOWN:
                appWinDispatchMouseEvent(AppEventType::MouseDown, AppMouseButton::Right);
                break;
            case WM_MBUTTONDOWN:
                appWinDispatchMouseEvent(AppEventType::MouseDown, AppMouseButton::Middle);
                break;
            case WM_LBUTTONUP:
                appWinDispatchMouseEvent(AppEventType::MouseUp, AppMouseButton::Left);
                break;
            case WM_RBUTTONUP:
                appWinDispatchMouseEvent(AppEventType::MouseUp, AppMouseButton::Right);
                break;
            case WM_MBUTTONUP:
                appWinDispatchMouseEvent(AppEventType::MouseUp, AppMouseButton::Middle);
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
                    appWinDispatchMouseEvent(AppEventType::MouseEnter, AppMouseButton::Invalid);
                }
                appWinDispatchMouseEvent(AppEventType::MouseMove, AppMouseButton::Invalid);
                break;
            case WM_MOUSEHOVER:
                if (gApp.mouseCursor == AppMouseCursor::None)
                    appSetCursor(AppMouseCursor::Arrow);
                break;
            case WM_MOUSELEAVE:
                gApp.mouseTracked = false;
                gApp.mouseCursor = AppMouseCursor::None;
                appWinDispatchMouseEvent(AppEventType::MouseLeave, AppMouseButton::Invalid);
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
        gApp.windowWidth = uint16((fl32)(rect.right - rect.left) / gApp.windowScale);
        gApp.windowHeight = uint16((fl32)(rect.bottom - rect.top) / gApp.windowScale);
        const uint16 fbWidth = uint16((fl32)gApp.windowWidth * gApp.contentScale);
        const uint16 fbHeight = uint16((fl32)gApp.windowHeight * gApp.contentScale);
        if ((fbWidth != gApp.framebufferWidth) || (fbHeight != gApp.framebufferHeight)) {
            gApp.framebufferWidth = uint16((fl32)gApp.windowWidth * gApp.contentScale);
            gApp.framebufferHeight = uint16((fl32)gApp.windowHeight * gApp.contentScale);
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
    AdjustWindowRectEx(&rect, winStyle, FALSE, winExStyle);
    const int winWidth = rect.right - rect.left;
    const int winHeight = rect.bottom - rect.top;
    
    static wchar_t winTitleWide[128];
    appWinUtf8ToWide(gApp.windowTitle, winTitleWide, sizeof(winTitleWide));

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
    
    ShowWindow(gApp.hwnd, settingsGetApp().launchMinimized ? SW_MINIMIZE : SW_SHOW);
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

    if (settingsGetApp().launchMinimized)
        ShowWindow(GetConsoleWindow(), SW_MINIMIZE);

    gApp.hStdin = GetStdHandle(STD_INPUT_HANDLE);
    gApp.hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

    // Default console handler
    SetConsoleCtrlHandler([](DWORD type)->BOOL { 
        if (type == CTRL_C_EVENT) {
            appQuit();
            return TRUE; 
        } 
        return FALSE;
    }, TRUE);

    timerInitialize();
    uint64 tmPrev = 0;

    // Initialize settings if not initialied before
    // Since this is not a recommended way, we also throw an assert
    if (!settingsIsInitialized()) {
        ASSERT_MSG(0, "Settings must be initialized before this call. See settingsInitialize() function");
        settingsInitialize({}); // initialize with default settings anyway
    }

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

    bool headless = settingsGetGraphics().headless;
    if (!headless) {
        appWinInitDpi();
        if (!appWinCreateWindow()) {
            ASSERT_MSG(0, "Creating win32 window failed");
            return false;
        }
        appWinUpdateDisplayInfo();
    }
    gApp.valid = true;

    // Main loop
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

            // check for window resized, this cannot happen in WM_SIZE as it explodes memory usage
            if (appWinUpdateDimensions()) {
                appWinDispatchEvent(AppEventType::Resized);
                gApp.windowModified = true;
            }

            // When minimized, bring down update frequency
            if (IsIconic(gApp.hwnd))
                Sleep(16);
            
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

const char* appGetClipboardString(void)
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
    
    appWinUtf8ToWide(gApp.clipboard, wcharBuff, gApp.desc.clipboardSizeBytes);
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

bool appIsKeyDown(AppKeycode keycode)
{
    return gApp.keysPressed[uint32(keycode)];
}

bool appIsAnyKeysDown(const AppKeycode* keycodes, uint32 numKeycodes)
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

API void* appWinGetConsoleHandle()
{
    return gApp.hStdOut;
}

void appCaptureMouse()
{
    SetCapture(gApp.hwnd);
}

void appReleaseMouse()
{
    ReleaseCapture();
}

#endif // PLATFORM_WINDOWS
