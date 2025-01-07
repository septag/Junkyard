#include "DebugHud.h"

#include "../Core/Jobs.h"
#include "../Core/Blobs.h"
#include "../Core/MathScalar.h"
#include "../Core/Log.h"

#include "../Common/JunkyardSettings.h"
#include "../Common/Application.h"

#include "../Assets/AssetManager.h"
#include "../ImGui/ImGuiMain.h"

#include "../Engine.h"

inline constexpr float DEBUG_HUD_FRAGMENTATION_INTERVAL = 1.0f;

struct DebugHudMemStatsItem
{
    String32 name;
    DebugHudMemoryStatsCallback callback;
    void* userData;
};

struct DebugHudMemStats
{
    Array<DebugHudMemStatsItem> items;
};

struct DebugHudMemoryView
{
    float gfxLastFragTm;
    float assetLastFragTm;
    float imguiLastFragTm;

    float gfxHeapFragmentation;
    float assetHeapFragmentation;
    float imguiHeapFragmentation;

    bool assetHeapValidate;
    bool gfxHeapValidate;
    bool imguiHeapValidate;
};

enum class DebugHudGraphType : uint32
{
    Fps = 0,
    FrameTime,
    CpuTime,
    GpuTime,
    _Count
};

static const char* DEBUGHUD_GRAPH_NAMES[uint32(DebugHudGraphType::_Count)] = {
    "FPS",
    "FrameTime",
    "CpuTime",
    "GpuTime"
};

struct DebugHudGraph
{
    RingBlob values;    // entry type = float
    float minValue;
    float avgValue;
    float maxValue;
    uint32 numSamples;
};

struct DebugHudContext
{
    SpinLockMutex statusLock;
    DebugHudMemStats memStats;

    DebugHudGraph graphs[uint32(DebugHudGraphType::_Count)];
    bool enabledGraphs[uint32(DebugHudGraphType::_Count)];
    bool showMemStats;

    uint32 monitorRefreshRate;

    String<256> statusText;
    Color statusColor;
    float statusShowTime;
};

static DebugHudContext gDebugHud;

namespace DebugHud
{
    static void _StatusBarLogCallback(const LogEntry& entry, void*)
    {
        SpinLockMutexScope lock(gDebugHud.statusLock);

        gDebugHud.statusText = entry.text;  
        gDebugHud.statusShowTime = 0;

        switch (entry.type) {
        case LogLevel::Info:	gDebugHud.statusColor = COLOR_WHITE; break;
        case LogLevel::Debug:	gDebugHud.statusColor = Color(0, 200, 200); break;
        case LogLevel::Verbose:	gDebugHud.statusColor = Color(128, 128, 128); break;
        case LogLevel::Warning:	gDebugHud.statusColor = COLOR_YELLOW; break;
        case LogLevel::Error:	gDebugHud.statusColor = COLOR_RED; break;
        default:			    gDebugHud.statusColor = COLOR_WHITE; break;
        }
    }

    static void _EventCallback(const AppEvent& ev, [[maybe_unused]] void* userData)
    {
        if (ev.type == AppEventType::DisplayUpdated) {
            for (uint32 i = 0; i < uint32(DebugHudGraphType::_Count); i++) {
                gDebugHud.graphs[i].values.Free();
            }

            gDebugHud.monitorRefreshRate = App::GetDisplayInfo().refreshRate;

            uint32 numSamples = gDebugHud.monitorRefreshRate;

            for (uint32 i = 0; i < uint32(DebugHudGraphType::_Count); i++) {
                gDebugHud.graphs[i] = {
                    .numSamples = numSamples
                };

                gDebugHud.graphs[i].values.Reserve(numSamples*sizeof(float));
            }
        }
    }

    static void _UpdateGraph(float value, DebugHudGraphType type)
    {
        MemTempAllocator tmpAlloc;
        DebugHudGraph& graph = gDebugHud.graphs[uint32(type)];
        
        if (graph.values.ExpectWrite() < sizeof(float))
            graph.values.Read<float>(nullptr);
        graph.values.Write<float>(value);

        float* values = tmpAlloc.MallocTyped<float>(graph.numSamples);
        uint32 numValues = uint32(graph.values.Peek(values, sizeof(float)*graph.numSamples)/sizeof(float));

        float avg = 0;
        float minVal = FLT_MAX;
        float maxVal = -FLT_MAX;
        for (uint32 i = 0; i < numValues; i++) {
            avg += values[i];
            if (values[i] < minVal)
                minVal = values[i];
            if (values[i] > maxVal)
                maxVal = values[i];
        }
        avg /= float(numValues);

        graph.minValue = minVal;
        graph.maxValue = maxVal;
        graph.avgValue = avg;
    }

    static void _DrawGraph(DebugHudGraphType type)
    {
        bool isFrameTime = (type == DebugHudGraphType::FrameTime || type == DebugHudGraphType::CpuTime || type == DebugHudGraphType::GpuTime);
        bool isFps = type == DebugHudGraphType::Fps;

        MemTempAllocator tmpAlloc;
        DebugHudGraph& graph = gDebugHud.graphs[uint32(type)];
        float* values = tmpAlloc.MallocTyped<float>(graph.numSamples);
        uint32 numValues = uint32(graph.values.Peek(values, sizeof(float)*graph.numSamples)/sizeof(float));

        if (isFrameTime) {
            for (uint32 i = 0; i < numValues; i++)
                values[i] = 33.0f - Min(values[i], 33.0f);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.8f);
        ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
         if (isFrameTime) {
            if (graph.avgValue >= 33.0f)        
                textColor = ImGui::ColorToImVec4(COLOR_RED);
        }
        else if (isFps) {
            if (graph.avgValue < 30)            
                textColor = ImGui::ColorToImVec4(COLOR_RED);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, textColor);

        const float kLineSize = ImGui::GetFrameHeightWithSpacing();

        String32 overlay = String32::Format("%s%s: %.1f", DEBUGHUD_GRAPH_NAMES[uint32(type)], isFrameTime ? "(ms)" : "", graph.avgValue);

        float plotMax = graph.maxValue;
        if (isFrameTime)
            plotMax = 33.0f;
        else if (isFps && SettingsJunkyard::Get().graphics.enableVsync)
            plotMax = float(gDebugHud.monitorRefreshRate);

        ImGui::PlotLines("##dt", values, (int)numValues, 0, overlay.CStr(), 0, plotMax, ImVec2(0, kLineSize*2));

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

    }

    static void _DrawHudMenu()
    {
        if (ImGui::ArrowButton("OpenContextMenu", ImGuiDir_Down)) {
            ImGui::OpenPopup("ContextMenu");
        }

        if (ImGui::BeginPopupContextItem("ContextMenu")) {
            for (uint32 i = 0; i < uint32(DebugHudGraphType::_Count); i++) {
                String64 graphItemMenuStr = String64::Format("Toggle %s", DEBUGHUD_GRAPH_NAMES[i]);
                ImGui::MenuItem(graphItemMenuStr.CStr(), nullptr, &gDebugHud.enabledGraphs[i]);
            }

            ImGui::Separator();

            ImGui::MenuItem("Memory Stats", nullptr, &gDebugHud.showMemStats);

            ImGui::EndPopup();
        }
    }

    static void _DrawMemBudgets()
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(400, 200), ImVec2(M_FLOAT32_MAX, M_FLOAT32_MAX));
        if (ImGui::Begin("Memory/Resource Stats")) {
            ImGui::BeginTabBar("MemoryTabs");

            DebugHudMemStats& mstats = gDebugHud.memStats;
            for (uint32 i = 0; i < mstats.items.Count(); i++) {
                DebugHudMemStatsItem& item = mstats.items[i];
                if (ImGui::BeginTabItem(item.name.CStr())) {
                    item.callback(item.userData);
                    ImGui::EndTabItem();
                }
            
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }
} // DebugHud


void DebugHud::DrawDebugHud(float dt, float yOffset)
{
    const ImVec2 kDisplaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, yOffset), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kDisplaySize.x*0.33f, 0), ImGuiCond_Always);
    const uint32 kWndFlags = ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar|
                             ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize;
    if (ImGui::Begin("Frame", nullptr, kWndFlags)) {
        _UpdateGraph(dt*1000.0f, DebugHudGraphType::FrameTime);
        _UpdateGraph(1.0f/dt, DebugHudGraphType::Fps);
        _UpdateGraph(Engine::GetEngineTimeMS(), DebugHudGraphType::CpuTime);
        _UpdateGraph(GfxBackend::GetRenderTimeNS()/1000000.0f, DebugHudGraphType::GpuTime);

        _DrawHudMenu();

        for (uint32 i = 0; i < uint32(DebugHudGraphType::_Count); i++) {
            if (gDebugHud.enabledGraphs[i])
                _DrawGraph(DebugHudGraphType(i));
        }

        if (gDebugHud.showMemStats)
            _DrawMemBudgets();
    }
    ImGui::End();
}

void DebugHud::DrawStatusBar(float dt)
{
    ImGuiStyle& kStyle = ImGui::GetStyle();
    const ImVec2 kDisplaySize = ImGui::GetIO().DisplaySize;
    const float kLineSize = ImGui::GetFrameHeightWithSpacing();

    SpinLockMutexScope lock(gDebugHud.statusLock);
    ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();
    float y = kDisplaySize.y - kLineSize;
    gDebugHud.statusShowTime += dt;
    float alpha = M::LinearStep(gDebugHud.statusShowTime, 0, 5.0f);
    alpha = 1.0f - M::Gain(alpha, 0.05f);
    gDebugHud.statusColor.a = uint8(alpha * 255.0f);

    fgDrawList->AddText(ImVec2(kStyle.WindowPadding.x, y), gDebugHud.statusColor.n, gDebugHud.statusText.CStr());
}

void DebugHud::Initialize()
{
    Log::RegisterCallback(_StatusBarLogCallback, nullptr);
    App::RegisterEventsCallback(_EventCallback);

    gDebugHud.monitorRefreshRate = App::GetDisplayInfo().refreshRate;
    uint32 numSamples = gDebugHud.monitorRefreshRate;

    for (uint32 i = 0; i < uint32(DebugHudGraphType::_Count); i++) {
        gDebugHud.graphs[i] = {
            .numSamples = numSamples
        };

        gDebugHud.graphs[i].values.Reserve(numSamples*sizeof(float));

        gDebugHud.enabledGraphs[i] = Str::ToBool(ImGui::GetSetting(String32::Format("DebugHud.%s", DEBUGHUD_GRAPH_NAMES[i]).CStr()));
    }

    gDebugHud.showMemStats = Str::ToBool(ImGui::GetSetting("DebugHud.MemStats"));
}

void DebugHud::Release()
{
    App::UnregisterEventsCallback(_EventCallback);
    Log::UnregisterCallback(_StatusBarLogCallback);

    for (uint32 i = 0; i < uint32(DebugHudGraphType::_Count); i++) {
        ImGui::SetSetting(String32::Format("DebugHud.%s", DEBUGHUD_GRAPH_NAMES[i]).CStr(), gDebugHud.enabledGraphs[i]);
        gDebugHud.graphs[i].values.Free();
    }

    ImGui::SetSetting("DebugHud.MemStats", gDebugHud.showMemStats);
}

void DebugHud::RegisterMemoryStats(const char* name, DebugHudMemoryStatsCallback callback, void* userData)
{
    uint32 index = gDebugHud.memStats.items.FindIf([name](const DebugHudMemStatsItem& i) { return i.name == name; });
    if (index == -1) {
        DebugHudMemStatsItem item {
            .name = name,
            .callback = callback,
            .userData = userData
        };
        gDebugHud.memStats.items.Push(item);
    }
    else {
        ASSERT_MSG(0, "Memory stats '%s' is already registered", name);
    }
}
