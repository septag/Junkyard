#include "Application.h"

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

#include "GLFW/glfw3.h"

#include <unistd.h>

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
};

static AppWindowState gApp;

namespace App
{
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
            int w, h;
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
    }    
}

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

    // _InitKeyTable();

    glfwSetErrorCallback(_GlfwErrorCallback);
    if (!glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW3");
        return false;
    }

    // Window creation
    if (settings.graphics.IsGraphicsEnabled()) {
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
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

        glfwSetWindowSizeCallback(window, _GlfwSizeCallback);
        glfwSetWindowPosCallback(window, _GlfwPosCallback);

        glfwSetWindowPos(window, gApp.mainRect.xmin, gApp.mainRect.ymin);
    }

    if (gApp.window) {
        GLFWwindow* window = gApp.window;
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {

            }
        }
    }

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

void* GetNativeAppHandle()
{
    return IntToPtr(getpid());
}

#endif // PLATFORM_LINUX