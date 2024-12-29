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

#include "../Graphics/GfxBackend.h"


struct AppImpl final : AppCallbacks
{
    CameraFPS   mFpsCam;
    CameraOrbit mOrbitCam;
    Camera*     mCam;
    GfxImageHandle mImage;
    GfxPipelineLayoutHandle mPipelineLayout;
    GfxPipelineHandle mPipeline;
    AssetHandleShader mShader;
    AssetHandleModel mModel;

    struct Vertex 
    {
        Float3 pos;
        Float3 normal;
        Float2 uv;
    };

    static void CreateGraphicsResources(void* userData)
    {
        AppImpl* self = (AppImpl*)userData;

        {
            GfxImageDesc desc {
                .width = App::GetFramebufferWidth(),
                .height = App::GetFramebufferHeight(),
                .format = GfxFormat::R16G16B16A16_SFLOAT,
                .usageFlags = GfxImageUsageFlags::TransferSrc|GfxImageUsageFlags::Storage|GfxImageUsageFlags::TransferDst,
            };
            self->mImage = GfxBackend::CreateImage(desc);
        }

        const GfxPipelineLayoutDesc::Binding bindings[] {
            {
                .name = "MainImage",
                .type = GfxDescriptorType::StorageImage,
                .stagesUsed = GfxShaderStage::Compute,
            }
        };

        const GfxPipelineLayoutDesc layoutDesc {
            .numBindings = 1,
            .bindings = bindings
        };

        AssetObjPtrScope<GfxShader> shader(self->mShader);
        self->mPipelineLayout = GfxBackend::CreatePipelineLayout(*shader, layoutDesc);
        self->mPipeline = GfxBackend::CreateComputePipeline(*shader, self->mPipelineLayout);
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

        AssetGroup assetGroup = Engine::RegisterInitializeResources(AppImpl::CreateGraphicsResources, this);
        mShader = Asset::LoadShader("/shaders/Fill.hlsl", ShaderLoadParams {}, assetGroup);        

        ModelLoadParams loadParams {
            .layout = {
                .vertexAttributes = {
                    {"POSITION", 0, 0, GfxFormat::R32G32B32_SFLOAT, offsetof(Vertex, pos)},
                    {"NORMAL", 0, 0, GfxFormat::R32G32B32_SFLOAT, offsetof(Vertex, normal)},
                    {"TEXCOORD", 0, 0, GfxFormat::R32G32_SFLOAT, offsetof(Vertex, uv)}
                },
                .vertexBufferStrides = {
                    sizeof(Vertex)
                }
            }
        };
        mModel = Asset::LoadModel("/data/models/Duck/Duck.gltf", loadParams, assetGroup);

        return true;
    };

    void ReleaseGraphicsResources()
    {
        GfxBackend::DestroyPipeline(mPipeline);
        GfxBackend::DestroyPipelineLayout(mPipelineLayout);
        GfxBackend::DestroyImage(mImage);
    }
    
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

        if (ImGui::IsEnabled()) {
            DebugHud::DrawDebugHud(dt);
            DebugHud::DrawStatusBar(dt);
        }


        GfxBackendCommandBuffer cmd = GfxBackend::BeginCommandBuffer(GfxQueueType::Graphics|GfxQueueType::Compute);

        cmd.BindPipeline(mPipeline);

        GfxBindingDesc bindings[] {
            {
                .name = "MainImage",
                .image = mImage
            }
        };

        const GfxImageDesc& imageDesc = GfxBackend::GetImageDesc(mImage);

        cmd.TransitionImage(mImage, GfxImageTransition::ComputeWrite);
        cmd.PushBindings(mPipelineLayout, CountOf(bindings), bindings);
        cmd.Dispatch((uint32)M::Ceil(float(imageDesc.width)/16.0f), (uint32)M::Ceil(float(imageDesc.height/16.0f)), 1);
        cmd.TransitionImage(mImage, GfxImageTransition::CopySource);
        cmd.CopyImageToSwapchain(mImage);

        // Finished working with mBuffer ?
        // cmd.TransitionBuffer(mBuffer, GfxBackendBufferTransition::TransferWrite);

        ImGui::DrawFrame(cmd);

        GfxBackend::EndCommandBuffer(cmd);
        GfxBackend::SubmitQueue(GfxQueueType::Graphics);


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

