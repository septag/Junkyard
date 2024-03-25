#include "FrameInfoHud.h"

#include "../Core/Log.h"
#include "../Core/System.h"
#include "../Core/MathTypes.h"
#include "../Core/MathScalar.h"
#include "../Core/Blobs.h"
#include "../Common/Application.h"
#include "../Common/JunkyardSettings.h"
#include "../ImGui/ImGuiWrapper.h"
#include "../Graphics/Graphics.h"
#include "../Engine.h"

struct FrameInfoContext
{
    RingBlob frameTimes;
    uint32 targetFps;
    String<256> statusText;
    Color statusColor;
    float statusShowTime;
    SpinLockMutex statusLock;
};

static FrameInfoContext gFrameInfo;

void frameInfoRender(float dt, bool *pOpen)
{
    const ImVec4& kTextColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const Color kTextColorU32 = Color(&kTextColor.x);
    const float kFontSize = ImGui::GetFontSize();
    const float kLineSize = ImGui::GetFrameHeightWithSpacing();
    const ImVec2 kDisplaySize = ImGui::GetIO().DisplaySize;
    ImGuiStyle& kStyle = ImGui::GetStyle();

    RingBlob& frameTimes = gFrameInfo.frameTimes;

    // First-time initialization
    if (gFrameInfo.targetFps == 0)
        gFrameInfo.targetFps = appGetDisplayInfo().refreshRate;

    if (frameTimes.Capacity() == 0) 
        frameTimes.Reserve(sizeof(float)*gFrameInfo.targetFps*2);
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
        float* values = tmpAlloc.MallocTyped<float>(gFrameInfo.targetFps*2);
        uint32 valuesRead = (uint32)frameTimes.Peek(values, sizeof(float)*gFrameInfo.targetFps*2)/sizeof(float);

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

        uint32 targetFps = settingsGet().graphics.enableVsync ? gFrameInfo.targetFps : uint32(1.0f / avgFt);
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
        SpinLockMutexScope lock(gFrameInfo.statusLock);
        ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();
        float y = kDisplaySize.y - kLineSize;
        gFrameInfo.statusShowTime += dt;
        float alpha = mathLinearStep(gFrameInfo.statusShowTime, 0, 5.0f);
        alpha = 1.0f - mathGain(alpha, 0.05f);
        gFrameInfo.statusColor.a = uint8(alpha * 255.0f);

        fgDrawList->AddText(ImVec2(kStyle.WindowPadding.x, y), gFrameInfo.statusColor.n, gFrameInfo.statusText.CStr());
    }
}

static void frameInfoLogCallback(const LogEntry& entry, void*)
{
    SpinLockMutexScope lock(gFrameInfo.statusLock);

    gFrameInfo.statusText = entry.text;  
    gFrameInfo.statusShowTime = 0;

    switch (entry.type) {
    case LogLevel::Info:	gFrameInfo.statusColor = kColorWhite; break;
    case LogLevel::Debug:	gFrameInfo.statusColor = Color(0, 200, 200); break;
    case LogLevel::Verbose:	gFrameInfo.statusColor = Color(128, 128, 128); break;
    case LogLevel::Warning:	gFrameInfo.statusColor = kColorYellow; break;
    case LogLevel::Error:	gFrameInfo.statusColor = kColorRed; break;
    default:			    gFrameInfo.statusColor = kColorWhite; break;
    }
}

static void frameInfoEventsCallback(const AppEvent& ev, [[maybe_unused]] void* userData)
{
    if (ev.type == AppEventType::DisplayUpdated) {
        gFrameInfo.targetFps = appGetDisplayInfo().refreshRate;
        gFrameInfo.frameTimes.Free();
        gFrameInfo.frameTimes.Reserve(gFrameInfo.targetFps*sizeof(float)*2);
    }
}

void frameInfoInitialize()
{
    logRegisterCallback(frameInfoLogCallback, nullptr);
    appRegisterEventsCallback(frameInfoEventsCallback);
}

void frameInfoRelease()
{
    appUnregisterEventsCallback(frameInfoEventsCallback);
    logUnregisterCallback(frameInfoLogCallback);
    gFrameInfo.frameTimes.Free();
}
