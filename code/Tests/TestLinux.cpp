#include "../Core/Settings.h"
#include "../Core/Log.h"
#include "../Core/MathScalar.h"

#include "../Common/Application.h"
#include "../Common/Camera.h"
#include "../Common/JunkyardSettings.h"
#include "../Common/VirtualFS.h"

#include "../Engine.h"

#include "../Graphics/GfxBackend.h"

#include "../ImGui/ImGuiMain.h"

#include "../UnityBuild.inl"

struct AppImpl : AppCallbacks
{
    bool Initialize() override
    {
        Vfs::HelperMountDataAndShaders(false, "data");
        if (!Engine::Initialize())
            return false;

        return true;
    }
    
    void Cleanup() override
    {
        Engine::Release();
    }

    void Update(fl32 dt) override
    {
        Engine::BeginFrame(dt);
        GfxCommandBuffer cmd = GfxBackend::BeginCommandBuffer(GfxQueueType::Graphics);
        
        static float f = 0;
        f += dt;
        cmd.ClearSwapchainColor(Float4(M::Sin(f) * 0.5f + 0.5f, 0, 0, 1.0f));
        
        if (ImGui::IsEnabled()) {
            ImGui::DrawFrame(cmd);
        }
        
        GfxBackend::EndCommandBuffer(cmd);
        GfxBackend::SubmitQueue(GfxQueueType::Graphics);
        Engine::EndFrame();
    }
    
    void OnEvent(const AppEvent& ev) override
    {
    }

    static void CreateGraphicsResources(void* userData)
    {
    }

    void ReleaseGraphicsObjects()
    {
    }
};

int Main(int argc, char* argv[])
{
    SettingsJunkyard::Initialize({
        .graphics = {
            .listExtensions = true
        }
    });
    Settings::InitializeFromCommandLine(argc, argv);
    LOG_DEBUG("Initializing engine");
    
    static AppImpl impl;
    App::Run(AppDesc { 
        .callbacks = &impl, 
        .windowTitle = "Junkyard: Linux Test",
    });

    Settings::Release();
    return 0;
}
