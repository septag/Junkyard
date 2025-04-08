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
    InputTouchPoint touches[INPUT_MAX_TOUCH_POINTS];
    uint16 windowWidth;
    uint16 windowHeight;
    uint16 framebufferWidth;
    uint16 framebufferHeight;
};

using AppEventCallback = void(*)(const AppEvent& ev, void* userData);
using AppUpdateOverrideCallback = void(*)(float dt, void* userData);

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
    bool highDPI              = true;
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

namespace App
{
    API bool Run(const AppDesc& desc);
    API void ShowMouse(bool visible);
    API void Quit();
    API bool SetClipboardString(const char* str);
    API const char* GetClipboardString();
    API const char* GetName();

    API uint16 GetWindowWidth();
    API uint16 GetWindowHeight();
    API uint16 GetFramebufferWidth();
    API uint16 GetFramebufferHeight();
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

    #if PLATFORM_ANDROID
    API AAssetManager* AndroidGetAssetManager();
    API void AndroidSetFramebufferTransform(AppFramebufferTransform transform);
    API ANativeActivity* AndroidGetActivity();
    #endif 
}

