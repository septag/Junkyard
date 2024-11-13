#include "DebugHud.h"

#include "../Core/Jobs.h"
#include "../Core/Blobs.h"
#include "../Core/MathScalar.h"
#include "../Core/Log.h"

#include "../Common/JunkyardSettings.h"
#include "../Common/Application.h"

#include "../Graphics/Graphics.h"
#include "../Assets/AssetManager.h"
#include "../ImGui/ImGuiMain.h"

#include "../Engine.h"

inline constexpr float DEBUG_HUD_FRAGMENTATION_INTERVAL = 1.0f;

struct DebugHudMemoryStatsItem
{
    String32 name;
    DebugHudMemoryStatsCallback callback;
    void* userData;
};

struct DebugHudMemStats
{
    Array<DebugHudMemoryStatsItem> items;
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

static DebugHudMemoryView gMemBudget;
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

    static void _QuickFrameInfoEventCallback(const AppEvent& ev, [[maybe_unused]] void* userData)
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

            ImGui::MenuItem("Memory Budgets", nullptr, &gDebugHud.showMemStats);

            ImGui::EndPopup();
        }
    }

    static void _DrawMemBudgets(float dt, bool* pOpen);

} // DebugHud


void DebugHud::_DrawMemBudgets(float dt, bool* pOpen)
{
    auto DivideInt = [](uint32 a, uint32 b)->float { return float(double(a)/double(b)); };
    auto DivideSize = [](size_t a, size_t b)->float { return float(double(a)/double(b)); };

    const ImVec4& kTextColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const float kFontSize = ImGui::GetFontSize();
    const float kLineSize = ImGui::GetFrameHeightWithSpacing();
    ImVec2 kDisplaySize = ImGui::GetIO().DisplaySize;
    const ImGuiStyle& kStyle = ImGui::GetStyle();

    IMGUI_ALPHA_WINDOW(Budgets);
    ImGui::SetNextWindowSizeConstraints(ImVec2(kFontSize*20, kLineSize*7), ImVec2(kFontSize*50, kLineSize*50));
    ImGui::SetNextWindowSize(ImVec2(kFontSize*20, kLineSize*7), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(kDisplaySize.x - kFontSize*20 - kStyle.WindowBorderSize*2 - kStyle.WindowPadding.x, kStyle.WindowPadding.x), 
                            ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("MemBudgets", pOpen, 0)) {
        IMGUI_ALPHA_CONTROL(Budgets);

        ImGui::Text("InitHeap Commited: %_$llu", Engine::GetInitHeap()->GetCommitedSize());

        bool vmOpen = Str::ToBool(ImGui::GetSetting("MemBudgets.VirtualMem"));
        bool transientOpen = Str::ToBool(ImGui::GetSetting("MemBudgets.TransientAllocs"));
        bool jobsOpen = Str::ToBool(ImGui::GetSetting("MemBudgets.Jobs"));
        bool assetOpen = Str::ToBool(ImGui::GetSetting("MemBudgets.AssetManager"));
        bool gfxOpen = Str::ToBool(ImGui::GetSetting("MemBudgets.Graphics"));
        bool imguiOpen = Str::ToBool(ImGui::GetSetting("MemBudgets.ImGui"));
        bool systemOpen = Str::ToBool(ImGui::GetSetting("MemBudgets.System"));

        vmOpen = ImGui::CollapsingHeader("Virtual Mem", nullptr, vmOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (vmOpen) {
            MemVirtualStats vmStats = Mem::VirtualGetStats();
            ImGui::Text("Commited: %_$llu", vmStats.commitedBytes);
            ImGui::Text("Reserved: %_$llu", vmStats.reservedBytes);
        }

        transientOpen = ImGui::CollapsingHeader("Transient Allocators", nullptr, transientOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (transientOpen) {
            MemTempAllocator tmpAlloc;
            MemTempAllocator::Stats* tempStats;
            uint32 numTempStats;
            MemTempAllocator::GetStats(&tmpAlloc, &tempStats, &numTempStats);
            for (uint32 i = 0; i < numTempStats; i++) {
                if (ImGui::TreeNodeEx(String64::Format("#%u: %s (tId: %u)", i+1, tempStats[i].threadName, tempStats[i].threadId).CStr(), 0)) {
                    ImGui::TextColored(kTextColor, "TempAlloc");
                    ImGui::SameLine();
                    ImGui::ProgressBar(DivideSize(tempStats[i].curPeak, tempStats[i].maxPeak), ImVec2(-1.0f, 0),
                                       String32::Format("%_$llu/%_$llu", tempStats[i].curPeak, tempStats[i].maxPeak).CStr());
                    ImGui::TreePop();
                }
            }
        }

        jobsOpen = ImGui::CollapsingHeader("Jobs", nullptr, jobsOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (jobsOpen) {
            JobsBudgetStats stats;
            Jobs::GetBudgetStats(&stats);
            
            ImGui::TextColored(kTextColor, "Busy LongTask Threads:");
            ImGui::SameLine();
            ImGui::ProgressBar(DivideInt(stats.numBusyLongThreads, stats.maxLongTaskThreads), ImVec2(-1.0f, 0), 
                               String32::Format("%u/%u", stats.numBusyLongThreads, stats.maxLongTaskThreads).CStr());

            ImGui::TextColored(kTextColor, "Busy ShortTask Threads:");
            ImGui::SameLine();
            ImGui::ProgressBar(DivideInt(stats.numBusyShortThreads, stats.maxShortTaskThreads), ImVec2(-1.0f, 0), 
                               String32::Format("%u/%u", stats.numBusyShortThreads, stats.maxShortTaskThreads).CStr());

            ImGui::TextColored(kTextColor, "Active Fibers:");
            ImGui::SameLine();
            ImGui::ProgressBar(DivideInt(stats.numActiveFibers, stats.numMaxActiveFibers), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numActiveFibers, stats.numMaxActiveFibers).CStr());
            
            ImGui::TextColored(kTextColor, "Jobs:");
            ImGui::SameLine();
            ImGui::ProgressBar(DivideInt(stats.numJobs, stats.maxJobs), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numJobs, stats.maxJobs).CStr());

            ImGui::Text("FibersPoolSize: %_$llu", stats.fibersMemoryPoolSize);
            ImGui::Text("InitHeapSize: %_$llu", stats.initHeapSize);
        }

        gfxOpen = ImGui::CollapsingHeader("Graphics", nullptr, gfxOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (gfxOpen) {
            GfxBudgetStats stats;
            gfxGetBudgetStats(&stats);

            ImGui::TextColored(kTextColor, "Buffers:");
            ImGui::SameLine();
            ImGui::ProgressBar(DivideInt(stats.numBuffers, stats.maxBuffers), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numBuffers, stats.maxBuffers).CStr());

            ImGui::TextColored(kTextColor, "Images:");
            ImGui::SameLine();
            ImGui::ProgressBar(DivideInt(stats.numImages, stats.maxImages), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numImages, stats.maxImages).CStr());

            ImGui::TextColored(kTextColor, "DescriptorSets:");
            ImGui::SameLine();
            ImGui::ProgressBar(DivideInt(stats.numDescriptorSets, stats.maxDescriptorSets), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numDescriptorSets, stats.maxDescriptorSets).CStr());

            ImGui::TextColored(kTextColor, "Pipelines:");
            ImGui::SameLine();
            ImGui::ProgressBar(DivideInt(stats.numPipelines, stats.maxPipelines), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numPipelines, stats.maxPipelines).CStr());
            
            ImGui::TextColored(kTextColor, "PipelineLayouts:");
            ImGui::SameLine();
            ImGui::ProgressBar(DivideInt(stats.numPipelineLayouts, stats.maxPipelineLayouts), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numPipelineLayouts, stats.maxPipelineLayouts).CStr());

            ImGui::TextColored(kTextColor, "Garbage:");
            ImGui::SameLine();
            ImGui::ProgressBar(DivideInt(stats.numGarbage, stats.maxGarbage), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numGarbage, stats.maxGarbage).CStr());

            if (ImGui::TreeNodeEx("Descriptors", ImGuiTreeNodeFlags_DefaultOpen)) {
                const GfxBudgetStats::DescriptorBudgetStats& dstats = stats.descriptors;
                ImGui::TextColored(kTextColor, "UniformBuffers:");
                ImGui::SameLine();
                ImGui::ProgressBar(DivideInt(dstats.numUniformBuffers, dstats.maxUniformBuffers), ImVec2(-1.0f, 0),
                                   String32::Format("%u/%u", dstats.numUniformBuffers, dstats.maxUniformBuffers).CStr());

                ImGui::TextColored(kTextColor, "SampledImages:");
                ImGui::SameLine();
                ImGui::ProgressBar(DivideInt(dstats.numSampledImages, dstats.maxSampledImages), ImVec2(-1.0f, 0),
                                   String32::Format("%u/%u", dstats.numSampledImages, dstats.maxSampledImages).CStr());

                ImGui::TextColored(kTextColor, "Samplers:");
                ImGui::SameLine();
                ImGui::ProgressBar(DivideInt(dstats.numSamplers, dstats.maxSamplers), ImVec2(-1.0f, 0),
                                   String32::Format("%u/%u", dstats.numSamplers, dstats.maxSamplers).CStr());

                ImGui::TextColored(kTextColor, "CombinedImageSamplers:");
                ImGui::SameLine();
                ImGui::ProgressBar(DivideInt(dstats.numCombinedImageSamplers, dstats.maxCombinedImageSamplers), ImVec2(-1.0f, 0),
                                   String32::Format("%u/%u", dstats.numCombinedImageSamplers, dstats.maxCombinedImageSamplers).CStr());

                ImGui::TreePop();
            }
            
            ImGui::TextColored(kTextColor, "RuntimeHeap:");
            ImGui::SameLine();
            ImGui::ProgressBar(DivideSize(stats.runtimeHeapSize, stats.runtimeHeapMax), ImVec2(-1.0f, 0),
                               String32::Format("%_$llu/%_$llu", stats.runtimeHeapSize, stats.runtimeHeapMax).CStr());

            gMemBudget.gfxLastFragTm += dt;
            if (gMemBudget.gfxLastFragTm >= DEBUG_HUD_FRAGMENTATION_INTERVAL) {
                gMemBudget.gfxHeapFragmentation = stats.runtimeHeap->CalculateFragmentation() * 100.0f;
                gMemBudget.gfxHeapValidate = stats.runtimeHeap->Validate();
                gMemBudget.gfxLastFragTm = 0;
            }
            ImGui::Text("RuntimeHeap fragmentation: %.1f%%", gMemBudget.gfxHeapFragmentation);
            ImGui::TextColored(gMemBudget.gfxHeapValidate ? ImGui::ColorToImVec4(COLOR_GREEN) : ImGui::ColorToImVec4(COLOR_RED), 
                       "RuntimeHeap validate: %s", gMemBudget.gfxHeapValidate ? "Ok" : "Fail");

            ImGui::Text("InitHeapSize: %_$llu", stats.initHeapSize);
        }

        imguiOpen = ImGui::CollapsingHeader("ImGui", nullptr, imguiOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (imguiOpen) {
            ImGuiBudgetStats stats;
            ImGui::GetBudgetStats(&stats);

            ImGui::TextColored(kTextColor, "Vertices:");
            ImGui::SameLine();
            ImGui::ProgressBar(DivideInt(stats.lastFrameVertices, stats.maxVertices), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.lastFrameVertices, stats.maxVertices).CStr());

            ImGui::TextColored(kTextColor, "Indices:");
            ImGui::SameLine();
            ImGui::ProgressBar(DivideInt(stats.lastFrameIndices, stats.maxIndices), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.lastFrameIndices, stats.maxIndices).CStr());
            
            ImGui::TextColored(kTextColor, "RuntimeHeap:");
            ImGui::SameLine();
            ImGui::ProgressBar(DivideSize(stats.runtimeHeapSize, stats.runtimeHeapMax), ImVec2(-1.0f, 0),
                               String32::Format("%_$llu/%_$llu", stats.runtimeHeapSize, stats.runtimeHeapMax).CStr());

            gMemBudget.imguiLastFragTm += dt;
            if (gMemBudget.imguiLastFragTm >= DEBUG_HUD_FRAGMENTATION_INTERVAL) {
                gMemBudget.imguiHeapFragmentation = stats.runtimeHeap->CalculateFragmentation() * 100.0f;
                gMemBudget.imguiHeapValidate = stats.runtimeHeap->Validate();
                gMemBudget.imguiLastFragTm = 0;
            }
            ImGui::Text("RuntimeHeap fragmentation: %.1f%%", gMemBudget.imguiHeapFragmentation);
            ImGui::TextColored(gMemBudget.imguiHeapValidate ? ImGui::ColorToImVec4(COLOR_GREEN) : ImGui::ColorToImVec4(COLOR_RED), 
                               "RuntimeHeap validate: %s", gMemBudget.imguiHeapValidate ? "Ok" : "Fail");

            ImGui::Text("InitHeapSize: %_$llu", stats.initHeapSize);
        }

        systemOpen = ImGui::CollapsingHeader("System/OS", nullptr, systemOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (systemOpen) {
            SysPrimitiveStats stats = GetSystemPrimitiveStats();
            ImGui::Text("Mutexes: %u", stats.numMutexes);
            ImGui::Text("Semaphores: %u", stats.numSemaphores);
            ImGui::Text("Signals: %u", stats.numSignals);
            ImGui::Text("Threads: %u", stats.numThreads);
            ImGui::Text("ThreadsStackMemory: %_$llu", stats.threadStackSize);
        }

        //
        ImGui::SetSetting("MemBudgets.VirtualMem", vmOpen);
        ImGui::SetSetting("MemBudgets.TransientAllocs", transientOpen);
        ImGui::SetSetting("MemBudgets.Jobs", jobsOpen);
        ImGui::SetSetting("MemBudgets.AssetManager", assetOpen);
        ImGui::SetSetting("MemBudgets.Graphics", gfxOpen);
        ImGui::SetSetting("MemBudgets.ImGui", imguiOpen);
        ImGui::SetSetting("MemBudgets.System", systemOpen);
    }
    ImGui::End();
}

void DebugHud::DrawDebugHud(float dt, bool *pOpen)
{
    const ImVec2 kDisplaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kDisplaySize.x*0.33f, 0), ImGuiCond_Always);
    const uint32 kWndFlags = ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar|
                             ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize;
    if (ImGui::Begin("Frame", pOpen, kWndFlags)) {
        _UpdateGraph(dt*1000.0f, DebugHudGraphType::FrameTime);
        _UpdateGraph(1.0f/dt, DebugHudGraphType::Fps);
        _UpdateGraph(Engine::GetEngineTimeMS(), DebugHudGraphType::CpuTime);
        _UpdateGraph(gfxGetRenderTimeNs()/1000000.0f, DebugHudGraphType::GpuTime);

        _DrawHudMenu();

        for (uint32 i = 0; i < uint32(DebugHudGraphType::_Count); i++) {
            if (gDebugHud.enabledGraphs[i])
                _DrawGraph(DebugHudGraphType(i));
        }

        if (gDebugHud.showMemStats)
            _DrawMemBudgets(dt, nullptr);
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
    App::RegisterEventsCallback(_QuickFrameInfoEventCallback);

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
    App::UnregisterEventsCallback(_QuickFrameInfoEventCallback);
    Log::UnregisterCallback(_StatusBarLogCallback);

    for (uint32 i = 0; i < uint32(DebugHudGraphType::_Count); i++) {
        ImGui::SetSetting(String32::Format("DebugHud.%s", DEBUGHUD_GRAPH_NAMES[i]).CStr(), gDebugHud.enabledGraphs[i]);
        gDebugHud.graphs[i].values.Free();
    }

    ImGui::SetSetting("DebugHud.MemStats", gDebugHud.showMemStats);
}

void DebugHud::RegisterMemoryStats(const char* name, DebugHudMemoryStatsCallback callback, void* userData)
{
    uint32 index = gDebugHud.memStats.items.FindIf([name](const DebugHudMemoryStatsItem& i) { return i.name == name; });
    if (index == -1) {
        DebugHudMemoryStatsItem item {
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
