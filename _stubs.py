import io

STUBS = """
//----------------------------------------------------------------------------------------------------------------
// Multi-window support
// Not implemented on this platform: only the main window exists and it is owned by App::Run.
// App::IsMultiWindowSupported reports false, so callers that drive secondary windows should never reach these.
static constexpr AppWindowHandle APP_MAIN_WINDOW_HANDLE { 1 };

bool App::IsMultiWindowSupported()
{
    return false;
}

AppWindowHandle App::GetMainWindow()
{
    return APP_MAIN_WINDOW_HANDLE;
}

AppWindowHandle App::CreateWindowHandle(const AppWindowDesc&)
{
    ASSERT_MSG(0, "Multiple windows are not supported on this platform");
    return AppWindowHandle {};
}

void App::DestroyWindowHandle(AppWindowHandle) {}
void App::ShowWindow(AppWindowHandle) {}
void App::SetWindowPos(AppWindowHandle, int, int) {}
void App::SetWindowSize(AppWindowHandle, int, int) {}
void App::SetWindowTitle(AppWindowHandle, const char*) {}
void App::SetWindowAlpha(AppWindowHandle, float) {}
void App::FocusWindow(AppWindowHandle) {}
bool App::IsWindowFocused(AppWindowHandle) { return true; }
bool App::IsWindowMinimized(AppWindowHandle) { return false; }

AppRect App::GetWindowGeometry(AppWindowHandle)
{
    return AppRect { 0, 0, int(GetWindowWidth()), int(GetWindowHeight()) };
}

float App::GetWindowDPIScale(AppWindowHandle)
{
    return GetDisplayInfo().dpiScale;
}

void* App::GetNativeWindowHandle(AppWindowHandle)
{
    return GetNativeWindowHandle();
}

AppWindowHandle App::GetWindowFromPoint(int, int)
{
    return APP_MAIN_WINDOW_HANDLE;
}

uint32 App::GetMonitors(AppMonitorInfo* outMonitors, uint32 maxMonitors)
{
    if (outMonitors == nullptr)
        return 1;
    if (maxMonitors == 0)
        return 0;

    AppDisplayInfo disp = GetDisplayInfo();
    AppRect rect { 0, 0, int(disp.width), int(disp.height) };
    outMonitors[0] = AppMonitorInfo {
        .mainRect = rect,
        .workRect = rect,
        .dpiScale = disp.dpiScale,
        .isPrimary = true
    };
    return 1;
}

"""

TARGETS = [
    ('code/Common/ApplicationLinux.cpp', '#endif // PLATFORM_LINUX'),
    ('code/Common/ApplicationAndroid.cpp', '#endif  // PLATFORM_ANDROID'),
    ('code/Common/ApplicationMac.mm', '#endif // PLATFORM_APPLE'),
]

for path, marker in TARGETS:
    s = io.open(path, encoding='utf-8', newline='').read()
    assert s.count(marker) == 1, (path, s.count(marker))
    body = STUBS.replace('\n', '\r\n') if '\r\n' in s else STUBS
    s = s.replace(marker, body + marker)
    io.open(path, 'w', encoding='utf-8', newline='').write(s)
    print('stubbed', path)
