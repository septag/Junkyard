#include <stdio.h>

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
    GfxBufferHandle mUniformBuffer;
    GfxDescriptorSetLayoutHandle mDsLayout;

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

    struct FrameTransform 
    {
        Mat4 viewMat;
        Mat4 projMat;
    };

    static void CreateGraphicsResources(void* userData)
    {
        AppImpl* self = (AppImpl*)userData;

        // Graphics Objects
        const GfxDescriptorSetLayoutBinding bindingLayout[] = {
            {
                .name = "FrameTransform",
                .type = GfxDescriptorType::UniformBuffer,
                .stages = GfxShaderStage::Vertex
            },
            {
                .name = "BaseColorTexture",
                .type = GfxDescriptorType::CombinedImageSampler,
                .stages = GfxShaderStage::Fragment
            }
        };

        GfxPushConstantDesc pushConstant = {
            .name = "ModelTransform",
            .stages = GfxShaderStage::Vertex,
            .range = { 0, sizeof(Mat4) }
        };

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

        GfxBufferDesc bufferDesc {
            .size = sizeof(FrameTransform),
            .type = GfxBufferType::Uniform,
            .usage = GfxBufferUsage::Stream 
        };

        AssetObjPtrScope<GfxShader> shader(self->mModelShaderAsset);
        self->mUniformBuffer = gfxCreateBuffer(bufferDesc);
        self->mDsLayout = gfxCreateDescriptorSetLayout(*shader, bindingLayout, CountOf(bindingLayout), 
                                                       GfxDescriptorSetLayoutFlags::PushDescriptor);


        self->mPipeline = gfxCreatePipeline(GfxPipelineDesc {
            .shader = shader,
            .inputAssemblyTopology = GfxPrimitiveTopology::TriangleList,
            .numDescriptorSetLayouts = 1,
            .descriptorSetLayouts = &self->mDsLayout,
            .numPushConstants = 1,
            .pushConstants = &pushConstant,
            .numVertexInputAttributes = CountOf(vertexInputAttDescs),
            .vertexInputAttributes = vertexInputAttDescs,
            .numVertexBufferBindings = 1,
            .vertexBufferBindings = &vertexBufferBindingDesc,
            .rasterizer = GfxRasterizerDesc {
                .cullMode = GfxCullModeFlags::Back
            },
            .blend = {
                .numAttachments = 1,
                .attachments = GfxBlendAttachmentDesc::GetDefault()
            },
            .depthStencil = GfxDepthStencilDesc {
                .depthTestEnable = true,
                .depthWriteEnable = true,
                .depthCompareOp = GfxCompareOp::Less
            }
        });
    }

    bool Initialize() override
    {
        MemTempAllocator::EnableCallstackCapture(true);

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
                },
                .vertexBufferUsage = GfxBufferUsage::Immutable,
                .indexBufferUsage = GfxBufferUsage::Immutable,
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
        
        gfxBeginCommandBuffer();

        float width = (float)App::GetFramebufferWidth();
        float height = (float)App::GetFramebufferHeight();

        static Mat4 modelMat = MAT4_IDENT;

        { // draw something
            
            // JobsHandle handle = jobsDispatch(JobsType::LongTask, MainTask);
            // jobsWaitForCompletion(handle);

            MemTempAllocator tmpAlloc;

            // PROFILE_ZONE_NAME("DrawSomething", true);
            PROFILE_GPU_ZONE_NAME("DrawSomething", true);

            // We are drawing to swapchain, so we need ClipSpaceTransform
            FrameTransform ubo {
                .viewMat = mCam->GetViewMat(),
                .projMat = gfxGetClipspaceTransform() * mCam->GetPerspectiveMat(width, height)
            };

            gfxCmdUpdateBuffer(mUniformBuffer, &ubo, sizeof(ubo));
            gfxCmdBeginSwapchainRenderPass();
            gfxCmdBindPipeline(mPipeline);

            // model transform
            // Viewport
            GfxViewport viewport {
                .width = width,
                .height = height,
            };

            gfxCmdSetViewports(0, 1, &viewport, true);

            RectInt scissor(0, 0, App::GetFramebufferWidth(), App::GetFramebufferHeight());
            gfxCmdSetScissors(0, 1, &scissor, true);

            AssetObjPtrScope<Model> model(mModelAsset);

            for (uint32 i = 0; i < model->numNodes; i++) {
                const ModelNode& node = model->nodes[i];
                if (node.meshId) {
                    Mat4 worldMat = modelMat * Transform3D::ToMat4(node.localTransform);
                    gfxCmdPushConstants(mPipeline, GfxShaderStage::Vertex, &worldMat, sizeof(worldMat));

                    const ModelMesh& mesh = model->meshes[IdToIndex(node.meshId)];

                    // Buffers
                    uint64* offsets = tmpAlloc.MallocTyped<uint64>(mesh.numVertexBuffers);
                    memset(offsets, 0x0, sizeof(uint64)*mesh.numVertexBuffers);
                    gfxCmdBindVertexBuffers(0, mesh.numVertexBuffers, mesh.gpuBuffers.vertexBuffers, offsets);
                    gfxCmdBindIndexBuffer(mesh.gpuBuffers.indexBuffer, 0, GfxIndexType::Uint32);

                    for (uint32 smi = 0; smi < mesh.numSubmeshes; smi++) {
                        const ModelSubmesh& submesh = mesh.submeshes[smi];
                        const ModelMaterial* mtl = model->materials[IdToIndex(submesh.materialId)].Get();
                        
                        GfxImageHandle imgHandle {};

                        if (mtl->pbrMetallicRoughness.baseColorTex.texture.IsValid()) {
                            AssetObjPtrScope<GfxImage> img(mtl->pbrMetallicRoughness.baseColorTex.texture);
                            if (!img.IsNull())
                                imgHandle = img->handle;
                        }

                        GfxDescriptorBindingDesc bindings[] = {
                            {
                                .name = "FrameTransform",
                                .type = GfxDescriptorType::UniformBuffer,
                                .buffer = { mUniformBuffer, 0, sizeof(FrameTransform) }
                            },
                            {
                                .name = "BaseColorTexture",
                                .type = GfxDescriptorType::CombinedImageSampler,
                                .image = imgHandle
                            }
                        };
                        gfxCmdPushDescriptorSet(mPipeline, GfxPipelineBindPoint::Graphics, 0, CountOf(bindings), bindings);

                        gfxCmdDrawIndexed(mesh.numIndices, 1, 0, 0, 0);
                    }

                }
            }
        } // Draw

        {
            DebugDraw::DrawGroundGrid(*mCam, width, height, DebugDrawGridProperties { 
                .lineColor = Color(0x565656), 
                .boldLineColor = Color(0xd6d6d6) 
            });
        }

        if (ImGui::IsEnabled()) { // imgui test
            PROFILE_GPU_ZONE_NAME("ImGuiRender", true);
            DebugHud::DrawQuickFrameInfo(dt);
            DebugHud::DrawStatusBar(dt);
            DebugHud::DrawMemBudgets(dt);

            #if 0
                Mat4 view = mFpsCam.GetViewMat();
                ImGuizmo::ViewManipulate(view.f, 0.1f, ImVec2(5.0f, height - 128.0f - 5.0f), ImVec2(128, 128), 0xff000000);
                mFpsCam.SetViewMat(view);
            #endif

            ImGui::DrawFrame();
        }

        // jobsWaitForCompletion(handle);

        gfxCmdEndSwapchainRenderPass();
        gfxEndCommandBuffer();        

        Engine::EndFrame();
    }
    
    void OnEvent(const AppEvent& ev) override
    {
        switch (ev.type) {
        case AppEventType::Resized:
            gfxResizeSwapchain(ev.framebufferWidth, ev.framebufferHeight);
            break;
        default:
            break;
        }

        //if (!ImGui::IsWindowHovered())
        if (!ImGui::IsAnyItemHovered() && !ImGui::GetIO().WantCaptureMouse && !ImGuizmo::IsOver())
            mCam->HandleRotationMouse(ev, 0.2f, 0.1f);
    }

    void ReleaseGraphicsResources()
    {
        gfxWaitForIdle();
        gfxDestroyDescriptorSetLayout(mDsLayout);
        gfxDestroyPipeline(mPipeline);
        gfxDestroyBuffer(mUniformBuffer);
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

