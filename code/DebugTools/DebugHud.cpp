#include "DebugHud.h"

#include "../Core/Blobs.h"
#include "../Core/MathScalar.h"
#include "../Core/Log.h"

#include "../Common/JunkyardSettings.h"
#include "../Common/Application.h"

#include "../ImGui/ImGuiMain.h"

#include "../Engine.h"

inline constexpr uint32 DEBUG_HUD_STATUS_BAR_MAX_LINES = 4;
inline constexpr uint32 DEBUG_HUD_STATUS_BAR_CAPACITY = DEBUG_HUD_STATUS_BAR_MAX_LINES + 1;  // +1 slot for the line that is scrolling out
inline constexpr float DEBUG_HUD_STATUS_BAR_FADE_DURATION = 5.0f;
inline constexpr float DEBUG_HUD_STATUS_BAR_SCROLL_DURATION = 0.1f;

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

enum class DebugHudSide : uint32
{
    Right = 0,      // default
    Left
};

struct DebugHudGraph
{
    RingBlob values;    // entry type = float
    float minValue;
    float avgValue;
    float maxValue;
    uint32 numSamples;
};

// Status lines are kept in a ring buffer (SoA). 'head' is the newest line
// scrollOffset is in line units and lerps back to zero after each new line is pushed
struct DebugHudStatusBarContext
{
    SpinLockMutex lock;

    String<256> texts[DEBUG_HUD_STATUS_BAR_CAPACITY];
    Color4u colors[DEBUG_HUD_STATUS_BAR_CAPACITY];
    float showTimes[DEBUG_HUD_STATUS_BAR_CAPACITY];

    uint32 head;
    uint32 count;

    float scrollFrom;
    float scrollOffset;
    float scrollTime;
};

struct DebugHudContext
{
    DebugHudStatusBarContext statusBar;
    DebugHudMemStats memStats;

    DebugHudGraph graphs[uint32(DebugHudGraphType::_Count)];
    bool enabledGraphs[uint32(DebugHudGraphType::_Count)];
    bool showMemStats;
    bool showMinMax;
    DebugHudSide hudSide;

    uint32 monitorRefreshRate;

    float transparency = 0.8f;
};

static DebugHudContext gDebugHud;

namespace DebugHud
{
    static void _StatusBarLogCallback(const LogEntry& entry, void*)
    {
        DebugHudStatusBarContext& sb = gDebugHud.statusBar;
        SpinLockMutexScope lock(sb.lock);

        uint32 index = (sb.head + 1) % DEBUG_HUD_STATUS_BAR_CAPACITY;
        sb.head = index;
        sb.count = Min(sb.count + 1, DEBUG_HUD_STATUS_BAR_CAPACITY);

        sb.texts[index] = entry.text;
        sb.showTimes[index] = 0;

        switch (entry.type) {
        case LogLevel::Info:	sb.colors[index] = COLOR4U_WHITE; break;
        case LogLevel::Debug:	sb.colors[index] = Color4u(0, 200, 200); break;
        case LogLevel::Verbose:	sb.colors[index] = Color4u(128, 128, 128); break;
        case LogLevel::Warning:	sb.colors[index] = COLOR4U_YELLOW; break;
        case LogLevel::Error:	sb.colors[index] = COLOR4U_RED; break;
        default:			    sb.colors[index] = COLOR4U_WHITE; break;
        }

        // Restart scroll animation from wherever the lines currently are, pushed down by one more line
        sb.scrollFrom = Min(sb.scrollOffset + 1.0f, float(DEBUG_HUD_STATUS_BAR_MAX_LINES));
        sb.scrollOffset = sb.scrollFrom;
        sb.scrollTime = 0;
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

        // Calculate minimum and maximum
        for (uint32 i = 0; i < numValues; i++) {
            if (values[i] < minVal)
                minVal = values[i];
            if (values[i] > maxVal)
                maxVal = values[i];
        }

        // Fps is a rate, so it should be averaged as harmonic mean instead. Arithmetic mean of (1/dt) samples is biased
        // upwards by frame-time jitter and can show values way above the monitor refresh rate even when we are
        // presenting exactly once per vblank. numValues/Sum(dt) is the actual frame rate over the sampling window
        if (type == DebugHudGraphType::Fps) {
            float sumFrameTimes = 0;
            uint32 numValidValues = 0;
            for (uint32 i = 0; i < numValues; i++) {
                if (values[i] > 0) {
                    sumFrameTimes += 1.0f/values[i];
                    numValidValues++;
                }
            }
            avg = sumFrameTimes > 0 ? float(numValidValues)/sumFrameTimes : 0;
        }
        else {
            for (uint32 i = 0; i < numValues; i++)
                avg += values[i];
            avg /= float(numValues);
        }

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

        ImGui::PushID((int)type);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, gDebugHud.transparency);
        ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        if (isFrameTime) {
            if (graph.avgValue >= 33.0f)        
                textColor = ImGui::ColorToImVec4(COLOR4U_RED);
        }
        else if (isFps) {
            if (graph.avgValue < 30)            
                textColor = ImGui::ColorToImVec4(COLOR4U_RED);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, textColor);

        const float kLineSize = ImGui::GetFrameHeightWithSpacing();

        String64 overlay;
        if (gDebugHud.showMinMax) {
            overlay = String64::Format("%s%s: %.1f (Min: %.1f, Max: %.1f)", DEBUGHUD_GRAPH_NAMES[uint32(type)], 
                                                isFrameTime ? "(ms)" : "", graph.avgValue, graph.minValue, graph.maxValue);
        }
        else {
            overlay = String64::Format("%s%s: %.1f", DEBUGHUD_GRAPH_NAMES[uint32(type)], isFrameTime ? "(ms)" : "", graph.avgValue);
        }

        float plotMax = graph.maxValue;
        if (isFrameTime)
            plotMax = 33.0f;
        else if (isFps && SettingsJunkyard::Get().graphics.enableVsync)
            plotMax = float(gDebugHud.monitorRefreshRate);

        ImGui::PlotLines("##dt", values, (int)numValues, 0, overlay.CStr(), 0, plotMax,
                          ImVec2(ImGui::GetContentRegionAvail().x, kLineSize*2));

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::PopID();

    }

    static void _DrawHudMenu()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, gDebugHud.transparency);
        if (ImGui::ArrowButton("OpenContextMenu", ImGuiDir_Down)) {
            ImGui::OpenPopup("ContextMenu");
        }
        ImGui::PopStyleVar();

        if (ImGui::BeginPopupContextItem("ContextMenu")) {
            for (uint32 i = 0; i < uint32(DebugHudGraphType::_Count); i++) {
                String64 graphItemMenuStr = String64::Format("Toggle %s", DEBUGHUD_GRAPH_NAMES[i]);
                ImGui::MenuItem(graphItemMenuStr.CStr(), nullptr, &gDebugHud.enabledGraphs[i]);
            }


            ImGui::Separator();

            ImGui::MenuItem("Show Min/Max", nullptr, &gDebugHud.showMinMax);
            ImGui::MenuItem("Memory Stats", nullptr, &gDebugHud.showMemStats);

            ImGui::Separator();

            if (ImGui::MenuItem("Show on Right", nullptr, gDebugHud.hudSide == DebugHudSide::Right))
                gDebugHud.hudSide = DebugHudSide::Right;
            if (ImGui::MenuItem("Show on Left", nullptr, gDebugHud.hudSide == DebugHudSide::Left))
                gDebugHud.hudSide = DebugHudSide::Left;

            ImGui::Separator();

            ImGui::SliderFloat("Transparency", &gDebugHud.transparency, 0.1f, 1.0f, "%.1f");

            ImGui::EndPopup();
        }
    }

    static void _DrawMemBudgets()
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(400, 200), ImVec2(M_FLOAT32_MAX, M_FLOAT32_MAX));
        if (ImGui::Begin("Memory/Resource Stats", &gDebugHud.showMemStats)) {
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
    const bool kHudOnRight = gDebugHud.hudSide == DebugHudSide::Right;
    const ImVec2 kHudPivot(kHudOnRight ? 1.0f : 0.0f, 0.0f);
    const float kHudPosX = kHudOnRight ? kDisplaySize.x : 0.0f;

    ImGui::SetNextWindowPos(ImVec2(kHudPosX, yOffset), ImGuiCond_Always, kHudPivot);
    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_Always);
    const uint32 kWndFlags = ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar|
                             ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoInputs|
                             ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Frame", nullptr, kWndFlags)) {
        _UpdateGraph(dt*1000.0f, DebugHudGraphType::FrameTime);
        _UpdateGraph(dt > 0 ? 1.0f/dt : 0, DebugHudGraphType::Fps);   // Zero values are skipped in Fps averaging
        _UpdateGraph(Engine::GetEngineTimeMS(), DebugHudGraphType::CpuTime);
        _UpdateGraph(GfxBackend::GetRenderTimeMS(), DebugHudGraphType::GpuTime);

        ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), 0));
        for (uint32 i = 0; i < uint32(DebugHudGraphType::_Count); i++) {
            if (gDebugHud.enabledGraphs[i])
                _DrawGraph(DebugHudGraphType(i));
        }
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(kHudPosX, yOffset), ImGuiCond_Always, kHudPivot);
    const uint32 kMenuWndFlags = ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar|
                                 ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_AlwaysAutoResize|
                                 ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("FrameMenu", nullptr, kMenuWndFlags))
        _DrawHudMenu();
    ImGui::End();

    if (gDebugHud.showMemStats)
        _DrawMemBudgets();
}

void DebugHud::DrawStatusBar(float dt)
{
    ImGuiStyle& kStyle = ImGui::GetStyle();
    const ImVec2 kDisplaySize = ImGui::GetIO().DisplaySize;
    const float kLineSize = ImGui::GetFrameHeight();

    const float kBaseY = kDisplaySize.y - kLineSize;

    DebugHudStatusBarContext& sb = gDebugHud.statusBar;
    SpinLockMutexScope lock(sb.lock);
    ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();

    sb.scrollTime += dt;
    float scrollT = M::SmoothStep(sb.scrollTime, 0, DEBUG_HUD_STATUS_BAR_SCROLL_DURATION);
    sb.scrollOffset = M::Lerp(sb.scrollFrom, 0, scrollT);

    for (uint32 i = 0; i < sb.count; i++) {
        uint32 index = (sb.head + DEBUG_HUD_STATUS_BAR_CAPACITY - i) % DEBUG_HUD_STATUS_BAR_CAPACITY;

        // Normal time based fade out of each line
        sb.showTimes[index] = Min(sb.showTimes[index] + dt, DEBUG_HUD_STATUS_BAR_FADE_DURATION);
        float timeAlpha = 1.0f - M::Gain(M::LinearStep(sb.showTimes[index], 0, DEBUG_HUD_STATUS_BAR_FADE_DURATION), 0.05f);

        // Position based fade: reaches zero when the line scrolls past the last visible slot
        float linePos = float(i) - sb.scrollOffset;
        float posAlpha = M::Saturate(1.0f - linePos/float(DEBUG_HUD_STATUS_BAR_MAX_LINES));

        float alpha = timeAlpha*posAlpha;
        if (alpha <= 0)
            continue;

        Color4u color = sb.colors[index];
        color.a = uint8(alpha*255.0f);
        fgDrawList->AddText(ImVec2(kStyle.WindowPadding.x, kBaseY - linePos*kLineSize), color.n, sb.texts[index].CStr());
    }
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
    gDebugHud.hudSide = DebugHudSide(Str::ToInt(ImGui::GetSetting("DebugHud.Side")));
    gDebugHud.transparency = Clamp(float(Str::ToDouble(ImGui::GetSetting("DebugHud.Transparency"))), 0.1f, 1.0f);
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
    ImGui::SetSetting("DebugHud.Side", int(gDebugHud.hudSide));
    ImGui::SetSetting("DebugHud.Transparency", gDebugHud.transparency);
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
