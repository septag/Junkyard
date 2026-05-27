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

inline constexpr uint32 SHAPE_COUNT = 1000;
inline constexpr Float2 MAP_EXTENTS = Float2(50, 50);

struct TestShape
{
    Mat4 transformMat;
    Quat rotation;
    Float3 scale;
    Float3 p1;
    Float3 p2;
    float speed;
    float t;
    uint64 collisionFrameIdx;
    uint64 raycastFrameIdx;
};

enum class TestMode : int
{
    Collision = 0,
    Rayhit,
    Intersection
};

enum class IntersectionShape 
{
    Sphere = 0,
    Box
};

struct TestCollisionApp final : AppCallbacks
{
    GfxImageHandle mRenderTargetDepth;
    GfxImageHandle mShadowMapDepth;

    RView mFwdRenderView;
    RView mShadowMapView;

    CollisionIsland mCollisionIsland;
    CameraFPS mCamera;
    Float2 mMapExtents = MAP_EXTENTS;
    GeometryData mBox;
    GeometryData mPlane;

    float mSunlightAngle = M_HALFPI;
    float mSimulationSpeed = 1;
    float mPrevSimulationSpeed = 1;

    TestShape mShapes[SHAPE_COUNT];
    TestMode mTestMode;
    IntersectionShape mIntersectionShape;
    float mRayLength = 20;
    Float3 mIntersectionShapeSize;
    Mat4 mIntersectionShapeMat = MAT4_IDENT;

    bool mShowGrid = true;
    bool mShowAABBs = false;
    bool mShowDebugGUI = false;
    CollisionDebugMode mDebugCollisionsMode = CollisionDebugMode::Collisions;
    float mDebugCollisionsHeatLimit = 1;

    CollisionDebugRaycastMode mDebugRaycastMode = CollisionDebugRaycastMode::Rayhits;
    float mDebugRaycastHeatLimit = 1;

    GfxImageHandle mCheckerImage;

    void GatherGeometries(const GeometryData& geo, RView& view, const Mat4& localToWorldMat, bool highlight = false, 
                          bool checkerTexture = false)
    {
        RGeometryChunk* chunk = view.NewGeometryChunk();
        chunk->localToWorldMat = localToWorldMat;
        chunk->posVertexBuffer = geo.vertexBuffers[0];
        chunk->lightingVertexBuffer = geo.vertexBuffers[1];
        chunk->indexBuffer = geo.indexBuffer;

        RGeometrySubChunk subchunk {
            .startIndex = 0,
            .numIndices = geo.numIndices,
            .baseColorImg = checkerTexture ? mCheckerImage : Image::GetWhite1x1(),
            .tintColor = highlight ? COLOR4U_RED : COLOR4U_WHITE
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
            float zrot = M::ToRad(45);
            Quat rotation = Quat::FromEuler(Float3(0, 0, Random::Float(0, M_PI2)));
            Float3 scale = Float3(Random::Float(), Random::Float(), Random::Float()) + Float3(0.4f);

            // Choose a random point in a near range radius to move back and forth
            float moveRange = Random::Float(0, 8);
            float theta = Random::Float(0, M_PI2);
            shape.p1 = position + Float3(moveRange*M::Cos(theta), moveRange*M::Sin(theta), 0);
            shape.p2 = position - Float3(moveRange*M::Cos(theta), moveRange*M::Sin(theta), 0);
            shape.speed = Random::Float();

            shape.transformMat = Mat4::TransformMat(shape.p1, rotation, scale);

            shape.rotation = rotation;
            shape.scale = scale;

            CollisionAddBoxDesc boxDesc {
                .id = IndexToId(i),
                .shape = {
                    .transform = {
                        .position = FLOAT3_ZERO,
                        .rotation = QUAT_INDENT
                    },
                    .extents = scale * 0.5f
                },
                .transform = {
                    .position = shape.p1,
                    .rotation = rotation
                }
            };
            mCollisionIsland.AddBox(boxDesc);
        }
    }

    void UpdateShapes(float dt)
    {
        PROFILE_ZONE("UpdateShapes");
        for (uint32 i = 0; i < CountOf(mShapes); i++) {
            TestShape& shape = mShapes[i];

            float t = M::Sin(shape.t*shape.speed)*0.5f + 0.5f;
            Float3 position = Float3::Lerp(shape.p1, shape.p2, t);
            Quat rotation = Quat::RotateZ(t*M_PI2);

            // shape.transformMat.SetCol4(Float4(position, 1));
            shape.transformMat = Mat4::TransformMat(position, rotation, shape.scale);

            CollisionTransform transform {
                .position = position,
                .rotation = rotation
            };
            mCollisionIsland.UpdateTransform(IndexToId(i), transform);

            shape.t += dt;
        }
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

        mCheckerImage = Image::CreateCheckerTexture(256, 128, COLOR4U_WHITE, Color4u(128, 128, 128));
        
        mCamera.SetLookAt(Float3(0, -2, 3), FLOAT3_ZERO);

        mFwdRenderView = R::CreateView(RViewType::FwdLight);
        mShadowMapView = R::CreateView(RViewType::ShadowMap);

        Collision::Initialize();

        RectFloat mapRect = RectFloat::CenterExtents(FLOAT2_ZERO, mMapExtents);
        mCollisionIsland = Collision::CreateIsland(mapRect, 4);
        SetupShapes();

        Engine::RegisterShortcut("SPACE", [](void* userData) { 
            TestCollisionApp* app = (TestCollisionApp*)userData;
            if (app->mSimulationSpeed != 0) {
                app->mPrevSimulationSpeed = app->mSimulationSpeed;
                app->mSimulationSpeed = 0;
            }
            else {
                app->mSimulationSpeed = app->mPrevSimulationSpeed;
            }
        }, this);

        return true;
    }

    void Cleanup() override
    {
        GfxBackend::DestroyImage(mCheckerImage);

        Collision::DestroyIsland(mCollisionIsland);
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
                    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "SPACE will toggle pause");
                    if (mSimulationSpeed != 0)
                        ImGui::SliderFloat("Simulation Speed", &mSimulationSpeed, 0.1f, 2.0f);

                    ImGui::Separator();
                    {
                        ImGui::PushID("TestModeRadio");
                        if (ImGui::RadioButton("Collisions", mTestMode == TestMode::Collision))
                            mTestMode = TestMode::Collision;
                        if (ImGui::RadioButton("Rayhit", mTestMode == TestMode::Rayhit))
                            mTestMode = TestMode::Rayhit;
                        if (ImGui::RadioButton("Intersection", mTestMode == TestMode::Intersection)) {
                            mTestMode = TestMode::Intersection;
                            mIntersectionShapeSize = Float3(1, 1, 1);
                            mIntersectionShapeMat = MAT4_IDENT;
                        }
                        ImGui::PopID();
                    }

                    if (mTestMode == TestMode::Rayhit) {
                        ImGui::SliderFloat("Ray Length", &mRayLength, 1, 100, "%.0f");
                    }
                    else if (mTestMode == TestMode::Intersection) {
                        ImGui::Separator();
                        ImGui::PushID("IntersectionTypeRadio");
                        if (ImGui::RadioButton("Sphere", mIntersectionShape == IntersectionShape::Sphere))
                            mIntersectionShape = IntersectionShape::Sphere;
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Box", mIntersectionShape == IntersectionShape::Box))
                            mIntersectionShape = IntersectionShape::Box;
                        ImGui::PopID();

                        switch (mIntersectionShape) {
                            case IntersectionShape::Sphere:
                                ImGui::DragFloat("Radius", &mIntersectionShapeSize.x, 0.1f, 0.1f, 20);
                                break;
                            case IntersectionShape::Box:
                                ImGui::DragFloat3("Extents", mIntersectionShapeSize.f, 0.2f, 0.1f, 20);
                                break;
                        }
                    }

                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Visuals")) {
                    ImGui::Checkbox("Show Grid", &mShowGrid);
                    ImGui::Checkbox("Show AABBs", &mShowAABBs);
                    {
                        ImGui::Checkbox("Debug", &mShowDebugGUI);
                        ImGui::PushID("CollisionModeRadio");
                        if (ImGui::RadioButton("Collisions", mDebugCollisionsMode ==  CollisionDebugMode::Collisions)) {
                            mDebugCollisionsMode = CollisionDebugMode::Collisions;
                        }
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Collisions Heat Map", mDebugCollisionsMode ==  CollisionDebugMode::Heatmap)) {
                            mDebugCollisionsMode = CollisionDebugMode::Heatmap;
                            mDebugCollisionsHeatLimit = 10;
                        }
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Entity Heat Map", mDebugCollisionsMode ==  CollisionDebugMode::EntityHeatmap)) {
                            mDebugCollisionsMode = CollisionDebugMode::EntityHeatmap;
                            mDebugCollisionsHeatLimit = 20;
                        }
                        ImGui::PopID();

                        ImGui::Separator();

                        ImGui::PushID("RaycastModeRadio");
                        if (ImGui::RadioButton("Ray Hits", mDebugRaycastMode == CollisionDebugRaycastMode::Rayhits)) {
                            mDebugRaycastMode = CollisionDebugRaycastMode::Rayhits;
                        }
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Ray Heat Map", mDebugRaycastMode == CollisionDebugRaycastMode::RayhitHeatmap)) {
                            mDebugRaycastMode = CollisionDebugRaycastMode::RayhitHeatmap;
                            mDebugRaycastHeatLimit = 5;
                        }
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Ray March Heat Map", mDebugRaycastMode == CollisionDebugRaycastMode::RaymarchHeatmap)) {
                            mDebugRaycastMode = CollisionDebugRaycastMode::RaymarchHeatmap;
                            mDebugRaycastHeatLimit = 1;
                        }

                        ImGui::PopID();
                    }
                    ImGui::Separator();
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
        uint64 frameIdx = Engine::GetFrameIndex();

        UpdateShapes(dt*mSimulationSpeed);

        if (mTestMode == TestMode::Collision) {
            MemTempAllocator tempAlloc;

            Span<CollisionPair> collisionPairs = mCollisionIsland.DetectCollisions(&tempAlloc);
            for (CollisionPair collisionPair : collisionPairs) {
                mShapes[IdToIndex(collisionPair.entity1)].collisionFrameIdx = frameIdx;
                mShapes[IdToIndex(collisionPair.entity2)].collisionFrameIdx = frameIdx;
            }
        }
        else if (mTestMode == TestMode::Rayhit) {
            MemTempAllocator tempAlloc;

            CollisionRay ray {
                .origin = mCamera.Position(),
                .direction = mCamera.Forward(),
                .length = mRayLength
            };

            Span<CollisionRayHit> hits = mCollisionIsland.IntersectRay(ray, 0xffffffff, &tempAlloc);
            for (CollisionRayHit hit : hits) {
                mShapes[IdToIndex(hit.entity)].raycastFrameIdx = frameIdx;
            }
        }
        else if (mTestMode == TestMode::Intersection) {
            MemTempAllocator tempAlloc;
            if (mIntersectionShape == IntersectionShape::Sphere) {
                Span<CollisionEntityId> intersections = 
                    mCollisionIsland.IntersectSphere(Float3(mIntersectionShapeMat.fc4), mIntersectionShapeSize.x, 0xffffffff, &tempAlloc);
                for (CollisionEntityId id : intersections) 
                    mShapes[IdToIndex(id)].collisionFrameIdx = frameIdx;
            }
            else if (mIntersectionShape == IntersectionShape::Box) {
                CollisionShapeBox box {
                    .transform = {
                        .position = Float3(mIntersectionShapeMat.fc4),
                        .rotation = Mat4::ToQuat(mIntersectionShapeMat)
                    },
                    .extents = mIntersectionShapeSize
                };
                Span<CollisionEntityId> intersections = mCollisionIsland.IntersectBox(box, 0xffffffff, &tempAlloc);
                for (CollisionEntityId id : intersections) 
                    mShapes[IdToIndex(id)].collisionFrameIdx = frameIdx;
            }
        }

        mCollisionIsland.ClearUpdates();

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
            mFwdRenderView.SetSunLight(sunlightDir, Color4u::ToFloat4(Color4u(251,250,204,50)),
                                       mShadowMapDepth, mShadowMapView.GetWorldToClipMat());
            mFwdRenderView.SetCamera(mCamera, Float2(float(App::GetWindowWidth()), float(App::GetWindowHeight())));

            GatherGeometries(mPlane, mFwdRenderView, Mat4::Translate(0, 0, -0.05f));

            {
                PROFILE_ZONE("MainPass_GatherGeometries");
                for (uint32 i = 0; i < CountOf(mShapes); i++) {
                    GatherGeometries(mBox, mFwdRenderView, mShapes[i].transformMat, 
                                     mShapes[i].collisionFrameIdx == frameIdx || mShapes[i].raycastFrameIdx == frameIdx,
                                     true);
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

            if (mShowAABBs)
                mCollisionIsland.DebugShapeBounds();

            if (mTestMode == TestMode::Intersection) {
                switch (mIntersectionShape) {
                case IntersectionShape::Sphere: 
                    DebugDraw::DrawBoundingSphere(Float4(mIntersectionShapeMat.fc4, mIntersectionShapeSize.x), COLOR4U_WHITE);
                    break;
                case IntersectionShape::Box:
                    DebugDraw::DrawBox(mIntersectionShapeSize, Float3(mIntersectionShapeMat.fc4), Mat4::ToQuat(mIntersectionShapeMat), COLOR4U_WHITE);
                    break;
                }
            }

            DebugDraw::EndDraw(cmd, mRenderTargetDepth);
        }

        // ImGui
        if (ImGui::IsEnabled()) {
            DebugHud::DrawDebugHud(dt, 20);
            DebugHud::DrawStatusBar(dt);

            UpdateGUI();

            if (mShowDebugGUI) {
                if (mTestMode == TestMode::Collision) {
                    mCollisionIsland.DebugCollisionsGUI(0.5f, mDebugCollisionsMode, mDebugCollisionsHeatLimit);
                }
                else if (mTestMode == TestMode::Rayhit) {
                    CollisionRay ray {
                        .origin = mCamera.Position(),
                        .direction = mCamera.Forward(),
                        .length = mRayLength
                    };

                    mCollisionIsland.DebugRaycastGUI(0.5f, mDebugRaycastMode, mDebugRaycastHeatLimit, &ray, 1);
                }
            }

            if (mTestMode == TestMode::Rayhit) {
                ImDrawList* drawList = ImGui::BeginFullscreenView("Crossair");
                ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x*0.5f, ImGui::GetIO().DisplaySize.y*0.5f);
                drawList->AddCircle(center, 5, COLOR4U_YELLOW.n, 12, 4);
            }
            else if (mTestMode == TestMode::Intersection) {
                ImGuizmo::SetOrthographic(false);
                ImGuizmo::AllowAxisFlip(false);
                Mat4 proj = mCamera.GetPerspectiveMat(float(App::GetWindowWidth()), float(App::GetWindowHeight()));
                proj.m22 *= -1.0f;      // YIKES!

                ImGuizmo::Manipulate(mCamera.GetViewMat().f, 
                                     proj.f,
                                     mIntersectionShape == IntersectionShape::Sphere ? 
                                        ImGuizmo::TRANSLATE : 
                                        (ImGuizmo::ROTATE_Z|ImGuizmo::TRANSLATE),
                                     ImGuizmo::WORLD,
                                     mIntersectionShapeMat.f);
            }
            
            ImGui::DrawFrame(cmd);
        }

        GfxBackend::EndCommandBuffer(cmd);
        GfxBackend::SubmitQueue(GfxQueueType::Graphics);

        Engine::EndFrame();
    }

    void OnEvent(const AppEvent& ev) override
    {
        if (!ImGui::IsAnyItemHovered() && !ImGui::GetIO().WantCaptureMouse)
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

