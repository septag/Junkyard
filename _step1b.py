import io
import re

CRLF = '\r\n'
path = 'code/Common/ApplicationWin.cpp'
s = io.open(path, encoding='utf-8', newline='').read()


def rep(a, b, n=1):
    global s
    pat = re.compile(r'[ \t]*\r\n'.join(re.escape(x) for x in a.split('\n')))
    b = b.replace('\n', CRLF)
    found = len(pat.findall(s))
    assert found == n, (found, n, a[:110])
    s = pat.sub(lambda m: b, s, count=n)


# --- _SaveInitRects
rep("        if (gApp.windowModified && gApp.hwnd) {", "        if (gApp.windowModified && _MainWindow().hwnd) {")
rep("            if (GetWindowRect(gApp.hwnd, &mainRect))", "            if (GetWindowRect(_MainWindow().hwnd, &mainRect))")

# --- _UpdateDisplayInfo becomes per-window
rep("""    // Returns true if window monitor has changed
    static bool _UpdateDisplayInfo()
    {
        HMONITOR hm = gApp.hwnd ?
            MonitorFromWindow(gApp.hwnd, MONITOR_DEFAULTTONEAREST) :
            MonitorFromPoint({ 1, 1 }, MONITOR_DEFAULTTONEAREST);
        if (hm == gApp.wndMonitor)
            return false;

        gApp.wndMonitor = hm;""",
    """    // Returns true if window monitor has changed
    static bool _UpdateDisplayInfo(AppWindow& wnd)
    {
        HMONITOR hm = wnd.hwnd ?
            MonitorFromWindow(wnd.hwnd, MONITOR_DEFAULTTONEAREST) :
            MonitorFromPoint({ 1, 1 }, MONITOR_DEFAULTTONEAREST);
        if (hm == wnd.monitor)
            return false;

        wnd.monitor = hm;""")

rep("""            gApp.windowScale = static_cast<float>(dpix) / 96.0f;
        }
        else {
            gApp.windowScale = 1.0f;
        }

        if (gApp.desc.highDPI) {
            gApp.contentScale = gApp.windowScale;
            gApp.mouseScale = 1.0f / gApp.windowScale;
        }
        else {
            gApp.contentScale = 1.0f;
            gApp.mouseScale = 1.0f / gApp.windowScale;
        }

        gApp.dpiScale = gApp.contentScale;

        // Display settings
        MONITORINFOEX monitorInfo { sizeof(MONITORINFOEX) };
        GetMonitorInfoA(hm, &monitorInfo);
        DEVMODEA mode { sizeof(DEVMODEA) };
        EnumDisplaySettingsA(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &mode);
        gApp.displayWidth = static_cast<uint16>(mode.dmPelsWidth);
        gApp.displayHeight = static_cast<uint16>(mode.dmPelsHeight);
        gApp.displayRefreshRate = static_cast<uint16>(mode.dmDisplayFrequency);""",
    """            wnd.windowScale = static_cast<float>(dpix) / 96.0f;
        }
        else {
            wnd.windowScale = 1.0f;
        }

        if (gApp.desc.highDPI) {
            wnd.contentScale = wnd.windowScale;
            wnd.mouseScale = 1.0f / wnd.windowScale;
        }
        else {
            wnd.contentScale = 1.0f;
            wnd.mouseScale = 1.0f / wnd.windowScale;
        }

        wnd.dpiScale = wnd.contentScale;

        // Display settings. App::GetDisplayInfo reports the main window's monitor
        if (wnd.isMain) {
            MONITORINFOEX monitorInfo { sizeof(MONITORINFOEX) };
            GetMonitorInfoA(hm, &monitorInfo);
            DEVMODEA mode { sizeof(DEVMODEA) };
            EnumDisplaySettingsA(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &mode);
            gApp.displayWidth = static_cast<uint16>(mode.dmPelsWidth);
            gApp.displayHeight = static_cast<uint16>(mode.dmPelsHeight);
            gApp.displayRefreshRate = static_cast<uint16>(mode.dmDisplayFrequency);
        }""")

rep("        _UpdateDisplayInfo();\n\n        if (user32)", "        _UpdateDisplayInfo(_MainWindow());\n\n        if (user32)")

# --- Clipboard (main window owns the clipboard)
rep("        ASSERT(gApp.hwnd);\n        ASSERT(gApp.desc.clipboardSizeBytes > 0);",
    "        ASSERT(_MainWindow().hwnd);\n        ASSERT(gApp.desc.clipboardSizeBytes > 0);")
rep("        if (!OpenClipboard(gApp.hwnd)) {\n            goto error;",
    "        if (!OpenClipboard(_MainWindow().hwnd)) {\n            goto error;")

# --- Event construction
rep("""    static AppEvent _NewEvent(AppEventType type)
    {
        return AppEvent {
            .type = type,
            .mouseButton = InputMouseButton::Invalid,
            .windowWidth = gApp.windowWidth,
            .windowHeight = gApp.windowHeight,
            .framebufferWidth = gApp.framebufferWidth,
            .framebufferHeight = gApp.framebufferHeight
        };
    }""",
    """    static AppEvent _NewEvent(AppEventType type)
    {
        const AppWindow& wnd = _MainWindow();
        return AppEvent {
            .type = type,
            .mouseButton = InputMouseButton::Invalid,
            .windowWidth = wnd.windowWidth,
            .windowHeight = wnd.windowHeight,
            .framebufferWidth = wnd.framebufferWidth,
            .framebufferHeight = wnd.framebufferHeight
        };
    }""")

rep("        e.mouseX = gApp.mouseX;\n        e.mouseY = gApp.mouseY;",
    "        e.mouseX = _MainWindow().mouseX;\n        e.mouseY = _MainWindow().mouseY;")

# --- WndProc
rep("        if (!gApp.hwnd)\n            return DefWindowProcW(hWnd, uMsg, wParam, lParam);",
    "        AppWindow& wnd = _MainWindow();\n        if (!wnd.hwnd)\n            return DefWindowProcW(hWnd, uMsg, wParam, lParam);")

rep("""                const bool iconified = wParam == SIZE_MINIMIZED;
                if (iconified != gApp.iconified) {
                    gApp.iconified = iconified;""",
    """                const bool iconified = wParam == SIZE_MINIMIZED;
                if (iconified != wnd.iconified) {
                    wnd.iconified = iconified;""")

rep("                if (_UpdateDisplayInfo())", "                if (_UpdateDisplayInfo(wnd))")

rep("""                gApp.mouseX = (fl32)GET_X_LPARAM(lParam) * gApp.mouseScale;
                gApp.mouseY = (fl32)GET_Y_LPARAM(lParam) * gApp.mouseScale;
                if (!gApp.mouseTracked) {
                    gApp.mouseTracked = true;""",
    """                wnd.mouseX = (fl32)GET_X_LPARAM(lParam) * wnd.mouseScale;
                wnd.mouseY = (fl32)GET_Y_LPARAM(lParam) * wnd.mouseScale;
                if (!wnd.mouseTracked) {
                    wnd.mouseTracked = true;""")

rep("                    tme.hwndTrack = gApp.hwnd;", "                    tme.hwndTrack = wnd.hwnd;")
rep("                gApp.mouseTracked = false;", "                wnd.mouseTracked = false;")
rep("                _UpdateDisplayInfo();\n                _CallEvent(_NewEvent(AppEventType::DisplayUpdated));",
    "                _UpdateDisplayInfo(wnd);\n                _CallEvent(_NewEvent(AppEventType::DisplayUpdated));")

# --- _UpdateWindowDimensions
rep("""    static bool _UpdateWindowDimensions(HWND hwnd)
    {
        RECT rect;
        if (GetClientRect(hwnd, &rect)) {
            gApp.windowWidth = uint16(float(rect.right - rect.left) / gApp.windowScale);
            gApp.windowHeight = uint16(float(rect.bottom - rect.top) / gApp.windowScale);
            uint16 fbWidth = uint16(float(gApp.windowWidth) * gApp.contentScale);
            uint16 fbHeight = uint16(float(gApp.windowHeight) * gApp.contentScale);

            // Fix framebuffer dimensions by getting the values straight from the graphics backend surface
            if (gApp.queryFramebufferFunc)
                gApp.queryFramebufferFunc(&fbWidth, &fbHeight);

            if ((fbWidth != gApp.framebufferWidth) || (fbHeight != gApp.framebufferHeight)) {
                gApp.framebufferWidth = Max<uint16>(fbWidth, 1u);
                gApp.framebufferHeight = Max<uint16>(fbHeight, 1u);
                return true;
            }
        }
        else {
            gApp.windowWidth = gApp.windowHeight = 1;
            gApp.framebufferWidth = gApp.framebufferHeight = 1;
        }
        return false;
    }""",
    """    static bool _UpdateWindowDimensions(AppWindow& wnd)
    {
        RECT rect;
        if (GetClientRect(wnd.hwnd, &rect)) {
            wnd.windowWidth = uint16(float(rect.right - rect.left) / wnd.windowScale);
            wnd.windowHeight = uint16(float(rect.bottom - rect.top) / wnd.windowScale);
            uint16 fbWidth = uint16(float(wnd.windowWidth) * wnd.contentScale);
            uint16 fbHeight = uint16(float(wnd.windowHeight) * wnd.contentScale);

            // Fix framebuffer dimensions by getting the values straight from the graphics backend surface.
            // The backend only owns a surface for the main window
            if (wnd.isMain && gApp.queryFramebufferFunc)
                gApp.queryFramebufferFunc(&fbWidth, &fbHeight);

            if ((fbWidth != wnd.framebufferWidth) || (fbHeight != wnd.framebufferHeight)) {
                wnd.framebufferWidth = Max<uint16>(fbWidth, 1u);
                wnd.framebufferHeight = Max<uint16>(fbHeight, 1u);
                return true;
            }
        }
        else {
            wnd.windowWidth = wnd.windowHeight = 1;
            wnd.framebufferWidth = wnd.framebufferHeight = 1;
        }
        return false;
    }""")

# --- _CreateMainWindow
rep("            rect = {0, 0, uint16(float(gApp.windowWidth)*gApp.windowScale) , uint16(float(gApp.windowHeight)*gApp.windowScale) };",
    "            rect = {0, 0, uint16(float(mainWnd.windowWidth)*mainWnd.windowScale) , uint16(float(mainWnd.windowHeight)*mainWnd.windowScale) };")

rep("""    static bool _CreateMainWindow()
    {
        const SettingsApp& settings = SettingsJunkyard::Get().app;""",
    """    static bool _CreateMainWindow()
    {
        AppWindow& mainWnd = _MainWindow();
        const SettingsApp& settings = SettingsJunkyard::Get().app;""")

rep("        Str::Utf8ToWide(gApp.windowTitle, winTitleWide, sizeof(winTitleWide));",
    "        Str::Utf8ToWide(mainWnd.title, winTitleWide, sizeof(winTitleWide));")

rep("""        ShowWindow(hwnd, settings.launchMinimized ? SW_MINIMIZE : SW_SHOW);
        _UpdateWindowDimensions(hwnd);
        gApp.hwnd = hwnd;""",
    """        mainWnd.hwnd = hwnd;
        ::ShowWindow(hwnd, settings.launchMinimized ? SW_MINIMIZE : SW_SHOW);
        _UpdateWindowDimensions(mainWnd);""")

# --- Run(): register the main window before anything touches it
rep("""        gApp.desc = desc;

        gApp.windowWidth = gApp.desc.initWidth;
        gApp.windowHeight = gApp.desc.initHeight;
        gApp.framebufferWidth = gApp.desc.initWidth;
        gApp.framebufferHeight = gApp.desc.initHeight;
        gApp.dpiScale = 1.0f;
        gApp.clipboardEnabled = desc.enableClipboard;""",
    """        gApp.desc = desc;

        gApp.mainWindow = _RegisterWindow(true);
        AppWindow& mainWnd = _MainWindow();
        mainWnd.windowWidth = gApp.desc.initWidth;
        mainWnd.windowHeight = gApp.desc.initHeight;
        mainWnd.framebufferWidth = gApp.desc.initWidth;
        mainWnd.framebufferHeight = gApp.desc.initHeight;
        gApp.clipboardEnabled = desc.enableClipboard;""")

rep("            Str::Copy(gApp.windowTitle, sizeof(gApp.windowTitle), desc.windowTitle);",
    "            Str::Copy(mainWnd.title, sizeof(mainWnd.title), desc.windowTitle);")
rep("            Str::Copy(gApp.windowTitle, sizeof(gApp.windowTitle), settings.app.appName);",
    "            Str::Copy(mainWnd.title, sizeof(mainWnd.title), settings.app.appName);")

rep("        _UpdateDisplayInfo();\n", "        _UpdateDisplayInfo(_MainWindow());\n")

# --- Main loop
rep("                if (gApp.iconified) {\n                    GetMessageW(&msg, nullptr, 0, 0);",
    "                if (_MainWindow().iconified) {\n                    GetMessageW(&msg, nullptr, 0, 0);")
rep("                if (_UpdateWindowDimensions(gApp.hwnd)) {", "                if (_UpdateWindowDimensions(_MainWindow())) {")
rep("            if (!gApp.iconified || (gApp.iconified && gApp.desc.updateWhenMinimized)) {",
    "            if (!_MainWindow().iconified || (_MainWindow().iconified && gApp.desc.updateWhenMinimized)) {")

# --- Shutdown
rep("""        if (settings.graphics.IsGraphicsEnabled()) {
            DestroyWindow(gApp.hwnd);""",
    """        if (settings.graphics.IsGraphicsEnabled()) {
            DestroyWindow(_MainWindow().hwnd);""")
rep("        gApp.hwnd = nullptr;\n", "        gApp.windows.Free();\n        gApp.mainWindow = AppWindowHandle {};\n")

# --- Accessors
rep("""    void* GetNativeWindowHandle()
    {
        return gApp.hwnd;
    }""",
    """    AppWindowHandle GetMainWindow()
    {
        return gApp.mainWindow;
    }

    void* GetNativeWindowHandle()
    {
        return _MainWindow().hwnd;
    }""")

rep("        return gApp.windowWidth;", "        return _MainWindow().windowWidth;")
rep("        return gApp.windowHeight;", "        return _MainWindow().windowHeight;")
rep("        return gApp.framebufferWidth;", "        return _MainWindow().framebufferWidth;")
rep("        return gApp.framebufferHeight;", "        return _MainWindow().framebufferHeight;")
rep("        return gApp.dpiScale;", "        return _MainWindow().dpiScale;")
rep("            .dpiScale = gApp.dpiScale", "            .dpiScale = _MainWindow().dpiScale")

# --- Clipboard getter + mouse capture
rep("        ASSERT(gApp.hwnd);\n", "        ASSERT(_MainWindow().hwnd);\n")
rep("        if (!OpenClipboard(gApp.hwnd)) {", "        if (!OpenClipboard(_MainWindow().hwnd)) {")
rep("        SetCapture(gApp.hwnd);", "        SetCapture(_MainWindow().hwnd);")

io.open(path, 'w', encoding='utf-8', newline='').write(s)
print('step1 part B ok')
