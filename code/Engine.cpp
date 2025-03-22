#include "Engine.h"

#include "Core/StringUtil.h"
#include "Core/System.h"
#include "Core/Settings.h"
#include "Core/Jobs.h"
#include "Core/Atomic.h"
#include "Core/Log.h"
#include "Core/TracyHelper.h"
#include "Core/BlitSort.h"

#include "Common/RemoteServices.h"
#include "Common/Application.h"
#include "Common/JunkyardSettings.h"

#include "Graphics/GfxBackend.h"

#include "DebugTools/DebugDraw.h"
#include "DebugTools/DebugHud.h"

#include "Assets/AssetManager.h"

#include "ImGui/ImGuiMain.h"

#include "Tool/Console.h"
#include <GL/gl.h>

static constexpr float  ENGINE_REMOTE_RECONNECT_INTERVAL = 5.0f;
static constexpr uint32 ENGINE_REMOTE_CONNECT_RETRIES = 3;
static constexpr size_t ENGINE_MAX_MEMORY_SIZE = 2*SIZE_GB;

using EngineInitializeResourcesPair = Pair<EngineInitializeResourcesCallback, void*>;

struct EngineShortcutKeys
{
    InputKeycode keys[2];
    InputKeyModifiers mods;
    EngineShortcutCallback callback;
    void* userData;
};

struct EngineProxyAllocItem
{
    uint32 id;
    const char* name;
    uint64 size;
    int64 sizeDiff;
    uint32 count;
};

struct EngineDebugMemStats
{
    bool refreshProxyAllocList;
    bool autoRefreshProxyAllocList = true;
    float autoRefreshProxyAllocListElapsed;
    float autoRefreshProxyAllocListInterval = 1.0f;
    ImGuiID proxyAllocSortId;
    ImGuiSortDirection proxyAllocSortDir = ImGuiSortDirection_Ascending;
    EngineProxyAllocItem* items;
    uint32 numItems;
};

struct EngineContext
{
    MemProxyAllocator alloc;
    MemProxyAllocator jobsAlloc;
    MemBumpAllocatorVM mainAlloc;   // Virtual memory bump allocator that is used for initializing all sub-systems

    SysInfo sysInfo = {};

    bool remoteReconnect;
    float remoteDisconnectTime;
    uint32 remoteRetryCount;
    
    float frameTime;
    double elapsedTime;
    AtomicUint64 frameIndex;
    uint64 rawFrameStartTime;
    uint64 rawFrameTime;

    bool initialized;
    bool resourcesInitialized;
    bool beginFrameCalled;
    bool endFrameCalled;
    uint32 mainThreadId;
    AssetGroup initResourcesGroup;

    Array<EngineShortcutKeys> shortcuts;
    Array<EngineInitializeResourcesPair> initResourcesCallbacks;
    Array<MemProxyAllocator*> proxyAllocs;

    EngineDebugMemStats debugMemStats;
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
    }

    static void _InitResourcesUpdate(float dt, void*)
    {
        // TODO: can show some cool anim or something
        BeginFrame(dt); 
        {
            GfxCommandBuffer cmd = GfxBackend::BeginCommandBuffer(GfxQueueType::Graphics);
            cmd.ClearSwapchainColor(FLOAT4_ZERO);
            GfxBackend::EndCommandBuffer(cmd);
            GfxBackend::SubmitQueue(GfxQueueType::Graphics);
        }
        EndFrame();

        if (gEng.initResourcesGroup.IsLoadFinished()) {
            for (EngineInitializeResourcesPair p : gEng.initResourcesCallbacks)
                p.first(p.second);
            gEng.resourcesInitialized = true;
            App::OverrideUpdateCallback(nullptr);   // Switch back to the regular app update loop
        }
    }

    static void _DrawMemStatsCallback(void*)
    {
        enum ProxyAllocColumnId
        {
            ProxyAllocColId_Row = 0,
            ProxyAllocColId_Name,
            ProxyAllocColId_AllocSize,
            ProxyAllocColId_AllocCount,
            ProxyAllocColId_Count
        };

        EngineDebugMemStats& mstats = gEng.debugMemStats;
    
        {
            MemVirtualStats stats = Mem::VirtualGetStats();
            float progress = float(double(stats.commitedBytes)/double(stats.reservedBytes));
            ImGui::TextUnformatted("VMem: ");
            ImGui::SameLine();
            ImGui::ProgressBar(progress, ImVec2(-1.0f, 0), String32::Format("%_$llu/%_$llu", stats.commitedBytes, stats.reservedBytes).CStr());
        }

        ImGui::SeparatorVertical();

        if (ImGui::Button("Refresh"))
            mstats.refreshProxyAllocList = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Auto refresh", &mstats.autoRefreshProxyAllocList))
            mstats.autoRefreshProxyAllocListElapsed = 0;

        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::InputFloat("Interval (secs)", &mstats.autoRefreshProxyAllocListInterval, 0.1f, 1.0f, "%.1f")) {
            mstats.autoRefreshProxyAllocListInterval = Max(mstats.autoRefreshProxyAllocListInterval, 0.1f);
            mstats.autoRefreshProxyAllocListElapsed = 0;
        }

        if (mstats.autoRefreshProxyAllocList) {
            mstats.autoRefreshProxyAllocListElapsed += Engine::GetFrameTime();
            if (mstats.autoRefreshProxyAllocListElapsed >= mstats.autoRefreshProxyAllocListInterval) {
                mstats.autoRefreshProxyAllocListElapsed = 0;
                mstats.refreshProxyAllocList = true;
            }
        }

        if (mstats.refreshProxyAllocList) {
            mstats.refreshProxyAllocList = false;

            if (mstats.numItems != gEng.proxyAllocs.Count()) {
                mstats.numItems = gEng.proxyAllocs.Count();
                mstats.items = Mem::ReallocTyped<EngineProxyAllocItem>(mstats.items, mstats.numItems);
            }
            
            for (uint32 i = 0; i < mstats.numItems; i++) {
                MemProxyAllocator* alloc = gEng.proxyAllocs[i];
                EngineProxyAllocItem& item = mstats.items[i];

                uint32 id = i + 1;
                if (item.id == id) 
                    item.sizeDiff = int(alloc->mTotalSizeAllocated) - int(item.size);

                item.id = id;
                item.name = alloc->mName;
                item.size = alloc->mTotalSizeAllocated;
                item.count = alloc->mNumAllocs;
            }

            BlitSort<EngineProxyAllocItem>(mstats.items, mstats.numItems, [sortId=mstats.proxyAllocSortId, sortDir=mstats.proxyAllocSortDir]
                (const EngineProxyAllocItem& a, const EngineProxyAllocItem& b)->int {
                // TODO: can go with a more optimized approach if the list is too long
                switch (sortId) {
                case ProxyAllocColId_Row:
                    if (sortDir == ImGuiSortDirection_Ascending)    return int(a.id) - int(b.id);
                    else                                            return int(b.id) - int(a.id);
                case ProxyAllocColId_Name:
                    if (sortDir == ImGuiSortDirection_Ascending)    return Str::Compare(a.name, b.name);
                    else                                            return Str::Compare(b.name, a.name);
                case ProxyAllocColId_AllocSize:
                    if (sortDir == ImGuiSortDirection_Ascending)    return int(int64(a.size) - int64(b.size));
                    else                                            return int(int64(b.size) - int64(a.size));
                case ProxyAllocColId_AllocCount:
                    if (sortDir == ImGuiSortDirection_Ascending)    return int(int64(a.count) - int64(b.count));
                    else                                            return int(int64(b.count) - int64(a.count));
                default: 
                    ASSERT(0);
                    return 0;
                }
            });
        }

        const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | 
            ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | 
            ImGuiTableFlags_BordersV  | ImGuiTableFlags_ScrollY;

        ImVec2 outerSize = ImGui::GetContentRegionAvail();
        if (ImGui::BeginTable("ProxyAllocatorList", ProxyAllocColId_Count, flags, outerSize)) {
            ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 0, ProxyAllocColId_Row);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0, ProxyAllocColId_Name);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 0, ProxyAllocColId_AllocSize);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 0, ProxyAllocColId_AllocCount);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
                if (sortSpecs->SpecsDirty) {
                    mstats.refreshProxyAllocList = true;
                    mstats.proxyAllocSortId = sortSpecs->Specs->ColumnUserID;
                    mstats.proxyAllocSortDir = sortSpecs->Specs->SortDirection;
                    sortSpecs->SpecsDirty = false;
                }
            }

            // TODO: use clipper if there are too many items
            static constexpr uint32 ImGuiSelectableFlags_SelectOnNav = (1 << 21);    // imgui_internal.h
            String<256> str;
            ImVec4 BaseTextColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);

            for (uint32 i = 0; i < mstats.numItems; i++) {
                const EngineProxyAllocItem& item = mstats.items[i];

                ImGui::PushID(i);
                ImGui::TableNextRow();

                if (item.sizeDiff > 0) 
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0.9f, 0, 1.0f));
                else if (item.sizeDiff < 0)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0, 0, 1.0f));
                else 
                    ImGui::PushStyleColor(ImGuiCol_Text, BaseTextColor);

                ImGui::TableNextColumn();
                str.FormatSelf("%u", item.id);
                ImGui::Selectable(str.CStr(), false, ImGuiSelectableFlags_SpanAllColumns|ImGuiSelectableFlags_SelectOnNav);

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(item.name);

                ImGui::TableNextColumn();
                str.FormatSelf("%_$$$llu", item.size);
                ImGui::TextUnformatted(str.CStr());

                ImGui::TableNextColumn();
                ImGui::Text("%u", item.count);

                ImGui::PopStyleColor();

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }
} // Engine

bool Engine::IsMainThread()
{
    return Thread::GetCurrentId() == gEng.mainThreadId;
}

bool Engine::Initialize()
{
    PROFILE_ZONE();

    Thread::SetCurrentThreadName("Main");
    gEng.mainThreadId = Thread::GetCurrentId();

    // Setup allocators
    // TODO: make main allocator commit all memory upfront in RELEASE builds (?)
    gEng.mainAlloc.Initialize(ENGINE_MAX_MEMORY_SIZE, SIZE_MB, SettingsJunkyard::Get().engine.debugAllocations);

    Engine::HelperInitializeProxyAllocator(&gEng.alloc, "Engine");
    Engine::HelperInitializeProxyAllocator(&gEng.jobsAlloc, "Jobs");
    Engine::RegisterProxyAllocator(&gEng.alloc);
    Engine::RegisterProxyAllocator(&gEng.jobsAlloc);

    // Note: We don't set any allocators for ProxyAllocs array because it will likely get populated before engine initialization
    gEng.shortcuts.SetAllocator(&gEng.alloc);
    gEng.initResourcesCallbacks.SetAllocator(&gEng.alloc);

    if (SettingsJunkyard::Get().engine.debugAllocations)
        MemTempAllocator::EnableDebugMode(true);

    {   // Cpu/Memory info
        OS::GetSysInfo(&gEng.sysInfo);

        char cpuCaps[128] = {0};
        if (gEng.sysInfo.cpuCapsSSE)
            Str::Concat(cpuCaps, sizeof(cpuCaps), "SSE ");
        if (gEng.sysInfo.cpuCapsSSE2)
            Str::Concat(cpuCaps, sizeof(cpuCaps), "SSE2 ");
        if (gEng.sysInfo.cpuCapsSSE3)
            Str::Concat(cpuCaps, sizeof(cpuCaps), "SSE3 ");
        if (gEng.sysInfo.cpuCapsSSE41)
            Str::Concat(cpuCaps, sizeof(cpuCaps), "SSE4.1 ");
        if (gEng.sysInfo.cpuCapsSSE42)
            Str::Concat(cpuCaps, sizeof(cpuCaps), "SSE4.2 ");
        if (gEng.sysInfo.cpuCapsAVX)
            Str::Concat(cpuCaps, sizeof(cpuCaps), "AVX ");
        if (gEng.sysInfo.cpuCapsAVX2)
            Str::Concat(cpuCaps, sizeof(cpuCaps), "AVX2 ");
        if (gEng.sysInfo.cpuCapsAVX512)
            Str::Concat(cpuCaps, sizeof(cpuCaps), "AVX512 ");
        if (gEng.sysInfo.cpuCapsNeon)
            Str::Concat(cpuCaps, sizeof(cpuCaps), "Neon ");
        
        LOG_INFO("(init) Compiler: %s", COMPILER_NAME);
        LOG_INFO("(init) CPU: %s", gEng.sysInfo.cpuModel);
        LOG_INFO("(init) CPU Cores: %u", gEng.sysInfo.coreCount); 
        LOG_INFO("(init) CPU Caps: %s", cpuCaps);
        LOG_INFO("(init) CPU L1 Cache: %u x %_$$$u (%u-way)", gEng.sysInfo.L1Cache.count, gEng.sysInfo.L1Cache.size, gEng.sysInfo.L1Cache.kway);
        LOG_INFO("(init) CPU L2 Cache: %u x %_$$$u (%u-way)", gEng.sysInfo.L2Cache.count, gEng.sysInfo.L2Cache.size, gEng.sysInfo.L2Cache.kway);
        LOG_INFO("(init) CPU L3 Cache: %u x %_$$$u (%u-way)", gEng.sysInfo.L3Cache.count, gEng.sysInfo.L3Cache.size, gEng.sysInfo.L3Cache.kway);
        LOG_INFO("(init) System RAM: %_$$$llu", gEng.sysInfo.physicalMemorySize);
    }

    Console::Initialize(&gEng.alloc);

    JobsInitParams jobsInitParams {                   
        .alloc = &gEng.jobsAlloc, 
        .numShortTaskThreads = SettingsJunkyard::Get().engine.jobsNumShortTaskThreads,
        .numLongTaskThreads = SettingsJunkyard::Get().engine.jobsNumLongTaskThreads,
        .debugAllocations = SettingsJunkyard::Get().engine.debugAllocations 
    };
    Jobs::Initialize(jobsInitParams);

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

        if (!GfxBackend::Initialize()) {
            LOG_ERROR("Initializing Graphics failed");
            return false;
        }
    }

    // Asset manager
    if (!Asset::Initialize()) {
        LOG_ERROR("Initializing AssetManager failed");
        return false;
    }

    // Initialization time resources
    gEng.initResourcesGroup = Asset::CreateGroup();

    if (gfxSettings.IsGraphicsEnabled()) {
        if (gfxSettings.enableImGui && !ImGui::Initialize()) {
            LOG_ERROR("Initializing ImGui failed");
            return false;
        }

        if (!DebugDraw::Initialize()) {
            LOG_ERROR("Initializing DebugDraw failed");
            return false;
        }
    }

    if (ImGui::IsEnabled()) {
        DebugHud::Initialize();
        DebugHud::RegisterMemoryStats("Engine", _DrawMemStatsCallback);
    }

    App::RegisterEventsCallback(_OnEvent);

    // Console commands
    {
        auto GetVMemStats = [](int, const char**, char* outResponse, uint32 responseSize, void*)->bool {
            MemVirtualStats stats = Mem::VirtualGetStats();
            Str::PrintFmt(outResponse, responseSize, "Reserverd: %_$$$llu, Commited: %_$$$llu", stats.reservedBytes, stats.commitedBytes);
            return true;
        };

        ConCommandDesc cmdVmem {
            .name = "vmem",
            .help = "Get VMem stats",
            .callback = GetVMemStats
        };
        Console::RegisterCommand(cmdVmem);
    }

    LOG_INFO("(init) Engine v%u.%u.%u initialized (%.1f ms)", 
             GetVersionMajor(JUNKYARD_VERSION),
             GetVersionMinor(JUNKYARD_VERSION),
             GetVersionPatch(JUNKYARD_VERSION),
             Timer::ToMS(Timer::GetTicks()));
    gEng.initialized = true;

    return true;
}

void Engine::_private::PostInitialize()
{
    if (!gEng.initialized)
        return;
    
    // Fire up resource loading and override the update loop, so we can show something and wait for the init resources to finish
    if (gEng.initResourcesGroup.HasItemsInQueue()) {
        LOG_INFO("(init) Loading initial resources");
        gEng.initResourcesGroup.Load();
        App::OverrideUpdateCallback(_InitResourcesUpdate);
    }
    else {
        gEng.resourcesInitialized = true;
    }
}

void Engine::Release()
{
    LOG_INFO("Releasing engine sub systems ...");
    gEng.initialized = false;

    const SettingsGraphics& gfxSettings = SettingsJunkyard::Get().graphics;
    if (gfxSettings.IsGraphicsEnabled()) {
        if (ImGui::IsEnabled()) {
            DebugHud::Release();
            ImGui::Release();
        }
        DebugDraw::Release();
    } 

    if (gEng.initResourcesGroup.mHandle.IsValid()) {
        gEng.initResourcesGroup.Unload();

        while (!gEng.initResourcesGroup.IsIdle()) {
            Asset::Update();
            Thread::Sleep(1);
        }
    }

    Asset::DestroyGroup(gEng.initResourcesGroup);
    Asset::Release();

    if (gfxSettings.enable)
        GfxBackend::Release();

    if (SettingsJunkyard::Get().engine.connectToServer)
        Remote::Disconnect();

    Jobs::Release();
    Console::Release();

    gEng.shortcuts.Free();
    gEng.proxyAllocs.Free();
    gEng.initResourcesCallbacks.Free();
    Mem::Free(gEng.debugMemStats.items);

    gEng.jobsAlloc.Release();
    gEng.alloc.Release();
    gEng.mainAlloc.Release();

    LOG_INFO("Engine released");
}

void Engine::BeginFrame(float dt)
{
    PROFILE_ZONE();
    ASSERT(gEng.initialized);
    ASSERT_MSG(!gEng.beginFrameCalled, "Cannot call BeginFrame twice");
    gEng.beginFrameCalled = true;
    gEng.endFrameCalled = false;

    gEng.frameTime = dt;
    gEng.elapsedTime += dt;

    const SettingsJunkyard& settings = SettingsJunkyard::Get();

    // Reconnect to remote server if it's disconnected
    if (settings.engine.connectToServer && gEng.remoteReconnect) {
        gEng.remoteDisconnectTime += dt;
        if (gEng.remoteDisconnectTime >= ENGINE_REMOTE_RECONNECT_INTERVAL) {
            gEng.remoteDisconnectTime = 0;
            gEng.remoteReconnect = false;
            if (++gEng.remoteRetryCount <= ENGINE_REMOTE_CONNECT_RETRIES) {
                if (Remote::Connect(settings.engine.remoteServicesUrl.CStr(), _RemoteDisconnected))
                    gEng.remoteRetryCount = 0;
                else
                    _RemoteDisconnected(settings.engine.remoteServicesUrl.CStr(), false, SocketErrorCode::None);
            }
            else {
                LOG_WARNING("Failed to connect to server '%s' after %u retries", settings.engine.remoteServicesUrl.CStr(), 
                    ENGINE_REMOTE_CONNECT_RETRIES);
            }
        }
    }

    // Graphics
    if (settings.graphics.IsGraphicsEnabled()) {
        if (gEng.resourcesInitialized)
            ImGui::BeginFrame(dt);
        GfxBackend::Begin();
    }

    Asset::Update();

    gEng.rawFrameStartTime = Timer::GetTicks();
}

void Engine::EndFrame()
{
    PROFILE_ZONE();

    ASSERT(gEng.initialized);
    ASSERT_MSG(!gEng.endFrameCalled, "Cannot call EndFrame twice");
    ASSERT_MSG(gEng.beginFrameCalled, "BeginFrame is not called");
    gEng.beginFrameCalled = false;
    gEng.endFrameCalled = true;

    gEng.rawFrameTime = Timer::Diff(Timer::GetTicks(), gEng.rawFrameStartTime);

    // Graphics
    if (SettingsJunkyard::Get().graphics.IsGraphicsEnabled())
        GfxBackend::End();

    MemTempAllocator::Reset();

    TracyCFrameMark;

    Atomic::FetchAddExplicit(&gEng.frameIndex, 1, AtomicMemoryOrder::Relaxed);
}

uint64 Engine::GetFrameIndex()
{
    return Atomic::LoadExplicit(&gEng.frameIndex, AtomicMemoryOrder::Relaxed);
}

float Engine::GetFrameTime()
{
    return gEng.frameTime;
}

const SysInfo& Engine::GetSysInfo()
{
    return gEng.sysInfo;
}

float Engine::GetEngineTimeMS()
{
    return (float)Timer::ToMS(gEng.rawFrameTime);
}

void Engine::RegisterShortcut(const char* shortcut, EngineShortcutCallback callback, void* userData)
{
    ASSERT(callback);
    ASSERT(shortcut);
    
    InputKeycode keys[2] = {};
    InputKeyModifiers mods = InputKeyModifiers::None;

    // Parse shortcut keys string
    shortcut = Str::SkipWhitespace(shortcut);

    uint32 numKeys = 0;
    const char* plus;
    char keystr[32];

    auto ParseShortcutKey = [&numKeys, &keys, &mods](const char* keystr)
    {
        uint32 len = Str::Len(keystr);

        // function keys
        bool isFn = (len == 2 || len == 3) && Str::ToUpper(keystr[0]) == 'F' && 
            ((len == 2 && Str::IsNumber(keystr[1])) || (len == 3 && Str::IsNumber(keystr[1]) && Str::IsNumber(keystr[2])));
        if (isFn && numKeys < 2) {
            char numStr[3] = {keystr[1], keystr[2], 0};
            int fnum = Str::ToInt(numStr) - 1;
            if (fnum >= 0 && fnum < 25) {
                keys[numKeys++] = static_cast<InputKeycode>(uint32(InputKeycode::F1) + fnum);
            }
        }
        else if (len > 1) {
            if (Str::IsEqualNoCase(keystr, "ALT"))        mods |= InputKeyModifiers::Alt;
            else if (Str::IsEqualNoCase(keystr, "CTRL"))  mods |= InputKeyModifiers::Ctrl;
            else if (Str::IsEqualNoCase(keystr, "SHIFT")) mods |= InputKeyModifiers::Shift;
            else if (Str::IsEqualNoCase(keystr, "SUPER")) mods |= InputKeyModifiers::Super;
            else if (Str::IsEqualNoCase(keystr, "ESC"))       keys[numKeys++] = InputKeycode::Escape;
            else if (Str::IsEqualNoCase(keystr, "INS"))       keys[numKeys++] = InputKeycode::Insert;
            else if (Str::IsEqualNoCase(keystr, "PGUP"))      keys[numKeys++] = InputKeycode::PageUp;
            else if (Str::IsEqualNoCase(keystr, "PGDOWN"))    keys[numKeys++] = InputKeycode::PageDown;
            else if (Str::IsEqualNoCase(keystr, "HOME"))      keys[numKeys++] = InputKeycode::Home;
            else if (Str::IsEqualNoCase(keystr, "END"))       keys[numKeys++] = InputKeycode::End;
            else if (Str::IsEqualNoCase(keystr, "TAB"))       keys[numKeys++] = InputKeycode::Tab;
            else    ASSERT_MSG(0, "Shortcut not recognized: %s", keystr);
        } 
        else if (len == 1 && numKeys < 2) {
            char key = Str::ToUpper(keystr[0]);
            if (keystr[0] > uint32(InputKeycode::Space))
                keys[numKeys++] = static_cast<InputKeycode>(key);
        }
    };

    while (*shortcut) {
        plus = Str::FindChar(shortcut, '+');
        if (!plus)
            break;

        Str::CopyCount(keystr, sizeof(keystr), shortcut, uint32(plus - shortcut));
        Str::Trim(keystr, sizeof(keystr), keystr);
        ParseShortcutKey(keystr);
        shortcut = Str::SkipWhitespace(plus + 1);
    }

    if (shortcut[0]) {
        Str::Copy(keystr, sizeof(keystr), shortcut);
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

const AssetGroup& Engine::RegisterInitializeResources(EngineInitializeResourcesCallback callback, void* userData)
{
    ASSERT_MSG(!gEng.resourcesInitialized, "Cannot call this function when init resources are already loaded");
    ASSERT_MSG(gEng.initResourcesCallbacks.FindIf([&callback](const EngineInitializeResourcesPair& p) { return p.first == callback; }) == -1,
               "Cannot register one callback twice");

    gEng.initResourcesCallbacks.Push(EngineInitializeResourcesPair(callback, userData));

    return gEng.initResourcesGroup;
}

void Engine::RegisterProxyAllocator(MemProxyAllocator* alloc)
{
    [[maybe_unused]] uint32 index = gEng.proxyAllocs.FindIf([alloc](const MemProxyAllocator* a) { return alloc == a; });
    ASSERT(index == -1);
    gEng.proxyAllocs.Push(alloc);
}

void Engine::HelperInitializeProxyAllocator(MemProxyAllocator* alloc, const char* name, MemAllocator* baseAlloc)
{
    MemProxyAllocatorFlags proxyAllocFlags = SettingsJunkyard::Get().engine.trackAllocations ? 
        MemProxyAllocatorFlags::EnableTracking : MemProxyAllocatorFlags::None;

    if (!baseAlloc) {
        ASSERT(gEng.mainAlloc.IsInitialized());
        alloc->Initialize(name, &gEng.mainAlloc, proxyAllocFlags);
    }
    else {
        alloc->Initialize(name, baseAlloc, proxyAllocFlags);
    }
}
