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

struct MemBudgetWindow
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

struct FrameInfoContext
{
    RingBlob frameTimes;
    uint32 targetFps;
    String<256> statusText;
    Color statusColor;
    float statusShowTime;
    SpinLockMutex statusLock;
};

static MemBudgetWindow gMemBudget;
static FrameInfoContext gQuickFrameInfo;

namespace DebugHud
{
void DrawMemBudgets(float dt, bool* pOpen)
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

        bool vmOpen = strToBool(ImGui::GetSetting("MemBudgets.VirtualMem"));
        bool transientOpen = strToBool(ImGui::GetSetting("MemBudgets.TransientAllocs"));
        bool jobsOpen = strToBool(ImGui::GetSetting("MemBudgets.Jobs"));
        bool assetOpen = strToBool(ImGui::GetSetting("MemBudgets.AssetManager"));
        bool gfxOpen = strToBool(ImGui::GetSetting("MemBudgets.Graphics"));
        bool imguiOpen = strToBool(ImGui::GetSetting("MemBudgets.ImGui"));
        bool systemOpen = strToBool(ImGui::GetSetting("MemBudgets.System"));

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

void DrawQuickFrameInfo(float dt, bool *pOpen)
{
    const float kFontSize = ImGui::GetFontSize();
    const float kLineSize = ImGui::GetFrameHeightWithSpacing();
    const ImVec2 kDisplaySize = ImGui::GetIO().DisplaySize;
    ImGuiStyle& kStyle = ImGui::GetStyle();

    RingBlob& frameTimes = gQuickFrameInfo.frameTimes;

    // First-time initialization
    if (gQuickFrameInfo.targetFps == 0)
        gQuickFrameInfo.targetFps = App::GetDisplayInfo().refreshRate;

    if (frameTimes.Capacity() == 0) 
        frameTimes.Reserve(sizeof(float)*gQuickFrameInfo.targetFps*2);
    // 

    if (frameTimes.ExpectWrite() < sizeof(float))
        frameTimes.Read<float>(nullptr);
    frameTimes.Write<float>(dt);

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kDisplaySize.x*0.33f, kLineSize*5), ImGuiCond_Always);
    const uint32 kWndFlags = ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar|
                             ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("Frame", pOpen, kWndFlags)) {
        ImGui::BeginTable("FrameTable", 2, ImGuiTableFlags_SizingFixedFit);
        ImGui::TableSetupColumn(nullptr, 0, kFontSize*10);
        ImGui::TableNextColumn();

        MemTempAllocator tmpAlloc;
        float* values = tmpAlloc.MallocTyped<float>(gQuickFrameInfo.targetFps*2);
        uint32 valuesRead = (uint32)frameTimes.Peek(values, sizeof(float)*gQuickFrameInfo.targetFps*2)/sizeof(float);

        static float elapsed = 0;
        static uint64 frameIdx = 0;
        static uint32 fps = 0;
        
        if (frameIdx == 0)
            frameIdx = Engine::GetFrameIndex();

        elapsed += dt;
        if (elapsed >= 1.0f) {
            fps = uint32(Engine::GetFrameIndex() - frameIdx);
            frameIdx = Engine::GetFrameIndex();
            elapsed = 0;
        }

        float avgFt = 0;
        float minFt = FLT_MAX;
        float maxFt = -FLT_MAX;
        for (uint32 i = 0; i < valuesRead; i++) {
            avgFt += values[i];
            if (values[i] < minFt)
                minFt = values[i];
            if (values[i] > maxFt)
                maxFt = values[i];
        }
        avgFt /= float(valuesRead);

        uint32 targetFps = SettingsJunkyard::Get().graphics.enableVsync ? gQuickFrameInfo.targetFps : uint32(1.0f / avgFt);
        uint32 warningFps = uint32(float(targetFps) * 0.8f);
        uint32 lowFps = targetFps / 2;

        Color fpsColor, cpuColor, gpuColor;
        if (fps <= lowFps)             fpsColor = COLOR_RED;
        else if (fps <= warningFps)    fpsColor = COLOR_YELLOW;
        else                           fpsColor = COLOR_GREEN;

        float cpuTimeMs = Engine::GetEngineTimeMS();
        float gpuTimeMs = gfxGetRenderTimeNs()/1000000.0f;
        float warnTimeMs = 1000.0f / float(warningFps);
        float lowTimeMs = 1000.0f / float(lowFps);

        if (cpuTimeMs >= lowTimeMs)         cpuColor = COLOR_RED;
        else if (cpuTimeMs >= warnTimeMs)   cpuColor = COLOR_YELLOW;
        else                                cpuColor = COLOR_GREEN;

        if (gpuTimeMs >= lowTimeMs)         gpuColor = COLOR_RED;
        else if (gpuTimeMs >= warnTimeMs)   gpuColor = COLOR_YELLOW;
        else                                gpuColor = COLOR_GREEN;
                        
        ImGui::TextColored(ImGui::ColorToImVec4(fpsColor), "Fps: %u", fps);
        ImGui::TextColored(ImGui::ColorToImVec4(fpsColor), "AvgFt: %.1fms", avgFt*1000.0f);
        ImGui::TextColored(ImGui::ColorToImVec4(fpsColor), "MinFt: %.1fms", minFt*1000.0f);
        ImGui::TextColored(ImGui::ColorToImVec4(fpsColor), "MaxFt: %.1fms", maxFt*1000.0f);
        ImGui::TextColored(ImGui::ColorToImVec4(cpuColor), "Cpu: %.1fms", cpuTimeMs);
        ImGui::TextColored(ImGui::ColorToImVec4(gpuColor), "Gpu: %.1fms", gpuTimeMs);
        
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(ImGui::GetWindowWidth() - kStyle.WindowPadding.x*2 - ImGui::GetCursorPos().x);

        float maxDt;
        float minDt;
        if (SettingsJunkyard::Get().graphics.enableVsync) {
            maxDt = 1.0f / float(warningFps);
            minDt = 1.0f / float(targetFps*2);
        }
        else {
            maxDt = 2.0f / float(targetFps);
            minDt = 0;
        }

        kStyle.Alpha = 0.7f;
        ImGui::PlotHistogram("##dt", values, (int)valuesRead, 0, nullptr, minDt, maxDt, ImVec2(0, kLineSize*2));
        kStyle.Alpha = 1.0f;
        ImGui::PopItemWidth();

        ImGui::EndTable();
    }
    ImGui::End();
}

static void StatusBarLogCallback(const LogEntry& entry, void*)
{
    SpinLockMutexScope lock(gQuickFrameInfo.statusLock);

    gQuickFrameInfo.statusText = entry.text;  
    gQuickFrameInfo.statusShowTime = 0;

    switch (entry.type) {
    case LogLevel::Info:	gQuickFrameInfo.statusColor = COLOR_WHITE; break;
    case LogLevel::Debug:	gQuickFrameInfo.statusColor = Color(0, 200, 200); break;
    case LogLevel::Verbose:	gQuickFrameInfo.statusColor = Color(128, 128, 128); break;
    case LogLevel::Warning:	gQuickFrameInfo.statusColor = COLOR_YELLOW; break;
    case LogLevel::Error:	gQuickFrameInfo.statusColor = COLOR_RED; break;
    default:			    gQuickFrameInfo.statusColor = COLOR_WHITE; break;
    }
}

void DrawStatusBar(float dt)
{
    ImGuiStyle& kStyle = ImGui::GetStyle();
    const ImVec2 kDisplaySize = ImGui::GetIO().DisplaySize;
    const float kLineSize = ImGui::GetFrameHeightWithSpacing();

    SpinLockMutexScope lock(gQuickFrameInfo.statusLock);
    ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();
    float y = kDisplaySize.y - kLineSize;
    gQuickFrameInfo.statusShowTime += dt;
    float alpha = mathLinearStep(gQuickFrameInfo.statusShowTime, 0, 5.0f);
    alpha = 1.0f - mathGain(alpha, 0.05f);
    gQuickFrameInfo.statusColor.a = uint8(alpha * 255.0f);

    fgDrawList->AddText(ImVec2(kStyle.WindowPadding.x, y), gQuickFrameInfo.statusColor.n, gQuickFrameInfo.statusText.CStr());
}

static void QuickFrameInfoEventCallback(const AppEvent& ev, [[maybe_unused]] void* userData)
{
    if (ev.type == AppEventType::DisplayUpdated) {
        gQuickFrameInfo.targetFps = App::GetDisplayInfo().refreshRate;
        gQuickFrameInfo.frameTimes.Free();
        gQuickFrameInfo.frameTimes.Reserve(gQuickFrameInfo.targetFps*sizeof(float)*2);
    }
}

void Initialize()
{
    Log::RegisterCallback(StatusBarLogCallback, nullptr);
    App::RegisterEventsCallback(QuickFrameInfoEventCallback);
}

void Release()
{
    App::UnregisterEventsCallback(QuickFrameInfoEventCallback);
    Log::UnregisterCallback(StatusBarLogCallback);
    gQuickFrameInfo.frameTimes.Free();
}

} // DebugHud
