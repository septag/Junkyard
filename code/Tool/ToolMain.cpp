#include <stdio.h>

#include "../Core/Settings.h"
#include "../Core/Log.h"
#include "../Core/TracyHelper.h"
#include "../Core/System.h"

#include "../Common/VirtualFS.h"
#include "../Common/Application.h"
#include "../Common/JunkyardSettings.h"

#include "../Engine.h"

#include "../UnityBuild.inl"

#include "../Tool/Console.h"

struct AppImpl final : AppCallbacks
{
    bool Initialize() override
    {
        // Mount file-systems before initializing engine
        if (SettingsJunkyard::Get().engine.connectToServer) {
            Vfs::MountRemote("data", true);
            Vfs::MountRemote("code", true);
        }
        else {        
            Vfs::MountLocal("data", "data", true);
            Vfs::MountLocal("code", "code", true);
        }

        if (!Engine::Initialize())
            return false;

        Engine::RegisterShortcut("ESC", [](void*) { App::Quit(); });

        LOG_INFO("Ready.");

        return true;
    };
    
    void Cleanup() override
    {
        Engine::Release();
    };
    
    void Update(fl32 dt) override
    {
        Engine::BeginFrame(dt);
        Thread::Sleep(16);
        Engine::EndFrame(dt);
    }
    
    void OnEvent(const AppEvent&) override
    {
    }

};

int main(int argc, char* argv[])
{
    SettingsJunkyard::Initialize(SettingsJunkyard {
        .graphics = {
            .headless = true
        },
        .tooling = {
            .enableServer = true
        }
    });

    settingsInitializeFromCommandLine(argc, argv);

    static AppImpl impl;
    App::Run(AppDesc { 
        .callbacks = &impl, 
        .windowTitle = "JunkyardTool" 
    });

    settingsRelease();
    return 0;
}