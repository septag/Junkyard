#include "../UnityBuild.inl"

#include "../Core/Settings.h"
#include "../Core/Log.h"
#include "../Core/TracyHelper.h"
#include "../Core/Jobs.h"
#include "../Core/MathAll.h"
#include "../Core/System.h"

#include "../Common/Application.h"
#include "../Common/JunkyardSettings.h"
#include "../Common/Camera.h"
#include "../Common/VirtualFS.h"

#include "../Assets/AssetManager.h"
#include "../Assets/Model.h"
#include "../Assets/Shader.h"

#include "../Graphics/GfxBackend.h"
#include "../Graphics/Geometry.h"

#include "../ImGui/ImGuiMain.h"
#include "../ImGui/ImGuizmo.h"

#include "../DebugTools/DebugDraw.h"
#include "../Debugtools/DebugHud.h"

#include "../Collision/Collision.h"
#include "../Renderer/Render.h"
#include "../Engine.h"

inline constexpr uint32 SHAPE_COUNT = 2000;
inline constexpr Float2 MAP_EXTENTS = Float2(100, 100);

struct TestShape
{
    Mat4 transformMat;
    Float3 p1;
    Float3 p2;
    float speed;
    float t;
    uint64 collisionFrameIdx;
    uint64 raycastFrameIdx;
};

struct TestCollisionApp final : AppCallbacks
{
    GfxImageHandle mRenderTargetDepth;
    GfxImageHandle mShadowMapDepth;

    RView mFwdRenderView;
    RView mShadowMapView;

    CollisionIsland colIsland;
    CameraFPS mCamera;
    float mSunlightAngle = M_HALFPI;
    Float2 mMapExtents = MAP_EXTENTS;
    GeometryData mBox;
    GeometryData mPlane;

    TestShape mShapes[SHAPE_COUNT];

    bool mShowGrid = true;

    void GatherGeometries(const GeometryData& geo, RView& view, const Mat4& localToWorldMat)
    {
        RGeometryChunk* chunk = view.NewGeometryChunk();
        chunk->localToWorldMat = localToWorldMat;
        chunk->posVertexBuffer = geo.vertexBuffers[0];
        chunk->lightingVertexBuffer = geo.vertexBuffers[1];
        chunk->indexBuffer = geo.indexBuffer;

        RGeometrySubChunk subchunk = {
            .startIndex = 0,
            .numIndices = geo.numIndices,
            .baseColorImg = Image::GetWhite1x1()
        };
        chunk->AddSubChunk(subchunk);
    }

    void ReleaseFramebufferResources()
    {
        GfxBackend::DestroyImage(mRenderTargetDepth);
        GfxBackend::DestroyImage(mShadowMapDepth);
    }

    void SetupShapes()
    {
        for (uint32 i = 0; i < CountOf(mShapes); i++) {
            TestShape& shape = mShapes[i];

            Float3 position = Float3(Random::Float(-MAP_EXTENTS.x, MAP_EXTENTS.x), Random::Float(-MAP_EXTENTS.y, MAP_EXTENTS.y), 0.5f);
            Quat rotation = Quat::FromEuler(Float3(0, 0, Random::Float(0, M_PI2)));
            Float3 scale = Float3(Random::Float(), Random::Float(), Random::Float()) + Float3(0.4f);

            // Choose a random point in a near range radius to move back and forth
            float moveRange = Random::Float(0, 8);
            float theta = Random::Float(0, M_PI2);
            shape.p1 = position + Float3(moveRange*M::Cos(theta), moveRange*M::Sin(theta), 0);
            shape.p2 = position - Float3(moveRange*M::Cos(theta), moveRange*M::Sin(theta), 0);
            shape.speed = Random::Float();

            shape.transformMat = Mat4::TransformMat(position, rotation, scale);
        }
    }

    void UpdateShapes()
    {

    }

    void InitializeFramebufferResources(uint16 width, uint16 height)
    {
        ReleaseFramebufferResources();

        {
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

        {
            GfxImageDesc desc {
                .width = 2048,
                .height = 2048,
                .format = GfxFormat::D16_UNORM,
                .usageFlags = GfxImageUsageFlags::DepthStencilAttachment | GfxImageUsageFlags::Sampled,
            };

            // Note: this won't probably work with tiled GPUs because it's incompatible with Sampled flag
            //       So we probably need to copy the contents of the zbuffer to another one
            #if PLATFORM_MOBILE
            desc.usageFlags |= GfxImageUsageFlags::TransientAttachment;
            #endif

            mShadowMapDepth = GfxBackend::CreateImage(desc);
            mCamera.SetLookAt(Float3(0, -2, 3), FLOAT3_ZERO);
        }
    }

    bool Initialize() override
    {
        Vfs::HelperMountDataAndShaders(false);

        if (!Engine::Initialize())
            return false;

        InitializeFramebufferResources(App::GetFramebufferWidth(), App::GetFramebufferHeight());

        {
            GeometryVertexLayout layout;
            R::GetCompatibleLayout(layout);
            Geometry::CreateAxisAlignedBox(Float3(0.5f, 0.5f, 0.5f), layout, mBox);
            Geometry::CreatePlane(mMapExtents, layout, mPlane);
        }
        
        mCamera.SetLookAt(Float3(0, -2, 3), FLOAT3_ZERO);

        mFwdRenderView = R::CreateView(RViewType::FwdLight);
        mShadowMapView = R::CreateView(RViewType::ShadowMap);

        Collision::Initialize();
        SetupShapes();

        return true;
    }

    void Cleanup() override
    {
        Collision::Release();

        Geometry::Destroy(mBox);
        Geometry::Destroy(mPlane);

        R::DestroyView(mFwdRenderView);
        R::DestroyView(mShadowMapView);

        ReleaseFramebufferResources();
        Engine::Release();
    }

    void UpdateGUI()
    {
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Collision Test")) {
            if (ImGui::BeginTabBar("MainTabBar")) {
                if (ImGui::BeginTabItem("Main")) {
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Visuals")) {
                    ImGui::Checkbox("Show Grid", &mShowGrid);
                    ImGui::SliderFloat("Sun Light Angle", &mSunlightAngle, 0, M_PI, "%0.2f");
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    void Update(float dt) override
    {
        mCamera.HandleMovementKeyboard(dt, 20, 5);
        Engine::BeginFrame(dt);

        UpdateShapes();

        GfxCommandBuffer cmd = GfxBackend::BeginCommandBuffer(GfxQueueType::Graphics);
        if (!mBox.firstUpdate)
            Geometry::UpdateGpuBuffers(mBox, cmd);
        if (!mPlane.firstUpdate)
            Geometry::UpdateGpuBuffers(mPlane, cmd);

        R::NewFrame();

        Float3 sunlightDir = Float3(-0.2f, M::Cos(mSunlightAngle), -M::Sin(mSunlightAngle));

        // Shadow map
        {
            PROFILE_ZONE("ShadowMap");

            AABB boundsWS = AABB::CenterExtents(FLOAT3_ZERO, Float3(mMapExtents.x, mMapExtents.y, 10));

            Camera shadowCam;
            shadowCam.Setup(0, -100, 100);
            shadowCam.SetPosDir(FLOAT3_ZERO, sunlightDir);

            Mat4 viewMat = shadowCam.GetViewMat();
            AABB boundsLS = AABB::Transform(boundsWS, viewMat);
            boundsLS = AABB::Expand(boundsLS, boundsLS.Extents()*0.1f);

            Float3 dim = boundsLS.Dimensions();
            float nearDist = boundsLS.zmin;
            float farDist = boundsLS.zmax;

            mShadowMapView.SetCamera(shadowCam, Float2(dim.x, dim.y));
            shadowCam.Setup(0, nearDist, farDist);

            for (uint32 i = 0; i < CountOf(mShapes); i++) {
                GatherGeometries(mBox, mShadowMapView, mShapes[i].transformMat);
            }

            R::ShadowMap::Update(mShadowMapView, cmd);
            R::ShadowMap::Render(mShadowMapView, cmd, mShadowMapDepth);
        }

        // Main pass
        {
            PROFILE_ZONE("MainPass");

            mFwdRenderView.SetAmbientLight(Color4u::ToFloat4(Color4u(36,54,81,26)), 
                                           Color4u::ToFloat4(Color4u(216,199,172,8)));
            mFwdRenderView.SetSunLight(sunlightDir, Color4u::ToFloat4(Color4u(251,250,204,20)),
                                       mShadowMapDepth, mShadowMapView.GetWorldToClipMat());
            mFwdRenderView.SetCamera(mCamera, Float2(float(App::GetWindowWidth()), float(App::GetWindowHeight())));

            GatherGeometries(mPlane, mFwdRenderView, Mat4::Translate(0, 0, -0.05f));

            {
                PROFILE_ZONE("MainPass_GatherGeometries");
                for (uint32 i = 0; i < CountOf(mShapes); i++) {
                    GatherGeometries(mBox, mFwdRenderView, mShapes[i].transformMat);
                }
            }

            R::FwdLight::Update(mFwdRenderView, cmd);
            R::FwdLight::Render(mFwdRenderView, cmd, GfxImageHandle(), mRenderTargetDepth, RDebugMode::None);
        }

        cmd.TransitionImage(mRenderTargetDepth, GfxImageTransition::RenderTarget, GfxImageTransitionFlags::DepthRead);

        // DebugDraw
        {
            DebugDraw::BeginDraw(cmd, mCamera, App::GetFramebufferWidth(), App::GetFramebufferHeight());
            if (mShowGrid) {
                DebugDrawGridProperties gridProps {
                    .distance = 200,
                    .lineColor = Color4u(0x565656),
                    .boldLineColor = Color4u(0xd6d6d6)
                };

                DebugDraw::DrawGroundGrid(mCamera, gridProps);
            }
            DebugDraw::EndDraw(cmd, mRenderTargetDepth);
        }

        // ImGui
        if (ImGui::IsEnabled()) {
            DebugHud::DrawDebugHud(dt, 20);
            DebugHud::DrawStatusBar(dt);

            UpdateGUI();

            ImGui::DrawFrame(cmd);
        }

        GfxBackend::EndCommandBuffer(cmd);
        GfxBackend::SubmitQueue(GfxQueueType::Graphics);

        Engine::EndFrame();
    }

    void OnEvent(const AppEvent& ev) override
    {
        if (!ImGui::IsAnyItemHovered() && !ImGui::GetIO().WantCaptureMouse && !ImGuizmo::IsOver())
            mCamera.HandleRotationMouse(ev, 0.2f, 0.1f);
        else if (ev.type == AppEventType::Resized) 
            InitializeFramebufferResources(ev.framebufferWidth, ev.framebufferHeight);
    }
};

int Main(int argc, char* argv[])
{
    SettingsJunkyard initSettings {
        .app = {
            .appName = "TestCollision"
        },
        .graphics = {
            .surfaceSRGB = true
        }
    };
    SettingsJunkyard::Initialize(initSettings);

    Settings::InitializeFromINI("TestCollision.ini");
    Settings::InitializeFromCommandLine(argc, argv);

    static TestCollisionApp impl;
    App::Run(AppDesc { 
        .callbacks = &impl, 
        .windowTitle = "Junkyard: Collision Test"
    });

    Settings::SaveToINI("TestCollision.ini");
    Settings::Release();
    return 0;
}

