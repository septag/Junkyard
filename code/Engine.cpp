#include "Engine.h"

#include "Core/StringUtil.h"
#include "Core/System.h"
#include "Core/Settings.h"
#include "Core/Jobs.h"
#include "Core/Atomic.h"
#include "Core/Log.h"
#include "Core/TracyHelper.h"

#include "Common/RemoteServices.h"
#include "Common/Application.h"
#include "Common/JunkyardSettings.h"

#include "Graphics/Graphics.h"

#include "DebugTools/DebugDraw.h"
#include "DebugTools/DebugHud.h"

#include "Assets/AssetManager.h"

#include "ImGui/ImGuiMain.h"

#include "Tool/Console.h"

static constexpr float  ENGINE_REMOTE_RECONNECT_INTERVAL = 5.0f;
static constexpr uint32 ENGINE_REMOTE_CONNECT_RETRIES = 3;
static constexpr float  ENGINE_JOBS_REFRESH_STATS_INTERVAL = 0.2f;
static constexpr size_t ENGINE_INIT_HEAP_MAX = 2*SIZE_GB;

struct EngineShortcutKeys
{
    InputKeycode keys[2];
    InputKeyModifiers mods;
    EngineShortcutCallback callback;
    void* userData;
};

struct EngineContext
{
    SysInfo    sysInfo = {};

    bool       remoteReconnect;
    float      remoteDisconnectTime;
    uint32     remoteRetryCount;
    
    double elapsedTime;
    AtomicUint64 frameIndex;
    uint64 rawFrameStartTime;
    uint64 rawFrameTime;        

    MemBumpAllocatorVM initHeap;

    bool initialized;
    float refreshStatsTime;
    uint32 mainThreadId;

    Array<EngineShortcutKeys> shortcuts;
};

static EngineContext gEng;

namespace Engine
{

static void _RemoteDisconnected(const char* url, bool onPurpose, SocketErrorCode::Enum errCode)
{
    if (onPurpose)
        return;

    if (errCode == SocketErrorCode::Timeout || errCode == SocketErrorCode::ConnectionReset || 
        errCode == SocketErrorCode::None /* invalid packet */)
    {
        if (gEng.remoteRetryCount <= ENGINE_REMOTE_CONNECT_RETRIES) {
            LOG_INFO("Disconnected from '%s', reconnecting in %.0f seconds ...", url, ENGINE_REMOTE_RECONNECT_INTERVAL);
            gEng.remoteReconnect = true;
        }
    }
    else {
        ASSERT(0);  // investigate errCode
    }
}

static void _OnEvent(const AppEvent& ev, [[maybe_unused]] void* userData)
{
    if (ev.type == AppEventType::KeyDown) {
        // Trigger shortcuts
        for (uint32 i = 0; i < gEng.shortcuts.Count(); i++) {
            const EngineShortcutKeys& shortcut = gEng.shortcuts[i];
            InputKeycode key1 = shortcut.keys[0];
            InputKeycode key2 = shortcut.keys[1];
            InputKeyModifiers mods = shortcut.mods;
            if (App::IsKeyDown(key1) && 
                (key2 == InputKeycode::Invalid || App::IsKeyDown(key2)) && 
                (mods == InputKeyModifiers::None || (mods & ev.keyMods) == mods)) 
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

bool IsMainThread()
{
    return Thread::GetCurrentId() == gEng.mainThreadId;
}

bool Initialize()
{
    PROFILE_ZONE();

    // Set main thread as `realtime` priority
    Thread::SetCurrentThreadPriority(ThreadPriority::Realtime);
    Thread::SetCurrentThreadName("Main");
    gEng.mainThreadId = Thread::GetCurrentId();

    // Initialize heaps
    // TODO: make all heaps commit all memory upfront in RELEASE builds
    gEng.shortcuts.SetAllocator(Mem::GetDefaultAlloc());
    gEng.initHeap.Initialize(ENGINE_INIT_HEAP_MAX, SIZE_MB, SettingsJunkyard::Get().engine.debugAllocations);

    if (SettingsJunkyard::Get().engine.debugAllocations)
        MemTempAllocator::EnableDebugMode(true);

    {   // Cpu/Memory info
        OS::GetSysInfo(&gEng.sysInfo);

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
        
        LOG_INFO("(init) Compiler: %s", COMPILER_NAME);
        LOG_INFO("(init) CPU: %s", gEng.sysInfo.cpuModel);
        LOG_INFO("(init) CPU Cores: %u", gEng.sysInfo.coreCount); 
        LOG_INFO("(init) CPU Caps: %s", cpuCaps);
        LOG_INFO("(init) System memory: %_$$$llu", gEng.sysInfo.physicalMemorySize);
    }

    Console::Initialize();
    Jobs::Initialize(JobsInitParams { 
                   .alloc = &gEng.initHeap, 
                   .numShortTaskThreads = SettingsJunkyard::Get().engine.jobsNumShortTaskThreads,
                   .numLongTaskThreads = SettingsJunkyard::Get().engine.jobsNumLongTaskThreads,
                   .debugAllocations = SettingsJunkyard::Get().engine.debugAllocations });
    Async::Initialize();

    if (SettingsJunkyard::Get().engine.connectToServer) {
        if (!Remote::Connect(SettingsJunkyard::Get().engine.remoteServicesUrl.CStr(), _RemoteDisconnected)) {
            return false;
        }

        // We have the connection, open up some tools on the host, based on the platform
        // TODO: com.junkyard.example is hardcoded, should be named after the actual package name
        if constexpr (PLATFORM_ANDROID) {
            Console::ExecuteRemote("exec scripts\\Android\\android-close-logcats.bat com.junkyard.example && scripts\\Android\\android-logcat.bat");
            Console::ExecuteRemote("exec-once {ScrCpy}");
        }
    }

    // Graphics
    const SettingsGraphics& gfxSettings = SettingsJunkyard::Get().graphics;
    if (gfxSettings.enable) {
        if (!gfxSettings.headless) {
            AppDisplayInfo dinfo = App::GetDisplayInfo();
            LOG_INFO("(init) Logical Window Size: %ux%u", App::GetWindowWidth(), App::GetWindowHeight());
            LOG_INFO("(init) Framebuffer Size: %ux%u", App::GetFramebufferWidth(), App::GetFramebufferHeight());
            LOG_INFO("(init) Display (%ux%u), DPI scale: %.2f, RefreshRate: %uhz", dinfo.width, dinfo.height, dinfo.dpiScale, dinfo.refreshRate);
        }

        if (!_private::gfxInitialize()) {
            LOG_ERROR("Initializing Graphics failed");
            return false;
        }
    }

    // Asset manager
    if (!_private::assetInitialize() || !Asset::Initialize()) {
        LOG_ERROR("Initializing AssetManager failed");
        return false;
    }

    if (gfxSettings.enable) {
        if (!gfxSettings.headless) {
            if (gfxSettings.enableImGui) {
                if (!ImGui::Initialize()) {
                    LOG_ERROR("Initializing ImGui failed");
                    return false;
                }
                
                DebugHud::Initialize();
            }

            if (!DebugDraw::Initialize()) {
                LOG_ERROR("Initializing DebugDraw failed");
                return false;
            }
        }
    }

    App::RegisterEventsCallback(_OnEvent);

    gEng.initialized = true;
    LOG_INFO("(init) Engine initialized (%.1f ms)", Timer::ToMS(Timer::GetTicks()));
    
    auto GetVMemStats = [](int, const char**, char* outResponse, uint32 responseSize, void*)->bool {
        MemVirtualStats stats = Mem::VirtualGetStats();
        strPrintFmt(outResponse, responseSize, "Reserverd: %_$$$llu, Commited: %_$$$llu", stats.reservedBytes, stats.commitedBytes);
        return true;
    };

    Console::RegisterCommand(ConCommandDesc {
        .name = "vmem",
        .help = "Get VMem stats",
        .callback = GetVMemStats
    });
    return true;
}

void Release()
{
    const SettingsGraphics& gfxSettings = SettingsJunkyard::Get().graphics;
    LOG_INFO("Releasing engine sub systems ...");
    gEng.initialized = false;

    if (gfxSettings.enable && !gfxSettings.headless) {
        if (gfxSettings.enableImGui) {
            DebugHud::Release();
            ImGui::Release();
        }
        DebugDraw::Release();
    } 

    _private::assetRelease();
    Asset::Release();

    if (gfxSettings.enable)
        _private::gfxRelease();

    if (SettingsJunkyard::Get().engine.connectToServer)
        Remote::Disconnect();

    Async::Release();
    Jobs::Release();
    Console::Release();

    gEng.shortcuts.Free();
    gEng.initHeap.Release();

    LOG_INFO("Engine released");
}

void BeginFrame(float dt)
{
    ASSERT(gEng.initialized);

    TracyCPlot("FrameTime", dt*1000.0f);

    gEng.elapsedTime += dt;

    const SettingsEngine& engineSettings = SettingsJunkyard::Get().engine;

    // Reconnect to remote server if it's disconnected
    if (engineSettings.connectToServer && gEng.remoteReconnect) {
        gEng.remoteDisconnectTime += dt;
        if (gEng.remoteDisconnectTime >= ENGINE_REMOTE_RECONNECT_INTERVAL) {
            gEng.remoteDisconnectTime = 0;
            gEng.remoteReconnect = false;
            if (++gEng.remoteRetryCount <= ENGINE_REMOTE_CONNECT_RETRIES) {
                if (Remote::Connect(engineSettings.remoteServicesUrl.CStr(), _RemoteDisconnected))
                    gEng.remoteRetryCount = 0;
                else
                    _RemoteDisconnected(engineSettings.remoteServicesUrl.CStr(), false, SocketErrorCode::None);
            }
            else {
                LOG_WARNING("Failed to connect to server '%s' after %u retries", engineSettings.remoteServicesUrl.CStr(), 
                    ENGINE_REMOTE_CONNECT_RETRIES);
            }
        }
    }

    // Reset jobs budget stats every `kRefreshStatsInterval`
    {
        gEng.refreshStatsTime += dt;
        if (gEng.refreshStatsTime > ENGINE_JOBS_REFRESH_STATS_INTERVAL) {
            Jobs::ResetBudgetStats();
            gEng.refreshStatsTime = 0;
        }
    }

    // Begin graphics
    if (!SettingsJunkyard::Get().graphics.headless) {
        ImGui::BeginFrame(dt);
        _private::gfxBeginFrame();
    }

    gEng.rawFrameStartTime = Timer::GetTicks();
}

void EndFrame(float dt)
{
    ASSERT(gEng.initialized);

    Asset::Update();

    gEng.rawFrameTime = Timer::Diff(Timer::GetTicks(), gEng.rawFrameStartTime);

    if (!SettingsJunkyard::Get().graphics.headless) {
        _private::gfxEndFrame();
        _private::assetCollectGarbage();
    }

    _private::assetUpdateCache(dt);

    MemTempAllocator::Reset();

    TracyCFrameMark;

    Atomic::FetchAddExplicit(&gEng.frameIndex, 1, AtomicMemoryOrder::Relaxed);
}

uint64 GetFrameIndex()
{
    return Atomic::LoadExplicit(&gEng.frameIndex, AtomicMemoryOrder::Relaxed);
}

const SysInfo& GetSysInfo()
{
    return gEng.sysInfo;
}

MemBumpAllocatorBase* GetInitHeap()
{
    return &gEng.initHeap;
}

float GetEngineTimeMS()
{
    return (float)Timer::ToMS(gEng.rawFrameTime);
}

void RegisterShortcut(const char* shortcut, EngineShortcutCallback callback, void* userData)
{
    ASSERT(callback);
    ASSERT(shortcut);
    
    InputKeycode keys[2] = {};
    InputKeyModifiers mods = InputKeyModifiers::None;

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
                keys[numKeys++] = static_cast<InputKeycode>(uint32(InputKeycode::F1) + fnum);
            }
        }
        else if (len > 1) {
            if (strIsEqualNoCase(keystr, "ALT"))        mods |= InputKeyModifiers::Alt;
            else if (strIsEqualNoCase(keystr, "CTRL"))  mods |= InputKeyModifiers::Ctrl;
            else if (strIsEqualNoCase(keystr, "SHIFT")) mods |= InputKeyModifiers::Shift;
            else if (strIsEqualNoCase(keystr, "SUPER")) mods |= InputKeyModifiers::Super;
            else if (strIsEqualNoCase(keystr, "ESC"))       keys[numKeys++] = InputKeycode::Escape;
            else if (strIsEqualNoCase(keystr, "INS"))       keys[numKeys++] = InputKeycode::Insert;
            else if (strIsEqualNoCase(keystr, "PGUP"))      keys[numKeys++] = InputKeycode::PageUp;
            else if (strIsEqualNoCase(keystr, "PGDOWN"))    keys[numKeys++] = InputKeycode::PageDown;
            else if (strIsEqualNoCase(keystr, "HOME"))      keys[numKeys++] = InputKeycode::Home;
            else if (strIsEqualNoCase(keystr, "END"))       keys[numKeys++] = InputKeycode::End;
            else if (strIsEqualNoCase(keystr, "TAB"))       keys[numKeys++] = InputKeycode::Tab;
            else    ASSERT_MSG(0, "Shortcut not recognized: %s", keystr);
        } 
        else if (len == 1 && numKeys < 2) {
            char key = strToUpper(keystr[0]);
            if (keystr[0] > uint32(InputKeycode::Space))
                keys[numKeys++] = static_cast<InputKeycode>(key);
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
    ASSERT_MSG(keys[0] != InputKeycode::Invalid, "Invalid shortcut string");

    // Look for previously registered combination
    if (keys[0] != InputKeycode::Invalid) {
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

} // Engine
