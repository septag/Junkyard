#include "Application.h"

#if PLATFORM_APPLE

#include "Core/System.h"
#include "Core/String.h"

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#define MAX_KEYCODES 512
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

typedef struct AppMacState
{
    NSWindow* window;
    appWindowDelegate* windowDelegate;
    appMacDelegate* appDelegate;
    appMacMetalView* view;
    appMacMetalViewDelegate* viewDelegate;
    id<MTLDevice> metalDevice;
    uint32 flagsChanged;
    
    bool valid;
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
    AppKeycode keycodes[MAX_KEYCODES];
    size_t clipboardSize;
    char* clipboard;
    fl32 windowScale;
    fl32 contentScale;
    fl32 mouseScale;
    bool firstFrame;
    bool initCalled;
    bool cleanupCalled;
    bool quitRequested;
    bool quitOrdered;
    bool eventConsumed;
    bool clipboardEnabled;
    bool iconified;
} AppMacState;

static AppMacState gApp;

static void appMacInitKeyTable(void) {
    gApp.keycodes[0x1D] = AppKeyCode::NUM0;
    gApp.keycodes[0x12] = AppKeyCode::NUM1;
    gApp.keycodes[0x13] = AppKeyCode::NUM2;
    gApp.keycodes[0x14] = AppKeyCode::NUM3;
    gApp.keycodes[0x15] = AppKeyCode::NUM4;
    gApp.keycodes[0x17] = AppKeyCode::NUM5;
    gApp.keycodes[0x16] = AppKeyCode::NUM6;
    gApp.keycodes[0x1A] = AppKeyCode::NUM7;
    gApp.keycodes[0x1C] = AppKeyCode::NUM8;
    gApp.keycodes[0x19] = AppKeyCode::NUM9;
    gApp.keycodes[0x00] = AppKeyCode::A;
    gApp.keycodes[0x0B] = AppKeyCode::B;
    gApp.keycodes[0x08] = AppKeyCode::C;
    gApp.keycodes[0x02] = AppKeyCode::D;
    gApp.keycodes[0x0E] = AppKeyCode::E;
    gApp.keycodes[0x03] = AppKeyCode::F;
    gApp.keycodes[0x05] = AppKeyCode::G;
    gApp.keycodes[0x04] = AppKeyCode::H;
    gApp.keycodes[0x22] = AppKeyCode::I;
    gApp.keycodes[0x26] = AppKeyCode::J;
    gApp.keycodes[0x28] = AppKeyCode::K;
    gApp.keycodes[0x25] = AppKeyCode::L;
    gApp.keycodes[0x2E] = AppKeyCode::M;
    gApp.keycodes[0x2D] = AppKeyCode::N;
    gApp.keycodes[0x1F] = AppKeyCode::O;
    gApp.keycodes[0x23] = AppKeyCode::P;
    gApp.keycodes[0x0C] = AppKeyCode::Q;
    gApp.keycodes[0x0F] = AppKeyCode::R;
    gApp.keycodes[0x01] = AppKeyCode::S;
    gApp.keycodes[0x11] = AppKeyCode::T;
    gApp.keycodes[0x20] = AppKeyCode::U;
    gApp.keycodes[0x09] = AppKeyCode::V;
    gApp.keycodes[0x0D] = AppKeyCode::W;
    gApp.keycodes[0x07] = AppKeyCode::X;
    gApp.keycodes[0x10] = AppKeyCode::Y;
    gApp.keycodes[0x06] = AppKeyCode::Z;
    gApp.keycodes[0x27] = AppKeyCode::Apostrophe;
    gApp.keycodes[0x2A] = AppKeyCode::Backslash;
    gApp.keycodes[0x2B] = AppKeyCode::Comma;
    gApp.keycodes[0x18] = AppKeyCode::Equal;
    gApp.keycodes[0x32] = AppKeyCode::GraveAccent;
    gApp.keycodes[0x21] = AppKeyCode::Left_bracket;
    gApp.keycodes[0x1B] = AppKeyCode::Minus;
    gApp.keycodes[0x2F] = AppKeyCode::Period;
    gApp.keycodes[0x1E] = AppKeyCode::RightBracket;
    gApp.keycodes[0x29] = AppKeyCode::Semicolon;
    gApp.keycodes[0x2C] = AppKeyCode::Slash;
    gApp.keycodes[0x0A] = AppKeyCode::World1;
    gApp.keycodes[0x33] = AppKeyCode::Backspace;
    gApp.keycodes[0x39] = AppKeyCode::CapsLock;
    gApp.keycodes[0x75] = AppKeyCode::Delete;
    gApp.keycodes[0x7D] = AppKeyCode::Down;
    gApp.keycodes[0x77] = AppKeyCode::End;
    gApp.keycodes[0x24] = AppKeyCode::Enter;
    gApp.keycodes[0x35] = AppKeyCode::Escape;
    gApp.keycodes[0x7A] = AppKeyCode::F1;
    gApp.keycodes[0x78] = AppKeyCode::F2;
    gApp.keycodes[0x63] = AppKeyCode::F3;
    gApp.keycodes[0x76] = AppKeyCode::F4;
    gApp.keycodes[0x60] = AppKeyCode::F5;
    gApp.keycodes[0x61] = AppKeyCode::F6;
    gApp.keycodes[0x62] = AppKeyCode::F7;
    gApp.keycodes[0x64] = AppKeyCode::F8;
    gApp.keycodes[0x65] = AppKeyCode::F9;
    gApp.keycodes[0x6D] = AppKeyCode::F10;
    gApp.keycodes[0x67] = AppKeyCode::F11;
    gApp.keycodes[0x6F] = AppKeyCode::F12;
    gApp.keycodes[0x69] = AppKeyCode::F13;
    gApp.keycodes[0x6B] = AppKeyCode::F14;
    gApp.keycodes[0x71] = AppKeyCode::F15;
    gApp.keycodes[0x6A] = AppKeyCode::F16;
    gApp.keycodes[0x40] = AppKeyCode::F17;
    gApp.keycodes[0x4F] = AppKeyCode::F18;
    gApp.keycodes[0x50] = AppKeyCode::F19;
    gApp.keycodes[0x5A] = AppKeyCode::F20;
    gApp.keycodes[0x73] = AppKeyCode::Home;
    gApp.keycodes[0x72] = AppKeyCode::Insert;
    gApp.keycodes[0x7B] = AppKeyCode::Left;
    gApp.keycodes[0x3A] = AppKeyCode::LeftAlt;
    gApp.keycodes[0x3B] = AppKeyCode::LeftControl;
    gApp.keycodes[0x38] = AppKeyCode::LeftShift;
    gApp.keycodes[0x37] = AppKeyCode::LeftSuper;
    gApp.keycodes[0x6E] = AppKeyCode::Menu;
    gApp.keycodes[0x47] = AppKeyCode::NumLock;
    gApp.keycodes[0x79] = AppKeyCode::PageDown;
    gApp.keycodes[0x74] = AppKeyCode::PageUp;
    gApp.keycodes[0x7C] = AppKeyCode::Right;
    gApp.keycodes[0x3D] = AppKeyCode::RightAlt;
    gApp.keycodes[0x3E] = AppKeyCode::RightControl;
    gApp.keycodes[0x3C] = AppKeyCode::RightShift;
    gApp.keycodes[0x36] = AppKeyCode::RightSuper;
    gApp.keycodes[0x31] = AppKeyCode::Space;
    gApp.keycodes[0x30] = AppKeyCode::Tab;
    gApp.keycodes[0x7E] = AppKeyCode::Up;
    gApp.keycodes[0x52] = AppKeyCode::KP0;
    gApp.keycodes[0x53] = AppKeyCode::KP1;
    gApp.keycodes[0x54] = AppKeyCode::KP2;
    gApp.keycodes[0x55] = AppKeyCode::KP3;
    gApp.keycodes[0x56] = AppKeyCode::KP4;
    gApp.keycodes[0x57] = AppKeyCode::KP5;
    gApp.keycodes[0x58] = AppKeyCode::KP6;
    gApp.keycodes[0x59] = AppKeyCode::KP7;
    gApp.keycodes[0x5B] = AppKeyCode::KP8;
    gApp.keycodes[0x5C] = AppKeyCode::KP9;
    gApp.keycodes[0x45] = AppKeyCode::KPAdd;
    gApp.keycodes[0x41] = AppKeyCode::KPDecimal;
    gApp.keycodes[0x4B] = AppKeyCode::KPDivide;
    gApp.keycodes[0x4C] = AppKeyCode::KPEnter;
    gApp.keycodes[0x51] = AppKeyCode::KPEqual;
    gApp.keycodes[0x43] = AppKeyCode::KPMultiply;
    gApp.keycodes[0x4E] = AppKeyCode::KPSubtract;
}

bool AppInitialize(const AppDesc* desc)
{
    gApp.desc = *desc;
    gApp.desc.width = desc->width;
    gApp.desc.height = desc->width;
    gApp.desc.clipboardSizeBytes = desc->clipboardSizeBytes;

    gApp.firstFrame = true;
    gApp.windowWidth = gApp.desc.width;
    gApp.windowHeight = gApp.desc.height;
    gApp.framebufferWidth = gApp.desc.width;
    gApp.framebufferHeight = gApp.desc.height;
    gApp.dpiScale = 1.0f;
    gApp.clipboardEnabled = desc->enableClipboard;
    if (desc->enableClipboard)
        gApp.clipboard = (char*)memAllocZero(memDefaultAlloc(), gApp.desc.clipboardSizeBytes);

    if (desc->windowTitle)
        strCopy(gApp.windowTitle, sizeof(gApp.windowTitle), desc->windowTitle);
    else
        strCopy(gApp.windowTitle, sizeof(gApp.windowTitle), "Junkyard");
    
    timerInitialize();
    appMacInitKeyTable();
    
    [NSApplication sharedApplication];
    NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
    gApp.appDelegate = [[appMacDelegate alloc] init];
    NSApp.delegate = gApp.appDelegate;
    [NSApp activateIgnoringOtherApps:YES];
    [NSApp run];
    
    if (gApp.desc.callbacks.Cleanup) {
        gApp.desc.callbacks.Cleanup();
    }
        
    if (gApp.clipboardEnabled) {
        ASSERT(gApp.clipboard);
        memFree(memDefaultAlloc(), gApp.clipboard);
    }
    memset((void*)&gApp, 0x0, sizeof(AppMacState));
    
    return true;
}

static void appMacUpdateDimensions(void)
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
        if (gApp.desc.callbacks.Initialize) {
            if (!gApp.desc.callbacks.Initialize()) {
                return false;
            }
        }
        gApp.initCalled = true;
    }
    
    if (gApp.desc.callbacks.Update)
        gApp.desc.callbacks.Update(dt);

    gApp.frameCount++;
    return true;
}

static void appMacFrame(void)
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
        if (gApp.desc.fullscreen) {
            NSRect screenRect = NSScreen.mainScreen.frame;
            gApp.windowWidth = screenRect.size.width;
            gApp.windowHeight = screenRect.size.height;
        if (gApp.desc.highDPI) {
                gApp.framebufferWidth = 2 * gApp.windowWidth;
                gApp.framebufferHeight = 2 * gApp.windowHeight;
            }
            else {
                gApp.framebufferWidth = gApp.windowWidth;
                gApp.framebufferHeight = gApp.windowHeight;
            }
            gApp.dpiScale = (float)gApp.framebufferWidth / (float) gApp.windowWidth;
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
        gApp.view.layer.magnificationFilter = kCAFilterNearest;
        gApp.valid = true;
        if (gApp.desc.fullscreen) {
            [gApp.window toggleFullScreen:self];
        }
        else {
            [gApp.window center];
        }
        [gApp.window makeKeyAndOrderFront:nil];
    }

    - (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
    {
        return YES;
    }
@end // appMacDelegate

static uint32 appMacKeyMods(NSEventModifierFlags f)
{
    uint32 m = 0;
    if (f & NSEventModifierFlagShift)
        m |= AppKeyModifiers::Shift;
    if (f & NSEventModifierFlagControl)
        m |= AppKeyModifiers::Ctrl;
    if (f & NSEventModifierFlagOption)
        m |= AppKeyModifiers::Alt;
    if (f & NSEventModifierFlagCommand)
        m |= AppKeyModifiers::Super;
    return m;
}

static inline bool appMacEventsEnabled(void)
{
    // only send events when an event callback is set, and the init function was called
    return gApp.desc.callbacks.OnEvent && gApp.initCalled;
}

static void appMacInitEvent(AppEventType type)
{
    memset(&gApp.ev, 0, sizeof(gApp.ev));
    gApp.ev.type = type;
    gApp.ev.mouseButton = AppMouseButton::Invalid;
    gApp.ev.windowWidth = gApp.windowWidth;
    gApp.ev.windowHeight = gApp.windowHeight;
    gApp.ev.framebufferWidth = gApp.framebufferWidth;
    gApp.ev.framebufferHeight = gApp.framebufferHeight;
}

static bool appMacCallEvent(const AppEvent& ev)
{
    if (!gApp.cleanupCalled) {
        if (gApp.desc.callbacks.OnEvent) {
            gApp.desc.callbacks.OnEvent(ev);
        }
    }
    if (gApp.eventConsumed) {
        gApp.eventConsumed = false;
        return true;
    }
    else {
        return false;
    }
}

static void appMacDispatchMouseEvent(AppEventType type, AppMouseButton btn, uint32 mod)
{
    if (appMacEventsEnabled()) {
        appMacInitEvent(type);
        gApp.ev.mouseButton = btn;
        gApp.ev.keyMods = mod;
        gApp.ev.mouseX = gApp.mouseX;
        gApp.ev.mouseY = gApp.mouseY;
        appMacCallEvent(&gApp.ev);
    }
}

static void appMacDispatchKeyEvent(AppEventType type, AppKeycode key, bool repeat, uint32 mod)
{
    if (appMacEventsEnabled()) {
        appMacInitEvent(type);
        gApp.ev.keycode = key;
        gApp.ev.keyRepeat = repeat;
        gApp.ev.keyMods = mod;
        appMacCallEvent(&gApp.ev);
    }
}

static void appMacDispatchAppEvent(AppEventType type)
{
    if (appMacEventsEnabled()) {
        appMacInitEvent(type);
        appMacCallEvent(&gApp.ev);
    }
}

static AppKeycode appTranslateKey(int scanCode)
{
    if ((scanCode >= 0) && (scanCode <MAX_KEYCODES)) {
        return gApp.keycodes[scanCode];
    }
    else {
        return AppKeyCode::Invalid;
    }
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
        appMacUpdateDimensions();
        appMacDispatchAppEvent(AppEventType::Resized);
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
        appMacDispatchMouseEvent(AppEventType::MouseEnter, AppMouseButton::Invalid, appMacKeyMods(event.modifierFlags));
    }

    - (void)mouseExited:(NSEvent*)event
    {
        appMacDispatchMouseEvent(AppEventType::MouseLeave, AppMouseButton::Invalid, appMacKeyMods(event.modifierFlags));
    }

    - (void)mouseDown:(NSEvent*)event
    {
        appMacDispatchMouseEvent(AppEventType::MouseDown, AppMouseButton::Left, appMacKeyMods(event.modifierFlags));
    }

    - (void)mouseUp:(NSEvent*)event
    {
        appMacDispatchMouseEvent(AppEventType::MouseUp, AppMouseButton::Left, appMacKeyMods(event.modifierFlags));
    }

    - (void)rightMouseDown:(NSEvent*)event
    {
        appMacDispatchMouseEvent(AppEventType::MouseDown, AppMouseButton::Right, appMacKeyMods(event.modifierFlags));
    }

    - (void)rightMouseUp:(NSEvent*)event
    {
        appMacDispatchMouseEvent(AppEventType::MouseUp, AppMouseButton::Right, appMacKeyMods(event.modifierFlags));
    }

    - (void)otherMouseDown:(NSEvent*)event
    {
        if (2 == event.buttonNumber) {
            appMacDispatchMouseEvent(AppEventType::MouseDown, AppMouseButton::Middle, appMacKeyMods(event.modifierFlags));
        }
    }

    - (void)otherMouseUp:(NSEvent*)event
    {
        if (2 == event.buttonNumber) {
            appMacDispatchMouseEvent(AppEventType::MouseUp, AppMouseButton::Middle, appMacKeyMods(event.modifierFlags));
        }
    }

    - (void)mouseMoved:(NSEvent*)event
    {
        appMacDispatchMouseEvent(AppEventType::MouseMove, AppMouseButton::Invalid , appMacKeyMods(event.modifierFlags));
    }

    - (void)mouseDragged:(NSEvent*)event
    {
        appMacDispatchMouseEvent(AppEventType::MouseMove, AppMouseButton::Invalid , appMacKeyMods(event.modifierFlags));
    }

    - (void)rightMouseDragged:(NSEvent*)event
    {
        appMacDispatchMouseEvent(AppEventType::MouseMove, AppMouseButton::Invalid, appMacKeyMods(event.modifierFlags));
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
                appMacCallEvent(&gApp.ev);
            }
        }
    }

    - (void)keyDown:(NSEvent*)event
    {
        if (appMacEventsEnabled()) {
            const uint32 mods = appMacKeyMods(event.modifierFlags);
            // NOTE: macOS doesn't send keyUp events while the Cmd key is pressed,
            //       as a workaround, to prevent key presses from sticking we'll send
            //       a keyup event following right after the keydown if SUPER is also pressed
            const AppKeycode key_code = appTranslateKey(event.keyCode);
            appMacDispatchKeyEvent(AppEventType::KeyDown, key_code, event.isARepeat, mods);
            if (0 != (mods & AppKeyModifiers::Super)) {
                appMacDispatchKeyEvent(AppEventType::KeyUp, key_code, event.isARepeat, mods);
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
                    appMacCallEvent(&gApp.ev);
                }
            }
            
            // if this is a Cmd+V (paste), also send a CLIPBOARD_PASTE event
            if (gApp.clipboard && (mods == AppKeyModifiers::Super) && (key_code == AppKeyCode::V)) {
                appMacInitEvent(AppEventType::ClipboardPasted);
                appMacCallEvent(&gApp.ev);
            }
        }
    }

    - (void)keyUp:(NSEvent*)event
    {
        appMacDispatchKeyEvent(AppEventType::KeyUp,
            appTranslateKey(event.keyCode),
            event.isARepeat,
            appMacKeyMods(event.modifierFlags));
    }

    - (void)flagsChanged:(NSEvent*)event
    {
        const uint32 old_f = gApp.flagsChanged;
        const uint32 new_f = (uint32)event.modifierFlags;
        gApp.flagsChanged = new_f;
        AppKeycode key_code = AppKeyCode::Invalid;
        bool down = false;
        if ((new_f ^ old_f) & NSEventModifierFlagShift) {
            key_code = AppKeyCode::LeftShift;
            down = 0 != (new_f & NSEventModifierFlagShift);
        }
        if ((new_f ^ old_f) & NSEventModifierFlagControl) {
            key_code = AppKeyCode::LeftControl;
            down = 0 != (new_f & NSEventModifierFlagControl);
        }
        if ((new_f ^ old_f) & NSEventModifierFlagOption) {
            key_code = AppKeyCode::LeftAlt;
            down = 0 != (new_f & NSEventModifierFlagOption);
        }
        if ((new_f ^ old_f) & NSEventModifierFlagCommand) {
            key_code = AppKeyCode::LeftSuper;
            down = 0 != (new_f & NSEventModifierFlagCommand);
        }
        if (key_code != AppKeyCode::Invalid) {
            appMacDispatchKeyEvent(down ? AppEventType::KeyDown : AppEventType::KeyUp,
                key_code,
                false,
                appMacKeyMods(event.modifierFlags));
        }
    }

    - (void)cursorUpdate:(NSEvent*)event
    {
        if (gApp.desc.userCursor) {
            appMacDispatchAppEvent(AppEventType::UpdateCursor);
        }
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
        void* obj = (__bridge void*) gApp.window;
        ASSERT(obj);
        return obj;
    #else
        return 0;
    #endif
}

#endif // PLATFORM_APPLE

