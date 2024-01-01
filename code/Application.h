#pragma once

#include "Core/Base.h"

#if PLATFORM_ANDROID
typedef struct AAssetManager AAssetManager; // <android/asset_manager.h>
typedef struct ANativeActivity ANativeActivity; // <android/native_activity.h>
#endif

inline constexpr uint32 kAppMaxTouchPoints = 8;

enum class AppKeycode : uint32
{
    Invalid = 0,
    Space = 32,
    Apostrophe = 39,  /* ' */
    Comma = 44,  /* , */
    Minus = 45,  /* - */
    Period = 46,  /* . */
    Slash = 47,  /* / */
    NUM0 = 48,
    NUM1 = 49,
    NUM2 = 50,
    NUM3 = 51,
    NUM4 = 52,
    NUM5 = 53,
    NUM6 = 54,
    NUM7 = 55,
    NUM8 = 56,
    NUM9 = 57,
    Semicolon = 59,  /* ; */
    Equal = 61,  /* = */
    A = 65,
    B = 66,
    C = 67,
    D = 68,
    E = 69,
    F = 70,
    G = 71,
    H = 72,
    I = 73,
    J = 74,
    K = 75,
    L = 76,
    M = 77,
    N = 78,
    O = 79,
    P = 80,
    Q = 81,
    R = 82,
    S = 83,
    T = 84,
    U = 85,
    V = 86,
    W = 87,
    X = 88,
    Y = 89,
    Z = 90,
    LeftBracket = 91,  /* [ */
    Backslash = 92,  /* \ */
    RightBracket = 93,  /* ] */
    GraveAccent = 96,  /* ` */
    World1 = 161, /* non-US #1 */
    World2 = 162, /* non-US #2 */
    Escape = 256,
    Enter = 257,
    Tab = 258,
    Backspace = 259,
    Insert = 260,
    Delete = 261,
    Right = 262,
    Left = 263,
    Down = 264,
    Up = 265,
    PageUp = 266,
    PageDown = 267,
    Home = 268,
    End = 269,
    CapsLock = 280,
    ScrollLock = 281,
    NumLock = 282,
    PrintScreen = 283,
    Pause = 284,
    F1 = 290,
    F2 = 291,
    F3 = 292,
    F4 = 293,
    F5 = 294,
    F6 = 295,
    F7 = 296,
    F8 = 297,
    F9 = 298,
    F10 = 299,
    F11 = 300,
    F12 = 301,
    F13 = 302,
    F14 = 303,
    F15 = 304,
    F16 = 305,
    F17 = 306,
    F18 = 307,
    F19 = 308,
    F20 = 309,
    F21 = 310,
    F22 = 311,
    F23 = 312,
    F24 = 313,
    F25 = 314,
    KP0 = 320,
    KP1 = 321,
    KP2 = 322,
    KP3 = 323,
    KP4 = 324,
    KP5 = 325,
    KP6 = 326,
    KP7 = 327,
    KP8 = 328,
    KP9 = 329,
    KPDecimal = 330,
    KPDivide = 331,
    KPMultiply = 332,
    KPSubtract = 333,
    KPAdd = 334,
    KPEnter = 335,
    KPEqual = 336,
    LeftShift = 340,
    LeftControl = 341,
    LeftAlt = 342,
    LeftSuper = 343,
    RightShift = 344,
    RightControl = 345,
    RightAlt = 346,
    RightSuper = 347,
    Menu = 348,
    
    _Count
};

enum class AppKeyModifiers : uint32
{
    None = 0,
    Shift = 0x1,
    Ctrl = 0x2,
    Alt = 0x4,
    Super = 0x8
};
ENABLE_BITMASK(AppKeyModifiers);

enum class AppMouseButton : int
{
    Invalid = -1,
    Left = 0,
    Right = 1,
    Middle = 2,
    _Count = 3
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
    NotAllowed
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
    _Count,
};

enum class AppFramebufferTransform : uint32
{
    None = 0,
    Rotate90,
    Rotate180,
    Rotate270
};

struct AppTouchPoint
{
    uintptr id;
    float posX;
    float posY;
    bool changed;
};

struct AppEvent
{
    AppEventType type;
    AppKeycode keycode;
    uint32 charcode;
    bool keyRepeat;
    AppKeyModifiers keyMods;
    float mouseX;
    float mouseY;
    AppMouseButton mouseButton;
    float scrollX;
    float scrollY;
    uint32 numTouches;
    AppTouchPoint touches[kAppMaxTouchPoints];
    uint16 windowWidth;
    uint16 windowHeight;
    uint16 framebufferWidth;
    uint16 framebufferHeight;
};

using appOnEventCallback = void(*)(const AppEvent& ev, void* userData);

struct NO_VTABLE AppCallbacks
{
    virtual bool Initialize() = 0;
    virtual void Update(fl32 dt) = 0;
    virtual void Cleanup() = 0;
    virtual void OnEvent(const AppEvent& ev) = 0;
};

struct AppDesc 
{
    AppCallbacks* callbacks   = nullptr;
    uint16 width              = 1280;
    uint16 height             = 800;
    const char* windowTitle   = nullptr;
    bool highDPI              = false;
    bool fullscreen           = false;
    bool userCursor           = true;
    bool enableClipboard      = true;
    size_t clipboardSizeBytes = 4096;
};

struct AppDisplayInfo
{
    uint16 width;
    uint16 height;
    uint16 refreshRate;
    float dpiScale;
};

API bool appInitialize(const AppDesc& desc);
API void appShowMouse(bool visible);
API void appQuit();
API bool appSetClipboardString(const char* str);
API const char* appGetClipboardString();
API const char* appGetName();

API uint16 appGetWindowWidth();
API uint16 appGetWindowHeight();
API uint16 appGetFramebufferWidth();
API uint16 appGetFramebufferHeight();
API AppFramebufferTransform appGetFramebufferTransform();

API bool   appIsMouseShown();
API void   appCaptureMouse();
API void   appReleaseMouse();
API void*  appGetNativeWindowHandle();
API void*  appGetNativeAppHandle();
API void   appRegisterEventsCallback(appOnEventCallback callback, void* userData = nullptr);
API void   appUnregisterEventsCallback(appOnEventCallback callback);
API void   appSetCursor(AppMouseCursor cursor);
API AppDisplayInfo appGetDisplayInfo();
API bool appIsKeyDown(AppKeycode keycode);
API bool appIsAnyKeysDown(const AppKeycode* keycodes, uint32 numKeycodes);
API AppKeyModifiers appGetKeyMods();

#if PLATFORM_ANDROID
API AAssetManager*  appAndroidGetAssetManager();
API void appAndroidSetFramebufferTransform(AppFramebufferTransform transform);
API ANativeActivity* appAndroidGetActivity();
#endif 
