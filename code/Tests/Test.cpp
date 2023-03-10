#include <stdio.h>

#include "../Core/System.h"
#include "../Core/Buffers.h"
#include "../Core/Hash.h"
#include "../Core/String.h"
#include "../Core/SocketIO.h"
#include "../Core/String.h"
#include "../Core/Settings.h"
#include "../Core/Jobs.h"
#include "../Core/TracyHelper.h"
#include "../Core/Atomic.h"
#include "../Core/Log.h"

#include "../Math/Math.h"

#include "../AssetManager.h"
#include "../VirtualFS.h"
#include "../Camera.h"
#include "../Engine.h"
#include "../Application.h"

#include "../Graphics/Graphics.h"
#include "../Graphics/DebugDraw.h"
#include "../Graphics/Shader.h"
#include "../Graphics/Model.h"

#include "../Tool/ImGuiTools.h"
#include "../Tool/Console.h"

#include "../UnityBuild.inl"

struct AppImpl : AppCallbacks
{
    GfxPipeline pipeline;
    GfxBuffer uniformBuffer;

    AssetHandleModel modelAsset;
    AssetHandleShader modelShaderAsset;
    Array<GfxDescriptorSet> descriptorSets;
    CameraFPS   fpsCam;
    CameraOrbit orbitCam;
    Camera*     cam;

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

        if (!CreateGraphicsObjects())
            return false;

        fpsCam.SetLookAt(Float3(0, -2.0f, 3.0f), kFloat3Zero);
        orbitCam.SetLookAt(Float3(0, -2.0f, 3.0f), kFloat3Zero);
        cam = &orbitCam;

        engineRegisterShortcut("TAB", [](void* userData) {
           AppImpl* app = reinterpret_cast<AppImpl*>(userData);
           if (app->cam == &app->orbitCam) {
               app->fpsCam.SetViewMat(app->cam->GetViewMat());
               app->cam = &app->fpsCam;
           }
           else {
               app->cam = &app->orbitCam;
           }
        }, this);

        logInfo("Use right mouse button to rotate camera. And [TAB] to switch between Orbital and FPS (WASD) camera");

        return true;
    };
    
    void Cleanup() override
    {
        ReleaseGraphicsObjects();

        assetUnload(modelAsset);
        assetUnload(modelShaderAsset);

        engineRelease();
    };
    
    static void OddTask(uint32 groupIndex, void*)
    {
        PROFILE_ZONE(true);
        //logDebug("OddTask - %u", threadId);
    }

    static void EvenTask(uint32 groupIndex, void*)
    {
        PROFILE_ZONE(true);
        //logDebug("EvenTask - %u", threadId);
        // threadSleep(100);
    }

    static void SomeTask(uint32 groupIndex, void*)
    {
        PROFILE_ZONE(true);

        // _private::jobsDebugThreadStats();
        JobsHandle handle1 = jobsDispatch(JobsType::ShortTask, EvenTask, nullptr, 1);
        //JobsHandle handle2 = jobsDispatch(JobsType::ShortTask, OddTask, nullptr, 1);
        jobsWaitForCompletion(handle1);
        //jobsWaitForCompletion(handle2);
        //logDebug("Done");
    }
    
    void Update(fl32 dt) override
    {
        PROFILE_ZONE(true);

        // JobsHandle handle = jobsDispatch(JobsType::LongTask, SomeTask, nullptr, 1);

        cam->HandleMovementKeyboard(dt, 10.0f, 5.0f);

        engineBeginFrame(dt);
        
        gfxBeginCommandBuffer();
        
        gfxCmdBeginSwapchainRenderPass();

        float width = (float)appGetFramebufferWidth();
        float height = (float)appGetFramebufferHeight();

        static Mat4 modelMat = kMat4Ident;

        { // draw something
            PROFILE_ZONE_NAME("DrawSomething", true);
            PROFILE_GPU_ZONE_NAME("DrawSomething", true);

            // We are drawing to swapchain, so we need ClipSpaceTransform
            FrameTransform ubo {
                .viewMat = cam->GetViewMat(),
                .projMat = gfxGetClipspaceTransform() * cam->GetPerspectiveMat(width, height)
            };

            gfxCmdUpdateBuffer(uniformBuffer, &ubo, sizeof(ubo));
            gfxCmdBindPipeline(pipeline);

            // model transform
            // Viewport
            GfxViewport viewport {
                .width = width,
                .height = height,
            };

            gfxCmdSetViewports(0, 1, &viewport, true);

            Recti scissor(0, 0, appGetFramebufferWidth(), appGetFramebufferHeight());
            gfxCmdSetScissors(0, 1, &scissor, true);

            Model* model = assetGetModel(modelAsset);

            for (uint32 i = 0; i < model->numNodes; i++) {
                const ModelNode& node = model->nodes[i];
                if (node.meshId) {
                    Mat4 worldMat = modelMat * transform3DToMat4(node.localTransform);
                    gfxCmdPushConstants(pipeline, GfxShaderStage::Vertex, &worldMat, sizeof(worldMat));

                    const ModelMesh& mesh = model->meshes[IdToIndex(node.meshId)];

                    // Buffers
                    uint64* offsets = (uint64*)alloca(sizeof(uint64)*mesh.numVertexBuffers);
                    memset(offsets, 0x0, sizeof(uint64)*mesh.numVertexBuffers);
                    gfxCmdBindVertexBuffers(0, mesh.numVertexBuffers, mesh.gpuBuffers.vertexBuffers, offsets);
                    gfxCmdBindIndexBuffer(mesh.gpuBuffers.indexBuffer, 0, GfxIndexType::Uint32);

                    // DescriptorSets
                    for (uint32 smi = 0; smi < mesh.numSubmeshes; smi++) {
                        const ModelSubmesh& submesh = mesh.submeshes[smi];

                        GfxDescriptorSet dset(PtrToInt<uint32>(submesh.material->userData));
                        gfxCmdBindDescriptorSets(1, &dset);
                        gfxCmdDrawIndexed(mesh.numIndices, 1, 0, 0, 0);
                    }

                }
            }            
        }

        {
            ddDrawGrid_XYAxis(*cam, width, height, DebugDrawGridProperties { 
                .lineColor = Color(0x565656), 
                .boldLineColor = Color(0xd6d6d6) 
            });
        }

        if (imguiIsEnabled()) { // imgui test
            PROFILE_GPU_ZONE_NAME("ImGuiRender", true);
            imguiBudgetHub(dt);
            imguiQuickInfoHud(dt);

            if constexpr (0) {
                Mat4 view = fpsCam.GetViewMat();
                ImGuizmo::ViewManipulate(view.f, 0.1f, ImVec2(5.0f, height - 128.0f - 5.0f), ImVec2(128, 128), 0xff000000);
                fpsCam.SetViewMat(view);
            }

            imguiRender();
        }

        // jobsWaitForCompletion(handle);

        gfxCmdEndSwapchainRenderPass();
        gfxEndCommandBuffer();        

        engineEndFrame(dt);
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
            cam->HandleRotationMouse(ev, 0.2f, 0.1f);
    }

    bool CreateGraphicsObjects()
    {
        // Graphics Objects
        const GfxDescriptorSetLayoutBinding bindingLayout[] = {
            {
                .name = "ModelTransform",
                .type = GfxDescriptorType::UniformBuffer,
                .stages = GfxShaderStage::Vertex,
                .pushConstantSize = sizeof(Mat4)
            },
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

        {
            AssetBarrierScope b;
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
                .indexBufferUsage = GfxBufferUsage::Immutable
            };

            modelAsset = assetLoadModel("/data/models/Duck/Duck.gltf", loadParams, b.Barrier());
            modelShaderAsset = assetLoadShader("/code/shaders/Model.hlsl", ShaderCompileDesc {}, b.Barrier());
        }
        if (!assetIsAlive(modelAsset) || !assetIsAlive(modelShaderAsset))
            return false;

        uniformBuffer = gfxCreateBuffer(GfxBufferDesc {
                                        .size = sizeof(FrameTransform),
                                        .type = GfxBufferType::Uniform,
                                        .usage = GfxBufferUsage::Stream
        });

        Model* model = assetGetModel(modelAsset);

        pipeline = gfxCreatePipeline(GfxPipelineDesc {
            .shader = assetGetShader(modelShaderAsset),
            .inputAssemblyTopology = GfxPrimitiveTopology::TriangleList,
            .numDescriptorSetBindings = CountOf(bindingLayout),
            .descriptorSetBindings = bindingLayout,
            .numVertexInputAttributes = CountOf(vertexInputAttDescs),
            .vertexInputAttributes = vertexInputAttDescs,
            .numVertexBufferBindings = 1,
            .vertexBufferBindings = &vertexBufferBindingDesc,
            .rasterizer = GfxRasterizerDesc {
                .cullMode = GfxCullModeFlags::Back
            },
            .blend = {
                .numAttachments = 1,
                .attachments = gfxBlendAttachmentDescGetDefault()
            },
            .depthStencil = GfxDepthStencilDesc {
                .depthTestEnable = true,
                .depthWriteEnable = true,
                .depthCompareOp = GfxCompareOp::Less
            }
        });

        // TODO: TEMP create descriptor sets and assign them to material userData for later rendering
        for (uint32 i = 0; i < model->numMeshes; i++) {
            ModelMesh& mesh = model->meshes[i];
            for (uint32 smi = 0; smi < mesh.numSubmeshes; smi++) {
                ModelMaterial* material = mesh.submeshes[smi].material.Get();

                GfxImage albedo = material->pbrMetallicRoughness.baseColorTex.texture.IsValid() ?
                    assetGetImage(material->pbrMetallicRoughness.baseColorTex.texture) :
                    GfxImage();

                GfxDescriptorSet dset = gfxCreateDescriptorSet(pipeline);
                
                GfxDescriptorBindingDesc descBindings[] = {
                    {
                        .name = "FrameTransform",
                        .type = GfxDescriptorType::UniformBuffer,
                        .buffer = { uniformBuffer, 0, sizeof(FrameTransform) }
                    },
                    {
                        .name = "BaseColorTexture",
                        .type = GfxDescriptorType::CombinedImageSampler,
                        .image = albedo
                    }
                };

                gfxUpdateDescriptorSet(dset, CountOf(descBindings), descBindings);

                material->userData = IntToPtr<uint32>(dset.id);
                descriptorSets.Push(dset);
            }
        }

        return true;
    }

    void ReleaseGraphicsObjects()
    {
        gfxWaitForIdle();
        for (uint32 i = 0; i < descriptorSets.Count(); i++) 
            gfxDestroyDescriptorSet(descriptorSets[i]);
        gfxDestroyPipeline(pipeline);
        gfxDestroyBuffer(uniformBuffer);
        descriptorSets.Free();
    }
};

int Main(int argc, char* argv[])
{
    settingsInitialize(SettingsAll {
        .engine = {
            .logLevel = SettingsEngine::LogLevel::Debug,
        }
    });

    if constexpr (PLATFORM_ANDROID)
        settingsLoadFromINI("/assets/Settings.ini");
    else
        settingsLoadFromCommandLine(argc, argv);
    
    static AppImpl impl;
    appInitialize(AppDesc { 
        .callbacks = &impl, 
        .windowTitle = "Junkyard",
        .highDPI = false
    });

    settingsRelease();
    return 0;
}