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

#include "../Core/System.h"
#include "../Core/StringUtil.h"
#include "../Core/Settings.h"
#include "../Core/Log.h"
#include "../Core/Debug.h"
#include "../Core/Arrays.h"
#include "../Core/Allocators.h"

#include "VirtualFS.h"
#include "RemoteServices.h"
#include "JunkyardSettings.h"

#include "../Engine.h"

inline constexpr uint32 kAppMaxKeycodes = 512;

// fwd declared, should be implemented by user (Using 'Main' macro)
int AndroidMain(int argc, char* argv[]);

struct AppEventCallbackPair
{
    AppEventCallback  callback;
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
    Pair<AppUpdateOverrideCallback, void*> overrideUpdateCallback;

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

    InputKeyModifiers keyMods;
    InputKeycode keycodes[kAppMaxKeycodes];
    bool keysDown[kAppMaxKeycodes];
};

static AppAndroidState gApp;

static bool appAndroidIsOnForeground()
{
    return gApp.focused && !gApp.paused;
}

bool App::SetClipboardString(const char* str)
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
    gApp.ev.mouseButton = InputMouseButton::Invalid;
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
    gApp.keycodes[AKEYCODE_NUMPAD_0] = InputKeycode::NUM0;
    gApp.keycodes[AKEYCODE_NUMPAD_1] = InputKeycode::NUM1;
    gApp.keycodes[AKEYCODE_NUMPAD_2] = InputKeycode::NUM2;
    gApp.keycodes[AKEYCODE_NUMPAD_3] = InputKeycode::NUM3;
    gApp.keycodes[AKEYCODE_NUMPAD_4] = InputKeycode::NUM4;
    gApp.keycodes[AKEYCODE_NUMPAD_5] = InputKeycode::NUM5;
    gApp.keycodes[AKEYCODE_NUMPAD_6] = InputKeycode::NUM6;
    gApp.keycodes[AKEYCODE_NUMPAD_7] = InputKeycode::NUM7;
    gApp.keycodes[AKEYCODE_NUMPAD_8] = InputKeycode::NUM8;
    gApp.keycodes[AKEYCODE_NUMPAD_9] = InputKeycode::NUM9;
    gApp.keycodes[AKEYCODE_A] = InputKeycode::A;
    gApp.keycodes[AKEYCODE_B] = InputKeycode::B;
    gApp.keycodes[AKEYCODE_C] = InputKeycode::C;
    gApp.keycodes[AKEYCODE_D] = InputKeycode::D;
    gApp.keycodes[AKEYCODE_E] = InputKeycode::E;
    gApp.keycodes[AKEYCODE_F] = InputKeycode::F;
    gApp.keycodes[AKEYCODE_G] = InputKeycode::G;
    gApp.keycodes[AKEYCODE_G] = InputKeycode::H;
    gApp.keycodes[AKEYCODE_I] = InputKeycode::I;
    gApp.keycodes[AKEYCODE_J] = InputKeycode::J;
    gApp.keycodes[AKEYCODE_K] = InputKeycode::K;
    gApp.keycodes[AKEYCODE_L] = InputKeycode::L;
    gApp.keycodes[AKEYCODE_M] = InputKeycode::M;
    gApp.keycodes[AKEYCODE_N] = InputKeycode::N;
    gApp.keycodes[AKEYCODE_O] = InputKeycode::O;
    gApp.keycodes[AKEYCODE_P] = InputKeycode::P;
    gApp.keycodes[AKEYCODE_Q] = InputKeycode::Q;
    gApp.keycodes[AKEYCODE_R] = InputKeycode::R;
    gApp.keycodes[AKEYCODE_S] = InputKeycode::S;
    gApp.keycodes[AKEYCODE_T] = InputKeycode::T;
    gApp.keycodes[AKEYCODE_U] = InputKeycode::U;
    gApp.keycodes[AKEYCODE_V] = InputKeycode::V;
    gApp.keycodes[AKEYCODE_W] = InputKeycode::W;
    gApp.keycodes[AKEYCODE_X] = InputKeycode::X;
    gApp.keycodes[AKEYCODE_Y] = InputKeycode::Y;
    gApp.keycodes[AKEYCODE_Z] = InputKeycode::Z;
    gApp.keycodes[AKEYCODE_APOSTROPHE] = InputKeycode::Apostrophe;
    gApp.keycodes[AKEYCODE_BACKSLASH] = InputKeycode::Backslash;
    gApp.keycodes[AKEYCODE_COMMA] = InputKeycode::Comma;
    gApp.keycodes[AKEYCODE_EQUALS] = InputKeycode::Equal;
    gApp.keycodes[AKEYCODE_GRAVE] = InputKeycode::GraveAccent;
    gApp.keycodes[AKEYCODE_LEFT_BRACKET] = InputKeycode::LeftBracket;
    gApp.keycodes[AKEYCODE_MINUS] = InputKeycode::Minus;
    gApp.keycodes[AKEYCODE_PERIOD] = InputKeycode::Period;
    gApp.keycodes[AKEYCODE_RIGHT_BRACKET] = InputKeycode::RightBracket;
    gApp.keycodes[AKEYCODE_SEMICOLON] = InputKeycode::Semicolon;
    gApp.keycodes[AKEYCODE_SLASH] = InputKeycode::Slash;
    gApp.keycodes[AKEYCODE_LANGUAGE_SWITCH] = InputKeycode::World2;
    gApp.keycodes[AKEYCODE_DEL] = InputKeycode::Backspace;
    gApp.keycodes[AKEYCODE_FORWARD_DEL] = InputKeycode::Delete;
    gApp.keycodes[AKEYCODE_MOVE_END] = InputKeycode::End;
    gApp.keycodes[AKEYCODE_ENTER] = InputKeycode::Enter;
    gApp.keycodes[AKEYCODE_ESCAPE] = InputKeycode::Escape;
    gApp.keycodes[AKEYCODE_MOVE_HOME] = InputKeycode::Home;
    gApp.keycodes[AKEYCODE_INSERT] = InputKeycode::Insert;
    gApp.keycodes[AKEYCODE_MENU] = InputKeycode::Menu;
    gApp.keycodes[AKEYCODE_PAGE_DOWN] = InputKeycode::PageDown;
    gApp.keycodes[AKEYCODE_PAGE_UP] = InputKeycode::PageUp;
    gApp.keycodes[AKEYCODE_BREAK] = InputKeycode::Pause;
    gApp.keycodes[AKEYCODE_SPACE] = InputKeycode::Space;
    gApp.keycodes[AKEYCODE_TAB] = InputKeycode::Tab;
    gApp.keycodes[AKEYCODE_CAPS_LOCK] = InputKeycode::CapsLock;
    gApp.keycodes[AKEYCODE_NUM] = InputKeycode::NumLock;
    gApp.keycodes[AKEYCODE_SCROLL_LOCK] = InputKeycode::ScrollLock;
    gApp.keycodes[AKEYCODE_F1] = InputKeycode::F1;
    gApp.keycodes[AKEYCODE_F2] = InputKeycode::F2;
    gApp.keycodes[AKEYCODE_F3] = InputKeycode::F3;
    gApp.keycodes[AKEYCODE_F4] = InputKeycode::F4;
    gApp.keycodes[AKEYCODE_F5] = InputKeycode::F5;
    gApp.keycodes[AKEYCODE_F6] = InputKeycode::F6;
    gApp.keycodes[AKEYCODE_F7] = InputKeycode::F7;
    gApp.keycodes[AKEYCODE_F8] = InputKeycode::F8;
    gApp.keycodes[AKEYCODE_F9] = InputKeycode::F9;
    gApp.keycodes[AKEYCODE_F10] = InputKeycode::F10;
    gApp.keycodes[AKEYCODE_F11] = InputKeycode::F11;
    gApp.keycodes[AKEYCODE_F12] = InputKeycode::F12;
    gApp.keycodes[AKEYCODE_ALT_LEFT] = InputKeycode::LeftAlt;
    gApp.keycodes[AKEYCODE_CTRL_LEFT] = InputKeycode::LeftControl;
    gApp.keycodes[AKEYCODE_SHIFT_LEFT] = InputKeycode::LeftShift;
    gApp.keycodes[AKEYCODE_SYSRQ] = InputKeycode::PrintScreen;
    gApp.keycodes[AKEYCODE_ALT_RIGHT] = InputKeycode::RightAlt;
    gApp.keycodes[AKEYCODE_CTRL_RIGHT] = InputKeycode::RightControl;
    gApp.keycodes[AKEYCODE_SHIFT_RIGHT] = InputKeycode::RightShift;
    gApp.keycodes[AKEYCODE_DPAD_DOWN] = InputKeycode::Down;
    gApp.keycodes[AKEYCODE_DPAD_LEFT] = InputKeycode::Left;
    gApp.keycodes[AKEYCODE_DPAD_RIGHT] = InputKeycode::Right;
    gApp.keycodes[AKEYCODE_DPAD_UP] = InputKeycode::Up;
    gApp.keycodes[AKEYCODE_NUMPAD_0] = InputKeycode::KP0;
    gApp.keycodes[AKEYCODE_NUMPAD_1] = InputKeycode::KP1;
    gApp.keycodes[AKEYCODE_NUMPAD_2] = InputKeycode::KP2;
    gApp.keycodes[AKEYCODE_NUMPAD_3] = InputKeycode::KP3;
    gApp.keycodes[AKEYCODE_NUMPAD_4] = InputKeycode::KP4;
    gApp.keycodes[AKEYCODE_NUMPAD_5] = InputKeycode::KP5;
    gApp.keycodes[AKEYCODE_NUMPAD_6] = InputKeycode::KP6;
    gApp.keycodes[AKEYCODE_NUMPAD_7] = InputKeycode::KP7;
    gApp.keycodes[AKEYCODE_NUMPAD_8] = InputKeycode::KP8;
    gApp.keycodes[AKEYCODE_NUMPAD_9] = InputKeycode::KP9;
    gApp.keycodes[AKEYCODE_NUMPAD_ADD] = InputKeycode::KPAdd;
    gApp.keycodes[AKEYCODE_NUMPAD_DOT] = InputKeycode::KPDecimal;
    gApp.keycodes[AKEYCODE_NUMPAD_DIVIDE] = InputKeycode::KPDivide;
    gApp.keycodes[AKEYCODE_NUMPAD_ENTER] = InputKeycode::KPEnter;
    gApp.keycodes[AKEYCODE_NUMPAD_MULTIPLY] = InputKeycode::KPMultiply;
    gApp.keycodes[AKEYCODE_NUMPAD_SUBTRACT] = InputKeycode::KPSubtract;
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
        Mem::Free(gApp.savedState);
        gApp.savedState = nullptr;
        gApp.savedStateSize = 0;
    }
    pthread_mutex_unlock(&gApp.mutex);
}

static void appAndroidWriteCmd(AppAndroidCmd event)
{
    if (write(gApp.eventWriteFd, &event, sizeof(event)) != sizeof(event)) {
        OS::AndroidPrintToLog(OSAndroidLogType::Fatal, gApp.name, "Android: Writing event to nessage pipe failed");
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
        OS::AndroidPrintToLog(OSAndroidLogType::Fatal, gApp.name, "Android: No data in command pipe");
    }
    return ANDROID_CMD_INVALID;
}

static void appAndroidCleanup()
{
    if (gApp.initCalled && !gApp.cleanupCalled) {
        if (gApp.desc.callbacks) {
            gApp.desc.callbacks->Cleanup();
        }

        Remote::Release();
        Vfs::Release();

        gApp.cleanupCalled = true;
    }
}

int appAndroidGetCharcodeFromKeycode(int eventType, int keyCode, int metaState)
{
    JNIEnv* jniEnv = OS::AndroidGetJniEnv();
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
                gApp.ev.numTouches = Min<uint32>(AMotionEvent_getPointerCount(event), INPUT_MAX_TOUCH_POINTS);

                for (uint32 i = 0; i < gApp.ev.numTouches; i++) {
                    InputTouchPoint* tp = &gApp.ev.touches[i];
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
            InputMouseButton mouseButton = InputMouseButton::Invalid;
            switch (action) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_MOVE:
                if (button == AMOTION_EVENT_BUTTON_PRIMARY || ((source & AINPUT_SOURCE_TOUCHSCREEN) && button == 0))
                    mouseButton = InputMouseButton::Left;
                else if (button == AMOTION_EVENT_BUTTON_SECONDARY)
                    mouseButton = InputMouseButton::Right;
                eventType = action == AMOTION_EVENT_ACTION_DOWN ? AppEventType::MouseDown : AppEventType::MouseMove;
                break;

            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
            case AMOTION_EVENT_ACTION_OUTSIDE:
                if (button == AMOTION_EVENT_BUTTON_PRIMARY || ((source & AINPUT_SOURCE_TOUCHSCREEN) && button == 0))
                    mouseButton = InputMouseButton::Left;
                else if (button == AMOTION_EVENT_BUTTON_SECONDARY)
                    mouseButton = InputMouseButton::Right;
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
                if (gApp.keycodes[keycode] == InputKeycode::LeftShift || gApp.keycodes[keycode] == InputKeycode::RightShift)
                    gApp.keyMods |= InputKeyModifiers::Shift;
                else if (gApp.keycodes[keycode] == InputKeycode::LeftControl || gApp.keycodes[keycode] == InputKeycode::RightControl)
                    gApp.keyMods |= InputKeyModifiers::Ctrl;
                else if (gApp.keycodes[keycode] == InputKeycode::LeftAlt || gApp.keycodes[keycode] == InputKeycode::RightAlt)
                    gApp.keyMods |= InputKeyModifiers::Alt;
                else if (gApp.keycodes[keycode] == InputKeycode::LeftSuper || gApp.keycodes[keycode] == InputKeycode::RightSuper)
                    gApp.keyMods |= InputKeyModifiers::Super;
                gApp.keysDown[uint32(gApp.keycodes[keycode])] = true;
                break;
            case AKEY_EVENT_ACTION_UP:
                eventType = AppEventType::KeyUp;
                if (gApp.keycodes[keycode] == InputKeycode::LeftShift || gApp.keycodes[keycode] == InputKeycode::RightShift)
                    gApp.keyMods &= ~InputKeyModifiers::Shift;
                else if (gApp.keycodes[keycode] == InputKeycode::LeftControl || gApp.keycodes[keycode] == InputKeycode::RightControl)
                    gApp.keyMods &= ~InputKeyModifiers::Ctrl;
                else if (gApp.keycodes[keycode] == InputKeycode::LeftAlt || gApp.keycodes[keycode] == InputKeycode::RightAlt)
                    gApp.keyMods &= ~InputKeyModifiers::Alt;
                else if (gApp.keycodes[keycode] == InputKeycode::LeftSuper || gApp.keycodes[keycode] == InputKeycode::RightSuper)
                    gApp.keyMods &= ~InputKeyModifiers::Super;
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

bool App::Run(const AppDesc& desc)
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
        gApp.clipboard = Mem::AllocZeroTyped<char>((uint32)gApp.desc.clipboardSizeBytes);

    if (desc.windowTitle)   Str::Copy(gApp.windowTitle, sizeof(gApp.windowTitle), desc.windowTitle);
    else                    Str::Copy(gApp.windowTitle, sizeof(gApp.windowTitle), "Junkyard");

    Str::Copy(gApp.name, sizeof(gApp.name), "Junkyard");

    appAndroidInitKeyTable();

    // Initialize settings if not initialied before
    // Since this is not a recommended way, we also throw an assert
    if (!SettingsJunkyard::IsInitialized()) {
        ASSERT_MSG(0, "Settings must be initialized before this call. See settingsInitialize() function");
        SettingsJunkyard::Initialize({}); // initialize with default settings
    }

    // Set some initial settings
    Mem::EnableMemPro(SettingsJunkyard::Get().engine.enableMemPro);
    MemTempAllocator::EnableCallstackCapture(SettingsJunkyard::Get().debug.captureStacktraceForTempAllocator);
    Debug::SetCaptureStacktraceForFiberProtector(SettingsJunkyard::Get().debug.captureStacktraceForFiberProtector);
    Log::SetSettings(static_cast<LogLevel>(SettingsJunkyard::Get().engine.logLevel), SettingsJunkyard::Get().engine.breakOnErrors, SettingsJunkyard::Get().engine.treatWarningsAsErrors);

    // RemoteServices
    if (!Remote::Initialize()) {
        ASSERT_MSG(0, "Initializing Server failed");
        return false;
    }

    // VirutalFS -> Depends on RemoteServices for some functionality
    if (!Vfs::Initialize()) {
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
        if (!gApp.desc.callbacks->Initialize()) {
            App::Quit();
            return false;
        }
        
        Engine::_private::PostInitialize();

        gApp.initCalled = true;
    }
    
    if (gApp.initCalled) {
        if (!gApp.overrideUpdateCallback.first)
            gApp.desc.callbacks->Update(dt);
        else
            gApp.overrideUpdateCallback.first(dt, gApp.overrideUpdateCallback.second);
    }

    gApp.frameCount++;
    return true;
}

void App::ShowMouse(bool visible)
{
}

bool App::IsMouseShown()
{
    return false;
}

const char* App::GetClipboardString()
{
    return nullptr;
}

void App::Quit()
{
    gApp.quitRequested = true;
}

uint16 App::GetWindowWidth()
{
    return gApp.windowWidth;
}

uint16 App::GetWindowHeight()
{
    return gApp.windowHeight;
}

uint16 App::GetFramebufferWidth()
{
    return gApp.framebufferWidth;
}

uint16 App::GetFramebufferHeight()
{
    return gApp.framebufferHeight;
}

float appGetDpiScale()
{
    return gApp.dpiScale;
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
    if (!alreadyExist) {
        gApp.eventCallbacks.Push({callback, userData});
    }    
}

void App::UnregisterEventsCallback(AppEventCallback callback)
{
    if (uint32 index = gApp.eventCallbacks.FindIf([callback](const AppEventCallbackPair& p)->bool {
        return p.callback == callback;});
        index != UINT32_MAX)
    {
        gApp.eventCallbacks.RemoveAndSwap(index);
    }
}

const char* App::GetName()
{
    return gApp.name;
}

void App::SetCursor(AppMouseCursor cursor)
{
    UNUSED(cursor);
}

void* App::GetNativeWindowHandle()
{
    return gApp.window;
}

static void* appAndroidMainThreadFn(void* userData)
{
    UNUSED(userData);

    OS::AndroidAcquireJniEnv(gApp.activity);

    // Just call the main function "AndroidMain" which is implemented by the user just like the usual "main"
    // `AndroidMain` basically implements callbacks, calls `appInitialize` or whatever initialization and returns
    int r = AndroidMain(0, nullptr);
    ASSERT_MSG(gApp.valid, "appInitialize is not called within AndroidMain function");
    if (r == 0 && gApp.valid) {
        // main loop
        uint64 tmPrev = 0;

        while (!gApp.quitRequested) {
            if (appAndroidIsOnForeground()) {
                uint64 tmNow = Timer::GetTicks();
                appAndroidFrame(!gApp.firstFrame ? static_cast<fl32>(Timer::ToSec(Timer::Diff(tmNow, tmPrev))) : 0);
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


    OS::AndroidReleaseJniEnv(gApp.activity);
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
        gApp.savedState = Mem::AllocCopy<uint8>((uint8*)savedState, (uint32)savedStateSize);
        gApp.savedStateSize = savedStateSize;
    }
    
    int msgPipe[2];
    if (pipe(msgPipe)) {
        OS::AndroidPrintToLog(OSAndroidLogType::Fatal, gApp.name, "Android: Writing event to message pipe failed");
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

void* App::GetNativeAppHandle()
{
    return gApp.activity;
}

AAssetManager* App::AndroidGetAssetManager()
{
    return gApp.activity->assetManager;
}

ANativeActivity* App::AndroidGetActivity()
{
    return gApp.activity;
}

AppDisplayInfo App::GetDisplayInfo()
{
    // TODO
    return AppDisplayInfo {
        .width = gApp.windowWidth,
        .height = gApp.windowHeight,
        .refreshRate = 60,
        .dpiScale = gApp.dpiScale
    };
}

bool App::IsKeyDown(InputKeycode keycode)
{
    return gApp.keysDown[uint32(keycode)];
}

bool App::IsAnyKeysDown(const InputKeycode* keycodes, uint32 numKeycodes)
{
    bool down = false;
    for (uint32 i = 0; i < numKeycodes; i++) {
        down |= gApp.keysDown[uint32(gApp.keycodes[i])];
    }
    return down;
}

InputKeyModifiers App::GetKeyMods()
{
    return gApp.keyMods;
}

void App::AndroidSetFramebufferTransform(AppFramebufferTransform transform)
{
    gApp.framebufferTransform = transform;
}

AppFramebufferTransform App::GetFramebufferTransform()
{
    return gApp.framebufferTransform;
}

void App::CaptureMouse()
{
}

void  App::ReleaseMouse()
{
}

void App::OverrideUpdateCallback(AppUpdateOverrideCallback callback, void* userData)
{
    gApp.overrideUpdateCallback.first = callback;
    gApp.overrideUpdateCallback.second = userData;
}


#endif  // PLATFORM_ANDROID
