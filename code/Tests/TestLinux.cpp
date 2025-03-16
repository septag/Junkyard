#include "../Core/Settings.h"
#include "../Core/Log.h"

#include "../Common/Application.h"
#include "../Common/Camera.h"
#include "../Common/JunkyardSettings.h"

#include "../UnityBuild.inl"

struct AppImpl : AppCallbacks
{
    bool Initialize() override
    {
        return true;
    }
    
    void Cleanup() override
    {
    }

    void Update(fl32 dt) override
    {
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
    SettingsJunkyard::Initialize({});
    Settings::InitializeFromCommandLine(argc, argv);

    LOG_DEBUG("Initializing engine.");
    
    static AppImpl impl;
    App::Run(AppDesc { 
        .callbacks = &impl, 
        .windowTitle = "Junkyard: Linux Test",
    });

    Settings::Release();
    return 0;
}
