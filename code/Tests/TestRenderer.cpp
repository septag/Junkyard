#include "../UnityBuild.inl"

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


#include "../Tool/Console.h"

#include "../Engine.h"

#include "../Graphics/GfxBackend.h"
#include "../Renderer/Render.h"

static const char* TESTRENDERER_MODELS[] = {
    "/data/Duck/Duck.gltf",
    "/data/DamagedHelmet/DamagedHelmet.gltf",
    "/data/FlightHelmet/FlightHelmet.gltf",
    "/data/Sponza/Sponza.gltf"
};

struct SceneLight
{
    Float4 boundingSphere;
    Float4 color;
};


struct ModelScene
{
    String32 mName;
    Path mModelFilepath;

    CameraFPS mCam;

    AssetHandleModel mModel;

    AssetGroup mAssetGroup;

    Array<SceneLight> mLights;

    float mSunlightAngle = M_HALFPI;
    Float4 mSunlightColor = Color4u::ToFloat4(Color4u(251,250,204,8)); 
    float mPointLightRadius = 1.0f;
    Float4 mLightColor = Float4(1.0f, 1.0f, 1.0f, 1.0f);
    Float4 mSkyAmbient = Color4u::ToFloat4(Color4u(36,54,81,26));
    Float4 mGroundAmbient = Color4u::ToFloat4(Color4u(216,199,172,8));
    bool mDebugLightCull = false;
    bool mDebugLightBounds = false;

    void Initialize(AssetGroup initAssetGroup, const char* modelFilepath)
    {
        ASSERT(mModelFilepath.IsEmpty());

        mModelFilepath = modelFilepath;
        mName = mModelFilepath.GetFileName().CStr();

        String32 posSetting = String32::Format("%s.CamPos", mName.CStr());
        String32 targetSetting = String32::Format("%s.CamTarget", mName.CStr());

        const char* posStr = Settings::GetValue(posSetting.CStr(), "0,-2.0,3.0");
        const char* targetStr = Settings::GetValue(targetSetting.CStr(), "0,0,0");
        Float3 camPos;
        Float3 camTarget;
        Str::ScanFmt(posStr, "%f,%f,%f", &camPos.x, &camPos.y, &camPos.z);
        Str::ScanFmt(targetStr, "%f,%f,%f", &camTarget.x, &camTarget.y, &camTarget.z);
        mCam.SetLookAt(camPos, camTarget);

        mAssetGroup = Asset::CreateGroup();

        LoadLights();        
    }

    void Release()
    {
        String32 posSetting = String32::Format("%s.CamPos", mName.CStr());
        String32 targetSetting = String32::Format("%s.CamTarget", mName.CStr());

        Settings::SetValue(posSetting.CStr(), 
                           String64::Format("%.2f,%.2f,%.2f", mCam.Position().x, mCam.Position().y, mCam.Position().z).CStr());

        Float3 target = mCam.Position() + mCam.Forward();
        Settings::SetValue(targetSetting.CStr(), String64::Format("%.2f,%.2f,%.2f", target.x, target.y, target.z).CStr());

        Unload();
    }

    void Load()
    {
        ModelLoadParams loadParams {};
        R::GetCompatibleLayout(CountOf(loadParams.layout.vertexAttributes), loadParams.layout.vertexAttributes,
                               CountOf(loadParams.layout.vertexBufferStrides), loadParams.layout.vertexBufferStrides);
        mModel = Model::Load(mModelFilepath.CStr(), loadParams, mAssetGroup);
        mAssetGroup.Load();
    }

    void Unload()
    {
        mAssetGroup.Unload();
        mLights.Free();
    }

    void SetLocalLights(RView& view)
    {
        uint32 numLights = mLights.Count();
        MemTempAllocator tempAlloc;
        RLightBounds* lightBounds = Mem::AllocTyped<RLightBounds>(numLights, &tempAlloc);
        RLightProps* lightProps = Mem::AllocTyped<RLightProps>(numLights, &tempAlloc);
        for (uint32 i = 0; i < numLights; i++) {
            const SceneLight& l = mLights[i];
            lightBounds[i] = {
                .position = Float3(l.boundingSphere.x, l.boundingSphere.y, l.boundingSphere.z),
                .radius = l.boundingSphere.w
            };

            lightProps[i] = {
                .color = Color4u::ToFloat4Linear(l.color)
            };
        }
        view.SetLocalLights(numLights, lightBounds, lightProps);
    }

    void UpdateImGui(RView& view)
    {
        ImGui::ColorEdit4("Sky Ambient Color", mSkyAmbient.f, ImGuiColorEditFlags_Float);
        ImGui::ColorEdit4("Ground Ambient Color", mGroundAmbient.f, ImGuiColorEditFlags_Float);
        ImGui::Separator();

        if (ImGui::SliderFloat("Sun Light Angle", &mSunlightAngle, 0, M_PI, "%0.1f"))
            view.SetSunLight(Float3(-0.2f, M::Cos(mSunlightAngle), -M::Sin(mSunlightAngle)), mSunlightColor);
        if (ImGui::ColorEdit4("Sun Light Color", mSunlightColor.f, ImGuiColorEditFlags_Float))
            view.SetSunLight(Float3(-0.2f, M::Cos(mSunlightAngle), -M::Sin(mSunlightAngle)), mSunlightColor);

        ImGui::SliderFloat("Point Light Radius", &mPointLightRadius, 0.1f, 10.0f, "%.1f");
        ImGui::ColorEdit4("Light Color", mLightColor.f, ImGuiColorEditFlags_Float);
        if (ImGui::Button("Add Point Light"))
            AddLightAtCameraPosition();

        if (ImGui::Button("Save Lights")) {
            SaveLights();
        }
        ImGui::Separator();

        ImGui::Checkbox("Debug Light Culling", &mDebugLightCull);
        ImGui::Checkbox("Debug Light Bounds", &mDebugLightBounds);
    }

    void SaveLights()
    {
        MemTempAllocator tempAlloc;
        Blob blob(&tempAlloc);
        String<128> line;
        for (SceneLight& light : mLights) {
            line.FormatSelf("%.3f, %.3f, %.3f, %.1f, %.2f, %.2f, %.2f, %.2f\n", 
                            light.boundingSphere.x, light.boundingSphere.y, light.boundingSphere.z, light.boundingSphere.w,
                            light.color.x, light.color.y, light.color.z, light.color.w);
            blob.Write(line.Ptr(), line.Length());
        }

        char curDir[CONFIG_MAX_PATH];
        OS::GetCurrentDir(curDir, sizeof(curDir));
        Path lightsFilepath(curDir);
        String32 name = String32::Format("%s_Lights.txt", mName.CStr());
        lightsFilepath.Join(name.CStr());
        Vfs::WriteFile(lightsFilepath.CStr(), blob, VfsFlags::AbsolutePath);
    }

    void LoadLights()
    {
        char curDir[CONFIG_MAX_PATH];
        OS::GetCurrentDir(curDir, sizeof(curDir));
        String32 name = String32::Format("%s_Lights.txt", mName.CStr());
        Path lightsFilepath(curDir);
        lightsFilepath.Join(name.CStr());
        MemTempAllocator tempAlloc;
        Blob blob = Vfs::ReadFile(lightsFilepath.CStr(), VfsFlags::AbsolutePath|VfsFlags::TextFile, &tempAlloc);
        if (blob.IsValid()) {
            Str::SplitResult r = Str::Split((const char*)blob.Data(), '\n', &tempAlloc);
            for (char* line : r.splits) {
                SceneLight light {};
                Str::ScanFmt(line, "%f, %f, %f, %f, %f, %f, %f, %f", 
                             &light.boundingSphere.x, &light.boundingSphere.y, &light.boundingSphere.z, &light.boundingSphere.w,
                             &light.color.x, &light.color.y, &light.color.z, &light.color.w);
                mLights.Push(light);
            }
        }
    }

    void AddLightAtCameraPosition()
    {
        SceneLight light {
            .boundingSphere = Float4(mCam.Position(), mPointLightRadius),
            .color = mLightColor
        };

        mLights.Push(light);
    }
};

struct AppImpl final : AppCallbacks
{
    Camera* mCam = nullptr;
    ModelScene mModelScenes[CountOf(TESTRENDERER_MODELS)];
    RView mFwdRenderView;
    GfxImageHandle mRenderTargetDepth;
    uint32 mSelectedSceneIdx;
    bool mFirstTime = true;
    bool mMinimized = false;
    bool mDrawGrid = false;

    void InitializeFramebufferResources(uint16 width, uint16 height)
    {
        GfxBackend::DestroyImage(mRenderTargetDepth);

        GfxImageDesc desc {
            .width = uint16(width),
            .height = uint16(height),
            .format = GfxBackend::GetValidDepthStencilFormat(),
            .usageFlags = GfxImageUsageFlags::DepthStencilAttachment | GfxImageUsageFlags::Sampled,
        };

        // Note: this won't probably work with tiled GPUs because it's incompatible with Sampled flag
        //       So we probably need to copy the contents of the zbuffer to another one
        #if PLATFORM_MOBILE
        desc.usageFlags |= GfxImageUsageFlags::TransientAttachment;
        #endif

        mRenderTargetDepth = GfxBackend::CreateImage(desc);
    }

    bool Initialize() override
    {
        bool isRemote = SettingsJunkyard::Get().engine.connectToServer;

        // For remote mode, you also have to use "-ToolingServerCustomDataMountDir=data/TestAsset" argument for the server tool
        Vfs::HelperMountDataAndShaders(isRemote, isRemote ? "data" : "data/TestBasicGfx");

        if (!Engine::Initialize())
            return false;

        AssetGroup initAssetGroup = Engine::RegisterInitializeResources([](void* userData) {
            AppImpl* self = (AppImpl*)userData;
            self->InitializeFramebufferResources(App::GetFramebufferWidth(), App::GetFramebufferHeight());
        }, this);

        for (uint32 i = 0; i < CountOf(TESTRENDERER_MODELS); i++)
            mModelScenes[i].Initialize(initAssetGroup, TESTRENDERER_MODELS[i]);

        mSelectedSceneIdx = (uint32)Str::ToInt(Settings::GetValue("TestRenderer.SelectedScene", "0"));
        mSelectedSceneIdx = Clamp(mSelectedSceneIdx, 0u, CountOf(TESTRENDERER_MODELS)-1);

        mCam = &mModelScenes[mSelectedSceneIdx].mCam;

        if constexpr (PLATFORM_APPLE || PLATFORM_ANDROID)
            mDrawGrid = false;

        mFwdRenderView = R::CreateView(RViewType::FwdLight);

        return true;
    };

    void Cleanup() override
    {
        Settings::SetValue("TestRenderer.SelectedScene", String32::Format("%u", mSelectedSceneIdx).CStr());

        for (uint32 i = 0; i < CountOf(TESTRENDERER_MODELS); i++)
            mModelScenes[i].Release();

        R::DestroyView(mFwdRenderView);
        GfxBackend::DestroyImage(mRenderTargetDepth);

        Engine::Release();
    };

    void Update(float dt) override
    {
        PROFILE_ZONE("Update");

        if (mMinimized)
            return;

        if (mFirstTime) {
            mModelScenes[mSelectedSceneIdx].Load();
            mCam = &mModelScenes[mSelectedSceneIdx].mCam;
            mFirstTime = false;
        }

        mCam->HandleMovementKeyboard(dt, 20.0f, 5.0f);

        Engine::BeginFrame(dt);

        GfxCommandBuffer cmd = GfxBackend::BeginCommandBuffer(GfxQueueType::Graphics);

        // Update
        ModelScene& scene = mModelScenes[mSelectedSceneIdx];
        R::FwdLight::Update(mFwdRenderView, cmd);

        // Render
        {
            R::NewFrame();

            scene.SetLocalLights(mFwdRenderView);
            mFwdRenderView.SetAmbientLight(scene.mSkyAmbient, scene.mGroundAmbient);
            mFwdRenderView.SetSunLight(Float3(-0.2f, M::Cos(scene.mSunlightAngle), -M::Sin(scene.mSunlightAngle)), scene.mSunlightColor);
            mFwdRenderView.SetCameraAndViewport(*mCam, Float2(float(App::GetWindowWidth()), float(App::GetWindowHeight())));

            AssetObjPtrScope<ModelData> model(scene.mModel);
            if (!model.IsNull()) {
                MemTempAllocator tempAlloc;
                Array<RGeometrySubChunk> subChunks(&tempAlloc);

                RGeometryChunk* chunk = mFwdRenderView.NewGeometryChunk();

                for (uint32 i = 0; i < model->numNodes; i++) {
                    const ModelNode& node = model->nodes[i];
                    if (node.meshId == 0)
                        continue;

                    chunk->localToWorldMat = Mat4::TransformMat(
                        node.localTransform.position,
                        node.localTransform.rotation,
                        node.localTransform.scale);

                    ASSERT(model->numVertexBuffers == 2);
                    chunk->posVertexBuffer = model->vertexBuffers[0];
                    chunk->lightingVertexBuffer = model->vertexBuffers[1];
                    chunk->indexBuffer = model->indexBuffer;
                    
                    const ModelMesh& mesh = model->meshes[IdToIndex(node.meshId)];

                    chunk->posVertexBufferOffset = mesh.vertexBufferOffsets[0];
                    chunk->lightingVertexBufferOffset = mesh.vertexBufferOffsets[1];
                    chunk->indexBufferOffset = mesh.indexBufferOffset;

                    for (uint32 smi = 0; smi < mesh.numSubmeshes; smi++) {
                        const ModelSubmesh& submesh = mesh.submeshes[smi];
                        const ModelMaterial* mtl = model->materials[IdToIndex(submesh.materialId)].Get();
                        
                        GfxImageHandle imgHandle {};

                        if (mtl->pbrMetallicRoughness.baseColorTex.texture.IsValid()) {
                            AssetObjPtrScope<GfxImage> img(mtl->pbrMetallicRoughness.baseColorTex.texture);
                            if (!img.IsNull())
                                imgHandle = img->handle;
                        }

                        RGeometrySubChunk subChunk {
                            .startIndex = submesh.startIndex,
                            .numIndices = submesh.numIndices,
                            .baseColorImg = imgHandle
                        };
                        subChunks.Push(subChunk);
                    }

                }

                chunk->AddSubChunks(subChunks.Count(), subChunks.Ptr());
            }

            R::FwdLight::Render(mFwdRenderView, cmd, GfxImageHandle(), mRenderTargetDepth, 
                                scene.mDebugLightCull ? RDebugMode::LightCull : RDebugMode::None);
        }

        cmd.TransitionImage(mRenderTargetDepth, GfxImageTransition::RenderTarget, GfxImageTransitionFlags::DepthRead);

        // DebugDraw
        if (!scene.mDebugLightCull) {
            PROFILE_ZONE("DebugDraw");
            DebugDraw::BeginDraw(cmd, *mCam, App::GetFramebufferWidth(), App::GetFramebufferHeight());
            if (mDrawGrid) {
                DebugDrawGridProperties gridProps {
                    .distance = 200,
                    .lineColor = Color4u(0x565656),
                    .boldLineColor = Color4u(0xd6d6d6)
                };

                DebugDraw::DrawGroundGrid(*mCam, gridProps);
            }

            if (scene.mDebugLightBounds) {
                for (const SceneLight& l : scene.mLights) {
                    DebugDraw::DrawBoundingSphere(l.boundingSphere, COLOR4U_WHITE);
                }
            }
            DebugDraw::EndDraw(cmd, mRenderTargetDepth);
        }

        // ImGui
        if (ImGui::IsEnabled()) {
            PROFILE_ZONE("ImGui");
            DebugHud::DrawDebugHud(dt, 20);
            DebugHud::DrawStatusBar(dt);

            ImGui::BeginMainMenuBar();
            {
                if (ImGui::BeginMenu("Scenes")) {
                    for (uint32 i = 0; i < CountOf(TESTRENDERER_MODELS); i++) {
                        if (ImGui::MenuItem(mModelScenes[i].mName.CStr(), nullptr, mSelectedSceneIdx == i)) {
                            if (i != mSelectedSceneIdx) {
                                scene.Unload();
                                mSelectedSceneIdx = i;
                                mModelScenes[i].Load();
                                mCam = &mModelScenes[i].mCam;
                            }
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Draw Grid", nullptr, mDrawGrid))
                        mDrawGrid = !mDrawGrid;
                    ImGui::EndMenu();
                }
            }        
            ImGui::EndMainMenuBar();

            ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Scene")) {
                scene.UpdateImGui(mFwdRenderView);
            }
            ImGui::End();

            ImGui::DrawFrame(cmd);
        }

        GfxBackend::EndCommandBuffer(cmd);
        GfxBackend::SubmitQueue(GfxQueueType::Graphics);

        Engine::EndFrame();
    }
    
    void OnEvent(const AppEvent& ev) override
    {
        if (mCam && !ImGui::IsAnyItemHovered() && !ImGui::GetIO().WantCaptureMouse && !ImGuizmo::IsOver())
            mCam->HandleRotationMouse(ev, 0.2f, 0.1f);

        if (ev.type  == AppEventType::Iconified) 
            mMinimized = true;            
        else if (ev.type == AppEventType::Restored)
            mMinimized = false;
        else if (ev.type == AppEventType::Resized) 
            InitializeFramebufferResources(ev.framebufferWidth, ev.framebufferHeight);
    }
};

int Main(int argc, char* argv[])
{
    SettingsJunkyard initSettings {
        .app = {
            .appName = "TestRenderer"
        },
        .graphics = {
            .surfaceSRGB = true
        }
    };
    SettingsJunkyard::Initialize(initSettings);

    Settings::InitializeFromINI("TestRenderer.ini");
    Settings::InitializeFromCommandLine(argc, argv);

    static AppImpl impl;
    App::Run(AppDesc { 
        .callbacks = &impl, 
        .windowTitle = "Junkyard: Renderer Test"
    });

    Settings::SaveToINI("TestRenderer.ini");
    Settings::Release();
    return 0;
}

