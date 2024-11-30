#include "../Core/Settings.h"
#include "../Core/Log.h"
#include "../Core/TracyHelper.h"
#include "../Core/Jobs.h"
#include "../Core/MathAll.h"
#include "../Core/System.h"
#include "../Core/Hash.h"

#include "../Common/VirtualFS.h"
#include "../Common/Application.h"
#include "../Common/Camera.h"
#include "../Common/JunkyardSettings.h"

#include "../Assets/AssetManager.h"

#include "../DebugTools/DebugDraw.h"
#include "../DebugTools/DebugHud.h"

#include "../ImGui/ImGuiMain.h"
#include "../ImGui/ImGuizmo.h"

#include "../Assets/Model.h"
#include "../Assets/Shader.h"

#include "../UnityBuild.inl"

#include "../Tool/Console.h"

#include "../Engine.h"


struct AppImpl final : AppCallbacks
{
    CameraFPS   mFpsCam;
    CameraOrbit mOrbitCam;
    Camera*     mCam;

    static void CreateGraphicsResources(void* userData)
    {
    }

    bool Initialize() override
    {
        Vfs::HelperMountDataAndShaders(SettingsJunkyard::Get().engine.connectToServer);

        if (!Engine::Initialize())
            return false;

        mFpsCam.SetLookAt(Float3(0, -2.0f, 3.0f), FLOAT3_ZERO);
        mOrbitCam.SetLookAt(Float3(0, -2.0f, 3.0f), FLOAT3_ZERO);
        mCam = &mFpsCam;

        Engine::RegisterShortcut("TAB", [](void* userData) {
            AppImpl* app = reinterpret_cast<AppImpl*>(userData);
            if (app->mCam == &app->mOrbitCam) {
                app->mFpsCam.SetViewMat(app->mCam->GetViewMat());
                app->mCam = &app->mFpsCam;
            }
            else {
                app->mCam = &app->mOrbitCam;
            }
        }, this);

        LOG_INFO("Use right mouse button to rotate camera. And [TAB] to switch between Orbital and FPS (WASD) camera");

        return true;
    };
    
    void Cleanup() override
    {
        ReleaseGraphicsResources();

        Engine::Release();
    };

    void Update(fl32 dt) override
    {
        PROFILE_ZONE();

        mCam->HandleMovementKeyboard(dt, 100.0f, 5.0f);

        Engine::BeginFrame(dt);

        Engine::EndFrame();
    }
    
    void OnEvent(const AppEvent& ev) override
    {
        switch (ev.type) {
        case AppEventType::Resized:
            break;
        default:
            break;
        }

        //if (!ImGui::IsAnyItemHovered() && !ImGui::GetIO().WantCaptureMouse && !ImGuizmo::IsOver())
        //    mCam->HandleRotationMouse(ev, 0.2f, 0.1f);
    }

    void ReleaseGraphicsResources()
    {
    }
};

int Main(int argc, char* argv[])
{
    SettingsJunkyard initSettings {
        .app = {
            .appName = "TestBasicGfx"
        }
    };
    SettingsJunkyard::Initialize(initSettings);

    #if PLATFORM_ANDROID
    Settings::InitializeFromAndroidAsset(App::AndroidGetAssetManager(), "Settings.ini");
    #else
    Settings::InitializeFromCommandLine(argc, argv);
    #endif

    static AppImpl impl;
    App::Run(AppDesc { 
        .callbacks = &impl, 
        .windowTitle = "Junkyard: Basic Graphics Backend test"
    });

    Settings::Release();
    return 0;
}

