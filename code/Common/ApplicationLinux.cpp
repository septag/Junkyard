#include "Application.h"
#include "InputTypes.h"

#if PLATFORM_LINUX

#include "VirtualFS.h"
#include "RemoteServices.h"
#include "JunkyardSettings.h"

#include "../Config.h"
#include "../Engine.h"

#include "../Core/Allocators.h"
#include "../Core/Debug.h"
#include "../Core/Log.h"
#include "../Core/MathAll.h"
#include "../Core/External/mgustavsson/ini.h"
#include "../Core/Arrays.h"

#include "GLFW/glfw3.h"

#include <unistd.h>

#define APP_MAX_KEY_CODES 512

struct AppEventCallbackPair
{
    AppEventCallback callback;
    void*            userData;
};

struct AppWindowState
{
    char name[32];
    AppDesc desc;
    uint16 windowWidth;
    uint16 windowHeight;
    uint16 framebufferWidth;
    uint16 framebufferHeight;
    float dpiScale;
    bool clipboardEnabled;
    AppMouseCursor mouseCursor;
    char windowTitle[128];
    
    GLFWwindow* window;
    RectInt mainRect;
    bool windowModified;
    
    Float2 mousePos;
    InputMouseButton mouseButton;
    InputKeyModifiers keyMods;
    InputKeycode keycodes[APP_MAX_KEY_CODES];
    bool keysPressed[APP_MAX_KEY_CODES];

    Array<AppEventCallbackPair> eventCallbacks;
    Pair<AppUpdateOverrideCallback, void*> overrideUpdateCallback;

    GLFWcursor* cursors[uint32(AppMouseCursor::_Count)];
};

static AppWindowState gApp;

namespace App
{
    static void _InitKeyTable()
    {
        gApp.keycodes[GLFW_KEY_KP_0] = InputKeycode::NUM0;
        gApp.keycodes[GLFW_KEY_KP_1] = InputKeycode::NUM1;
        gApp.keycodes[GLFW_KEY_KP_2] = InputKeycode::NUM2;
        gApp.keycodes[GLFW_KEY_KP_3] = InputKeycode::NUM3;
        gApp.keycodes[GLFW_KEY_KP_4] = InputKeycode::NUM4;
        gApp.keycodes[GLFW_KEY_KP_5] = InputKeycode::NUM5;
        gApp.keycodes[GLFW_KEY_KP_6] = InputKeycode::NUM6;
        gApp.keycodes[GLFW_KEY_KP_7] = InputKeycode::NUM7;
        gApp.keycodes[GLFW_KEY_KP_8] = InputKeycode::NUM8;
        gApp.keycodes[GLFW_KEY_KP_9] = InputKeycode::NUM9;
        gApp.keycodes[GLFW_KEY_A] = InputKeycode::A;
        gApp.keycodes[GLFW_KEY_B] = InputKeycode::B;
        gApp.keycodes[GLFW_KEY_C] = InputKeycode::C;
        gApp.keycodes[GLFW_KEY_D] = InputKeycode::D;
        gApp.keycodes[GLFW_KEY_E] = InputKeycode::E;
        gApp.keycodes[GLFW_KEY_F] = InputKeycode::F;
        gApp.keycodes[GLFW_KEY_G] = InputKeycode::G;
        gApp.keycodes[GLFW_KEY_H] = InputKeycode::H;
        gApp.keycodes[GLFW_KEY_I] = InputKeycode::I;
        gApp.keycodes[GLFW_KEY_J] = InputKeycode::J;
        gApp.keycodes[GLFW_KEY_K] = InputKeycode::K;
        gApp.keycodes[GLFW_KEY_L] = InputKeycode::L;
        gApp.keycodes[GLFW_KEY_M] = InputKeycode::M;
        gApp.keycodes[GLFW_KEY_N] = InputKeycode::N;
        gApp.keycodes[GLFW_KEY_O] = InputKeycode::O;
        gApp.keycodes[GLFW_KEY_P] = InputKeycode::P;
        gApp.keycodes[GLFW_KEY_Q] = InputKeycode::Q;
        gApp.keycodes[GLFW_KEY_R] = InputKeycode::R;
        gApp.keycodes[GLFW_KEY_S] = InputKeycode::S;
        gApp.keycodes[GLFW_KEY_T] = InputKeycode::T;
        gApp.keycodes[GLFW_KEY_U] = InputKeycode::U;
        gApp.keycodes[GLFW_KEY_V] = InputKeycode::V;
        gApp.keycodes[GLFW_KEY_W] = InputKeycode::W;
        gApp.keycodes[GLFW_KEY_X] = InputKeycode::X;
        gApp.keycodes[GLFW_KEY_Y] = InputKeycode::Y;
        gApp.keycodes[GLFW_KEY_Z] = InputKeycode::Z;
        gApp.keycodes[GLFW_KEY_APOSTROPHE] = InputKeycode::Apostrophe;
        gApp.keycodes[GLFW_KEY_BACKSLASH] = InputKeycode::Backslash;
        gApp.keycodes[GLFW_KEY_COMMA] = InputKeycode::Comma;
        gApp.keycodes[GLFW_KEY_EQUAL] = InputKeycode::Equal;
        gApp.keycodes[GLFW_KEY_GRAVE_ACCENT] = InputKeycode::GraveAccent;
        gApp.keycodes[GLFW_KEY_LEFT_BRACKET] = InputKeycode::LeftBracket;
        gApp.keycodes[GLFW_KEY_MINUS] = InputKeycode::Minus;
        gApp.keycodes[GLFW_KEY_PERIOD] = InputKeycode::Period;
        gApp.keycodes[GLFW_KEY_RIGHT_BRACKET] = InputKeycode::RightBracket;
        gApp.keycodes[GLFW_KEY_SEMICOLON] = InputKeycode::Semicolon;
        gApp.keycodes[GLFW_KEY_SLASH] = InputKeycode::Slash;
        gApp.keycodes[GLFW_KEY_WORLD_2] = InputKeycode::World2;
        gApp.keycodes[GLFW_KEY_BACKSPACE] = InputKeycode::Backspace;
        gApp.keycodes[GLFW_KEY_DELETE] = InputKeycode::Delete;
        gApp.keycodes[GLFW_KEY_END] = InputKeycode::End;
        gApp.keycodes[GLFW_KEY_ENTER] = InputKeycode::Enter;
        gApp.keycodes[GLFW_KEY_ESCAPE] = InputKeycode::Escape;
        gApp.keycodes[GLFW_KEY_HOME] = InputKeycode::Home;
        gApp.keycodes[GLFW_KEY_INSERT] = InputKeycode::Insert;
        gApp.keycodes[GLFW_KEY_MENU] = InputKeycode::Menu;
        gApp.keycodes[GLFW_KEY_PAGE_DOWN] = InputKeycode::PageDown;
        gApp.keycodes[GLFW_KEY_PAGE_UP] = InputKeycode::PageUp;
        gApp.keycodes[GLFW_KEY_PAUSE] = InputKeycode::Pause;
        gApp.keycodes[GLFW_KEY_SPACE] = InputKeycode::Space;
        gApp.keycodes[GLFW_KEY_TAB] = InputKeycode::Tab;
        gApp.keycodes[GLFW_KEY_CAPS_LOCK] = InputKeycode::CapsLock;
        gApp.keycodes[GLFW_KEY_NUM_LOCK] = InputKeycode::NumLock;
        gApp.keycodes[GLFW_KEY_SCROLL_LOCK] = InputKeycode::ScrollLock;
        gApp.keycodes[GLFW_KEY_F1] = InputKeycode::F1;
        gApp.keycodes[GLFW_KEY_F2] = InputKeycode::F2;
        gApp.keycodes[GLFW_KEY_F3] = InputKeycode::F3;
        gApp.keycodes[GLFW_KEY_F4] = InputKeycode::F4;
        gApp.keycodes[GLFW_KEY_F5] = InputKeycode::F5;
        gApp.keycodes[GLFW_KEY_F6] = InputKeycode::F6;
        gApp.keycodes[GLFW_KEY_F7] = InputKeycode::F7;
        gApp.keycodes[GLFW_KEY_F8] = InputKeycode::F8;
        gApp.keycodes[GLFW_KEY_F9] = InputKeycode::F9;
        gApp.keycodes[GLFW_KEY_F10] = InputKeycode::F10;
        gApp.keycodes[GLFW_KEY_F11] = InputKeycode::F11;
        gApp.keycodes[GLFW_KEY_F12] = InputKeycode::F12;
        gApp.keycodes[GLFW_KEY_F13] = InputKeycode::F13;
        gApp.keycodes[GLFW_KEY_F14] = InputKeycode::F14;
        gApp.keycodes[GLFW_KEY_F15] = InputKeycode::F15;
        gApp.keycodes[GLFW_KEY_F16] = InputKeycode::F16;
        gApp.keycodes[GLFW_KEY_F17] = InputKeycode::F17;
        gApp.keycodes[GLFW_KEY_F18] = InputKeycode::F18;
        gApp.keycodes[GLFW_KEY_F19] = InputKeycode::F19;
        gApp.keycodes[GLFW_KEY_F20] = InputKeycode::F20;
        gApp.keycodes[GLFW_KEY_F21] = InputKeycode::F21;
        gApp.keycodes[GLFW_KEY_F22] = InputKeycode::F22;
        gApp.keycodes[GLFW_KEY_F23] = InputKeycode::F23;
        gApp.keycodes[GLFW_KEY_F24] = InputKeycode::F24;
        gApp.keycodes[GLFW_KEY_LEFT_ALT] = InputKeycode::LeftAlt;
        gApp.keycodes[GLFW_KEY_LEFT_CONTROL] = InputKeycode::LeftControl;
        gApp.keycodes[GLFW_KEY_LEFT_SHIFT] = InputKeycode::LeftShift;
        gApp.keycodes[GLFW_KEY_LEFT_SUPER] = InputKeycode::LeftSuper;
        gApp.keycodes[GLFW_KEY_PRINT_SCREEN] = InputKeycode::PrintScreen;
        gApp.keycodes[GLFW_KEY_RIGHT_ALT] = InputKeycode::RightAlt;
        gApp.keycodes[GLFW_KEY_RIGHT_CONTROL] = InputKeycode::RightControl;
        gApp.keycodes[GLFW_KEY_RIGHT_SHIFT] = InputKeycode::RightShift;
        gApp.keycodes[GLFW_KEY_RIGHT_SUPER] = InputKeycode::RightSuper;
        gApp.keycodes[GLFW_KEY_DOWN] = InputKeycode::Down;
        gApp.keycodes[GLFW_KEY_LEFT] = InputKeycode::Left;
        gApp.keycodes[GLFW_KEY_RIGHT] = InputKeycode::Right;
        gApp.keycodes[GLFW_KEY_UP] = InputKeycode::Up;
        gApp.keycodes[GLFW_KEY_KP_0] = InputKeycode::KP0;
        gApp.keycodes[GLFW_KEY_KP_1] = InputKeycode::KP1;
        gApp.keycodes[GLFW_KEY_KP_2] = InputKeycode::KP2;
        gApp.keycodes[GLFW_KEY_KP_3] = InputKeycode::KP3;
        gApp.keycodes[GLFW_KEY_KP_4] = InputKeycode::KP4;
        gApp.keycodes[GLFW_KEY_KP_5] = InputKeycode::KP5;
        gApp.keycodes[GLFW_KEY_KP_6] = InputKeycode::KP6;
        gApp.keycodes[GLFW_KEY_KP_7] = InputKeycode::KP7;
        gApp.keycodes[GLFW_KEY_KP_8] = InputKeycode::KP8;
        gApp.keycodes[GLFW_KEY_KP_9] = InputKeycode::KP9;
        gApp.keycodes[GLFW_KEY_KP_ADD] = InputKeycode::KPAdd;
        gApp.keycodes[GLFW_KEY_KP_DECIMAL] = InputKeycode::KPDecimal;
        gApp.keycodes[GLFW_KEY_KP_DIVIDE] = InputKeycode::KPDivide;
        gApp.keycodes[GLFW_KEY_KP_ENTER] = InputKeycode::KPEnter;
        gApp.keycodes[GLFW_KEY_KP_MULTIPLY] = InputKeycode::KPMultiply;
        gApp.keycodes[GLFW_KEY_KP_SUBTRACT] = InputKeycode::KPSubtract;
    }

    static void _LoadInitRects()
    {
        ini_t* windowsIni = nullptr;
        char iniFilename[64];
        Str::PrintFmt(iniFilename, sizeof(iniFilename), "%s_windows.ini", GetName());

        Blob data = Vfs::ReadFile(iniFilename, VfsFlags::TextFile|VfsFlags::AbsolutePath);
        if (data.IsValid()) {
            windowsIni = ini_load((const char*)data.Data(), Mem::GetDefaultAlloc());
            data.Free();
        }
    
        auto GetWindowData = [](ini_t* ini, const char* name, RectInt* rc) {
            int id = ini_find_section(ini, name, Str::Len(name));
            if (id != -1) {
            int topId = ini_find_property(ini, id, "top", 0);
            int bottomId = ini_find_property(ini, id, "bottom", 0);
            int leftId = ini_find_property(ini, id, "left", 0);
            int rightId = ini_find_property(ini, id, "right", 0);

            if (topId != -1)
                rc->ymin = Str::ToInt(ini_property_value(ini, id, topId));
            if (bottomId != -1)
                rc->ymax = Str::ToInt(ini_property_value(ini, id, bottomId));
            if (leftId != -1)
                rc->xmin = Str::ToInt(ini_property_value(ini, id, leftId));
            if (rightId != -1)
                rc->xmax = Str::ToInt(ini_property_value(ini, id, rightId));
            }
            return rc;
        };

        gApp.mainRect = RECTINT_EMPTY;
        if (windowsIni) {
            GetWindowData(windowsIni, "Main", &gApp.mainRect);
            gApp.windowWidth = (uint16)gApp.mainRect.Width();
            gApp.windowHeight = (uint16)gApp.mainRect.Height();
            gApp.framebufferWidth = gApp.windowWidth;
            gApp.framebufferHeight = gApp.windowHeight;
            ini_destroy(windowsIni);
        }
    }

    static void _SaveInitRects()
    {
        auto PutWindowData = [](ini_t* ini, const char* name, const RectInt& rc) {
            int id = ini_section_add(ini, name, Str::Len(name));
            char value[32];
            Str::PrintFmt(value, sizeof(value), "%d", rc.ymin);
            ini_property_add(ini, id, "top", 0, value, Str::Len(value));

            Str::PrintFmt(value, sizeof(value), "%d", rc.ymax);
            ini_property_add(ini, id, "bottom", 0, value, Str::Len(value));

            Str::PrintFmt(value, sizeof(value), "%d", rc.xmin);
            ini_property_add(ini, id, "left", 0, value, Str::Len(value));

            Str::PrintFmt(value, sizeof(value), "%d", rc.xmax);
            ini_property_add(ini, id, "right", 0, value, Str::Len(value));
        };

        if (gApp.windowModified && gApp.window) {
            ini_t* windowsIni = ini_create(Mem::GetDefaultAlloc());
            char iniFilename[64];
            Str::PrintFmt(iniFilename, sizeof(iniFilename), "%s_windows.ini", GetName());

            RectInt mainRect;
            glfwGetWindowPos(gApp.window, &mainRect.xmin, &mainRect.ymin);
            mainRect.SetWidth(gApp.windowWidth);
            mainRect.SetHeight(gApp.windowHeight);
            PutWindowData(windowsIni, "Main", mainRect);

            int size = ini_save(windowsIni, nullptr, 0);
            if (size > 0) {
                char* data = Mem::AllocTyped<char>(size);
                ini_save(windowsIni, data, size);

                while (size && data[size-1] == '\0')
                    --size;
    
                File f;
                if (f.Open(iniFilename, FileOpenFlags::Write)) {
                    f.Write<char>(data, static_cast<size_t>(size));
                    f.Close();
                }
                Mem::Free(data);
            }
    
            ini_destroy(windowsIni);
        }
    }

    static void _GlfwErrorCallback(int error, const char* description)
    {
        LOG_ERROR("GLFW error %d: %s", error, description);
    }

    static void _CallEvent(const AppEvent& ev)
    {
        gApp.desc.callbacks->OnEvent(ev);

        // Call extra registered event callbacks
        for (AppEventCallbackPair& c : gApp.eventCallbacks)
            c.callback(ev, c.userData);
    }

    static void _GlfwPosCallback(GLFWwindow* window, int xpos, int ypos)
    {
        gApp.windowModified = true;
    }

    static void _GlfwContentScaleCallback(GLFWwindow* window, float xscale, float yscale)
    {
        gApp.windowModified = true;
        gApp.dpiScale = Max(xscale, yscale);
    }

    static void _GlfwSizeCallback(GLFWwindow* window, int width, int height)
    {
        gApp.windowModified = true;
        gApp.windowWidth = uint16(float(width) / gApp.dpiScale);
        gApp.windowHeight = uint16(float(height) / gApp.dpiScale);
        gApp.framebufferWidth = width;
        gApp.framebufferHeight = height;

        AppEvent event {
            .type = AppEventType::Resized,
            .windowWidth = gApp.windowWidth,
            .windowHeight = gApp.windowHeight,
            .framebufferWidth = gApp.framebufferWidth,
            .framebufferHeight = gApp.framebufferHeight
        };

        _CallEvent(event);
    }

    static InputKeyModifiers _GlfwTranslateKeyMods(int mods)
    {
        InputKeyModifiers myMods = InputKeyModifiers::None;
        if (mods & GLFW_MOD_SHIFT)
            myMods |= InputKeyModifiers::Shift;
        if (mods & GLFW_MOD_ALT)
            myMods |= InputKeyModifiers::Alt;
        if (mods & GLFW_MOD_CONTROL)
            myMods |= InputKeyModifiers::Ctrl;
        if (mods & GLFW_MOD_SUPER)
            myMods |= InputKeyModifiers::Super;
        return myMods;
    }

    static void _GlfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
    {
        InputMouseButton myButton;
        switch (button) {
            case GLFW_MOUSE_BUTTON_LEFT: myButton = InputMouseButton::Left; break;
            case GLFW_MOUSE_BUTTON_RIGHT: myButton = InputMouseButton::Right; break;
            case GLFW_MOUSE_BUTTON_MIDDLE: myButton = InputMouseButton::Middle; break;
            default: break;
        }
        
        AppEventType type = AppEventType::_Count;
        if (action == GLFW_PRESS) {
            gApp.mouseButton = myButton;
            type = AppEventType::MouseDown;
        }
        else if (action == GLFW_RELEASE) {
            type = AppEventType::MouseUp;
            gApp.mouseButton = InputMouseButton::Invalid;
        }

        if (type != AppEventType::_Count) {
            AppEvent event {
                .type = type,
                .keyMods = gApp.keyMods,
                .mouseX = gApp.mousePos.x,
                .mouseY = gApp.mousePos.y,
                .mouseButton = myButton,
                .windowWidth = gApp.windowWidth,
                .windowHeight = gApp.windowHeight,
                .framebufferWidth = gApp.framebufferWidth,
                .framebufferHeight = gApp.framebufferHeight
            };
            _CallEvent(event);
        }
    }

    static void _GlfwMouseMoveCallback(GLFWwindow* window, double xpos, double ypos)
    {
        gApp.mousePos = Float2(xpos/gApp.dpiScale, ypos/gApp.dpiScale);
        AppEvent event {
            .type = AppEventType::MouseMove,
            .keyMods = gApp.keyMods,
            .mouseX = gApp.mousePos.x,
            .mouseY = gApp.mousePos.y,
            .mouseButton = gApp.mouseButton,
            .windowWidth = gApp.windowWidth,
            .windowHeight = gApp.windowHeight,
            .framebufferWidth = gApp.framebufferWidth,
            .framebufferHeight = gApp.framebufferHeight
        };
        _CallEvent(event);        
    }

    static void _GlfwMouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
    {
        AppEvent event {
            .type = AppEventType::MouseScroll,
            .keyMods = gApp.keyMods,
            .mouseX = gApp.mousePos.x, 
            .mouseY = gApp.mousePos.y,
            .mouseButton = gApp.mouseButton,
            .scrollX = float(xoffset),
            .scrollY = float(yoffset),
            .windowWidth = gApp.windowWidth,
            .windowHeight = gApp.windowHeight,
            .framebufferWidth = gApp.framebufferWidth,
            .framebufferHeight = gApp.framebufferHeight
        };

        _CallEvent(event);
    }

    static void _GlfwKeysCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        gApp.keyMods = _GlfwTranslateKeyMods(mods);
        if (action == GLFW_PRESS || action == GLFW_RELEASE || action == GLFW_REPEAT) {
            ASSERT(key >=0 && key < APP_MAX_KEY_CODES);
            InputKeycode keycode = gApp.keycodes[key];
            gApp.keysPressed[uint32(keycode)] = action == GLFW_PRESS | action == GLFW_REPEAT;
            
            AppEvent event {
                .type = (action == GLFW_PRESS || action == GLFW_REPEAT) ? AppEventType::KeyDown : AppEventType::KeyUp,
                .keycode = keycode,
                .keyRepeat = 1,
                .keyMods = gApp.keyMods
            };

            _CallEvent(event);
        }
    }

    static void _GlfwCharCallback(GLFWwindow* window, unsigned int codepoint)
    {
        AppEvent event {
            .type = AppEventType::Char,
            .charcode = codepoint,
            .keyRepeat = 1,
            .keyMods = gApp.keyMods
        };
        _CallEvent(event);
    }
} // App

bool App::Run(const AppDesc &desc)
{
    TimerStopWatch stopwatch;

    ASSERT_MSG(desc.callbacks, "App callbacks is not set");
    if (!desc.callbacks)
        return false;

    gApp.desc = desc;

    // Set some initial values, sizes and DPI will be changed later based on ini config and screen DPI
    gApp.windowWidth = gApp.desc.initWidth;
    gApp.windowHeight = gApp.desc.initHeight;
    gApp.framebufferWidth = gApp.desc.initWidth;
    gApp.framebufferHeight = gApp.desc.initHeight;
    gApp.dpiScale = 1.0f;
    gApp.clipboardEnabled = desc.enableClipboard;
    gApp.mouseCursor = AppMouseCursor::None;    

    char moduleFilename[128];
    OS::GetMyPath(moduleFilename, sizeof(moduleFilename));
    PathUtils::GetFilename(moduleFilename, moduleFilename, sizeof(moduleFilename));
    Str::Copy(gApp.name, sizeof(gApp.name), moduleFilename);

    // Initialize settings if not initialied before
    // Since this is not a recommended way, we also throw an assert
    if (!SettingsJunkyard::IsInitialized()) {
        ASSERT_MSG(0, "Settings must be initialized before this call. See settingsInitialize() function");
        SettingsJunkyard::Initialize({}); // initialize with default settings
    }

    const SettingsJunkyard& settings = SettingsJunkyard::Get();

    MemTempAllocator::EnableCallstackCapture(settings.debug.captureStacktraceForTempAllocator);
    Debug::SetCaptureStacktraceForFiberProtector(settings.debug.captureStacktraceForFiberProtector);
    Log::SetSettings(static_cast<LogLevel>(settings.engine.logLevel), SettingsJunkyard::Get().engine.breakOnErrors, SettingsJunkyard::Get().engine.treatWarningsAsErrors);

    if (desc.windowTitle)
        Str::Copy(gApp.windowTitle, sizeof(gApp.windowTitle), desc.windowTitle);
    else
        Str::Copy(gApp.windowTitle, sizeof(gApp.windowTitle), settings.app.appName);

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

    _InitKeyTable();

    glfwSetErrorCallback(_GlfwErrorCallback);
    if (!glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW3");
        return false;
    }

    // Window creation
    if (settings.graphics.IsGraphicsEnabled()) {
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        _LoadInitRects();
    
        GLFWwindow* window = glfwCreateWindow(gApp.windowWidth, gApp.windowHeight, gApp.windowTitle, nullptr, nullptr);
        if (!window) {
            LOG_ERROR("Failed to create main window");
            return false;
        }
        gApp.window = window;

        float xscale, yscale;
        glfwGetWindowContentScale(window, &xscale, &yscale);
        glfwSetWindowContentScaleCallback(window, _GlfwContentScaleCallback);
        gApp.dpiScale = Max(xscale, yscale);

        // Scale framebuffer
        gApp.framebufferWidth = uint16(float(gApp.windowWidth) * gApp.dpiScale);
        gApp.framebufferHeight = uint16(float(gApp.windowHeight) * gApp.dpiScale);

        glfwSetWindowSizeCallback(window, _GlfwSizeCallback);
        glfwSetWindowPosCallback(window, _GlfwPosCallback);
        glfwSetMouseButtonCallback(window, _GlfwMouseButtonCallback);
        glfwSetCursorPosCallback(window, _GlfwMouseMoveCallback);
        glfwSetScrollCallback(window, _GlfwMouseScrollCallback);
        glfwSetKeyCallback(window, _GlfwKeysCallback);
        glfwSetCharCallback(window, _GlfwCharCallback);

        glfwSetWindowPos(window, gApp.mainRect.xmin, gApp.mainRect.ymin);

        // Cursors
        GLFWcursor* arrowCursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        gApp.cursors[uint32(AppMouseCursor::Arrow)] = arrowCursor;
        gApp.cursors[uint32(AppMouseCursor::TextInput)] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
        gApp.cursors[uint32(AppMouseCursor::ResizeAll)] = arrowCursor;
        gApp.cursors[uint32(AppMouseCursor::ResizeNS)] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
        gApp.cursors[uint32(AppMouseCursor::ResizeWE)] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
        gApp.cursors[uint32(AppMouseCursor::ResizeNESW)] = arrowCursor;
        gApp.cursors[uint32(AppMouseCursor::ResizeNWSE)] = arrowCursor;
        gApp.cursors[uint32(AppMouseCursor::Hand)] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
        gApp.cursors[uint32(AppMouseCursor::NotAllowed)] = arrowCursor;
    }

    LOG_INFO("(init) %s v%u.%u.%u initialized (%.1f ms)", 
        settings.app.appName,  
        GetVersionMajor(settings.app.appVersion),
        GetVersionMinor(settings.app.appVersion),
        GetVersionPatch(settings.app.appVersion),
        stopwatch.ElapsedMS());

    if (!desc.callbacks->Initialize()) {
        LOG_ERROR("Initialization failed");
        return false;
    }    

    Engine::_private::PostInitialize();

    // Main message loop
    uint64 tmNow = Timer::GetTicks();
    uint64 tmPrev = tmNow;
    if (gApp.window) {
        GLFWwindow* window = gApp.window;
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {

            }

            tmNow = Timer::GetTicks();
            float dt = float(Timer::ToSec(tmNow - tmPrev));
            if (!gApp.overrideUpdateCallback.first)
                desc.callbacks->Update(dt);
            else
                gApp.overrideUpdateCallback.first(dt, gApp.overrideUpdateCallback.second);

            tmPrev = tmNow;
        }
    }

    gApp.desc.callbacks->Cleanup();

    Remote::Release();
    Vfs::Release();

    if (gApp.window) {
        _SaveInitRects();
        glfwDestroyWindow(gApp.window);
        gApp.window = nullptr;
    }
    glfwTerminate();

    return true;
}

const char* App::GetName(void)
{
    return gApp.name;
}

void* App::GetNativeWindowHandle()
{
    return gApp.window;
}

void* App::GetNativeAppHandle()
{
    return IntToPtr(getpid());
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

AppFramebufferTransform App::GetFramebufferTransform()
{
    return AppFramebufferTransform::None;
}

AppDisplayInfo App::GetDisplayInfo()
{
    ASSERT(gApp.window);
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    ASSERT(mon);

    float xscale, yscale;
    const GLFWvidmode* vidMode = glfwGetVideoMode(mon);
    glfwGetMonitorContentScale(mon, &xscale, &yscale);
    AppDisplayInfo disp {
        .width = uint16(vidMode->width),
        .height = uint16(vidMode->height),
        .refreshRate = uint16(vidMode->refreshRate),
        .dpiScale = Max(xscale, yscale)
    };
    return disp;
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
    return InputKeyModifiers::None;
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
    if (uint32 index = gApp.eventCallbacks.FindIf([callback](const AppEventCallbackPair& p)->bool {return p.callback == callback;});
        index != UINT32_MAX)
    {
        gApp.eventCallbacks.RemoveAndSwap(index);
    }
}

void App::OverrideUpdateCallback(AppUpdateOverrideCallback callback, void* userData)
{
    gApp.overrideUpdateCallback.first = callback;
    gApp.overrideUpdateCallback.second = userData;
}

void App::SetCursor(AppMouseCursor cursor)
{
    ASSERT(gApp.window);
    if (cursor != AppMouseCursor::None)
        glfwSetCursor(gApp.window, gApp.cursors[uint32(cursor)]);
}

void App::CaptureMouse()
{
    ASSERT(gApp.window);
    glfwSetInputMode(gApp.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void App::ReleaseMouse()
{
    ASSERT(gApp.window);
    glfwSetInputMode(gApp.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

bool App::SetClipboardString(const char* str)
{
    ASSERT(gApp.window);
    glfwSetClipboardString(gApp.window, str);
    return true;
}

const char* App::GetClipboardString()
{
    ASSERT(gApp.window);
    return glfwGetClipboardString(gApp.window);
}



#endif // PLATFORM_LINUX