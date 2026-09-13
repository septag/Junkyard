#pragma once

#include "../Core/Base.h"

#include "InputTypes.h"

#if PLATFORM_ANDROID
typedef struct AAssetManager AAssetManager; // <android/asset_manager.h>
typedef struct ANativeActivity ANativeActivity; // <android/native_activity.h>
#endif

// Handle to a platform window. The main window is created by App::Run and can be fetched with App::GetMainWindow.
// Note: This is deliberately not DEFINE_HANDLE, because Core/Pools.h hides Handle<> from ObjectiveC translation
//       units (Apple's MacTypes.h declares its own 'Handle') and ApplicationMac.mm includes this header.
struct AppWindowHandle
{
    uint32 mId = 0;

    bool IsValid() const { return mId != 0; }
    bool operator==(const AppWindowHandle& v) const { return mId == v.mId; }
    bool operator!=(const AppWindowHandle& v) const { return mId != v.mId; }
};

// Mirrors the subset of ImGuiViewportFlags_ that a platform window has to honor
enum class AppWindowFlags : uint32
{
    None                = 0,
    NoDecoration        = 0x01,     // Borderless: no title bar, no resize frame
    NoTaskBarIcon       = 0x02,
    TopMost             = 0x04,
    NoFocusOnAppearing  = 0x08,
    NoFocusOnClick      = 0x10
};
ENABLE_BITMASK(AppWindowFlags);

// A rectangle in desktop (virtual screen) coordinates. For windows this is the client area, excluding decoration
struct AppRect
{
    int x;
    int y;
    int width;
    int height;
};

struct AppMonitorInfo
{
    AppRect mainRect;   // Full monitor area
    AppRect workRect;   // Minus task bars and docked toolbars
    float dpiScale;     // 1.0f = 96 DPI
    bool isPrimary;
};

struct AppWindowDesc
{
    const char* title         = nullptr;
    AppRect geometry {};
    AppWindowFlags flags      = AppWindowFlags::None;
    AppWindowHandle parent    {};
};

enum class AppMouseCursor
{
    None = -1,
    Arrow = 0,
    TextInput,
    ResizeAll,
    ResizeNS,
    ResizeWE,
    ResizeNESW,
    ResizeNWSE,
    Hand,
    Wait,
    Progress,
    NotAllowed,
    _Count
};

enum class AppEventType 
{
    Invalid = 0,
    KeyDown,
    KeyUp,
    Char,
    MouseDown,
    MouseUp,
    MouseScroll,
    MouseMove,
    MouseEnter,
    MouseLeave,
    TouchBegin,
    TouchMove,
    TouchEnd,
    TouchCancel,
    Resized,
    Moved,
    Iconified,
    Restored,
    Suspended,
    Resumed,
    UpdateCursor,
    ClipboardPasted,
    DisplayUpdated,
    WindowClose,        // Secondary window was asked to close (title bar X, Alt+F4). Owner decides whether to destroy it
    _Count,
};

enum class AppFramebufferTransform : uint32
{
    None = 0,
    Rotate90,
    Rotate180,
    Rotate270
};

struct AppEvent
{
    AppEventType type;
    AppWindowHandle window;     // Window this event originated from. Compare against App::GetMainWindow()
    InputKeycode keycode;
    uint32 charcode;
    bool keyRepeat;
    InputKeyModifiers keyMods;
    float mouseX;               // Client-relative to `window`, in logical (DPI-divided) units
    float mouseY;
    float mouseDesktopX;        // Desktop/virtual-screen, in physical pixels. Matches AppRect and AppMonitorInfo
    float mouseDesktopY;
    InputMouseButton mouseButton;
    float scrollX;
    float scrollY;
    uint32 numTouches;
    InputTouchPoint touches[INPUT_MAX_TOUCH_POINTS];
    uint16 windowWidth;
    uint16 windowHeight;
    uint16 framebufferWidth;
    uint16 framebufferHeight;
};

using AppEventCallback = void(*)(const AppEvent& ev, void* userData);
using AppUpdateOverrideCallback = void(*)(float dt, void* userData);
using AppFramebufferSizeQueryFunc = bool(*)(uint16* width, uint16* height);

struct NO_VTABLE AppCallbacks
{
    virtual bool Initialize() = 0;
    virtual void Update(float dt) = 0;
    virtual void Cleanup() = 0;
    virtual void OnEvent(const AppEvent& ev) = 0;
};

struct AppDesc 
{
    AppCallbacks* callbacks   = nullptr;
    uint16 initWidth          = 1280;
    uint16 initHeight         = 800;
    const char* windowTitle   = nullptr;
    size_t clipboardSizeBytes = 4096;
    bool highDPI              = true;
    bool fullscreen           = false;
    bool userCursor           = true;
    bool enableClipboard      = true;
    bool updateWhenMinimized  = false;
};

struct AppDisplayInfo
{
    uint16 width;
    uint16 height;
    uint16 refreshRate;
    float dpiScale;
};

namespace App
{
    API bool Run(const AppDesc& desc);
    API void ShowMouse(bool visible);
    API void Quit();
    API bool SetClipboardString(const char* str);
    API const char* GetClipboardString();
    API const char* GetName();

    API AppWindowHandle GetMainWindow();

    // False on platforms that only ever have the main window. Callers that drive secondary windows
    // (e.g. the ImGui multi-viewport backend) must check this before enabling themselves
    API bool IsMultiWindowSupported();

    // Secondary windows. Created hidden so geometry/title can be set before ShowWindow
    API AppWindowHandle CreateWindowHandle(const AppWindowDesc& desc);
    API void DestroyWindowHandle(AppWindowHandle handle);
    API void ShowWindow(AppWindowHandle handle);
    API AppRect GetWindowGeometry(AppWindowHandle handle);
    API void SetWindowPos(AppWindowHandle handle, int x, int y);
    API void SetWindowSize(AppWindowHandle handle, int width, int height);
    API void SetWindowTitle(AppWindowHandle handle, const char* title);
    API void SetWindowAlpha(AppWindowHandle handle, float alpha);
    API void FocusWindow(AppWindowHandle handle);
    API bool IsWindowFocused(AppWindowHandle handle);
    API bool IsWindowMinimized(AppWindowHandle handle);
    API float GetWindowDPIScale(AppWindowHandle handle);
    API void* GetNativeWindowHandle(AppWindowHandle handle);

    // Fills up to `maxMonitors` entries, primary monitor always first. Returns the number written.
    // Pass nullptr to just query how many monitors are connected
    API uint32 GetMonitors(AppMonitorInfo* outMonitors, uint32 maxMonitors);

    // Topmost app window under a desktop-space point, invalid handle if the point is over another process
    API AppWindowHandle GetWindowFromPoint(int x, int y);

    API uint16 GetWindowWidth();
    API uint16 GetWindowHeight();
    API uint16 GetFramebufferWidth();
    API uint16 GetFramebufferHeight();
    API float GetWindowDPIScale();
    API AppFramebufferTransform GetFramebufferTransform();

    API bool IsMouseShown();
    API void CaptureMouse();
    API void ReleaseMouse();
    API void* GetNativeWindowHandle();
    API void* GetNativeAppHandle();
    API void RegisterEventsCallback(AppEventCallback callback, void* userData = nullptr);
    API void UnregisterEventsCallback(AppEventCallback callback);
    API void OverrideUpdateCallback(AppUpdateOverrideCallback callback, void* userData = nullptr);
    API void SetCursor(AppMouseCursor cursor);
    API AppDisplayInfo GetDisplayInfo();
    API bool IsKeyDown(InputKeycode keycode);
    API bool IsAnyKeysDown(const InputKeycode* keycodes, uint32 numKeycodes);
    API InputKeyModifiers GetKeyMods();

    // Used by GfxBackend
    API void RegisterFramebufferSizeQueryFunc(AppFramebufferSizeQueryFunc fn);

    #if PLATFORM_ANDROID
    API AAssetManager* AndroidGetAssetManager();
    API void AndroidSetFramebufferTransform(AppFramebufferTransform transform);
    API ANativeActivity* AndroidGetActivity();
    #endif 
}

