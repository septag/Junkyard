#include "BudgetViewer.h"

#include "../Core/Jobs.h"
#include "../Assets/AssetManager.h"

#include "../Graphics/Graphics.h"

#include "../Application.h"
#include "../Engine.h"
#include "../JunkyardSettings.h"

#include "../ImGui/ImGuiWrapper.h"

inline constexpr float kImGuiFragUpdateInterval = 1.0f;

struct BudgetViewerContext
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

static BudgetViewerContext gBudgetViewer;

template <typename _T> float imguiDivideInt(_T a, _T b);
template <> float imguiDivideInt(uint32 a, uint32 b) { return static_cast<float>(double(a)/double(b)); }
template <> float imguiDivideInt(size_t a, size_t b) { return static_cast<float>(double(a)/double(b)); }

void budgetViewerRender(float dt, bool* pOpen)
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
            ImGui::ProgressBar(imguiDivideInt(stats.numBusyLongThreads, stats.maxLongTaskThreads), ImVec2(-1.0f, 0), 
                               String32::Format("%u/%u", stats.numBusyLongThreads, stats.maxLongTaskThreads).CStr());

            ImGui::TextColored(kTextColor, "Busy ShortTask Threads:");
            ImGui::SameLine();
            ImGui::ProgressBar(imguiDivideInt(stats.numBusyShortThreads, stats.maxShortTaskThreads), ImVec2(-1.0f, 0), 
                               String32::Format("%u/%u", stats.numBusyShortThreads, stats.maxShortTaskThreads).CStr());

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

            gBudgetViewer.assetLastFragTm += dt;
            if (gBudgetViewer.assetLastFragTm >= kImGuiFragUpdateInterval) {
                gBudgetViewer.assetHeapFragmentation = stats.runtimeHeap->CalculateFragmentation() * 100.0f;
                gBudgetViewer.assetHeapValidate = stats.runtimeHeap->Validate();
                gBudgetViewer.assetLastFragTm = 0;
            }
            imguiLabel("RuntimeHeap fragmentation:", "%.1f%%", gBudgetViewer.assetHeapFragmentation);

            imguiLabel(Color(kTextColor.x, kTextColor.y, kTextColor.z, kTextColor.w), 
                       gBudgetViewer.assetHeapValidate ? kColorGreen : kColorRed, 
                       "RuntimeHeap validate:",
                       gBudgetViewer.assetHeapValidate ? "Ok" : "Fail");

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

            gBudgetViewer.gfxLastFragTm += dt;
            if (gBudgetViewer.gfxLastFragTm >= kImGuiFragUpdateInterval) {
                gBudgetViewer.gfxHeapFragmentation = stats.runtimeHeap->CalculateFragmentation() * 100.0f;
                gBudgetViewer.gfxHeapValidate = stats.runtimeHeap->Validate();
                gBudgetViewer.gfxLastFragTm = 0;
            }
            imguiLabel("RuntimeHeap fragmentation:", "%.1f%%", gBudgetViewer.gfxHeapFragmentation);
            imguiLabel(Color(kTextColor.x, kTextColor.y, kTextColor.z, kTextColor.w), 
                       gBudgetViewer.gfxHeapValidate ? kColorGreen : kColorRed, 
                       "RuntimeHeap validate:",
                       gBudgetViewer.gfxHeapValidate ? "Ok" : "Fail");

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

            gBudgetViewer.imguiLastFragTm += dt;
            if (gBudgetViewer.imguiLastFragTm >= kImGuiFragUpdateInterval) {
                gBudgetViewer.imguiHeapFragmentation = stats.runtimeHeap->CalculateFragmentation() * 100.0f;
                gBudgetViewer.imguiHeapValidate = stats.runtimeHeap->Validate();
                gBudgetViewer.imguiLastFragTm = 0;
            }
            imguiLabel("RuntimeHeap fragmentation:", "%.1f%%", gBudgetViewer.imguiHeapFragmentation);
            imguiLabel(Color(kTextColor.x, kTextColor.y, kTextColor.z, kTextColor.w), 
                       gBudgetViewer.imguiHeapValidate ? kColorGreen : kColorRed, 
                       "RuntimeHeap validate:",
                       gBudgetViewer.imguiHeapValidate ? "Ok" : "Fail");

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

