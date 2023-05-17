#include "ImGuiTools.h"

#include "../Core/Settings.h"
#include "../Core/String.h"
#include "../Core/Buffers.h"
#include "../Core/Log.h"
#include "../Core/Atomic.h"
#include "../Core/Jobs.h"
#include "../Core/System.h"
#include "../Core/MathTypes.h"
#include "../Core/MathScalar.h"

#include "../Graphics/ImGuiWrapper.h"
#include "../Graphics/Graphics.h"

#include "../AssetManager.h"
#include "../Engine.h"
#include "../Application.h"
#include "../JunkyardSettings.h"

inline constexpr float kImGuiFragUpdateInterval = 1.0f;

struct ImGuiBudgetHubState
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

struct ImGuiQuickInfoState
{
    RingBuffer frameTimes;
    uint32 targetFps;
    String<256> statusText;
    Color statusColor;
    float statusShowTime;
    AtomicLock statusLock;

    // TODO: take this out of dtor?
    ~ImGuiQuickInfoState()
    {
        frameTimes.Free();
    }
};

static ImGuiBudgetHubState gImGuiBudgetHubState;
static ImGuiQuickInfoState gImGuiQuickInfoState;

template <typename _T> float imguiDivideInt(_T a, _T b);
template <> float imguiDivideInt(uint32 a, uint32 b) { return static_cast<float>(double(a)/double(b)); }
template <> float imguiDivideInt(size_t a, size_t b) { return static_cast<float>(double(a)/double(b)); }

void imguiBudgetHub(float dt, bool* pOpen)
{
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
    
    if (ImGui::Begin("Budgets", pOpen, 0)) {
        IMGUI_ALPHA_CONTROL(Budgets);

        imguiLabel("InitHeap Commited", "%_$llu", engineGetInitHeap()->GetCommitedSize());

        bool transientOpen = strToBool(imguiGetSetting("Budgets.TransientAllocs"));
        bool jobsOpen = strToBool(imguiGetSetting("Budgets.Jobs"));
        bool assetOpen = strToBool(imguiGetSetting("Budgets.AssetManager"));
        bool gfxOpen = strToBool(imguiGetSetting("Budgets.Graphics"));
        bool imguiOpen = strToBool(imguiGetSetting("Budgets.ImGui"));

        transientOpen = ImGui::CollapsingHeader("Transient Allocators", nullptr, transientOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (transientOpen) {
            MemTransientAllocatorStats frameStats = memFrameGetStats();
            ImGui::TextColored(kTextColor, "FrameAlloc");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(frameStats.curPeak, frameStats.maxPeak), ImVec2(-1.0f, 0),
                               String32::Format("%_$llu/%_$llu", frameStats.curPeak, frameStats.maxPeak).CStr());

            MemTempAllocator tmpAlloc;
            MemTransientAllocatorStats* tempStats;
            uint32 numTempStats;
            memTempGetStats(&tmpAlloc, &tempStats, &numTempStats);
            for (uint32 i = 0; i < numTempStats; i++) {
                if (ImGui::TreeNodeEx(String64::Format("#%u: %s (tId: %u)", i+1, tempStats[i].threadName, tempStats[i].threadId).CStr(), 0)) {
                    ImGui::TextColored(kTextColor, "TempAlloc");
                    ImGui::SameLine();
                    ImGui::ProgressBar(imguiDivideInt(tempStats[i].curPeak, tempStats[i].maxPeak), ImVec2(-1.0f, 0),
                                       String32::Format("%_$llu/%_$llu", tempStats[i].curPeak, tempStats[i].maxPeak).CStr());
                    ImGui::TreePop();
                }
            }
        }

        jobsOpen = ImGui::CollapsingHeader("Jobs", nullptr, jobsOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (jobsOpen) {
            JobsBudgetStats stats;
            jobsGetBudgetStats(&stats);
            
            ImGui::TextColored(kTextColor, "Busy LongTask Threads:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.numBusyLongThreads, stats.maxThreads), ImVec2(-1.0f, 0), 
                               String32::Format("%u/%u", stats.numBusyLongThreads, stats.maxThreads).CStr());

            ImGui::TextColored(kTextColor, "Busy ShortTask Threads:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.numBusyShortThreads, stats.maxThreads), ImVec2(-1.0f, 0), 
                               String32::Format("%u/%u", stats.numBusyShortThreads, stats.maxThreads).CStr());

            ImGui::TextColored(kTextColor, "Fibers:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.numFibers, stats.maxFibers), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numFibers, stats.maxFibers).CStr());
            
            ImGui::TextColored(kTextColor, "Jobs:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.numJobs, stats.maxJobs), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numJobs, stats.maxJobs).CStr());

            ImGui::TextColored(kTextColor, "FiberHeap:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.fiberHeapSize, stats.fiberHeapMax), ImVec2(-1.0f, 0),
                               String32::Format("%_$llu/%_$llu", stats.fiberHeapSize, stats.fiberHeapMax).CStr());

            imguiLabel("InitHeapSize", "%_$llu", stats.initHeapSize);
        }

        assetOpen = ImGui::CollapsingHeader("AssetManager", nullptr, assetOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (assetOpen) {
            AssetBudgetStats stats;
            assetGetBudgetStats(&stats);

            ImGui::TextColored(kTextColor, "Assets:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.numAssets, stats.maxAssets), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numAssets, stats.maxAssets).CStr());

            ImGui::TextColored(kTextColor, "Barriers:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.numBarriers, stats.maxBarriers), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numBarriers, stats.maxBarriers).CStr());

            ImGui::TextColored(kTextColor, "Garbage:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.numGarbage, stats.maxGarbage), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numGarbage, stats.maxGarbage).CStr());

            ImGui::TextColored(kTextColor, "RuntimeHeap:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.runtimeHeapSize, stats.runtimeHeapMax), ImVec2(-1.0f, 0),
                               String32::Format("%_$llu/%_$llu", stats.runtimeHeapSize, stats.runtimeHeapMax).CStr());

            gImGuiBudgetHubState.assetLastFragTm += dt;
            if (gImGuiBudgetHubState.assetLastFragTm >= kImGuiFragUpdateInterval) {
                gImGuiBudgetHubState.assetHeapFragmentation = stats.runtimeHeap->CalculateFragmentation() * 100.0f;
                gImGuiBudgetHubState.assetHeapValidate = stats.runtimeHeap->Validate();
                gImGuiBudgetHubState.assetLastFragTm = 0;
            }
            imguiLabel("RuntimeHeap fragmentation:", "%.1f%%", gImGuiBudgetHubState.assetHeapFragmentation);

            imguiLabel(Color(kTextColor.x, kTextColor.y, kTextColor.z, kTextColor.w), 
                       gImGuiBudgetHubState.assetHeapValidate ? kColorGreen : kColorRed, 
                       "RuntimeHeap validate:",
                       gImGuiBudgetHubState.assetHeapValidate ? "Ok" : "Fail");

            imguiLabel("InitHeapSize", "%_$llu", stats.initHeapSize);
        }

        gfxOpen = ImGui::CollapsingHeader("Graphics", nullptr, gfxOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (gfxOpen) {
            GfxBudgetStats stats;
            gfxGetBudgetStats(&stats);

            ImGui::TextColored(kTextColor, "Buffers:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.numBuffers, stats.maxBuffers), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numBuffers, stats.maxBuffers).CStr());

            ImGui::TextColored(kTextColor, "Images:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.numImages, stats.maxImages), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numImages, stats.maxImages).CStr());

            ImGui::TextColored(kTextColor, "DescriptorSets:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.numDescriptorSets, stats.maxDescriptorSets), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numDescriptorSets, stats.maxDescriptorSets).CStr());

            ImGui::TextColored(kTextColor, "Pipelines:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.numPipelines, stats.maxPipelines), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numPipelines, stats.maxPipelines).CStr());
            
            ImGui::TextColored(kTextColor, "PipelineLayouts:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.numPipelineLayouts, stats.maxPipelineLayouts), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numPipelineLayouts, stats.maxPipelineLayouts).CStr());

            ImGui::TextColored(kTextColor, "Garbage:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.numGarbage, stats.maxGarbage), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.numGarbage, stats.maxGarbage).CStr());

            if (ImGui::TreeNodeEx("Descriptors", ImGuiTreeNodeFlags_DefaultOpen)) {
                const GfxBudgetStats::DescriptorBudgetStats& dstats = stats.descriptors;
                ImGui::TextColored(kTextColor, "UniformBuffers:");
                ImGui::SameLine();
                ImGui::ProgressBar(imguiDivideInt(dstats.numUniformBuffers, dstats.maxUniformBuffers), ImVec2(-1.0f, 0),
                                   String32::Format("%u/%u", dstats.numUniformBuffers, dstats.maxUniformBuffers).CStr());

                ImGui::TextColored(kTextColor, "SampledImages:");
                ImGui::SameLine();
                ImGui::ProgressBar(imguiDivideInt(dstats.numSampledImages, dstats.maxSampledImages), ImVec2(-1.0f, 0),
                                   String32::Format("%u/%u", dstats.numSampledImages, dstats.maxSampledImages).CStr());

                ImGui::TextColored(kTextColor, "Samplers:");
                ImGui::SameLine();
                ImGui::ProgressBar(imguiDivideInt(dstats.numSamplers, dstats.maxSamplers), ImVec2(-1.0f, 0),
                                   String32::Format("%u/%u", dstats.numSamplers, dstats.maxSamplers).CStr());

                ImGui::TextColored(kTextColor, "CombinedImageSamplers:");
                ImGui::SameLine();
                ImGui::ProgressBar(imguiDivideInt(dstats.numCombinedImageSamplers, dstats.maxCombinedImageSamplers), ImVec2(-1.0f, 0),
                                   String32::Format("%u/%u", dstats.numCombinedImageSamplers, dstats.maxCombinedImageSamplers).CStr());

                ImGui::TreePop();
            }
            
            ImGui::TextColored(kTextColor, "RuntimeHeap:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.runtimeHeapSize, stats.runtimeHeapMax), ImVec2(-1.0f, 0),
                               String32::Format("%_$llu/%_$llu", stats.runtimeHeapSize, stats.runtimeHeapMax).CStr());

            gImGuiBudgetHubState.gfxLastFragTm += dt;
            if (gImGuiBudgetHubState.gfxLastFragTm >= kImGuiFragUpdateInterval) {
                gImGuiBudgetHubState.gfxHeapFragmentation = stats.runtimeHeap->CalculateFragmentation() * 100.0f;
                gImGuiBudgetHubState.gfxHeapValidate = stats.runtimeHeap->Validate();
                gImGuiBudgetHubState.gfxLastFragTm = 0;
            }
            imguiLabel("RuntimeHeap fragmentation:", "%.1f%%", gImGuiBudgetHubState.gfxHeapFragmentation);
            imguiLabel(Color(kTextColor.x, kTextColor.y, kTextColor.z, kTextColor.w), 
                       gImGuiBudgetHubState.gfxHeapValidate ? kColorGreen : kColorRed, 
                       "RuntimeHeap validate:",
                       gImGuiBudgetHubState.gfxHeapValidate ? "Ok" : "Fail");

            imguiLabel("InitHeapSize", "%_$llu", stats.initHeapSize);
        }

        imguiOpen = ImGui::CollapsingHeader("ImGui", nullptr, imguiOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (imguiOpen) {
            ImGuiBudgetStats stats;
            imguiGetBudgetStats(&stats);

            ImGui::TextColored(kTextColor, "Vertices:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.lastFrameVertices, stats.maxVertices), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.lastFrameVertices, stats.maxVertices).CStr());

            ImGui::TextColored(kTextColor, "Indices:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.lastFrameIndices, stats.maxIndices), ImVec2(-1.0f, 0),
                               String32::Format("%u/%u", stats.lastFrameIndices, stats.maxIndices).CStr());
            
            ImGui::TextColored(kTextColor, "RuntimeHeap:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.runtimeHeapSize, stats.runtimeHeapMax), ImVec2(-1.0f, 0),
                               String32::Format("%_$llu/%_$llu", stats.runtimeHeapSize, stats.runtimeHeapMax).CStr());

            gImGuiBudgetHubState.imguiLastFragTm += dt;
            if (gImGuiBudgetHubState.imguiLastFragTm >= kImGuiFragUpdateInterval) {
                gImGuiBudgetHubState.imguiHeapFragmentation = stats.runtimeHeap->CalculateFragmentation() * 100.0f;
                gImGuiBudgetHubState.imguiHeapValidate = stats.runtimeHeap->Validate();
                gImGuiBudgetHubState.imguiLastFragTm = 0;
            }
            imguiLabel("RuntimeHeap fragmentation:", "%.1f%%", gImGuiBudgetHubState.imguiHeapFragmentation);
            imguiLabel(Color(kTextColor.x, kTextColor.y, kTextColor.z, kTextColor.w), 
                       gImGuiBudgetHubState.imguiHeapValidate ? kColorGreen : kColorRed, 
                       "RuntimeHeap validate:",
                       gImGuiBudgetHubState.imguiHeapValidate ? "Ok" : "Fail");

            imguiLabel("InitHeapSize", "%_$llu", stats.initHeapSize);
        }

        //
        imguiSetSetting("Budgets.TransientAllocs", transientOpen);
        imguiSetSetting("Budgets.Jobs", jobsOpen);
        imguiSetSetting("Budgets.AssetManager", assetOpen);
        imguiSetSetting("Budgets.Graphics", gfxOpen);
        imguiSetSetting("Budgets.ImGui", imguiOpen);
    }
    ImGui::End();
}

void imguiQuickInfoHud(float dt, bool *pOpen)
{
    const ImVec4& kTextColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const Color kTextColorU32 = Color(&kTextColor.x);
    const float kFontSize = ImGui::GetFontSize();
    const float kLineSize = ImGui::GetFrameHeightWithSpacing();
    const ImVec2 kDisplaySize = ImGui::GetIO().DisplaySize;
    ImGuiStyle& kStyle = ImGui::GetStyle();

    RingBuffer& frameTimes = gImGuiQuickInfoState.frameTimes;

    // First-time initialization
    if (gImGuiQuickInfoState.targetFps == 0)
        gImGuiQuickInfoState.targetFps = appGetDisplayInfo().refreshRate;

    if (frameTimes.Capacity() == 0) 
        frameTimes.Reserve(sizeof(float)*gImGuiQuickInfoState.targetFps*2);
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
        float* values = tmpAlloc.MallocTyped<float>(gImGuiQuickInfoState.targetFps*2);
        uint32 valuesRead = (uint32)frameTimes.Peek(values, sizeof(float)*gImGuiQuickInfoState.targetFps*2)/sizeof(float);

        static float elapsed = 0;
        static uint64 frameIdx = 0;
        static uint32 fps = 0;
        
        if (frameIdx == 0)
            frameIdx = engineFrameIndex();

        elapsed += dt;
        if (elapsed >= 1.0f) {
            fps = uint32(engineFrameIndex() - frameIdx);
            frameIdx = engineFrameIndex();
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

        uint32 targetFps = settingsGet().graphics.enableVsync ? gImGuiQuickInfoState.targetFps : uint32(1.0f / avgFt);
        uint32 warningFps = uint32(float(targetFps) * 0.8f);
        uint32 lowFps = targetFps / 2;

        Color fpsColor, cpuColor, gpuColor;
        if (fps <= lowFps)             fpsColor = kColorRed;
        else if (fps <= warningFps)    fpsColor = kColorYellow;
        else                           fpsColor = kColorGreen;

        float cpuTimeMs = engineGetCpuFrameTimeMS();
        float gpuTimeMs = gfxGetRenderTimeNs()/1000000.0f;
        float warnTimeMs = 1000.0f / float(warningFps);
        float lowTimeMs = 1000.0f / float(lowFps);

        if (cpuTimeMs >= lowTimeMs)         cpuColor = kColorRed;
        else if (cpuTimeMs >= warnTimeMs)   cpuColor = kColorYellow;
        else                                cpuColor = kColorGreen;

        if (gpuTimeMs >= lowTimeMs)         gpuColor = kColorRed;
        else if (gpuTimeMs >= warnTimeMs)   gpuColor = kColorYellow;
        else                                gpuColor = kColorGreen;
                        
        imguiLabel(kTextColorU32, fpsColor, "Fps", "%u", fps);
        imguiLabel(kTextColorU32, fpsColor, "AvgFt", "%.1fms", avgFt*1000.0f);
        imguiLabel(kTextColorU32, fpsColor, "MinFt", "%.1fms", minFt*1000.0f);
        imguiLabel(kTextColorU32, fpsColor, "MaxFt", "%.1fms", maxFt*1000.0f);
        imguiLabel(kTextColorU32, cpuColor, "Cpu", "%.1fms", cpuTimeMs);
        imguiLabel(kTextColorU32, gpuColor, "Gpu", "%.1fms", gpuTimeMs);
        
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(ImGui::GetWindowWidth() - kStyle.WindowPadding.x*2 - ImGui::GetCursorPos().x);

        float maxDt;
        float minDt;
        if (settingsGet().graphics.enableVsync) {
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

    // Status text at the bottom
    {
        AtomicLockScope lock(gImGuiQuickInfoState.statusLock);
        ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();
        float y = kDisplaySize.y - kLineSize;
        gImGuiQuickInfoState.statusShowTime += dt;
        float alpha = mathLinearStep(gImGuiQuickInfoState.statusShowTime, 0, 5.0f);
        alpha = 1.0f - mathGain(alpha, 0.05f);
        gImGuiQuickInfoState.statusColor.a = uint8(alpha * 255.0f);

        fgDrawList->AddText(ImVec2(kStyle.WindowPadding.x, y), gImGuiQuickInfoState.statusColor.n, gImGuiQuickInfoState.statusText.CStr());
    }
}

void _private::imguiQuickInfoHud_SetTargetFps(uint32 targetFps)
{
    ASSERT(targetFps);

    gImGuiQuickInfoState.targetFps = targetFps;
    gImGuiQuickInfoState.frameTimes.Free();
    gImGuiQuickInfoState.frameTimes.Reserve(targetFps*sizeof(float)*2);
}

void _private::imguiQuickInfoHud_Log(const LogEntry& entry, void*)
{
    AtomicLockScope lock(gImGuiQuickInfoState.statusLock);

    gImGuiQuickInfoState.statusText = entry.text;  
    gImGuiQuickInfoState.statusShowTime = 0;

    switch (entry.type) {
    case LogLevel::Info:	gImGuiQuickInfoState.statusColor = kColorWhite; break;
    case LogLevel::Debug:	gImGuiQuickInfoState.statusColor = Color(0, 200, 200); break;
    case LogLevel::Verbose:	gImGuiQuickInfoState.statusColor = Color(128, 128, 128); break;
    case LogLevel::Warning:	gImGuiQuickInfoState.statusColor = kColorYellow; break;
    case LogLevel::Error:	gImGuiQuickInfoState.statusColor = kColorRed; break;
    default:			    gImGuiQuickInfoState.statusColor = kColorWhite; break;
    }
}