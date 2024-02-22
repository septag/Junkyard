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

inline constexpr uint32 kMaxKeycodes = 512;

#define APP_ABS(a) (((a)<0.0f)?-(a):(a))

@interface appMacDelegate : NSObject<NSApplicationDelegate>
@end

@interface appWindowDelegate : NSObject<NSWindowDelegate>
@end

@interface appMacMetalViewDelegate : NSObject<MTKViewDelegate>
@end

@interface appMacMetalView : MTKView
{
    NSTrackingArea* trackingArea;
}
@end

struct AppEventCallbackPair
{
    appOnEventCallback callback;
    void*              userData;
};

struct AppMacState
{
    NSApplication* app;
    NSWindow* window;
    appWindowDelegate* windowDelegate;
    appMacDelegate* appDelegate;
    appMacMetalView* view;
    appMacMetalViewDelegate* viewDelegate;
    id<MTLDevice> metalDevice;
    uint32 flagsChanged;
    
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
    uint64 frameCount;
    fl32 mouseX;
    fl32 mouseY;
    bool mouseTracked;
    AppEvent ev;
    AppDesc desc;
    InputKeycode keycodes[kMaxKeycodes];
    size_t clipboardSize;
    char* clipboard;
    Array<AppEventCallbackPair> eventCallbacks;
    fl32 windowScale;
    fl32 contentScale;
    fl32 mouseScale;
    InputKeyModifiers keyMods;
    bool keysPressed[kMaxKeycodes];
    bool firstFrame;
    bool initCalled;
    bool cleanupCalled;
    bool quitRequested;
    bool quitOrdered;
    bool eventConsumed;
    bool clipboardEnabled;
    bool iconified;
    bool mouseDown;
};

static AppMacState gApp;

static void appMacInitKeyTable() {
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

bool appInitialize(const AppDesc& desc)
{
    gApp.desc = desc;
    gApp.desc.width = desc.width;
    gApp.desc.height = desc.height;
    gApp.desc.clipboardSizeBytes = desc.clipboardSizeBytes;

    gApp.firstFrame = true;
    gApp.windowWidth = gApp.desc.width;
    gApp.windowHeight = gApp.desc.height;
    gApp.framebufferWidth = gApp.desc.width;
    gApp.framebufferHeight = gApp.desc.height;
    gApp.dpiScale = 1.0f;
    gApp.clipboardEnabled = desc.enableClipboard;
    if (desc.enableClipboard)
        gApp.clipboard = (char*)memAllocZero(gApp.desc.clipboardSizeBytes);

    if (desc.windowTitle)
        strCopy(gApp.windowTitle, sizeof(gApp.windowTitle), desc.windowTitle);
    else
        strCopy(gApp.windowTitle, sizeof(gApp.windowTitle), "Junkyard");
    
    appMacInitKeyTable();
    
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

    gApp.app = [NSApplication sharedApplication];
    
    NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
    gApp.appDelegate = [[appMacDelegate alloc] init];
    NSApp.delegate = gApp.appDelegate;
    [NSApp activateIgnoringOtherApps:YES];
    [NSApp run];
    
    return true;
}

static void appMacUpdateDimensions()
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

static bool appMacFrameUpdate(fl32 dt)
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

static void appMacFrame()
{
    static uint64 tmPrev = 0;
    const NSPoint mousePos = [gApp.window mouseLocationOutsideOfEventStream];
    gApp.mouseX = mousePos.x * gApp.dpiScale;
    gApp.mouseY = gApp.framebufferHeight - (mousePos.y * gApp.dpiScale) - 1;
    
    uint64 tmNow = timerGetTicks();
    if (!appMacFrameUpdate(!gApp.firstFrame ? (fl32)timerToSec(timerDiff(timerGetTicks(), tmPrev)) : 0)) {
        appQuit();
    }
    tmPrev = tmNow;

    if (gApp.quitRequested || gApp.quitOrdered) {
        [gApp.window performClose:nil];
    }
}

@implementation appMacDelegate
    - (void)applicationDidFinishLaunching:(NSNotification*)aNotification
    {
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
            gApp.framebufferWidth = 2 * gApp.windowWidth;
            gApp.framebufferHeight = 2 * gApp.windowHeight;
            gApp.dpiScale = [screen backingScaleFactor];
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
        gApp.windowDelegate = [[appWindowDelegate alloc] init];
        gApp.window.delegate = gApp.windowDelegate;
        gApp.metalDevice = MTLCreateSystemDefaultDevice();
        gApp.viewDelegate = [[appMacMetalViewDelegate alloc] init];
        gApp.view = [[appMacMetalView alloc] init];
        [gApp.view updateTrackingAreas];
        gApp.view.preferredFramesPerSecond = 60;
        gApp.view.delegate = gApp.viewDelegate;
        gApp.view.device = gApp.metalDevice;
        gApp.view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
        gApp.view.depthStencilPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        gApp.window.contentView = gApp.view;
        [gApp.window makeFirstResponder:gApp.view];
        if (!gApp.desc.highDPI) {
            CGSize drawableSize = { (CGFloat) gApp.framebufferWidth, (CGFloat) gApp.framebufferHeight };
            gApp.view.drawableSize = drawableSize;
        }
        appMacUpdateDimensions();
        // gApp.view.layer.magnificationFilter = kCAFilterNearest;
        gApp.valid = true;
        if (gApp.desc.fullscreen) {
            [gApp.window toggleFullScreen:self];
        }
        else {
            [gApp.window center];
        }
        [gApp.window makeKeyAndOrderFront:nil];
        [gApp.window setReleasedWhenClosed:NO];
    }

    - (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
    {
        return YES;
    }

    -(void)applicationWillTerminate:(NSNotification *)notification
    {
        if (gApp.desc.callbacks) {
            gApp.desc.callbacks->Cleanup();
        }
        
        if (gApp.clipboardEnabled) {
            ASSERT(gApp.clipboard);
            memFree(gApp.clipboard);
        }
        
        gApp.eventCallbacks.Free();
        memset(&gApp, 0x0, sizeof(AppMacState));
    }
@end // appMacDelegate

static InputKeyModifiers appMacKeyMods(NSEventModifierFlags f)
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

static inline bool appMacEventsEnabled(void)
{
    // only send events when an event callback is set, and the init function was called
    return gApp.desc.callbacks && gApp.initCalled;
}

static void appMacInitEvent(AppEventType type)
{
    memset(&gApp.ev, 0, sizeof(gApp.ev));
    gApp.ev.type = type;
    gApp.ev.mouseButton = InputMouseButton::Invalid;
    gApp.ev.windowWidth = gApp.windowWidth;
    gApp.ev.windowHeight = gApp.windowHeight;
    gApp.ev.framebufferWidth = gApp.framebufferWidth;
    gApp.ev.framebufferHeight = gApp.framebufferHeight;
}

static bool appMacCallEvent(const AppEvent& ev)
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

static void appMacDispatchMouseEvent(AppEventType type, InputMouseButton btn, InputKeyModifiers mod)
{
    if (appMacEventsEnabled()) {
        appMacInitEvent(type);
        gApp.ev.mouseButton = btn;
        gApp.ev.keyMods = mod;
        gApp.ev.mouseX = gApp.mouseX;
        gApp.ev.mouseY = gApp.mouseY;
        appMacCallEvent(gApp.ev);
    }
}

static void appMacDispatchKeyEvent(AppEventType type, InputKeycode key, bool repeat, InputKeyModifiers mod)
{
    if (appMacEventsEnabled()) {
        appMacInitEvent(type);
        gApp.ev.keycode = key;
        gApp.ev.keyRepeat = repeat;
        gApp.ev.keyMods = mod;
        appMacCallEvent(gApp.ev);
    }
}

static void appMacDispatchAppEvent(AppEventType type)
{
    if (appMacEventsEnabled()) {
        appMacInitEvent(type);
        appMacCallEvent(gApp.ev);
    }
}

static InputKeycode appTranslateKey(int scanCode)
{
    if ((scanCode >= 0) && (scanCode <kMaxKeycodes))
        return gApp.keycodes[scanCode];
    else
        return InputKeycode::Invalid;
}

@implementation appWindowDelegate
    - (BOOL)windowShouldClose:(id)sender
    {
        // only give user-code a chance to intervene when sapp_quit() wasn't already called
        if (!gApp.quitOrdered) {
            // if window should be closed and event handling is enabled, give user code
            // a chance to intervene via sapp_cancel_quit()
            gApp.quitRequested = true;
            // user code hasn't intervened, quit the app
            if (gApp.quitRequested) {
                gApp.quitOrdered = true;
            }
        }
        
        return gApp.quitOrdered ? YES : NO;
    }

    - (void)windowDidResize:(NSNotification*)notification
    {
        if (!gApp.mouseDown) {
            appMacUpdateDimensions();
            appMacDispatchAppEvent(AppEventType::Resized);
        }
    }

    - (void)windowDidMiniaturize:(NSNotification*)notification
    {
        appMacDispatchAppEvent(AppEventType::Iconified);
        gApp.iconified = true;
    }

    - (void)windowDidDeminiaturize:(NSNotification*)notification
    {
        appMacDispatchAppEvent(AppEventType::Restored);
        gApp.iconified = false;
    }
@end // appWindowDelegate

@implementation appMacMetalViewDelegate
    - (void)drawInMTKView:(MTKView*)view
    {
        @autoreleasepool
        {
            appMacFrame();
        }
    }

    - (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size
    {
        // this is required by the protocol, but we can't do anything useful here
    }
@end // appMacMetalViewDelegate

@implementation appMacMetalView
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
        appMacDispatchMouseEvent(AppEventType::MouseEnter, InputMouseButton::Invalid, appMacKeyMods(event.modifierFlags));
    }

    - (void)mouseExited:(NSEvent*)event
    {
        appMacDispatchMouseEvent(AppEventType::MouseLeave, InputMouseButton::Invalid, appMacKeyMods(event.modifierFlags));
    }

    - (void)mouseDown:(NSEvent*)event
    {
        gApp.mouseDown = true;
        appMacDispatchMouseEvent(AppEventType::MouseDown, InputMouseButton::Left, appMacKeyMods(event.modifierFlags));
    }

    - (void)mouseUp:(NSEvent*)event
    {
        gApp.mouseDown = false;
        appMacDispatchMouseEvent(AppEventType::MouseUp, InputMouseButton::Left, appMacKeyMods(event.modifierFlags));
    }

    - (void)rightMouseDown:(NSEvent*)event
    {
        appMacDispatchMouseEvent(AppEventType::MouseDown, InputMouseButton::Right, appMacKeyMods(event.modifierFlags));
    }

    - (void)rightMouseUp:(NSEvent*)event
    {
        appMacDispatchMouseEvent(AppEventType::MouseUp, InputMouseButton::Right, appMacKeyMods(event.modifierFlags));
    }

    - (void)otherMouseDown:(NSEvent*)event
    {
        if (2 == event.buttonNumber) {
            appMacDispatchMouseEvent(AppEventType::MouseDown, InputMouseButton::Middle, appMacKeyMods(event.modifierFlags));
        }
    }

    - (void)otherMouseUp:(NSEvent*)event
    {
        if (2 == event.buttonNumber) {
            appMacDispatchMouseEvent(AppEventType::MouseUp, InputMouseButton::Middle, appMacKeyMods(event.modifierFlags));
        }
    }

    - (void)mouseMoved:(NSEvent*)event
    {
        appMacDispatchMouseEvent(AppEventType::MouseMove, InputMouseButton::Invalid , appMacKeyMods(event.modifierFlags));
    }

    - (void)mouseDragged:(NSEvent*)event
    {
        appMacDispatchMouseEvent(AppEventType::MouseMove, InputMouseButton::Invalid , appMacKeyMods(event.modifierFlags));
    }

    - (void)rightMouseDragged:(NSEvent*)event
    {
        appMacDispatchMouseEvent(AppEventType::MouseMove, InputMouseButton::Invalid, appMacKeyMods(event.modifierFlags));
    }

    - (void)scrollWheel:(NSEvent*)event
    {
        if (appMacEventsEnabled()) {
            float dx = (float) event.scrollingDeltaX;
            float dy = (float) event.scrollingDeltaY;
            if (event.hasPreciseScrollingDeltas) {
                dx *= 0.1;
                dy *= 0.1;
            }
            if ((APP_ABS(dx) > 0.0f) || (APP_ABS(dy) > 0.0f)) {
                appMacInitEvent(AppEventType::MouseScroll);
                gApp.ev.keyMods = appMacKeyMods(event.modifierFlags);
                gApp.ev.mouseX = gApp.mouseX;
                gApp.ev.mouseY = gApp.mouseY;
                gApp.ev.scrollX = dx;
                gApp.ev.scrollY = dy;
                appMacCallEvent(gApp.ev);
            }
        }
    }

    - (void)keyDown:(NSEvent*)event
    {
        if (appMacEventsEnabled()) {
            InputKeyModifiers mods = appMacKeyMods(event.modifierFlags);
            InputKeycode keyCode = appTranslateKey(event.keyCode);
            gApp.keyMods |= mods;
            gApp.keysPressed[uint32(keyCode)] = true;
            
            appMacDispatchKeyEvent(AppEventType::KeyDown, keyCode, event.isARepeat, mods);

            // NOTE: macOS doesn't send keyUp events while the Cmd key is pressed,
            //       as a workaround, to prevent key presses from sticking we'll send
            //       a keyup event following right after the keydown if SUPER is also pressed
            if ((mods & InputKeyModifiers::Super) == InputKeyModifiers::Super) {
                appMacDispatchKeyEvent(AppEventType::KeyUp, keyCode, event.isARepeat, mods);
            }
            
            const NSString* chars = event.characters;
            const NSUInteger len = chars.length;
            if (len > 0) {
                appMacInitEvent(AppEventType::Char);
                gApp.ev.keyMods = mods;
                for (NSUInteger i = 0; i < len; i++) {
                    const unichar codepoint = [chars characterAtIndex:i];
                    if ((codepoint & 0xFF00) == 0xF700) {
                        continue;
                    }
                    gApp.ev.charcode = codepoint;
                    gApp.ev.keyRepeat = event.isARepeat;
                    appMacCallEvent(gApp.ev);
                }
            }
            
            // if this is a Cmd+V (paste), also send a CLIPBOARD_PASTE event
            if (gApp.clipboard && (mods == InputKeyModifiers::Super) && (keyCode == InputKeycode::V)) {
                appMacInitEvent(AppEventType::ClipboardPasted);
                appMacCallEvent(gApp.ev);
            }
        }
    }

    - (void)keyUp:(NSEvent*)event
    {
        InputKeyModifiers mods = appMacKeyMods(event.modifierFlags);
        InputKeycode keyCode = appTranslateKey(event.keyCode);
        gApp.keysPressed[uint32(keyCode)] = false;
        gApp.keyMods &= ~mods;

        appMacDispatchKeyEvent(AppEventType::KeyUp,
            keyCode,
            event.isARepeat,
            appMacKeyMods(event.modifierFlags));
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
        if (key_code != InputKeycode::Invalid) {
            appMacDispatchKeyEvent(down ? AppEventType::KeyDown : AppEventType::KeyUp,
                key_code,
                false,
                appMacKeyMods(event.modifierFlags));
        }
    }

    - (void)cursorUpdate:(NSEvent*)event
    {
        if (gApp.desc.userCursor)
            appMacDispatchAppEvent(AppEventType::UpdateCursor);
    }
@end // appMacMetalView

bool appSetClipboardString(const char* str)
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
    
    strCopy(gApp.clipboard, (uint32)gApp.clipboardSize, str);
    return true;
}

void appShowMouse(bool visible)
{
    if (visible) {
        [NSCursor unhide];
    }
    else {
        [NSCursor hide];
    }
}

const char* appGetClipboardString(void)
{
    if (!gApp.clipboardEnabled) {
        return "";
    }

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
        strCopy(gApp.clipboard, (uint32)gApp.clipboardSize, [str UTF8String]);
    }
    return gApp.clipboard;
}

void appRequestQuit(void)
{
    gApp.quitRequested = true;
}

void appCancelQuit(void)
{
    gApp.quitRequested = false;
}

void appQuit(void)
{
    gApp.quitOrdered = true;
}

uint16 appGetWindowWidth(void)
{
    return gApp.windowWidth;
}

uint16 appGetWindowHeight(void)
{
    return gApp.windowHeight;
}

bool appIsMouseShown(void)
{
    return false;       // TODO: Not implemented yet
}

void* appGetNativeWindowHandle(void)
{
    #if PLATFORM_OSX
        void* obj = (__bridge void*) gApp.window.contentView.layer;
        ASSERT(obj);
        return obj;
    #else
        return 0;
    #endif
}

uint16 appGetFramebufferWidth()
{
    return gApp.framebufferWidth;
}

uint16 appGetFramebufferHeight()
{
    return gApp.framebufferHeight;
}

AppFramebufferTransform appGetFramebufferTransform()
{
    return AppFramebufferTransform::None;
}

// TODO
void appCaptureMouse()
{
}

void appReleaseMouse()
{
}

void*  appGetNativeAppHandle()
{
    return (__bridge void*) gApp.app;
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

void appSetCursor(AppMouseCursor cursor)
{
    UNUSED(cursor);
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

InputKeyModifiers appGetKeyMods()
{
    return gApp.keyMods;
}

const char* appGetName()
{
    return "";
}

#endif // PLATFORM_APPLE

