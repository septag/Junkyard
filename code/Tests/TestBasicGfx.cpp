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

#include <stdio.h>

static const char* TESTBASICGFX_MODELS[] = {
    "/data/Duck/Duck.gltf",
    "/data/DamagedHelmet/DamagedHelmet.gltf",
    "/data/FlightHelmet/FlightHelmet.gltf",
    "/data/Sponza/Sponza.gltf"
};

struct ModelScene
{
    String32 mName;
    Path mModelFilepath;

    CameraFPS mCam;

    AssetHandleModel mModel;
    AssetHandleShader mShader;

    GfxPipelineHandle mPipeline;
    GfxPipelineLayoutHandle mPipelineLayout;
    GfxBufferHandle mUniformBuffer;

    AssetGroup mAssetGroup;

    float mLightAngle = M_HALFPI;
    bool mEnableLight = false;

    struct FrameInfo 
    {
        Mat4 worldToClipMat;
        Float3 lightDir;
        float lightFactor;
    };

    struct ModelTransform
    {
        Mat4 modelMat;
    };

    struct Vertex 
    {
        Float3 pos;
        Float3 normal;
        Float2 uv;
    };

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
        sscanf(posStr, "%f,%f,%f", &camPos.x, &camPos.y, &camPos.z);
        sscanf(targetStr, "%f,%f,%f", &camTarget.x, &camTarget.y, &camTarget.z);
        mCam.SetLookAt(camPos, camTarget);

        GfxBufferDesc bufferDesc {
            .sizeBytes = sizeof(FrameInfo),
            .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Uniform,
            .arena = GfxMemoryArena::PersistentGPU,
            .perFrameUpdates = true
        };
        mUniformBuffer = GfxBackend::CreateBuffer(bufferDesc);

        mShader = Shader::Load("/shaders/Model.hlsl", ShaderLoadParams{}, initAssetGroup);

        mAssetGroup = Asset::CreateGroup();
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
        GfxBackend::DestroyBuffer(mUniformBuffer);
    }

    void Load()
    {
        AssetObjPtrScope<GfxShader> shader(mShader);

        GfxVertexBufferBindingDesc vertexBufferBindingDesc {
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = GfxVertexInputRate::Vertex
        };

        GfxVertexInputAttributeDesc vertexInputAttDescs[] = {
            {
                .semantic = "POSITION",
                .binding = 0,
                .format = GfxFormat::R32G32B32_SFLOAT,
                .offset = offsetof(Vertex, pos)
            },
            {
                .semantic = "NORMAL",
                .binding = 0,
                .format = GfxFormat::R32G32B32_SFLOAT,
                .offset = offsetof(Vertex, normal)
            },
            {
                .semantic = "TEXCOORD",
                .binding = 0,
                .format = GfxFormat::R32G32_SFLOAT,
                .offset = offsetof(Vertex, uv)
            }
        };

        GfxPipelineLayoutDesc::Binding bindings[] = {
            {
                .name = "FrameInfo",
                .type = GfxDescriptorType::UniformBuffer,
                .stagesUsed = GfxShaderStage::Vertex|GfxShaderStage::Fragment
            },
            {
                .name = "BaseColorTexture",
                .type = GfxDescriptorType::CombinedImageSampler,
                .stagesUsed = GfxShaderStage::Fragment
            }
        };

        GfxPipelineLayoutDesc::PushConstant pushConstant {
            .name = "ModelTransform",
            .stagesUsed = GfxShaderStage::Vertex,
            .size = sizeof(ModelTransform)
        };

        GfxPipelineLayoutDesc pipelineLayoutDesc {
            .numBindings = CountOf(bindings),
            .bindings = bindings,
            .numPushConstants = 1,
            .pushConstants = &pushConstant
        };

        mPipelineLayout = GfxBackend::CreatePipelineLayout(*shader, pipelineLayoutDesc);

        GfxGraphicsPipelineDesc pipelineDesc {
            .numVertexInputAttributes = CountOf(vertexInputAttDescs),
            .vertexInputAttributes = vertexInputAttDescs,
            .numVertexBufferBindings = 1,
            .vertexBufferBindings = &vertexBufferBindingDesc,
            .rasterizer = {
                .cullMode = GfxCullMode::Back
            },
            .blend = {
                .numAttachments = 1,
                .attachments = GfxBlendAttachmentDesc::GetDefault()
            },
            .depthStencil = {
                .depthTestEnable = true,
                .depthWriteEnable = true,
                .depthCompareOp = GfxCompareOp::Less
            },
            .numColorAttachments = 1,
            .colorAttachmentFormats = {GfxBackend::GetSwapchainFormat()},
            .depthAttachmentFormat = GfxBackend::GetValidDepthStencilFormat(),
            .stencilAttachmentFormat = GfxBackend::GetValidDepthStencilFormat()
        };

        mPipeline = GfxBackend::CreateGraphicsPipeline(*shader, mPipelineLayout, pipelineDesc);

        ModelLoadParams modelParams {
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

        mModel = Model::Load(mModelFilepath.CStr(), modelParams, mAssetGroup);
        mAssetGroup.Load();
    }

    void Unload()
    {
        mAssetGroup.Unload();

        GfxBackend::DestroyPipeline(mPipeline);
        GfxBackend::DestroyPipelineLayout(mPipelineLayout);
    }

    void Update(GfxCommandBuffer cmd)
    {
        if (!mAssetGroup.IsValid() || !mAssetGroup.IsLoadFinished())
            return;

        float vwidth = (float)App::GetFramebufferWidth();
        float vheight = (float)App::GetFramebufferHeight();
        FrameInfo ubo {
            .worldToClipMat = GfxBackend::GetSwapchainTransformMat() * mCam.GetPerspectiveMat(vwidth, vheight) * mCam.GetViewMat(),
            .lightDir = Float3(-0.2f, M::Cos(mLightAngle), -M::Sin(mLightAngle)),
            .lightFactor = mEnableLight ? 0 : 1.0f
        };

        GfxBufferDesc stagingDesc {
            .sizeBytes = sizeof(ubo),
            .usageFlags = GfxBufferUsageFlags::TransferSrc,
            .arena = GfxMemoryArena::TransientCPU
        };
        GfxBufferHandle stagingBuff = GfxBackend::CreateBuffer(stagingDesc);

        FrameInfo* dstUbo;
        cmd.MapBuffer(stagingBuff, (void**)&dstUbo);
        *dstUbo = ubo;
        cmd.FlushBuffer(stagingBuff);

        cmd.TransitionBuffer(mUniformBuffer, GfxBufferTransition::TransferWrite);
        cmd.CopyBufferToBuffer(stagingBuff, mUniformBuffer, GfxShaderStage::Vertex|GfxShaderStage::Fragment);

        GfxBackend::DestroyBuffer(stagingBuff);
    }

    void UpdateImGui()
    {
        ImGui::Checkbox("EnableLight", &mEnableLight);
        ImGui::SliderFloat("LightAngle", &mLightAngle, 0, M_PI, "%0.1f");
    }

    void Render(GfxCommandBuffer cmd)
    {
        if (!mAssetGroup.IsValid() || !mAssetGroup.IsLoadFinished())
            return;

        cmd.BindPipeline(mPipeline);
        
        cmd.HelperSetFullscreenViewportAndScissor();

        AssetObjPtrScope<ModelData> model(mModel);

        for (uint32 i = 0; i < model->numNodes; i++) {
            const ModelNode& node = model->nodes[i];
            if (node.meshId == 0)
                continue;

            ModelTransform transform {
                .modelMat = Mat4::TransformMat(node.localTransform.position, node.localTransform.rotation, node.localTransform.scale)
            };
            cmd.PushConstants(mPipelineLayout, "ModelTransform", &transform, sizeof(transform));

            const ModelMesh& mesh = model->meshes[IdToIndex(node.meshId)];

            // Buffers
            cmd.BindVertexBuffers(0, model->numVertexBuffers, model->vertexBuffers, mesh.vertexBufferOffsets);
            cmd.BindIndexBuffer(model->indexBuffer, mesh.indexBufferOffset, GfxIndexType::Uint32);

            for (uint32 smi = 0; smi < mesh.numSubmeshes; smi++) {
                const ModelSubmesh& submesh = mesh.submeshes[smi];
                const ModelMaterial* mtl = model->materials[IdToIndex(submesh.materialId)].Get();
                        
                GfxImageHandle imgHandle {};

                if (mtl->pbrMetallicRoughness.baseColorTex.texture.IsValid()) {
                    AssetObjPtrScope<GfxImage> img(mtl->pbrMetallicRoughness.baseColorTex.texture);
                    if (!img.IsNull())
                        imgHandle = img->handle;
                }

                GfxBindingDesc bindings[] = {
                    {
                        .name = "FrameInfo",
                        .buffer = mUniformBuffer
                    },
                    {
                        .name = "BaseColorTexture",
                        .image = imgHandle.IsValid() ? imgHandle : Image::GetWhite1x1()
                    }
                };
                cmd.PushBindings(mPipelineLayout, CountOf(bindings), bindings);

                cmd.DrawIndexed(submesh.numIndices, 1, submesh.startIndex, 0, 0);
            }
        }
    }
};

struct AppImpl final : AppCallbacks
{
    Camera* mCam = nullptr;
    ModelScene mModelScenes[CountOf(TESTBASICGFX_MODELS)];
    GfxImageHandle mRenderTargetDepth;
    uint32 mSelectedSceneIdx;
    bool mFirstTime = true;
    bool mMinimized = false;
    bool mDrawGrid = true;

    static void InitializeResources(void* userData)
    {
        AppImpl* self = (AppImpl*)userData;

        {
            Int2 extent = GfxBackend::GetSwapchainExtent();
            GfxImageDesc desc {
                .width = uint16(extent.x),
                .height = uint16(extent.y),
                .multisampleFlags = GfxMultiSampleCount::SampleCount1,
                .format = GfxBackend::GetValidDepthStencilFormat(),
                .usageFlags = GfxImageUsageFlags::DepthStencilAttachment|GfxImageUsageFlags::TransientAttachment,
                .arena = GfxMemoryArena::PersistentGPU
            };

            self->mRenderTargetDepth = GfxBackend::CreateImage(desc);
        }
    }

    bool Initialize() override
    {
        bool isRemote = SettingsJunkyard::Get().engine.connectToServer;

        // For remote mode, you also have to use "-ToolingServerCustomDataMountDir=data/TestAsset" argument for the server tool
        Vfs::HelperMountDataAndShaders(isRemote, isRemote ? "data" : "data/TestBasicGfx");

        if (!Engine::Initialize())
            return false;


        AssetGroup initAssetGroup = Engine::RegisterInitializeResources(InitializeResources, this);
        for (uint32 i = 0; i < CountOf(TESTBASICGFX_MODELS); i++)
            mModelScenes[i].Initialize(initAssetGroup, TESTBASICGFX_MODELS[i]);

        mSelectedSceneIdx = (uint32)Str::ToInt(Settings::GetValue("TestBasicGfx.SelectedScene", "0"));
        mSelectedSceneIdx = Clamp(mSelectedSceneIdx, 0u, CountOf(TESTBASICGFX_MODELS)-1);

        mCam = &mModelScenes[mSelectedSceneIdx].mCam;

        if constexpr (PLATFORM_APPLE || PLATFORM_ANDROID)
            mDrawGrid = false;

        return true;
    };

    void Cleanup() override
    {
        Settings::SetValue("TestBasicGfx.SelectedScene", String32::Format("%u", mSelectedSceneIdx).CStr());

        for (uint32 i = 0; i < CountOf(TESTBASICGFX_MODELS); i++)
            mModelScenes[i].Release();

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
        mModelScenes[mSelectedSceneIdx].Update(cmd);

        // Render
        GfxBackendRenderPass pass { 
            .colorAttachments = {{ 
                .clear = true,
                .clearValue = {
                    .color = Color4u::ToFloat4(COLOR4U_BLACK)
                }
            }},
            .depthAttachment = {
                .image = mRenderTargetDepth,
                .clear = true,
                .clearValue = {
                    .depth = 1.0f
                }
            },
            .swapchain = true,
            .hasDepth = true
        };

        cmd.TransitionImage(mRenderTargetDepth, GfxImageTransition::RenderTarget, GfxImageTransitionFlags::DepthRead);

        {
            GPU_PROFILE_ZONE(cmd, "ModelRender");

            cmd.BeginRenderPass(pass);
            mModelScenes[mSelectedSceneIdx].Render(cmd);
            cmd.EndRenderPass();
        }

        if (mDrawGrid) {
            DebugDraw::BeginDraw(cmd, *mCam, App::GetFramebufferWidth(), App::GetFramebufferHeight());
            DebugDrawGridProperties gridProps {
                .distance = 200,
                .lineColor = Color4u(0x565656),
                .boldLineColor = Color4u(0xd6d6d6)
            };

            DebugDraw::DrawGroundGrid(*mCam, gridProps);
            DebugDraw::EndDraw(cmd, mRenderTargetDepth);
        }

        if (ImGui::IsEnabled()) {
            DebugHud::DrawDebugHud(dt, 20);
            DebugHud::DrawStatusBar(dt);

            ImGui::BeginMainMenuBar();
            {
                if (ImGui::BeginMenu("Scenes")) {
                    for (uint32 i = 0; i < CountOf(TESTBASICGFX_MODELS); i++) {
                        if (ImGui::MenuItem(mModelScenes[i].mName.CStr(), nullptr, mSelectedSceneIdx == i)) {
                            if (i != mSelectedSceneIdx) {
                                mModelScenes[mSelectedSceneIdx].Unload();
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
                mModelScenes[mSelectedSceneIdx].UpdateImGui();
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

    Settings::InitializeFromINI("TestBasicGfx.ini");
    Settings::InitializeFromCommandLine(argc, argv);

    static AppImpl impl;
    App::Run(AppDesc { 
        .callbacks = &impl, 
        .windowTitle = "Junkyard: Basic Graphics test"
    });

    Settings::SaveToINI("TestBasicGfx.ini");
    Settings::Release();
    return 0;
}

