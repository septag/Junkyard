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
    GfxPipelineHandle mPipeline;
    GfxPipelineLayoutHandle mPipelineLayout;
    GfxBufferHandle mUniformBuffer;

    GfxImageHandle mRenderTargetColor;
    GfxImageHandle mRenderTargetDepth;

    AssetHandleModel mModelAsset;
    AssetHandleShader mModelShaderAsset;
    CameraFPS   mFpsCam;
    CameraOrbit mOrbitCam;
    Camera*     mCam;

    struct Vertex 
    {
        Float3 pos;
        Float3 normal;
        Float2 uv;
    };

    struct ModelTransform
    {
        Mat4 modelMat;
    };

    struct FrameTransform 
    {
        Mat4 viewMat;
        Mat4 projMat;
    };

    static void CreateGraphicsResources(void* userData)
    {
        AppImpl* self = (AppImpl*)userData;
        AssetObjPtrScope<GfxShader> shader(self->mModelShaderAsset);

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
                .name = "FrameTransform",
                .type = GfxDescriptorType::UniformBuffer,
                .stagesUsed = GfxShaderStage::Vertex
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

        self->mPipelineLayout = GfxBackend::CreatePipelineLayout(*shader, pipelineLayoutDesc);


        GfxBufferDesc bufferDesc {
            .sizeBytes = sizeof(FrameTransform),
            .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Uniform,
            .arena = GfxMemoryArena::PersistentGPU
        };
        self->mUniformBuffer = GfxBackend::CreateBuffer(bufferDesc);

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
            .colorAttachmentFormats = {GfxFormat::R8G8B8A8_UNORM},
            .depthAttachmentFormat = GfxFormat::D24_UNORM_S8_UINT
        };

        self->mPipeline = GfxBackend::CreateGraphicsPipeline(*shader, self->mPipelineLayout, pipelineDesc);

        {
            GfxImageDesc desc {
                .width = App::GetFramebufferWidth(),
                .height = App::GetFramebufferHeight(),
                .multisampleFlags = GfxSampleCountFlags::SampleCount1,
                .format = GfxFormat::R8G8B8A8_UNORM,
                .usageFlags = GfxImageUsageFlags::ColorAttachment | GfxImageUsageFlags::TransferSrc,
                .arena = GfxMemoryArena::PersistentGPU
            };

            self->mRenderTargetColor = GfxBackend::CreateImage(desc);
        }

        {
            GfxImageDesc desc {
                .width = App::GetFramebufferWidth(),
                .height = App::GetFramebufferHeight(),
                .multisampleFlags = GfxSampleCountFlags::SampleCount1,
                .format = GfxFormat::D24_UNORM_S8_UINT,
                .usageFlags = GfxImageUsageFlags::DepthStencilAttachment,
                .arena = GfxMemoryArena::PersistentGPU
            };

            self->mRenderTargetDepth = GfxBackend::CreateImage(desc);
        }
    }

    bool Initialize() override
    {
        Vfs::HelperMountDataAndShaders(SettingsJunkyard::Get().engine.connectToServer);

        if (!Engine::Initialize())
            return false;

        {
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

            AssetGroup assetGroup = Engine::RegisterInitializeResources(AppImpl::CreateGraphicsResources, this);
            mModelAsset = Asset::LoadModel("/data/models/Duck/Duck.gltf", loadParams, assetGroup);
            mModelShaderAsset = Asset::LoadShader("/shaders/Model.hlsl", ShaderLoadParams {}, assetGroup);
        }

        mFpsCam.SetLookAt(Float3(0, -2.0f, 3.0f), FLOAT3_ZERO);
        mOrbitCam.SetLookAt(Float3(0, -2.0f, 3.0f), FLOAT3_ZERO);
        mCam = &mOrbitCam;

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
    
    static void ChildTask(uint32 groupIndex, void*)
    {
        PROFILE_ZONE();
        
        Thread::Sleep(5);
    }

    static void MainTaskSub()
    {
        PROFILE_ZONE();
        Thread::Sleep(3);
        JobsHandle handle;
        handle = Jobs::Dispatch(JobsType::LongTask, ChildTask, nullptr, 1);
        Jobs::WaitForCompletionAndDelete(handle);
        Thread::Sleep(1);
    }

    static void MainTask(uint32 groupIndex, void*)
    {
        PROFILE_ZONE();
        Thread::Sleep(1);
        MainTaskSub();
        Thread::Sleep(7);
    }

    void Update(fl32 dt) override
    {
        PROFILE_ZONE();

        mCam->HandleMovementKeyboard(dt, 100.0f, 5.0f);

        Engine::BeginFrame(dt);
        GfxBackendCommandBuffer cmd = GfxBackend::BeginCommandBuffer(GfxQueueType::Graphics);

        float width = (float)App::GetFramebufferWidth();
        float height = (float)App::GetFramebufferHeight();

        {
            FrameTransform ubo {
                .viewMat = mCam->GetViewMat(),
                .projMat = GfxBackend::GetSwapchainTransformMat() * mCam->GetPerspectiveMat(width, height)
            };

            GfxBufferDesc stagingDesc {
                .sizeBytes = sizeof(ubo),
                .usageFlags = GfxBufferUsageFlags::TransferSrc,
                .arena = GfxMemoryArena::TransientCPU
            };
            GfxBufferHandle stagingBuff = GfxBackend::CreateBuffer(stagingDesc);

            FrameTransform* dstUbo;
            cmd.MapBuffer(stagingBuff, (void**)&dstUbo);
            *dstUbo = ubo;
            cmd.FlushBuffer(stagingBuff);

            cmd.TransitionBuffer(mUniformBuffer, GfxBufferTransition::TransferWrite);
            cmd.CopyBufferToBuffer(stagingBuff, mUniformBuffer, GfxShaderStage::Vertex);

            GfxBackend::DestroyBuffer(stagingBuff);
        }

        GfxBackendRenderPass pass { 
            .numAttachments = 1,
            .colorAttachments = {{ 
                .image = mRenderTargetColor,
                .clear = true,
                .clearValue = {
                    .color = Color::ToFloat4(COLOR_BLACK)
                }
            }},
            .depthAttachment = {
                .image = mRenderTargetDepth,
                .clear = true,
                .clearValue = {
                    .depth = 1.0f
                }
            },
            .hasDepth = true
        };

        cmd.TransitionImage(mRenderTargetColor, GfxImageTransition::RenderTarget);
        cmd.TransitionImage(mRenderTargetDepth, GfxImageTransition::RenderTarget);
        cmd.BeginRenderPass(pass);
        
        static Mat4 modelMat = MAT4_IDENT;

        { // draw something
            
            MemTempAllocator tmpAlloc;

            cmd.BindPipeline(mPipeline);

            // model transform
            // Viewport
            GfxViewport viewport {
                .width = width,
                .height = height,
            };

            cmd.SetViewports(0, 1, &viewport);

            RectInt scissor(0, 0, App::GetFramebufferWidth(), App::GetFramebufferHeight());
            cmd.SetScissors(0, 1, &scissor);

            AssetObjPtrScope<Model> model(mModelAsset);

            for (uint32 i = 0; i < model->numNodes; i++) {
                const ModelNode& node = model->nodes[i];
                if (node.meshId) {
                    ModelTransform transform {
                        .modelMat = modelMat * Transform3D::ToMat4(node.localTransform)
                    };
                    cmd.PushConstants(mPipelineLayout, "ModelTransform", &transform, sizeof(transform));

                    const ModelMesh& mesh = model->meshes[IdToIndex(node.meshId)];

                    // Buffers
                    uint64 offsets[MODEL_MAX_VERTEX_BUFFERS_PER_SHADER] = {};
                    cmd.BindVertexBuffers(0, mesh.numVertexBuffers, mesh.gpuBuffers.vertexBuffers, offsets);
                    cmd.BindIndexBuffer(mesh.gpuBuffers.indexBuffer, 0, GfxIndexType::Uint32);

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
                                .name = "FrameTransform",
                                .buffer = mUniformBuffer
                            },
                            {
                                .name = "BaseColorTexture",
                                .image = imgHandle
                            }
                        };
                        cmd.PushBindings(mPipelineLayout, CountOf(bindings), bindings);

                        cmd.DrawIndexed(mesh.numIndices, 1, 0, 0, 0);
                    }

                }
            }
        } // Draw
        cmd.EndRenderPass();

        cmd.TransitionImage(mRenderTargetColor, GfxImageTransition::CopySource);
        cmd.CopyImageToSwapchain(mRenderTargetColor);

        DebugDraw::BeginDraw(cmd, App::GetFramebufferWidth(), App::GetFramebufferHeight());
        DebugDrawGridProperties gridProps {
            .distance = 200,
            .lineColor = Color(0x565656), 
            .boldLineColor = Color(0xd6d6d6)            
        };

        DebugDraw::DrawGroundGrid(*mCam, gridProps);
        DebugDraw::EndDraw(cmd, *mCam, mRenderTargetDepth);

        if (ImGui::IsEnabled()) { // imgui test
            PROFILE_GPU_ZONE_NAME("ImGuiRender", true);
            DebugHud::DrawDebugHud(dt);
            DebugHud::DrawStatusBar(dt);

            ImGui::DrawFrame(cmd);
        }

        GfxBackend::EndCommandBuffer(cmd);
        GfxBackend::SubmitQueue(GfxQueueType::Graphics);

        Engine::EndFrame();
    }
    
    void OnEvent(const AppEvent& ev) override
    {
        if (!ImGui::IsAnyItemHovered() && !ImGui::GetIO().WantCaptureMouse && !ImGuizmo::IsOver())
            mCam->HandleRotationMouse(ev, 0.2f, 0.1f);
    }

    void ReleaseGraphicsResources()
    {
        GfxBackend::DestroyImage(mRenderTargetColor);
        GfxBackend::DestroyImage(mRenderTargetDepth);
        GfxBackend::DestroyPipeline(mPipeline);
        GfxBackend::DestroyPipelineLayout(mPipelineLayout);
        GfxBackend::DestroyBuffer(mUniformBuffer);
    }
};

int Main(int argc, char* argv[])
{
    SettingsJunkyard::Initialize({});

    #if PLATFORM_ANDROID
        Settings::InitializeFromAndroidAsset(App::AndroidGetAssetManager(), "Settings.ini");
    #else
        Settings::InitializeFromCommandLine(argc, argv);
    #endif
   
    static AppImpl impl;
    App::Run(AppDesc { 
        .callbacks = &impl, 
        .windowTitle = "Junkyard"
    });

    Settings::Release();
    return 0;
}

