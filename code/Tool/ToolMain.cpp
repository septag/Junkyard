#include <stdio.h>

#include "../Core/Settings.h"
#include "../Core/Log.h"
#include "../Core/TracyHelper.h"
#include "../Core/System.h"

#include "../VirtualFS.h"
#include "../Application.h"
#include "../Engine.h"

#include "../UnityBuild.inl"

struct AppImpl : AppCallbacks
{
    bool Initialize() override
    {
        // Mount file-systems before initializing engine
        if (settingsGetEngine().connectToServer) {
            vfsMountRemote("data", true);
            vfsMountRemote("code", true);
        }
        else {        
            vfsMountLocal("data", "data", true);
            vfsMountLocal("code", "code", true);
        }

        if (!engineInitialize())
            return false;

        engineRegisterShortcut("ESC", [](void*) { appQuit(); });

        logInfo("Ready. Waiting for incoming connections ...");

        return true;
    };
    
    void Cleanup() override
    {
        engineRelease();
    };
    
    void Update(fl32 dt) override
    {
        engineBeginFrame(dt);
        threadSleep(16);
        engineEndFrame(dt);
    }
    
    void OnEvent(const AppEvent&) override
    {
    }

};

int main(int argc, char* argv[])
{
    settingsInitialize(SettingsAll {
        .engine = {
            .logLevel = SettingsEngine::LogLevel::Debug,
        },
        .graphics = {
            .headless = true
        }
    });

    settingsLoadFromCommandLine(argc, argv);

    static AppImpl impl;
    appInitialize(AppDesc { 
        .callbacks = &impl, 
        .windowTitle = "JunkyardTool" 
    });

    settingsRelease();
    return 0;
}