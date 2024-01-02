#include "Engine.h"

#include "Core/StringUtil.h"
#include "Core/System.h"
#include "Core/Settings.h"
#include "Core/Jobs.h"
#include "Core/Atomic.h"
#include "Core/Log.h"
#include "Core/TracyHelper.h"

#include "Graphics/Graphics.h"
#include "Graphics/ImGuiWrapper.h"
#include "Graphics/DebugDraw.h"
#include "Graphics/Model.h"
#include "Graphics/Shader.h"

#include "AssetManager.h"
#include "RemoteServices.h"
#include "Application.h"
#include "JunkyardSettings.h"

#include "Tool/Console.h"
#include "Tool/ImGuiTools.h"

static constexpr float  kReconnectInterval = 5.0f;
static constexpr uint32 kReconnectRetries = 3;
static constexpr float  kRefreshStatsInterval = 0.2f;

//------------------------------------------------------------------------
// Memory Budgets
static constexpr size_t kHeapInitBudget = 2*kGB;

struct EngineShortcutKeys
{
    AppKeycode keys[2];
    AppKeyModifiers mods;
    EngineShortcutCallback callback;
    void* userData;
};

struct EngineState
{
    SysInfo    sysInfo = {};

    bool       remoteReconnect = false;
    float      remoteDisconnectTime = false;
    uint32     remoteRetryCount = false;
    
    double elapsedTime = 0.0;
    atomicUint64 frameIndex = 0;
    uint64 rawFrameStartTime;
    uint64 rawFrameTime;        

    MemBudgetAllocator initHeap;

    bool initialized = false;
    float refreshStatsTime = 0;

    Array<EngineShortcutKeys> shortcuts;

    EngineState() : 
        initHeap("Init")
    {
    }
};

static EngineState gEng;

static void engineRemoteDisconnected(const char* url, bool onPurpose, SocketErrorCode errCode)
{
    if (onPurpose)
        return;

    if (errCode == SocketErrorCode::Timeout || errCode == SocketErrorCode::ConnectionReset || 
        errCode == SocketErrorCode::None /* invalid packet */)
    {
        if (gEng.remoteRetryCount <= kReconnectRetries) {
            logInfo("Disconnected from '%s', reconnecting in %.0f seconds ...", url, kReconnectInterval);
            gEng.remoteReconnect = true;
        }
    }
    else {
        ASSERT(0);  // investigate errCode
    }
}

static void engineOnEvent(const AppEvent& ev, [[maybe_unused]] void* userData)
{
    if (ev.type == AppEventType::KeyDown) {
        // Trigger shortcuts
        for (uint32 i = 0; i < gEng.shortcuts.Count(); i++) {
            const EngineShortcutKeys& shortcut = gEng.shortcuts[i];
            AppKeycode key1 = shortcut.keys[0];
            AppKeycode key2 = shortcut.keys[1];
            AppKeyModifiers mods = shortcut.mods;
            if (appIsKeyDown(key1) && 
                (key2 == AppKeycode::Invalid || appIsKeyDown(key2)) && 
                (mods == AppKeyModifiers::None || (mods & ev.keyMods) == mods)) 
            {
                shortcut.callback(gEng.shortcuts[i].userData);
                break;
            } 
        }
    }
#if PLATFORM_ANDROID
    else if (ev.type == AppEventType::Suspended) 
        gfxDestroySurfaceAndSwapchain();
    else if (ev.type == AppEventType::Resumed)
        gfxRecreateSurfaceAndSwapchain();
#endif
}

bool engineInitialize()
{
    PROFILE_ZONE(true);

    // Set main thread as `realtime` priority
    threadSetCurrentThreadPriority(ThreadPriority::Realtime);
    threadSetCurrentThreadName("Main");

    // Initialize heaps
    // TODO: make all heaps commit all memory upfront in RELEASE builds
    gEng.shortcuts.SetAllocator(memDefaultAlloc());
    gEng.initHeap.Initialize(kHeapInitBudget, kMB, false, settingsGet().engine.debugAllocations);

    if (settingsGet().engine.debugAllocations) {
        memTempSetDebugMode(true);
    }

    {   // Cpu/Memory info
        sysGetSysInfo(&gEng.sysInfo);

        char cpuCaps[128] = {0};
        if (gEng.sysInfo.cpuCapsSSE)
            strConcat(cpuCaps, sizeof(cpuCaps), "SSE ");
        if (gEng.sysInfo.cpuCapsSSE2)
            strConcat(cpuCaps, sizeof(cpuCaps), "SSE2 ");
        if (gEng.sysInfo.cpuCapsSSE3)
            strConcat(cpuCaps, sizeof(cpuCaps), "SSE3 ");
        if (gEng.sysInfo.cpuCapsSSE41)
            strConcat(cpuCaps, sizeof(cpuCaps), "SSE4.1 ");
        if (gEng.sysInfo.cpuCapsSSE42)
            strConcat(cpuCaps, sizeof(cpuCaps), "SSE4.2 ");
        if (gEng.sysInfo.cpuCapsAVX)
            strConcat(cpuCaps, sizeof(cpuCaps), "AVX ");
        if (gEng.sysInfo.cpuCapsAVX2)
            strConcat(cpuCaps, sizeof(cpuCaps), "AVX2 ");
        if (gEng.sysInfo.cpuCapsAVX512)
            strConcat(cpuCaps, sizeof(cpuCaps), "AVX512 ");
        if (gEng.sysInfo.cpuCapsNeon)
            strConcat(cpuCaps, sizeof(cpuCaps), "Neon ");
        
        logInfo("(init) Compiler: %s", COMPILER_NAME);
        logInfo("(init) CPU: %s", gEng.sysInfo.cpuModel);
        logInfo("(init) CPU Cores: %u", gEng.sysInfo.coreCount); 
        logInfo("(init) CPU Caps: %s", cpuCaps);
        logInfo("(init) System memory: %_$$$llu", gEng.sysInfo.physicalMemorySize);
    }

    _private::conInitialize();
    jobsInitialize(JobsInitParams { 
                   .alloc = &gEng.initHeap, 
                   .numShortTaskThreads = settingsGet().engine.jobsNumShortTaskThreads,
                   .numLongTaskThreads = settingsGet().engine.jobsNumLongTaskThreads,
                   .debugAllocations = settingsGet().engine.debugAllocations });

    if (settingsGet().engine.connectToServer) {
        if (!_private::remoteConnect(settingsGet().engine.remoteServicesUrl.CStr(), engineRemoteDisconnected)) {
            return false;
        }

        // We have the connection, open up some tools on the host, based on the platform
        if constexpr (PLATFORM_ANDROID) {
            conExecuteRemote("exec scripts\\close-logcats.cmd com.JunkyardAndroid && scripts\\android-logcat.bat");
            conExecuteRemote("exec-once {ScrCpy}");
        }
    }

    // Asset manager
    if (!_private::assetInitialize()) {
        logError("Initializing AssetManager failed");
        return false;
    }

    // [Tooling] ShaderCompiler
    if (!_private::shaderInitialize()) {
        logError("Initializing ShaderCompiler failed");
        return false;
    }

    // [Tooling] ModelBaker/Loader
    if (!_private::modelInitialize()) {
        logError("Initializing ModelBaker failed");
        return false;
    }

    // Graphics
    const SettingsGraphics& gfxSettings = settingsGet().graphics;
    if (gfxSettings.enable) {
        if (!gfxSettings.headless) {
            AppDisplayInfo dinfo = appGetDisplayInfo();
            logInfo("(init) Window Size: %ux%u", appGetWindowWidth(), appGetWindowHeight());
            logInfo("(init) Framebuffer Size: %ux%u", appGetFramebufferWidth(), appGetFramebufferHeight());
            logInfo("(init) Display (%ux%u), DPI scale: %.1f, RefreshRate: %uhz", dinfo.width, dinfo.height, dinfo.dpiScale, dinfo.refreshRate);
        }

        if (!_private::gfxInitialize()) {
            logError("Initializing Graphics failed");
            return false;
        }

        if (!gfxSettings.headless) {
            if (gfxSettings.enableImGui) {
                if (!_private::imguiInitialize()) {
                    logError("Initializing ImGui failed");
                    return false;
                }
                logRegisterCallback(_private::imguiQuickInfoHud_Log, nullptr);
            }

            if (!_private::ddInitialize()) {
                logError("Initializing DebugDraw failed");
                return false;
            }
        }
    }

    appRegisterEventsCallback(engineOnEvent);

    gEng.initialized = true;

    auto GetVMemStats = [](int, const char**, char* outResponse, uint32 responseSize, void*)->bool {
        MemVirtualStats stats = memVirtualGetStats();
        strPrintFmt(outResponse, responseSize, "Reserverd: %_$$$llu, Commited: %_$$$llu", stats.reservedBytes, stats.commitedBytes);
        return true;
    };

    conRegisterCommand(ConCommandDesc {
        .name = "vmem",
        .help = "Get VMem stats",
        .callback = GetVMemStats
    });
    return true;
}

void engineRelease()
{
    const SettingsGraphics& gfxSettings = settingsGet().graphics;
    logInfo("Releasing engine sub systems ...");
    gEng.initialized = false;

    _private::modelRelease();
    _private::shaderRelease();
    _private::gfxReleaseImageManager();
    if (gfxSettings.enable && !gfxSettings.headless) {
        if (gfxSettings.enableImGui) {
            logUnregisterCallback(_private::imguiQuickInfoHud_Log);
            _private::imguiRelease();
        }
        _private::ddRelease();
    } 


    _private::assetDetectAndReleaseLeaks();

    if (gfxSettings.enable)
        _private::gfxRelease();
    _private::assetRelease();

    if (settingsGet().engine.connectToServer)
        _private::remoteDisconnect();

    jobsRelease();
    _private::conRelease();

    gEng.shortcuts.Free();
    gEng.initHeap.Release();

    logInfo("Engine released");
}

void engineBeginFrame(float dt)
{
    ASSERT(gEng.initialized);

    TracyCPlot("frameTime", dt);

    gEng.elapsedTime += dt;

    const SettingsEngine& engineSettings = settingsGet().engine;

    // Reconnect to remote server if it's disconnected
    if (engineSettings.connectToServer && gEng.remoteReconnect) {
        gEng.remoteDisconnectTime += dt;
        if (gEng.remoteDisconnectTime >= kReconnectInterval) {
            gEng.remoteDisconnectTime = 0;
            gEng.remoteReconnect = false;
            if (++gEng.remoteRetryCount <= kReconnectRetries) {
                if (_private::remoteConnect(engineSettings.remoteServicesUrl.CStr(), engineRemoteDisconnected))
                    gEng.remoteRetryCount = 0;
                else
                    engineRemoteDisconnected(engineSettings.remoteServicesUrl.CStr(), false, SocketErrorCode::None);
            }
            else {
                logWarning("Failed to connect to server '%s' after %u retries", engineSettings.remoteServicesUrl.CStr(), 
                    kReconnectRetries);
            }
        }
    }

    // Reset jobs budget stats every `kRefreshStatsInterval`
    {
        gEng.refreshStatsTime += dt;
        if (gEng.refreshStatsTime > kRefreshStatsInterval) {
            jobsResetBudgetStats();
            gEng.refreshStatsTime = 0;
        }
    }

    // Begin graphics
    if (!settingsGet().graphics.headless) {
        _private::imguiBeginFrame(dt);
        gfxBeginFrame();
    }

    gEng.rawFrameStartTime = timerGetTicks();
}

void engineEndFrame(float dt)
{
    ASSERT(gEng.initialized);

    gEng.rawFrameTime = timerDiff(timerGetTicks(), gEng.rawFrameStartTime);

    if (!settingsGet().graphics.headless) {
        gfxEndFrame();
        assetCollectGarbage();
    }

    _private::assetUpdateCache(dt);

    memTempReset(dt);

    TracyCFrameMark;

    atomicFetchAdd64Explicit(&gEng.frameIndex, 1, AtomicMemoryOrder::Relaxed);
}

uint64 engineFrameIndex()
{
    return atomicLoad64Explicit(&gEng.frameIndex, AtomicMemoryOrder::Relaxed);
}

const SysInfo& engineGetSysInfo()
{
    return gEng.sysInfo;
}

MemBudgetAllocator* engineGetInitHeap()
{
    return &gEng.initHeap;
}

float engineGetCpuFrameTimeMS()
{
    return (float)timerToMS(gEng.rawFrameTime);
}

void engineRegisterShortcut(const char* shortcut, EngineShortcutCallback callback, void* userData)
{
    ASSERT(callback);
    ASSERT(shortcut);
    
    AppKeycode keys[2] = {};
    AppKeyModifiers mods = AppKeyModifiers::None;

    // Parse shortcut keys string
    shortcut = strSkipWhitespace(shortcut);

    uint32 numKeys = 0;
    const char* plus;
    char keystr[32];

    auto ParseShortcutKey = [&numKeys, &keys, &mods](const char* keystr)
    {
        uint32 len = strLen(keystr);

        // function keys
        bool isFn = (len == 2 || len == 3) && strToUpper(keystr[0]) == 'F' && 
            ((len == 2 && strIsNumber(keystr[1])) || (len == 3 && strIsNumber(keystr[1]) && strIsNumber(keystr[2])));
        if (isFn && numKeys < 2) {
            char numStr[3] = {keystr[1], keystr[2], 0};
            int fnum = strToInt(numStr) - 1;
            if (fnum >= 0 && fnum < 25) {
                keys[numKeys++] = static_cast<AppKeycode>(uint32(AppKeycode::F1) + fnum);
            }
        }
        else if (len > 1) {
            if (strIsEqualNoCase(keystr, "ALT"))        mods |= AppKeyModifiers::Alt;
            else if (strIsEqualNoCase(keystr, "CTRL"))  mods |= AppKeyModifiers::Ctrl;
            else if (strIsEqualNoCase(keystr, "SHIFT")) mods |= AppKeyModifiers::Shift;
            else if (strIsEqualNoCase(keystr, "SUPER")) mods |= AppKeyModifiers::Super;
            else if (strIsEqualNoCase(keystr, "ESC"))       keys[numKeys++] = AppKeycode::Escape;
            else if (strIsEqualNoCase(keystr, "INS"))       keys[numKeys++] = AppKeycode::Insert;
            else if (strIsEqualNoCase(keystr, "PGUP"))      keys[numKeys++] = AppKeycode::PageUp;
            else if (strIsEqualNoCase(keystr, "PGDOWN"))    keys[numKeys++] = AppKeycode::PageDown;
            else if (strIsEqualNoCase(keystr, "HOME"))      keys[numKeys++] = AppKeycode::Home;
            else if (strIsEqualNoCase(keystr, "END"))       keys[numKeys++] = AppKeycode::End;
            else if (strIsEqualNoCase(keystr, "TAB"))       keys[numKeys++] = AppKeycode::Tab;
            else    ASSERT_MSG(0, "Shortcut not recognized: %s", keystr);
        } 
        else if (len == 1 && numKeys < 2) {
            char key = strToUpper(keystr[0]);
            if (keystr[0] > uint32(AppKeycode::Space))
                keys[numKeys++] = static_cast<AppKeycode>(key);
        }
    };

    while (*shortcut) {
        plus = strFindChar(shortcut, '+');
        if (!plus)
            break;

        strCopyCount(keystr, sizeof(keystr), shortcut, uint32(plus - shortcut));
        strTrim(keystr, sizeof(keystr), keystr);
        ParseShortcutKey(keystr);
        shortcut = strSkipWhitespace(plus + 1);
    }

    if (shortcut[0]) {
        strCopy(keystr, sizeof(keystr), shortcut);
        ParseShortcutKey(keystr);
    }

    // Register callback
    ASSERT_MSG(keys[0] != AppKeycode::Invalid, "Invalid shortcut string");

    // Look for previously registered combination
    if (keys[0] != AppKeycode::Invalid) {
        [[maybe_unused]] uint32 index = gEng.shortcuts.FindIf([keys, mods](const EngineShortcutKeys& shortcut)->bool {
            return shortcut.mods == mods && 
                   ((keys[0] == shortcut.keys[0] && keys[1] == shortcut.keys[1]) ||
                    (keys[0] == shortcut.keys[1] && keys[1] == shortcut.keys[0]));
        });
        ASSERT_MSG(index == UINT32_MAX, "Shortcut already exists: %s", shortcut);

        gEng.shortcuts.Push(EngineShortcutKeys {
            .keys = { keys[0], keys[1] },
            .mods = mods,
            .callback = callback,
            .userData = userData
        });
    }
}
