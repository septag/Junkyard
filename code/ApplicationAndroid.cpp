#include "Application.h"

#if PLATFORM_ANDROID

#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <jni.h>

#include <android/native_activity.h>
#include <android/looper.h>
#include <android/log.h>
#include <android/configuration.h>

#include "Core/System.h"
#include "Core/String.h"
#include "Core/Settings.h"
#include "Core/Buffers.h"
#include "Core/Log.h"

#include "VirtualFS.h"
#include "RemoteServices.h"
#include "JunkyardSettings.h"

inline constexpr uint32 kAppMaxKeycodes = 512;

// fwd declared, should be implemented by user (Using 'Main' macro)
int AndroidMain(int argc, char* argv[]);

struct AppEventCallbackPair
{
    appOnEventCallback  callback;
    void*               userData;
};

enum AppAndroidCmd : uint32
{
    ANDROID_CMD_INPUT_CHANGED,
    ANDROID_CMD_INIT_WINDOW,
    ANDROID_CMD_TERM_WINDOW,
    ANDROID_CMD_WINDOW_RESIZED,
    ANDROID_CMD_WINDOW_REDRAW_NEEDED,
    ANDROID_CMD_CONTENT_RECT_CHANGED,
    ANDROID_CMD_GAINED_FOCUS,
    ANDROID_CMD_LOST_FOCUS,
    ANDROID_CMD_CONFIG_CHANGED,
    ANDROID_CMD_LOW_MEMORY,
    ANDROID_CMD_START,
    ANDROID_CMD_RESUME,
    ANDROID_CMD_SAVE_STATE,
    ANDROID_CMD_PAUSE,
    ANDROID_CMD_STOP,
    ANDROID_CMD_DESTROY,
    ANDROID_CMD_INVALID = 0x7fffffff
};

struct AppAndroidState
{
    bool valid;
    char name[32];
    char windowTitle[128];
    
    uint16 windowWidth;
    uint16 windowHeight;
    uint16 framebufferWidth;
    uint16 framebufferHeight;
    AppFramebufferTransform framebufferTransform;
    float dpiScale;

    AppDesc desc;
    AppEvent ev;

    bool firstFrame;
    bool initCalled;
    bool cleanupCalled;
    bool eventConsumed;
    bool clipboardEnabled;
    bool quitRequested;
    bool stateIsSaved;
    bool destroyed;
    bool focused;
    bool paused;

    size_t clipboardSize;
    char* clipboard;
    
    Array<AppEventCallbackPair> eventCallbacks;

    uint64 frameCount;
    int eventReadFd;
    int eventWriteFd;
    
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;
    void*   savedState;
    size_t  savedStateSize;
    AppAndroidCmd activityState;

    ANativeActivity* activity;
    ANativeWindow* window;
    ANativeWindow* pendingWindow;
    ALooper* looper;
    AInputQueue* inputQueue;
    AInputQueue* pendingInputQueue;
    AConfiguration* config;

    AppKeyModifiers keyMods;
    AppKeycode keycodes[kAppMaxKeycodes];
    bool keysDown[kAppMaxKeycodes];
};

static AppAndroidState gApp;

static bool appAndroidIsOnForeground()
{
    return gApp.focused && !gApp.paused;
}

bool appSetClipboardString(const char* str)
{

    return false;
}

static inline bool appAndroidEventsEnabled()
{
    // only send events when an event callback is set, and the init function was called
    return gApp.desc.callbacks && gApp.initCalled;
}

static bool appAndroidCallEvent(const AppEvent& ev)
{
    if (!gApp.cleanupCalled) {
        if (gApp.desc.callbacks) {
            gApp.desc.callbacks->OnEvent(ev);
        }

        // Call extra registered event callbacks
        for (auto c : gApp.eventCallbacks) {
            c.callback(ev, c.userData);
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

static void appAndroidInitEvent(AppEventType type)
{
    memset(&gApp.ev, 0, sizeof(gApp.ev));
    gApp.ev.type = type;
    gApp.ev.mouseButton = AppMouseButton::Invalid;
    gApp.ev.windowWidth = gApp.windowWidth;
    gApp.ev.windowHeight = gApp.windowHeight;
    gApp.ev.framebufferWidth = gApp.framebufferWidth;
    gApp.ev.framebufferHeight = gApp.framebufferHeight;
}


static void appAndroidDispatchEvent(AppEventType type)
{
    if (appAndroidEventsEnabled()) {
        appAndroidInitEvent(type);
        appAndroidCallEvent(gApp.ev);
    }
}

static void appAndroidInitKeyTable()
{
    gApp.keycodes[AKEYCODE_NUMPAD_0] = AppKeycode::NUM0;
    gApp.keycodes[AKEYCODE_NUMPAD_1] = AppKeycode::NUM1;
    gApp.keycodes[AKEYCODE_NUMPAD_2] = AppKeycode::NUM2;
    gApp.keycodes[AKEYCODE_NUMPAD_3] = AppKeycode::NUM3;
    gApp.keycodes[AKEYCODE_NUMPAD_4] = AppKeycode::NUM4;
    gApp.keycodes[AKEYCODE_NUMPAD_5] = AppKeycode::NUM5;
    gApp.keycodes[AKEYCODE_NUMPAD_6] = AppKeycode::NUM6;
    gApp.keycodes[AKEYCODE_NUMPAD_7] = AppKeycode::NUM7;
    gApp.keycodes[AKEYCODE_NUMPAD_8] = AppKeycode::NUM8;
    gApp.keycodes[AKEYCODE_NUMPAD_9] = AppKeycode::NUM9;
    gApp.keycodes[AKEYCODE_A] = AppKeycode::A;
    gApp.keycodes[AKEYCODE_B] = AppKeycode::B;
    gApp.keycodes[AKEYCODE_C] = AppKeycode::C;
    gApp.keycodes[AKEYCODE_D] = AppKeycode::D;
    gApp.keycodes[AKEYCODE_E] = AppKeycode::E;
    gApp.keycodes[AKEYCODE_F] = AppKeycode::F;
    gApp.keycodes[AKEYCODE_G] = AppKeycode::G;
    gApp.keycodes[AKEYCODE_G] = AppKeycode::H;
    gApp.keycodes[AKEYCODE_I] = AppKeycode::I;
    gApp.keycodes[AKEYCODE_J] = AppKeycode::J;
    gApp.keycodes[AKEYCODE_K] = AppKeycode::K;
    gApp.keycodes[AKEYCODE_L] = AppKeycode::L;
    gApp.keycodes[AKEYCODE_M] = AppKeycode::M;
    gApp.keycodes[AKEYCODE_N] = AppKeycode::N;
    gApp.keycodes[AKEYCODE_O] = AppKeycode::O;
    gApp.keycodes[AKEYCODE_P] = AppKeycode::P;
    gApp.keycodes[AKEYCODE_Q] = AppKeycode::Q;
    gApp.keycodes[AKEYCODE_R] = AppKeycode::R;
    gApp.keycodes[AKEYCODE_S] = AppKeycode::S;
    gApp.keycodes[AKEYCODE_T] = AppKeycode::T;
    gApp.keycodes[AKEYCODE_U] = AppKeycode::U;
    gApp.keycodes[AKEYCODE_V] = AppKeycode::V;
    gApp.keycodes[AKEYCODE_W] = AppKeycode::W;
    gApp.keycodes[AKEYCODE_X] = AppKeycode::X;
    gApp.keycodes[AKEYCODE_Y] = AppKeycode::Y;
    gApp.keycodes[AKEYCODE_Z] = AppKeycode::Z;
    gApp.keycodes[AKEYCODE_APOSTROPHE] = AppKeycode::Apostrophe;
    gApp.keycodes[AKEYCODE_BACKSLASH] = AppKeycode::Backslash;
    gApp.keycodes[AKEYCODE_COMMA] = AppKeycode::Comma;
    gApp.keycodes[AKEYCODE_EQUALS] = AppKeycode::Equal;
    gApp.keycodes[AKEYCODE_GRAVE] = AppKeycode::GraveAccent;
    gApp.keycodes[AKEYCODE_LEFT_BRACKET] = AppKeycode::LeftBracket;
    gApp.keycodes[AKEYCODE_MINUS] = AppKeycode::Minus;
    gApp.keycodes[AKEYCODE_PERIOD] = AppKeycode::Period;
    gApp.keycodes[AKEYCODE_RIGHT_BRACKET] = AppKeycode::RightBracket;
    gApp.keycodes[AKEYCODE_SEMICOLON] = AppKeycode::Semicolon;
    gApp.keycodes[AKEYCODE_SLASH] = AppKeycode::Slash;
    gApp.keycodes[AKEYCODE_LANGUAGE_SWITCH] = AppKeycode::World2;
    gApp.keycodes[AKEYCODE_DEL] = AppKeycode::Backspace;
    gApp.keycodes[AKEYCODE_FORWARD_DEL] = AppKeycode::Delete;
    gApp.keycodes[AKEYCODE_MOVE_END] = AppKeycode::End;
    gApp.keycodes[AKEYCODE_ENTER] = AppKeycode::Enter;
    gApp.keycodes[AKEYCODE_ESCAPE] = AppKeycode::Escape;
    gApp.keycodes[AKEYCODE_MOVE_HOME] = AppKeycode::Home;
    gApp.keycodes[AKEYCODE_INSERT] = AppKeycode::Insert;
    gApp.keycodes[AKEYCODE_MENU] = AppKeycode::Menu;
    gApp.keycodes[AKEYCODE_PAGE_DOWN] = AppKeycode::PageDown;
    gApp.keycodes[AKEYCODE_PAGE_UP] = AppKeycode::PageUp;
    gApp.keycodes[AKEYCODE_BREAK] = AppKeycode::Pause;
    gApp.keycodes[AKEYCODE_SPACE] = AppKeycode::Space;
    gApp.keycodes[AKEYCODE_TAB] = AppKeycode::Tab;
    gApp.keycodes[AKEYCODE_CAPS_LOCK] = AppKeycode::CapsLock;
    gApp.keycodes[AKEYCODE_NUM] = AppKeycode::NumLock;
    gApp.keycodes[AKEYCODE_SCROLL_LOCK] = AppKeycode::ScrollLock;
    gApp.keycodes[AKEYCODE_F1] = AppKeycode::F1;
    gApp.keycodes[AKEYCODE_F2] = AppKeycode::F2;
    gApp.keycodes[AKEYCODE_F3] = AppKeycode::F3;
    gApp.keycodes[AKEYCODE_F4] = AppKeycode::F4;
    gApp.keycodes[AKEYCODE_F5] = AppKeycode::F5;
    gApp.keycodes[AKEYCODE_F6] = AppKeycode::F6;
    gApp.keycodes[AKEYCODE_F7] = AppKeycode::F7;
    gApp.keycodes[AKEYCODE_F8] = AppKeycode::F8;
    gApp.keycodes[AKEYCODE_F9] = AppKeycode::F9;
    gApp.keycodes[AKEYCODE_F10] = AppKeycode::F10;
    gApp.keycodes[AKEYCODE_F11] = AppKeycode::F11;
    gApp.keycodes[AKEYCODE_F12] = AppKeycode::F12;
    gApp.keycodes[AKEYCODE_ALT_LEFT] = AppKeycode::LeftAlt;
    gApp.keycodes[AKEYCODE_CTRL_LEFT] = AppKeycode::LeftControl;
    gApp.keycodes[AKEYCODE_SHIFT_LEFT] = AppKeycode::LeftShift;
    gApp.keycodes[AKEYCODE_SYSRQ] = AppKeycode::PrintScreen;
    gApp.keycodes[AKEYCODE_ALT_RIGHT] = AppKeycode::RightAlt;
    gApp.keycodes[AKEYCODE_CTRL_RIGHT] = AppKeycode::RightControl;
    gApp.keycodes[AKEYCODE_SHIFT_RIGHT] = AppKeycode::RightShift;
    gApp.keycodes[AKEYCODE_DPAD_DOWN] = AppKeycode::Down;
    gApp.keycodes[AKEYCODE_DPAD_LEFT] = AppKeycode::Left;
    gApp.keycodes[AKEYCODE_DPAD_RIGHT] = AppKeycode::Right;
    gApp.keycodes[AKEYCODE_DPAD_UP] = AppKeycode::Up;
    gApp.keycodes[AKEYCODE_NUMPAD_0] = AppKeycode::KP0;
    gApp.keycodes[AKEYCODE_NUMPAD_1] = AppKeycode::KP1;
    gApp.keycodes[AKEYCODE_NUMPAD_2] = AppKeycode::KP2;
    gApp.keycodes[AKEYCODE_NUMPAD_3] = AppKeycode::KP3;
    gApp.keycodes[AKEYCODE_NUMPAD_4] = AppKeycode::KP4;
    gApp.keycodes[AKEYCODE_NUMPAD_5] = AppKeycode::KP5;
    gApp.keycodes[AKEYCODE_NUMPAD_6] = AppKeycode::KP6;
    gApp.keycodes[AKEYCODE_NUMPAD_7] = AppKeycode::KP7;
    gApp.keycodes[AKEYCODE_NUMPAD_8] = AppKeycode::KP8;
    gApp.keycodes[AKEYCODE_NUMPAD_9] = AppKeycode::KP9;
    gApp.keycodes[AKEYCODE_NUMPAD_ADD] = AppKeycode::KPAdd;
    gApp.keycodes[AKEYCODE_NUMPAD_DOT] = AppKeycode::KPDecimal;
    gApp.keycodes[AKEYCODE_NUMPAD_DIVIDE] = AppKeycode::KPDivide;
    gApp.keycodes[AKEYCODE_NUMPAD_ENTER] = AppKeycode::KPEnter;
    gApp.keycodes[AKEYCODE_NUMPAD_MULTIPLY] = AppKeycode::KPMultiply;
    gApp.keycodes[AKEYCODE_NUMPAD_SUBTRACT] = AppKeycode::KPSubtract;
}

static void appAndroidUpdateDimensions(ANativeWindow* window)
{
    ASSERT(window);
    
    const int winWidth = ANativeWindow_getWidth(window);
    const int winHeight = ANativeWindow_getHeight(window);
    ASSERT(winWidth > 0 && winHeight > 0);
    int framebufferWidth = gApp.desc.highDPI ? winWidth : (winWidth/2);
    int framebufferHeight = gApp.desc.highDPI ? winHeight : (winHeight/2);

    const bool winChanged = (winWidth != gApp.windowWidth) || (winHeight != gApp.windowHeight);
    if (winChanged) {
        gApp.windowWidth = winWidth;
        gApp.windowHeight = winHeight;

        if (!gApp.desc.highDPI) {
            // NOTE: calling ANativeWindow_setBuffersGeometry() with the same dimensions
            //       as the ANativeWindow size results in weird display artefacts, that's
            //       why it's only called when the buffer geometry is different from
            //       the window size
            // TODO: We are currently hard coding hardware buffer format
            //       https://developer.android.com/ndk/reference/group/a-hardware-buffer
            [[maybe_unused]] int32_t result = 
                ANativeWindow_setBuffersGeometry(window, framebufferWidth, framebufferHeight, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);
            ASSERT(result == 0);
        }
    }

    // TODO: query framebuffer width and height
    const bool fbChanged = (framebufferWidth != gApp.framebufferWidth) || (framebufferHeight != gApp.framebufferHeight);
    if (fbChanged) {
        gApp.framebufferWidth = framebufferWidth;
        gApp.framebufferHeight = framebufferHeight;
        gApp.dpiScale = (float)gApp.framebufferWidth / (float)gApp.windowWidth;
    }

    if (winChanged || fbChanged) {
        if (!gApp.firstFrame)
            appAndroidDispatchEvent(AppEventType::Resized);
    }
}

static void appAndroidFreeSavedState()
{
    pthread_mutex_lock(&gApp.mutex);
    if (gApp.savedState != nullptr) {
        memFree(gApp.savedState);
        gApp.savedState = nullptr;
        gApp.savedStateSize = 0;
    }
    pthread_mutex_unlock(&gApp.mutex);
}

static void appAndroidWriteCmd(AppAndroidCmd event)
{
    if (write(gApp.eventWriteFd, &event, sizeof(event)) != sizeof(event)) {
        sysAndroidPrintToLog(SysAndroidLogType::Fatal, gApp.name, "Android: Writing event to nessage pipe failed");
    }
}

static AppAndroidCmd appAndroidReadCmd() 
{
    AppAndroidCmd cmd;
    if (read(gApp.eventReadFd, &cmd, sizeof(cmd)) == sizeof(cmd)) {
        if (cmd == ANDROID_CMD_SAVE_STATE)
            appAndroidFreeSavedState();
        return cmd;
    } else {
        sysAndroidPrintToLog(SysAndroidLogType::Fatal, gApp.name, "Android: No data in command pipe");
    }
    return ANDROID_CMD_INVALID;
}

static void appAndroidCleanup()
{
    if (gApp.initCalled && !gApp.cleanupCalled) {
        if (gApp.desc.callbacks) {
            gApp.desc.callbacks->Cleanup();
        }

        _private::remoteRelease();
        _private::vfsRelease();

        gApp.cleanupCalled = true;
    }
}

int appAndroidGetCharcodeFromKeycode(int eventType, int keyCode, int metaState)
{
    JNIEnv* jniEnv = sysAndroidGetJniEnv();
    jclass class_key_event = jniEnv->FindClass("android/view/KeyEvent");
    int unicodeKey = 0;
    if (metaState == 0) {
        jmethodID method_get_unicode_char = jniEnv->GetMethodID(class_key_event, "getUnicodeChar", "()I");
        jmethodID eventConstructor = jniEnv->GetMethodID(class_key_event, "<init>", "(II)V");
        jobject eventObj = jniEnv->NewObject(class_key_event, eventConstructor, eventType, keyCode);

        unicodeKey = jniEnv->CallIntMethod(eventObj, method_get_unicode_char);
        jniEnv->DeleteLocalRef(eventObj);
    }
    else
    {
        jmethodID method_get_unicode_char = jniEnv->GetMethodID(class_key_event, "getUnicodeChar", "(I)I");
        jmethodID eventConstructor = jniEnv->GetMethodID(class_key_event, "<init>", "(II)V");
        jobject eventObj = jniEnv->NewObject(class_key_event, eventConstructor, eventType, keyCode);

        unicodeKey = jniEnv->CallIntMethod(eventObj, method_get_unicode_char, metaState);
        jniEnv->DeleteLocalRef(eventObj);
    }

    return unicodeKey;
}

static int appAndroidInputEventsFn(int fd, int events, void* data)
{
    if ((events & ALOOPER_EVENT_INPUT) == 0) {
        ASSERT_MSG(0, "Unsupported event");
        return 1;
    }

    ASSERT(gApp.inputQueue);
    AInputEvent* event = nullptr;
    AInputQueue* input = gApp.inputQueue;

    while (AInputQueue_getEvent(input, &event) >= 0) {
        if (AInputQueue_preDispatchEvent(input, event) != 0) 
            continue;

        int handled = 0;
        int32 androidEventType = AInputEvent_getType(event);

        // Touch events
        if (androidEventType == AINPUT_EVENT_TYPE_MOTION && appAndroidEventsEnabled())  {
            int actionIdx = AMotionEvent_getAction(event);
            int action = actionIdx & AMOTION_EVENT_ACTION_MASK;
            AppEventType eventType = AppEventType::Invalid;
            int32 button = AMotionEvent_getButtonState(event);
            int32 source = AInputEvent_getSource(event);
            
            // Touch events
            switch (action) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                eventType = AppEventType::TouchBegin;
                break;

            case AMOTION_EVENT_ACTION_MOVE:
                eventType = AppEventType::TouchMove;
                break;

            case AMOTION_EVENT_ACTION_UP:                
            case AMOTION_EVENT_ACTION_POINTER_UP:
                eventType = AppEventType::TouchEnd;
                break;

            case AMOTION_EVENT_ACTION_CANCEL:
            case AMOTION_EVENT_ACTION_OUTSIDE:
                eventType = AppEventType::TouchCancel;
                break;
            }

            if (eventType != AppEventType::Invalid) {
                int index = actionIdx >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
                appAndroidInitEvent(eventType);
                gApp.ev.numTouches = Min<uint32>(AMotionEvent_getPointerCount(event), kAppMaxTouchPoints);

                for (uint32 i = 0; i < gApp.ev.numTouches; i++) {
                    AppTouchPoint* tp = &gApp.ev.touches[i];
                    tp->id = AMotionEvent_getPointerId(event, i);
                    tp->posX = AMotionEvent_getX(event, i);
                    tp->posY = AMotionEvent_getY(event, i);
                    if (action == AMOTION_EVENT_ACTION_POINTER_DOWN || action == AMOTION_EVENT_ACTION_POINTER_UP)
                        tp->changed = i == index;
                    else
                        tp->changed = true;
                }

                handled = 1;
                appAndroidCallEvent(gApp.ev);
                eventType = AppEventType::Invalid;
            }

            // Mouse events
            float scroll = 0;
            AppMouseButton mouseButton = AppMouseButton::Invalid;
            switch (action) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_MOVE:
                if (button == AMOTION_EVENT_BUTTON_PRIMARY || ((source & AINPUT_SOURCE_TOUCHSCREEN) && button == 0))
                    mouseButton = AppMouseButton::Left;
                else if (button == AMOTION_EVENT_BUTTON_SECONDARY)
                    mouseButton = AppMouseButton::Right;
                eventType = action == AMOTION_EVENT_ACTION_DOWN ? AppEventType::MouseDown : AppEventType::MouseMove;
                break;

            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
            case AMOTION_EVENT_ACTION_OUTSIDE:
                if (button == AMOTION_EVENT_BUTTON_PRIMARY || ((source & AINPUT_SOURCE_TOUCHSCREEN) && button == 0))
                    mouseButton = AppMouseButton::Left;
                else if (button == AMOTION_EVENT_BUTTON_SECONDARY)
                    mouseButton = AppMouseButton::Right;
                eventType = AppEventType::MouseUp;
                break;

            case AMOTION_EVENT_ACTION_SCROLL:
                scroll = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0);
                eventType = AppEventType::MouseScroll;
                break;
            }

            if (eventType != AppEventType::Invalid) {
                appAndroidInitEvent(eventType);
                gApp.ev.mouseButton = mouseButton;
                gApp.ev.mouseX = AMotionEvent_getX(event, 0);
                gApp.ev.mouseY = AMotionEvent_getY(event, 0);
                gApp.ev.scrollY = scroll;
                appAndroidCallEvent(gApp.ev);

                handled = 1;
                eventType = AppEventType::Invalid;
            }
        }

        // Key events
        
        if (androidEventType == AINPUT_EVENT_TYPE_KEY) {
            // TODO: handle AKEYCODE_BACK separately
            handled = 1;            

            int32 keycode = AKeyEvent_getKeyCode(event);
            int32 action = AKeyEvent_getAction(event);
            int32 repeatCount = AKeyEvent_getRepeatCount(event);
            // int32 flags = AKeyEvent_getFlags(event);

            AppEventType eventType;
            switch (action) {
            case AKEY_EVENT_ACTION_DOWN:
                eventType = AppEventType::KeyDown;
                if (gApp.keycodes[keycode] == AppKeycode::LeftShift || gApp.keycodes[keycode] == AppKeycode::RightShift)
                    gApp.keyMods |= AppKeyModifiers::Shift;
                else if (gApp.keycodes[keycode] == AppKeycode::LeftControl || gApp.keycodes[keycode] == AppKeycode::RightControl)
                    gApp.keyMods |= AppKeyModifiers::Ctrl;
                else if (gApp.keycodes[keycode] == AppKeycode::LeftAlt || gApp.keycodes[keycode] == AppKeycode::RightAlt)
                    gApp.keyMods |= AppKeyModifiers::Alt;
                else if (gApp.keycodes[keycode] == AppKeycode::LeftSuper || gApp.keycodes[keycode] == AppKeycode::RightSuper)
                    gApp.keyMods |= AppKeyModifiers::Super;
                gApp.keysDown[uint32(gApp.keycodes[keycode])] = true;
                break;
            case AKEY_EVENT_ACTION_UP:
                eventType = AppEventType::KeyUp;
                if (gApp.keycodes[keycode] == AppKeycode::LeftShift || gApp.keycodes[keycode] == AppKeycode::RightShift)
                    gApp.keyMods &= ~AppKeyModifiers::Shift;
                else if (gApp.keycodes[keycode] == AppKeycode::LeftControl || gApp.keycodes[keycode] == AppKeycode::RightControl)
                    gApp.keyMods &= ~AppKeyModifiers::Ctrl;
                else if (gApp.keycodes[keycode] == AppKeycode::LeftAlt || gApp.keycodes[keycode] == AppKeycode::RightAlt)
                    gApp.keyMods &= ~AppKeyModifiers::Alt;
                else if (gApp.keycodes[keycode] == AppKeycode::LeftSuper || gApp.keycodes[keycode] == AppKeycode::RightSuper)
                    gApp.keyMods &= ~AppKeyModifiers::Super;
                gApp.keysDown[uint32(gApp.keycodes[keycode])] = false;
                break;
            }
            appAndroidInitEvent(eventType);
            gApp.ev.keycode = gApp.keycodes[keycode];
            gApp.ev.keyRepeat = repeatCount > 1;
            gApp.ev.keyMods = gApp.keyMods;
            appAndroidCallEvent(gApp.ev);

            if (action == AKEY_EVENT_ACTION_DOWN) {
                int charcode = appAndroidGetCharcodeFromKeycode(androidEventType, keycode, AKeyEvent_getMetaState(event));
                if (charcode >= 32 && charcode <= 127)  {
                    appAndroidInitEvent(AppEventType::Char);
                    gApp.ev.charcode = static_cast<uint32>(charcode);
                    gApp.ev.keyRepeat = repeatCount > 1;
                    gApp.ev.keyMods = gApp.keyMods;
                    appAndroidCallEvent(gApp.ev);
                }
            }
        }

        AInputQueue_finishEvent(input, event, handled);
    }

    return 1;
}

static int appAndroidMainEventsFn(int fd, int events, void* data)
{
    UNUSED(fd);
    UNUSED(events);
    UNUSED(data);

    if (gApp.destroyed)
        return 1;

    AppEventType eventType = AppEventType::Invalid;

    AppAndroidCmd cmd = appAndroidReadCmd();
    if (cmd == ANDROID_CMD_INVALID)
        return 1;

    switch (cmd) {
    case ANDROID_CMD_INPUT_CHANGED:
        pthread_mutex_lock(&gApp.mutex);
        if (gApp.inputQueue != NULL) {
            AInputQueue_detachLooper(gApp.inputQueue);
        }
        gApp.inputQueue = gApp.pendingInputQueue;
        if (gApp.inputQueue != NULL) {
            AInputQueue_attachLooper(gApp.inputQueue,
                                     gApp.looper, 
                                     ALOOPER_POLL_CALLBACK, 
                                     appAndroidInputEventsFn,
                                     nullptr);
        }
        pthread_cond_broadcast(&gApp.cond);
        pthread_mutex_unlock(&gApp.mutex);
        break;

    case ANDROID_CMD_INIT_WINDOW:
        pthread_mutex_lock(&gApp.mutex);
        gApp.window = gApp.pendingWindow;
        pthread_cond_broadcast(&gApp.cond);
        pthread_mutex_unlock(&gApp.mutex);
        break;

    case ANDROID_CMD_TERM_WINDOW:
        pthread_cond_broadcast(&gApp.cond);
        break;

    case ANDROID_CMD_RESUME:
        gApp.paused = false;
        pthread_mutex_lock(&gApp.mutex);
        gApp.activityState = cmd;
        pthread_cond_broadcast(&gApp.cond);
        pthread_mutex_unlock(&gApp.mutex);
        break;

    case ANDROID_CMD_PAUSE:
        gApp.paused = true;
        pthread_mutex_lock(&gApp.mutex);
        gApp.activityState = cmd;
        pthread_cond_broadcast(&gApp.cond);
        pthread_mutex_unlock(&gApp.mutex);
        break;
        
    case ANDROID_CMD_LOST_FOCUS:
        eventType = AppEventType::Suspended;
        gApp.focused = false;
        break;
    case ANDROID_CMD_GAINED_FOCUS:
        eventType = AppEventType::Resumed;
        gApp.focused = true;
        break;
    case ANDROID_CMD_START:
    case ANDROID_CMD_STOP:
        pthread_mutex_lock(&gApp.mutex);
        gApp.activityState = cmd;
        pthread_cond_broadcast(&gApp.cond);
        pthread_mutex_unlock(&gApp.mutex);
        break;

    case ANDROID_CMD_CONFIG_CHANGED:
        AConfiguration_fromAssetManager(gApp.config, gApp.activity->assetManager);
        // TODO: print_cur_config(android_app);
        break;

    case ANDROID_CMD_DESTROY:
        appAndroidCleanup();
        gApp.quitRequested = true;
        break;

    default:
        break;
    }

    // dispatch events based on CMD
    if (eventType != AppEventType::Invalid)
        appAndroidDispatchEvent(eventType);

    switch (cmd) {
    case ANDROID_CMD_TERM_WINDOW:
        pthread_mutex_lock(&gApp.mutex);
        gApp.window = nullptr;
        pthread_cond_broadcast(&gApp.cond);
        pthread_mutex_unlock(&gApp.mutex);
        break;

    case ANDROID_CMD_SAVE_STATE:
        pthread_mutex_lock(&gApp.mutex);
        gApp.stateIsSaved = true;
        pthread_cond_broadcast(&gApp.cond);
        pthread_mutex_unlock(&gApp.mutex);
        break;

    case ANDROID_CMD_RESUME:
        appAndroidFreeSavedState();
        break;

    default:
        break;
    }

    return 1;
}

bool appInitialize(const AppDesc& desc)
{
    gApp.desc = desc;

    gApp.firstFrame = true;

    /*
    gApp.windowWidth = gApp.desc.width;
    gApp.windowHeight = gApp.desc.height;
    gApp.framebufferWidth = gApp.desc.width;
    gApp.framebufferHeight = gApp.desc.height;
    gApp.dpiScale = 1.0f;
    */
    gApp.clipboardEnabled = desc.enableClipboard;

    if (desc.enableClipboard)
        gApp.clipboard = memAllocZeroTyped<char>((uint32)gApp.desc.clipboardSizeBytes);

    if (desc.windowTitle)   strCopy(gApp.windowTitle, sizeof(gApp.windowTitle), desc.windowTitle);
    else                    strCopy(gApp.windowTitle, sizeof(gApp.windowTitle), "Junkyard");

    strCopy(gApp.name, sizeof(gApp.name), "Junkyard");

    appAndroidInitKeyTable();
    timerInitialize();

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
    
    gApp.config = AConfiguration_new();
    AConfiguration_fromAssetManager(gApp.config, gApp.activity->assetManager);
    // TODO: print configuration
    
    gApp.looper = ALooper_prepare(0 /*ALOOPER_PREPARE_ALLOW_NON_CALLBACKS*/);
    ASSERT(gApp.looper);
    ALooper_addFd(gApp.looper, gApp.eventReadFd, 
                  ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, 
                  appAndroidMainEventsFn, nullptr);
    
    pthread_mutex_lock(&gApp.mutex);
    gApp.valid = true;
    pthread_cond_broadcast(&gApp.cond);
    pthread_mutex_unlock(&gApp.mutex);
    
    return true;
}


static void appAndroidDestroy()
{
    pthread_mutex_lock(&gApp.mutex);
    appAndroidWriteCmd(ANDROID_CMD_DESTROY);
    while (!gApp.destroyed)
        pthread_cond_wait(&gApp.cond, &gApp.mutex);
    pthread_mutex_unlock(&gApp.mutex);
    
    close(gApp.eventReadFd);
    close(gApp.eventWriteFd);
    pthread_cond_destroy(&gApp.cond);
    pthread_mutex_destroy(&gApp.mutex);
}

[[maybe_unused]] static void appAndroidShutdown()
{
    appAndroidDestroy();
    ANativeActivity_finish(gApp.activity);
}

static bool appAndroidFrame(fl32 dt)
{
    appAndroidUpdateDimensions(gApp.window);

    if (gApp.firstFrame) {
        gApp.firstFrame = false;
        if (gApp.desc.callbacks) {
            if (!gApp.desc.callbacks->Initialize()) {
                appQuit();
                return false;
            }
        }
        gApp.initCalled = true;
    }
    
    if (gApp.desc.callbacks && gApp.initCalled)
        gApp.desc.callbacks->Update(dt);

    gApp.frameCount++;
    return true;
}

void appShowMouse(bool visible)
{
}

bool appIsMouseShown()
{
    return false;
}

const char* appGetClipboardString()
{
    return nullptr;
}

void appQuit()
{
    gApp.quitRequested = true;
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

float appGetDpiScale()
{
    return gApp.dpiScale;
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
    if (uint32 index = gApp.eventCallbacks.FindIf([callback](const AppEventCallbackPair& p)->bool {
        return p.callback == callback;});
        index != UINT32_MAX)
    {
        gApp.eventCallbacks.RemoveAndSwap(index);
    }
}

const char* appGetName()
{
    return gApp.name;
}

void appSetCursor(AppMouseCursor cursor)
{
    UNUSED(cursor);
}

void* appGetNativeWindowHandle()
{
    return gApp.window;
}

static void* appAndroidMainThreadFn(void* userData)
{
    UNUSED(userData);

    sysAndroidAcquireJniEnv(gApp.activity);

    // Just call the main function "AndroidMain" which is implemented by the user just like the usual "main"
    // `AndroidMain` basically implements callbacks, calls `appInitialize` or whatever initialization and returns
    int r = AndroidMain(0, nullptr);
    ASSERT_MSG(gApp.valid, "appInitialize is not called within AndroidMain function");
    if (r == 0 && gApp.valid) {
        // main loop
        uint64 tmPrev = 0;

        while (!gApp.quitRequested) {
            if (appAndroidIsOnForeground()) {
                uint64 tmNow = timerGetTicks();
                appAndroidFrame(!gApp.firstFrame ? static_cast<fl32>(timerToSec(timerDiff(tmNow, tmPrev))) : 0);
                tmPrev = tmNow;
            }
            
            bool processEvents = true;
            while (processEvents && !gApp.quitRequested) {
                bool blockEvent = !gApp.quitRequested && !appAndroidIsOnForeground();
                processEvents = 
                    ALooper_pollOnce(blockEvent ? -1 : 0, nullptr, nullptr, nullptr) == ALOOPER_POLL_CALLBACK;
            }
        }

        appAndroidFreeSavedState();
        pthread_mutex_lock(&gApp.mutex);
        if (gApp.inputQueue != nullptr)
            AInputQueue_detachLooper(gApp.inputQueue);
        AConfiguration_delete(gApp.config);
        gApp.destroyed = true;
        pthread_cond_broadcast(&gApp.cond);
        pthread_mutex_unlock(&gApp.mutex);
    }


    sysAndroidReleaseJniEnv(gApp.activity);
    return (void*)static_cast<uintptr_t>(r);
}

static void appAndroidSetActivityState(AppAndroidCmd cmd)
{
    pthread_mutex_lock(&gApp.mutex);
    appAndroidWriteCmd(cmd);
    while (gApp.activityState != cmd)
        pthread_cond_wait(&gApp.cond, &gApp.mutex);
    pthread_mutex_unlock(&gApp.mutex);    
}

static void appAndroidSetWindow(ANativeWindow* window)
{
    pthread_mutex_lock(&gApp.mutex);
    if (gApp.pendingWindow != nullptr)
        appAndroidWriteCmd(ANDROID_CMD_TERM_WINDOW);
    gApp.pendingWindow = window;
    if (window != nullptr)
        appAndroidWriteCmd(ANDROID_CMD_INIT_WINDOW);
    while (gApp.window != gApp.pendingWindow)
        pthread_cond_wait(&gApp.cond, &gApp.mutex);
    pthread_mutex_unlock(&gApp.mutex);    
}

static void appAndroidSetInput(AInputQueue* inputQueue)
{
    pthread_mutex_lock(&gApp.mutex);
    gApp.pendingInputQueue = inputQueue;
    appAndroidWriteCmd(ANDROID_CMD_INPUT_CHANGED);
    while (gApp.inputQueue != gApp.pendingInputQueue)
        pthread_cond_wait(&gApp.cond, &gApp.mutex);
    pthread_mutex_unlock(&gApp.mutex);
}

extern "C" JNIEXPORT void ANativeActivity_onCreate(ANativeActivity * activity, void* savedState, size_t savedStateSize)
{
    activity->callbacks->onStart = [](ANativeActivity* activity) {
        appAndroidSetActivityState(ANDROID_CMD_START);
    };

    activity->callbacks->onDestroy = [](ANativeActivity* activity) {
        appAndroidDestroy();
    };

    activity->callbacks->onResume = [](ANativeActivity* activity) {
        appAndroidSetActivityState(ANDROID_CMD_RESUME);
    };

    activity->callbacks->onSaveInstanceState = [](ANativeActivity* activity, size_t* outLen)->void* {
        void* savedState = nullptr;
        
        pthread_mutex_lock(&gApp.mutex);
        gApp.stateIsSaved = false;
        appAndroidWriteCmd(ANDROID_CMD_SAVE_STATE);
        while (!gApp.stateIsSaved)
            pthread_cond_wait(&gApp.cond, &gApp.mutex);
        
        if (gApp.savedState != nullptr) {
            savedState = gApp.savedState;
            *outLen = gApp.savedStateSize;
            gApp.savedState = nullptr;
            gApp.savedStateSize = 0;
        }
        
        pthread_mutex_unlock(&gApp.mutex);
        
        return savedState;
    };

    activity->callbacks->onPause = [](ANativeActivity* activity) {
        appAndroidSetActivityState(ANDROID_CMD_PAUSE);
    };

    activity->callbacks->onStop = [](ANativeActivity* activity) {
        appAndroidSetActivityState(ANDROID_CMD_STOP);
    };

    activity->callbacks->onConfigurationChanged = [](ANativeActivity* activity) {
        appAndroidWriteCmd(ANDROID_CMD_CONFIG_CHANGED);
    };

    activity->callbacks->onLowMemory = [](ANativeActivity* activity) {
        appAndroidWriteCmd(ANDROID_CMD_LOW_MEMORY);
    };

    activity->callbacks->onWindowFocusChanged = [](ANativeActivity* activity, int focused) {
        appAndroidWriteCmd(focused ? ANDROID_CMD_GAINED_FOCUS : ANDROID_CMD_LOST_FOCUS);
    };

    activity->callbacks->onNativeWindowCreated = [](ANativeActivity* activity, ANativeWindow* window) {
        appAndroidSetWindow(window);
    };

    activity->callbacks->onNativeWindowDestroyed = [](ANativeActivity* activity, ANativeWindow* window) {
        appAndroidSetWindow(nullptr);
    };

    activity->callbacks->onInputQueueCreated = [](ANativeActivity* activity, AInputQueue* queue) {
        appAndroidSetInput(queue);
    };

    activity->callbacks->onInputQueueDestroyed = [](ANativeActivity* activity, AInputQueue* queue) {
        appAndroidSetInput(nullptr);
    };
    
    // 
    gApp.activity = activity;
    pthread_mutex_init(&gApp.mutex, NULL);
    pthread_cond_init(&gApp.cond, NULL);
    
    if (savedState != nullptr) {
        ASSERT(savedStateSize > 0);
        gApp.savedState = memAllocCopy<uint8>((uint8*)savedState, (uint32)savedStateSize);
        gApp.savedStateSize = savedStateSize;
    }
    
    int msgPipe[2];
    if (pipe(msgPipe)) {
        sysAndroidPrintToLog(SysAndroidLogType::Fatal, gApp.name, "Android: Writing event to message pipe failed");
        return;
    }
    gApp.eventReadFd = msgPipe[0];
    gApp.eventWriteFd = msgPipe[1];
    
    pthread_attr_t attr; 
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&gApp.thread, &attr, appAndroidMainThreadFn, nullptr);
    ASSERT_ALWAYS(gApp.thread, "Creating android main thread failed");
    pthread_attr_destroy(&attr);

    // Wait for thread to start.
    pthread_mutex_lock(&gApp.mutex);
    while (!gApp.valid)
        pthread_cond_wait(&gApp.cond, &gApp.mutex);
    pthread_mutex_unlock(&gApp.mutex);
}

void* appGetNativeAppHandle()
{
    return gApp.activity;
}

AAssetManager* appAndroidGetAssetManager()
{
    return gApp.activity->assetManager;
}

ANativeActivity* appAndroidGetActivity()
{
    return gApp.activity;
}

AppDisplayInfo appGetDisplayInfo()
{
    // TODO
    return AppDisplayInfo {
        .width = gApp.windowWidth,
        .height = gApp.windowHeight,
        .refreshRate = 60,
        .dpiScale = gApp.dpiScale
    };
}

bool appIsKeyDown(AppKeycode keycode)
{
    return gApp.keysDown[uint32(keycode)];
}

bool appIsAnyKeysDown(const AppKeycode* keycodes, uint32 numKeycodes)
{
    bool down = false;
    for (uint32 i = 0; i < numKeycodes; i++) {
        down |= gApp.keysDown[uint32(gApp.keycodes[i])];
    }
    return down;
}

AppKeyModifiers appGetKeyMods()
{
    return gApp.keyMods;
}

void appAndroidSetFramebufferTransform(AppFramebufferTransform transform)
{
    gApp.framebufferTransform = transform;
}

AppFramebufferTransform appGetFramebufferTransform()
{
    return gApp.framebufferTransform;
}

void appCaptureMouse()
{
}

void appReleaseMouse()
{
}

#endif  // PLATFORM_ANDROID
