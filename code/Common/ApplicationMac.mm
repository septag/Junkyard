#include "Application.h"

#if PLATFORM_APPLE

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include "../Core/System.h"
#include "../Core/StringUtil.h"
#include "../Core/Allocators.h"
#include "../Core/Log.h"
#include "../Core/Debug.h"
#include "../Core/Arrays.h"

#include "JunkyardSettings.h"
#include "VirtualFS.h"
#include "RemoteServices.h"

#include "../Engine.h"

inline constexpr uint32 APP_MAX_KEY_CODES = 512;

#define APP_ABS(a) (((a)<0.0f)?-(a):(a))

@interface AppWindowDelegate : NSObject<NSWindowDelegate>
@end

@interface AppMacMetalViewDelegate : NSObject<MTKViewDelegate>
@end

@interface AppMacMetalView : MTKView
{
    NSTrackingArea* trackingArea;
}
@end

struct AppEventCallbackPair
{
    AppEventCallback callback;
    void* userData;
};

struct AppMacState
{
    char name[32];
    NSApplication* app;
    NSWindow* window;
    AppWindowDelegate* windowDelegate;
    AppMacMetalView* view;
    AppMacMetalViewDelegate* viewDelegate;
    id<MTLDevice> metalDevice;
    uint32 flagsChanged;
    AppMouseCursor curCursor = AppMouseCursor::Arrow;
    
    bool valid;
    uint16 displayWidth;
    uint16 displayHeight;
    uint16 displayRefreshRate;
    uint16 windowWidth;
    uint16 windowHeight;
    uint16 framebufferWidth;
    uint16 framebufferHeight;
    fl32 dpiScale;
    char windowTitle[128];
    fl32 mouseX;
    fl32 mouseY;
    bool mouseTracked;
    AppEvent ev;
    AppDesc desc;
    InputKeycode keycodes[APP_MAX_KEY_CODES];
    size_t clipboardSize;
    char* clipboard;
    Array<AppEventCallbackPair> eventCallbacks;
    Pair<AppUpdateOverrideCallback, void*> overrideUpdateCallback;
    fl32 windowScale;
    fl32 contentScale;
    fl32 mouseScale;
    InputKeyModifiers keyMods;
    bool keysPressed[APP_MAX_KEY_CODES];
    bool firstFrame;
    bool initCalled;
    bool cleanupCalled;
    bool eventConsumed;
    bool clipboardEnabled;
    bool iconified;
    bool mouseDown;
    bool shouldQuit;
};

static AppMacState gApp;

namespace App
{
    static void _InitKeyTable()
    {
        gApp.keycodes[0x1D] = InputKeycode::NUM0;
        gApp.keycodes[0x12] = InputKeycode::NUM1;
        gApp.keycodes[0x13] = InputKeycode::NUM2;
        gApp.keycodes[0x14] = InputKeycode::NUM3;
        gApp.keycodes[0x15] = InputKeycode::NUM4;
        gApp.keycodes[0x17] = InputKeycode::NUM5;
        gApp.keycodes[0x16] = InputKeycode::NUM6;
        gApp.keycodes[0x1A] = InputKeycode::NUM7;
        gApp.keycodes[0x1C] = InputKeycode::NUM8;
        gApp.keycodes[0x19] = InputKeycode::NUM9;
        gApp.keycodes[0x00] = InputKeycode::A;
        gApp.keycodes[0x0B] = InputKeycode::B;
        gApp.keycodes[0x08] = InputKeycode::C;
        gApp.keycodes[0x02] = InputKeycode::D;
        gApp.keycodes[0x0E] = InputKeycode::E;
        gApp.keycodes[0x03] = InputKeycode::F;
        gApp.keycodes[0x05] = InputKeycode::G;
        gApp.keycodes[0x04] = InputKeycode::H;
        gApp.keycodes[0x22] = InputKeycode::I;
        gApp.keycodes[0x26] = InputKeycode::J;
        gApp.keycodes[0x28] = InputKeycode::K;
        gApp.keycodes[0x25] = InputKeycode::L;
        gApp.keycodes[0x2E] = InputKeycode::M;
        gApp.keycodes[0x2D] = InputKeycode::N;
        gApp.keycodes[0x1F] = InputKeycode::O;
        gApp.keycodes[0x23] = InputKeycode::P;
        gApp.keycodes[0x0C] = InputKeycode::Q;
        gApp.keycodes[0x0F] = InputKeycode::R;
        gApp.keycodes[0x01] = InputKeycode::S;
        gApp.keycodes[0x11] = InputKeycode::T;
        gApp.keycodes[0x20] = InputKeycode::U;
        gApp.keycodes[0x09] = InputKeycode::V;
        gApp.keycodes[0x0D] = InputKeycode::W;
        gApp.keycodes[0x07] = InputKeycode::X;
        gApp.keycodes[0x10] = InputKeycode::Y;
        gApp.keycodes[0x06] = InputKeycode::Z;
        gApp.keycodes[0x27] = InputKeycode::Apostrophe;
        gApp.keycodes[0x2A] = InputKeycode::Backslash;
        gApp.keycodes[0x2B] = InputKeycode::Comma;
        gApp.keycodes[0x18] = InputKeycode::Equal;
        gApp.keycodes[0x32] = InputKeycode::GraveAccent;
        gApp.keycodes[0x21] = InputKeycode::LeftBracket;
        gApp.keycodes[0x1B] = InputKeycode::Minus;
        gApp.keycodes[0x2F] = InputKeycode::Period;
        gApp.keycodes[0x1E] = InputKeycode::RightBracket;
        gApp.keycodes[0x29] = InputKeycode::Semicolon;
        gApp.keycodes[0x2C] = InputKeycode::Slash;
        gApp.keycodes[0x0A] = InputKeycode::World1;
        gApp.keycodes[0x33] = InputKeycode::Backspace;
        gApp.keycodes[0x39] = InputKeycode::CapsLock;
        gApp.keycodes[0x75] = InputKeycode::Delete;
        gApp.keycodes[0x7D] = InputKeycode::Down;
        gApp.keycodes[0x77] = InputKeycode::End;
        gApp.keycodes[0x24] = InputKeycode::Enter;
        gApp.keycodes[0x35] = InputKeycode::Escape;
        gApp.keycodes[0x7A] = InputKeycode::F1;
        gApp.keycodes[0x78] = InputKeycode::F2;
        gApp.keycodes[0x63] = InputKeycode::F3;
        gApp.keycodes[0x76] = InputKeycode::F4;
        gApp.keycodes[0x60] = InputKeycode::F5;
        gApp.keycodes[0x61] = InputKeycode::F6;
        gApp.keycodes[0x62] = InputKeycode::F7;
        gApp.keycodes[0x64] = InputKeycode::F8;
        gApp.keycodes[0x65] = InputKeycode::F9;
        gApp.keycodes[0x6D] = InputKeycode::F10;
        gApp.keycodes[0x67] = InputKeycode::F11;
        gApp.keycodes[0x6F] = InputKeycode::F12;
        gApp.keycodes[0x69] = InputKeycode::F13;
        gApp.keycodes[0x6B] = InputKeycode::F14;
        gApp.keycodes[0x71] = InputKeycode::F15;
        gApp.keycodes[0x6A] = InputKeycode::F16;
        gApp.keycodes[0x40] = InputKeycode::F17;
        gApp.keycodes[0x4F] = InputKeycode::F18;
        gApp.keycodes[0x50] = InputKeycode::F19;
        gApp.keycodes[0x5A] = InputKeycode::F20;
        gApp.keycodes[0x73] = InputKeycode::Home;
        gApp.keycodes[0x72] = InputKeycode::Insert;
        gApp.keycodes[0x7B] = InputKeycode::Left;
        gApp.keycodes[0x3A] = InputKeycode::LeftAlt;
        gApp.keycodes[0x3B] = InputKeycode::LeftControl;
        gApp.keycodes[0x38] = InputKeycode::LeftShift;
        gApp.keycodes[0x37] = InputKeycode::LeftSuper;
        gApp.keycodes[0x6E] = InputKeycode::Menu;
        gApp.keycodes[0x47] = InputKeycode::NumLock;
        gApp.keycodes[0x79] = InputKeycode::PageDown;
        gApp.keycodes[0x74] = InputKeycode::PageUp;
        gApp.keycodes[0x7C] = InputKeycode::Right;
        gApp.keycodes[0x3D] = InputKeycode::RightAlt;
        gApp.keycodes[0x3E] = InputKeycode::RightControl;
        gApp.keycodes[0x3C] = InputKeycode::RightShift;
        gApp.keycodes[0x36] = InputKeycode::RightSuper;
        gApp.keycodes[0x31] = InputKeycode::Space;
        gApp.keycodes[0x30] = InputKeycode::Tab;
        gApp.keycodes[0x7E] = InputKeycode::Up;
        gApp.keycodes[0x52] = InputKeycode::KP0;
        gApp.keycodes[0x53] = InputKeycode::KP1;
        gApp.keycodes[0x54] = InputKeycode::KP2;
        gApp.keycodes[0x55] = InputKeycode::KP3;
        gApp.keycodes[0x56] = InputKeycode::KP4;
        gApp.keycodes[0x57] = InputKeycode::KP5;
        gApp.keycodes[0x58] = InputKeycode::KP6;
        gApp.keycodes[0x59] = InputKeycode::KP7;
        gApp.keycodes[0x5B] = InputKeycode::KP8;
        gApp.keycodes[0x5C] = InputKeycode::KP9;
        gApp.keycodes[0x45] = InputKeycode::KPAdd;
        gApp.keycodes[0x41] = InputKeycode::KPDecimal;
        gApp.keycodes[0x4B] = InputKeycode::KPDivide;
        gApp.keycodes[0x4C] = InputKeycode::KPEnter;
        gApp.keycodes[0x51] = InputKeycode::KPEqual;
        gApp.keycodes[0x43] = InputKeycode::KPMultiply;
        gApp.keycodes[0x4E] = InputKeycode::KPSubtract;
    }

    static void _UpdateDimensions()
    {
        const CGSize fbSize = [gApp.view drawableSize];
        gApp.framebufferWidth = fbSize.width;
        gApp.framebufferHeight = fbSize.height;
        
        const NSRect bounds = [gApp.view bounds];
        gApp.windowWidth = bounds.size.width;
        gApp.windowHeight = bounds.size.height;
        ASSERT((gApp.framebufferWidth > 0) && (gApp.framebufferHeight > 0));
        gApp.dpiScale = (float)gApp.framebufferWidth / (float)gApp.windowWidth;
    }

    static void _UpdateFrame()
    {
        static uint64 tmPrev = 0;
        const NSPoint mousePos = [gApp.window mouseLocationOutsideOfEventStream];
        gApp.mouseX = mousePos.x * gApp.dpiScale;
        gApp.mouseY = gApp.framebufferHeight - (mousePos.y * gApp.dpiScale) - 1;
        
        uint64 tmNow = Timer::GetTicks();
        double dt = Timer::ToSec(Timer::Diff(Timer::GetTicks(), tmPrev));
        
        if (gApp.firstFrame) {
            gApp.firstFrame = false;
            if (!gApp.desc.callbacks->Initialize()) {
                App::Quit();
                return;
            }
            
            Engine::_private::PostInitialize();
            
            dt = 0;
            gApp.initCalled = true;
        }
        
        if (!gApp.overrideUpdateCallback.first)
            gApp.desc.callbacks->Update(dt);
        else
            gApp.overrideUpdateCallback.first(dt, gApp.overrideUpdateCallback.second);
        
        tmPrev = tmNow;
    }

    static InputKeyModifiers _GetKeyMods(NSEventModifierFlags f)
    {
        InputKeyModifiers m = InputKeyModifiers::None;
        if (f & NSEventModifierFlagShift)
            m |= InputKeyModifiers::Shift;
        if (f & NSEventModifierFlagControl)
            m |= InputKeyModifiers::Ctrl;
        if (f & NSEventModifierFlagOption)
            m |= InputKeyModifiers::Alt;
        if (f & NSEventModifierFlagCommand)
            m |= InputKeyModifiers::Super;
        return m;
    }

    static inline bool _EventsEnabled()
    {
        // only send events when an event callback is set, and the init function was called
        return gApp.desc.callbacks && gApp.initCalled;
    }

    static void _InitEvent(AppEventType type)
    {
        memset(&gApp.ev, 0, sizeof(gApp.ev));
        gApp.ev.type = type;
        gApp.ev.mouseButton = InputMouseButton::Invalid;
        gApp.ev.windowWidth = gApp.windowWidth;
        gApp.ev.windowHeight = gApp.windowHeight;
        gApp.ev.framebufferWidth = gApp.framebufferWidth;
        gApp.ev.framebufferHeight = gApp.framebufferHeight;
    }

    static bool _CallEvent(const AppEvent& ev)
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

    static void _DispatchMouseEvent(AppEventType type, InputMouseButton btn, InputKeyModifiers mod)
    {
        if (App::_EventsEnabled()) {
            _InitEvent(type);
            gApp.ev.mouseButton = btn;
            gApp.ev.keyMods = mod;
            gApp.ev.mouseX = gApp.mouseX;
            gApp.ev.mouseY = gApp.mouseY;
            _CallEvent(gApp.ev);
        }
    }

    static void _DispatchKeyEvent(AppEventType type, InputKeycode key, bool repeat, InputKeyModifiers mod)
    {
        if (App::_EventsEnabled()) {
            _InitEvent(type);
            gApp.ev.keycode = key;
            gApp.ev.keyRepeat = repeat;
            gApp.ev.keyMods = mod;
            _CallEvent(gApp.ev);
        }
    }

    static void _DispatchAppEvent(AppEventType type)
    {
        if (App::_EventsEnabled()) {
            _InitEvent(type);
            _CallEvent(gApp.ev);
        }
    }

    static InputKeycode _TranslateKey(int scanCode)
    {
        if ((scanCode >= 0) && (scanCode <APP_MAX_KEY_CODES))
            return gApp.keycodes[scanCode];
        else
            return InputKeycode::Invalid;
    }

} // App

bool App::Run(const AppDesc& desc)
{
    gApp.desc = desc;
    gApp.desc.initWidth = desc.initWidth;
    gApp.desc.initHeight = desc.initHeight;
    gApp.desc.clipboardSizeBytes = desc.clipboardSizeBytes;
    
    // TODO: currently, I don't quite understand how highDPI works on Mac
    //       So forcefully disable highDPI for now
    gApp.desc.highDPI = false;
    
    gApp.firstFrame = true;
    gApp.windowWidth = gApp.desc.initWidth;
    gApp.windowHeight = gApp.desc.initHeight;
    gApp.framebufferWidth = gApp.desc.initWidth;
    gApp.framebufferHeight = gApp.desc.initHeight;
    gApp.dpiScale = 1.0f;
    gApp.clipboardEnabled = desc.enableClipboard;
    if (desc.enableClipboard)
        gApp.clipboard = (char*)Mem::Alloc(gApp.desc.clipboardSizeBytes);
    
    if (desc.windowTitle)
        Str::Copy(gApp.windowTitle, sizeof(gApp.windowTitle), desc.windowTitle);
    else
        Str::Copy(gApp.windowTitle, sizeof(gApp.windowTitle), "Junkyard");
    
    char moduleFilename[CONFIG_MAX_PATH];
    OS::GetMyPath(moduleFilename, sizeof(moduleFilename));
    PathUtils::GetFilename(moduleFilename, moduleFilename, sizeof(moduleFilename));
    Str::Copy(gApp.name, sizeof(gApp.name), moduleFilename);
    
    _InitKeyTable();
    
    // Initialize settings if not initialied before
    // Since this is not a recommended way, we also throw an assert
    if (!SettingsJunkyard::IsInitialized()) {
        ASSERT_MSG(0, "Settings must be initialized before this call. See settingsInitialize() function");
        SettingsJunkyard::Initialize({});
    }
    
    // Set some initial settings
    const SettingsJunkyard& settings = SettingsJunkyard::Get();
    Mem::EnableMemPro(settings.engine.enableMemPro);
    MemTempAllocator::EnableCallstackCapture(settings.debug.captureStacktraceForTempAllocator);
    Debug::SetCaptureStacktraceForFiberProtector(settings.debug.captureStacktraceForTempAllocator);
    Log::SetSettings((const LogLevel)settings.engine.logLevel, settings.engine.breakOnErrors, settings.engine.treatWarningsAsErrors);
    
    Thread::SetCurrentThreadPriority(ThreadPriority::Low); // Experimental
    
    // RemoteServices
    if (!Remote::Initialize()) {
        ASSERT_MSG(0, "Initializing RemoteServices failed");
        return false;
    }
    
    // VirutalFS -> Depends on RemoteServices for some functionality
    if (!Vfs::Initialize()) {
        ASSERT_MSG(0, "Initializing VirtualFS failed");
        return false;
    }
    
    Thread::SetCurrentThreadPriority(ThreadPriority::Normal);
    
    gApp.app = [NSApplication sharedApplication];
    NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
    [NSApp activateIgnoringOtherApps:YES];
    
    NSScreen* screen = NSScreen.mainScreen;
    NSRect screenRect = NSScreen.mainScreen.frame;
    
    gApp.displayWidth = screenRect.size.width;
    gApp.displayHeight = screenRect.size.height;
    if (@available(macOS 12.0, *)) {
        NSTimeInterval interval = [screen maximumRefreshInterval];
        gApp.displayRefreshRate = uint16(1.0f / interval);
    } else {
        gApp.displayRefreshRate = 60;
    }

    if (gApp.desc.fullscreen) {
        gApp.windowWidth = screenRect.size.width;
        gApp.windowHeight = screenRect.size.height;
    }
    
    if (gApp.desc.highDPI) {
        gApp.dpiScale = [screen backingScaleFactor];
        gApp.framebufferWidth = gApp.dpiScale * gApp.windowWidth;
        gApp.framebufferHeight = gApp.dpiScale * gApp.windowHeight;
    }
    else {
        gApp.framebufferWidth = gApp.windowWidth;
        gApp.framebufferHeight = gApp.windowHeight;
        gApp.dpiScale = 1.0f;
    }
    
    const NSUInteger style =
        NSWindowStyleMaskTitled |
        NSWindowStyleMaskClosable |
        NSWindowStyleMaskMiniaturizable |
        NSWindowStyleMaskResizable;
    NSRect windowRect = NSMakeRect(0, 0, gApp.windowWidth, gApp.windowHeight);
    gApp.window = [[NSWindow alloc]
        initWithContentRect:windowRect
        styleMask:style
        backing:NSBackingStoreBuffered
        defer:NO];
    
    gApp.window.title = [NSString stringWithUTF8String:gApp.windowTitle];
    gApp.window.acceptsMouseMovedEvents = YES;
    gApp.window.restorable = YES;
    gApp.windowDelegate = [[AppWindowDelegate alloc] init];
    gApp.window.delegate = gApp.windowDelegate;
    gApp.metalDevice = MTLCreateSystemDefaultDevice();
    gApp.viewDelegate = [[AppMacMetalViewDelegate alloc] init];
    gApp.view = [[AppMacMetalView alloc] init];
    [gApp.view updateTrackingAreas];
    gApp.view.delegate = gApp.viewDelegate;
    gApp.view.device = gApp.metalDevice;
    gApp.view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    gApp.view.depthStencilPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    gApp.window.contentView = gApp.view;
    [gApp.window makeFirstResponder:gApp.view];
    CGSize drawableSize = { (CGFloat) gApp.framebufferWidth, (CGFloat) gApp.framebufferHeight };
    gApp.view.drawableSize = drawableSize;
    App::_UpdateDimensions();
    // gApp.view.layer.magnificationFilter = kCAFilterNearest;
    gApp.valid = true;
    [gApp.window center];
    
    [gApp.window makeKeyAndOrderFront:nil];
    [gApp.window setReleasedWhenClosed:NO];
    
    NSEvent *event;
    while (!gApp.shouldQuit) {
        event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                   untilDate:[NSDate distantFuture]
                                      inMode:NSDefaultRunLoopMode
                                     dequeue:YES];
        [NSApp sendEvent:event];
        [NSApp updateWindows];
    }
    
    if (gApp.desc.callbacks) {
        gApp.desc.callbacks->Cleanup();
    }
    
    if (gApp.clipboardEnabled) {
        ASSERT(gApp.clipboard);
        Mem::Free(gApp.clipboard);
    }
    
    gApp.eventCallbacks.Free();
    memset(&gApp, 0x0, sizeof(AppMacState));
    
    return true;
}

bool App::SetClipboardString(const char* str)
{
    if (!gApp.clipboardEnabled) {
        return false;
    }
    
    @autoreleasepool
    {
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard declareTypes:@[NSPasteboardTypeString] owner:nil];
        [pasteboard setString:@(str) forType:NSPasteboardTypeString];
    }
    
    Str::Copy(gApp.clipboard, (uint32)gApp.clipboardSize, str);
    return true;
}

void App::ShowMouse(bool visible)
{
    if (visible) {
        [NSCursor unhide];
    }
    else {
        [NSCursor hide];
    }
}

const char* App::GetClipboardString(void)
{
    if (!gApp.clipboardEnabled)
        return "";
    
    ASSERT(gApp.clipboard);
    @autoreleasepool
    {
        gApp.clipboard[0] = 0;
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        if (![[pasteboard types] containsObject:NSPasteboardTypeString]) {
            return gApp.clipboard;
        }
        NSString* str = [pasteboard stringForType:NSPasteboardTypeString];
        if (!str) {
            return gApp.clipboard;
        }
        Str::Copy(gApp.clipboard, (uint32)gApp.clipboardSize, [str UTF8String]);
    }
    return gApp.clipboard;
}

void App::Quit(void)
{
    [gApp.window performClose:nil];
    gApp.shouldQuit = true;
}

uint16 App::GetWindowWidth(void)
{
    return gApp.windowWidth;
}

uint16 App::GetWindowHeight(void)
{
    return gApp.windowHeight;
}

bool App::IsMouseShown(void)
{
    return false;       // TODO: Not implemented yet
}

void* App::GetNativeWindowHandle(void)
{
#if PLATFORM_OSX
    void* obj = (__bridge void*) gApp.window.contentView.layer;
    ASSERT(obj);
    return obj;
#else
    return 0;
#endif
}

uint16 App::GetFramebufferWidth()
{
    return gApp.framebufferWidth;
}

uint16 App::GetFramebufferHeight()
{
    return gApp.framebufferHeight;
}

AppFramebufferTransform App::GetFramebufferTransform()
{
    return AppFramebufferTransform::None;
}

// TODO: as far as I know, there is no easy way to Capture/Release the mouse when it goes outside the window on Mac
void App::CaptureMouse()
{
    SetCursor(AppMouseCursor::None);
}

// TODO
void App::ReleaseMouse()
{
    SetCursor(AppMouseCursor::Arrow);
}

void* App::GetNativeAppHandle()
{
    return (__bridge void*) gApp.app;
}

void App::RegisterEventsCallback(AppEventCallback callback, void* userData)
{
    bool alreadyExist = false;
    for (uint32 i = 0; i < gApp.eventCallbacks.Count(); i++) {
        if (callback == gApp.eventCallbacks[i].callback) {
            alreadyExist = true;
            break;
        }
    }
    
    ASSERT_MSG(!alreadyExist, "Callback function already exists in event callbacks");
    if (!alreadyExist)
        gApp.eventCallbacks.Push({callback, userData});
}

void App::UnregisterEventsCallback(AppEventCallback callback)
{
    uint32 index = gApp.eventCallbacks.FindIf([callback](const AppEventCallbackPair& p)->bool { return p.callback == callback; });
    if (index != -1)
        gApp.eventCallbacks.RemoveAndSwap(index);
}

void App::SetCursor(AppMouseCursor cursor)
{
    if (cursor == gApp.curCursor)
        return;
    gApp.curCursor = cursor;
    
    if (cursor != AppMouseCursor::None)
        [NSCursor unhide];
    
    switch (cursor) {
    case AppMouseCursor::None:          [NSCursor hide];       break;
    case AppMouseCursor::Arrow:         [[NSCursor arrowCursor] set];   break;
    case AppMouseCursor::TextInput:     [[NSCursor IBeamCursor] set];   break;
    case AppMouseCursor::ResizeAll:     [[NSCursor arrowCursor] set]; break;
    case AppMouseCursor::ResizeNS:      [[NSCursor resizeUpDownCursor] set];  break;
    case AppMouseCursor::ResizeWE:      [[NSCursor resizeLeftRightCursor] set]; break;
    case AppMouseCursor::ResizeNESW:    [[NSCursor arrowCursor] set]; break;
    case AppMouseCursor::ResizeNWSE:    [[NSCursor arrowCursor] set]; break;
    case AppMouseCursor::Hand:          [[NSCursor openHandCursor] set]; break;
    case AppMouseCursor::NotAllowed:    [[NSCursor operationNotAllowedCursor] set]; break;
    default: break;
    }
}

AppDisplayInfo App::GetDisplayInfo()
{
    return AppDisplayInfo {
        .width = gApp.displayWidth,
        .height = gApp.displayHeight,
        .refreshRate = gApp.displayRefreshRate,
        .dpiScale = gApp.dpiScale
    };
}

bool App::IsKeyDown(InputKeycode keycode)
{
    return gApp.keysPressed[uint32(keycode)];
}

bool App::IsAnyKeysDown(const InputKeycode* keycodes, uint32 numKeycodes)
{
    bool down = false;
    for (uint32 i = 0; i < numKeycodes; i++)
        down |= gApp.keysPressed[uint32(keycodes[i])];
    return down;
}

InputKeyModifiers App::GetKeyMods()
{
    return gApp.keyMods;
}

const char* App::GetName()
{
    return gApp.name;
}

void App::OverrideUpdateCallback(AppUpdateOverrideCallback callback, void* userData)
{
    gApp.overrideUpdateCallback.first = callback;
    gApp.overrideUpdateCallback.second = userData;
}

//----------------------------------------------------------------------------------------------------------------------
@implementation AppWindowDelegate
    - (BOOL)windowShouldClose:(id)sender
    {
        return YES;
    }

    - (void)windowWillClose:(NSNotification *)notification
{
        gApp.shouldQuit = true;
    }

    - (void)windowDidResize:(NSNotification*)notification
    {
        if (!gApp.mouseDown) {
            App::_UpdateDimensions();
            App::_DispatchAppEvent(AppEventType::Resized);
        }
    }

    - (void)windowDidMiniaturize:(NSNotification*)notification
    {
        App::_DispatchAppEvent(AppEventType::Iconified);
        gApp.iconified = true;
    }

    - (void)windowDidDeminiaturize:(NSNotification*)notification
    {
        App::_DispatchAppEvent(AppEventType::Restored);
        gApp.iconified = false;
    }
@end // AppWindowDelegate

//----------------------------------------------------------------------------------------------------------------------
@implementation AppMacMetalViewDelegate
    - (void)drawInMTKView:(MTKView*)view
    {
        @autoreleasepool
        {
            App::_UpdateFrame();
        }
    }

    - (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size
    {
        // this is required by the protocol, but we can't do anything useful here
    }
@end // AppMacMetalViewDelegate

//----------------------------------------------------------------------------------------------------------------------
@implementation AppMacMetalView
    - (BOOL)isOpaque
    {
        return YES;
    }

    - (BOOL)canBecomeKey
    {
        return YES;
    }

    - (BOOL)acceptsFirstResponder
    {
        return YES;
    }

    - (void)updateTrackingAreas
    {
        if (trackingArea != nil) {
            [self removeTrackingArea:trackingArea];
            trackingArea = nil;
        }
        const NSTrackingAreaOptions options = NSTrackingMouseEnteredAndExited |
                                              NSTrackingActiveInKeyWindow |
                                              NSTrackingEnabledDuringMouseDrag |
                                              NSTrackingCursorUpdate |
                                              NSTrackingInVisibleRect |
                                              NSTrackingAssumeInside;
        trackingArea = [[NSTrackingArea alloc] initWithRect:[self bounds] options:options owner:self userInfo:nil];
        [self addTrackingArea:trackingArea];
        [super updateTrackingAreas];
    }

    - (void)mouseEntered:(NSEvent*)event
    {
        App::_DispatchMouseEvent(AppEventType::MouseEnter, InputMouseButton::Invalid, App::_GetKeyMods(event.modifierFlags));
    }

    - (void)mouseExited:(NSEvent*)event
    {
        App::_DispatchMouseEvent(AppEventType::MouseLeave, InputMouseButton::Invalid, App::_GetKeyMods(event.modifierFlags));
    }

    - (void)mouseDown:(NSEvent*)event
    {
        gApp.mouseDown = true;
        App::_DispatchMouseEvent(AppEventType::MouseDown, InputMouseButton::Left, App::_GetKeyMods(event.modifierFlags));
    }

    - (void)mouseUp:(NSEvent*)event
    {
        gApp.mouseDown = false;
        App::_DispatchMouseEvent(AppEventType::MouseUp, InputMouseButton::Left, App::_GetKeyMods(event.modifierFlags));
    }

    - (void)rightMouseDown:(NSEvent*)event
    {
        App::_DispatchMouseEvent(AppEventType::MouseDown, InputMouseButton::Right, App::_GetKeyMods(event.modifierFlags));
    }

    - (void)rightMouseUp:(NSEvent*)event
    {
        App::_DispatchMouseEvent(AppEventType::MouseUp, InputMouseButton::Right, App::_GetKeyMods(event.modifierFlags));
    }

    - (void)otherMouseDown:(NSEvent*)event
    {
        if (2 == event.buttonNumber) {
            App::_DispatchMouseEvent(AppEventType::MouseDown, InputMouseButton::Middle, App::_GetKeyMods(event.modifierFlags));
        }
    }

    - (void)otherMouseUp:(NSEvent*)event
    {
        if (2 == event.buttonNumber) {
            App::_DispatchMouseEvent(AppEventType::MouseUp, InputMouseButton::Middle, App::_GetKeyMods(event.modifierFlags));
        }
    }

    - (void)mouseMoved:(NSEvent*)event
    {
        App::_DispatchMouseEvent(AppEventType::MouseMove, InputMouseButton::Invalid , App::_GetKeyMods(event.modifierFlags));
    }

    - (void)mouseDragged:(NSEvent*)event
    {
        App::_DispatchMouseEvent(AppEventType::MouseMove, InputMouseButton::Invalid , App::_GetKeyMods(event.modifierFlags));
    }

    - (void)rightMouseDragged:(NSEvent*)event
    {
        App::_DispatchMouseEvent(AppEventType::MouseMove, InputMouseButton::Invalid, App::_GetKeyMods(event.modifierFlags));
    }

    - (void)scrollWheel:(NSEvent*)event
    {
        if (App::_EventsEnabled()) {
            float dx = (float) event.scrollingDeltaX;
            float dy = (float) event.scrollingDeltaY;
            if (event.hasPreciseScrollingDeltas) {
                dx *= 0.1;
                dy *= 0.1;
            }
            if ((APP_ABS(dx) > 0.0f) || (APP_ABS(dy) > 0.0f)) {
                App::_InitEvent(AppEventType::MouseScroll);
                gApp.ev.keyMods = App::_GetKeyMods(event.modifierFlags);
                gApp.ev.mouseX = gApp.mouseX;
                gApp.ev.mouseY = gApp.mouseY;
                gApp.ev.scrollX = dx;
                gApp.ev.scrollY = dy;
                App::_CallEvent(gApp.ev);
            }
        }
    }

    - (void)keyDown:(NSEvent*)event
    {
        if (App::_EventsEnabled()) {
            InputKeyModifiers mods = App::_GetKeyMods(event.modifierFlags);
            InputKeycode keyCode = App::_TranslateKey(event.keyCode);
            gApp.keyMods |= mods;
            gApp.keysPressed[uint32(keyCode)] = true;
            
            App::_DispatchKeyEvent(AppEventType::KeyDown, keyCode, event.isARepeat, mods);

            // NOTE: macOS doesn't send keyUp events while the Cmd key is pressed,
            //       as a workaround, to prevent key presses from sticking we'll send
            //       a keyup event following right after the keydown if SUPER is also pressed
            if ((mods & InputKeyModifiers::Super) == InputKeyModifiers::Super) {
                App::_DispatchKeyEvent(AppEventType::KeyUp, keyCode, event.isARepeat, mods);
            }
            
            const NSString* chars = event.characters;
            const NSUInteger len = chars.length;
            if (len > 0) {
                App::_InitEvent(AppEventType::Char);
                gApp.ev.keyMods = mods;
                for (NSUInteger i = 0; i < len; i++) {
                    const unichar codepoint = [chars characterAtIndex:i];
                    if ((codepoint & 0xFF00) == 0xF700) {
                        continue;
                    }
                    gApp.ev.charcode = codepoint;
                    gApp.ev.keyRepeat = event.isARepeat;
                    App::_CallEvent(gApp.ev);
                }
            }
            
            // if this is a Cmd+V (paste), also send a CLIPBOARD_PASTE event
            if (gApp.clipboard && (mods == InputKeyModifiers::Super) && (keyCode == InputKeycode::V)) {
                App::_InitEvent(AppEventType::ClipboardPasted);
                App::_CallEvent(gApp.ev);
            }
        }
    }

    - (void)keyUp:(NSEvent*)event
    {
        InputKeyModifiers mods = App::_GetKeyMods(event.modifierFlags);
        InputKeycode keyCode = App::_TranslateKey(event.keyCode);
        gApp.keysPressed[uint32(keyCode)] = false;
        gApp.keyMods &= ~mods;

        App::_DispatchKeyEvent(AppEventType::KeyUp, keyCode, event.isARepeat, App::_GetKeyMods(event.modifierFlags));
    }

    - (void)flagsChanged:(NSEvent*)event
    {
        const uint32 old_f = gApp.flagsChanged;
        const uint32 new_f = (uint32)event.modifierFlags;
        gApp.flagsChanged = new_f;
        InputKeycode key_code = InputKeycode::Invalid;
        bool down = false;
        if ((new_f ^ old_f) & NSEventModifierFlagShift) {
            key_code = InputKeycode::LeftShift;
            down = 0 != (new_f & NSEventModifierFlagShift);
        }
        if ((new_f ^ old_f) & NSEventModifierFlagControl) {
            key_code = InputKeycode::LeftControl;
            down = 0 != (new_f & NSEventModifierFlagControl);
        }
        if ((new_f ^ old_f) & NSEventModifierFlagOption) {
            key_code = InputKeycode::LeftAlt;
            down = 0 != (new_f & NSEventModifierFlagOption);
        }
        if ((new_f ^ old_f) & NSEventModifierFlagCommand) {
            key_code = InputKeycode::LeftSuper;
            down = 0 != (new_f & NSEventModifierFlagCommand);
        }
        if (key_code != InputKeycode::Invalid)
            App::_DispatchKeyEvent(down ? AppEventType::KeyDown : AppEventType::KeyUp, key_code, false, App::_GetKeyMods(event.modifierFlags));
    }

    - (void)cursorUpdate:(NSEvent*)event
    {
        if (gApp.desc.userCursor)
            App::_DispatchAppEvent(AppEventType::UpdateCursor);
    }
@end // AppMacMetalView



#endif // PLATFORM_APPLE

