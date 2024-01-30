#pragma once

#include "../Core/Base.h"

#include "InputTypes.h"

#if PLATFORM_ANDROID
typedef struct AAssetManager AAssetManager; // <android/asset_manager.h>
typedef struct ANativeActivity ANativeActivity; // <android/native_activity.h>
#endif

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

struct AppEvent
{
    AppEventType type;
    InputKeycode keycode;
    uint32 charcode;
    bool keyRepeat;
    InputKeyModifiers keyMods;
    float mouseX;
    float mouseY;
    InputMouseButton mouseButton;
    float scrollX;
    float scrollY;
    uint32 numTouches;
    InputTouchPoint touches[kInputMaxTouchPoints];
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

API bool appIsMouseShown();
API void appCaptureMouse();
API void appReleaseMouse();
API void* appGetNativeWindowHandle();
API void* appGetNativeAppHandle();
API void appRegisterEventsCallback(appOnEventCallback callback, void* userData = nullptr);
API void appUnregisterEventsCallback(appOnEventCallback callback);
API void appSetCursor(AppMouseCursor cursor);
API AppDisplayInfo appGetDisplayInfo();
API bool appIsKeyDown(InputKeycode keycode);
API bool appIsAnyKeysDown(const InputKeycode* keycodes, uint32 numKeycodes);
API InputKeyModifiers appGetKeyMods();

#if PLATFORM_ANDROID
API AAssetManager* appAndroidGetAssetManager();
API void appAndroidSetFramebufferTransform(AppFramebufferTransform transform);
API ANativeActivity* appAndroidGetActivity();
#endif 
