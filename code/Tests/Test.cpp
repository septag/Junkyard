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
    GfxPipeline pipeline;
    GfxBuffer uniformBuffer;
    GfxDescriptorSetLayout dsLayout;

    AssetHandleModel modelAsset;
    AssetHandleShader modelShaderAsset;
    Array<GfxDescriptorSet> descriptorSets;
    HashTable<GfxDescriptorSet> materialToDset;
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
        MemTempAllocator::EnableCallstackCapture(true);

        Vfs::HelperMountDataAndShaders(SettingsJunkyard::Get().engine.connectToServer);

        if (!Engine::Initialize())
            return false;

        if (!CreateGraphicsObjects())
            return false;

        fpsCam.SetLookAt(Float3(0, -2.0f, 3.0f), FLOAT3_ZERO);
        orbitCam.SetLookAt(Float3(0, -2.0f, 3.0f), FLOAT3_ZERO);
        cam = &orbitCam;

        Engine::RegisterShortcut("TAB", [](void* userData) {
           AppImpl* app = reinterpret_cast<AppImpl*>(userData);
           if (app->cam == &app->orbitCam) {
               app->fpsCam.SetViewMat(app->cam->GetViewMat());
               app->cam = &app->fpsCam;
           }
           else {
               app->cam = &app->orbitCam;
           }
        }, this);

        LOG_INFO("Use right mouse button to rotate camera. And [TAB] to switch between Orbital and FPS (WASD) camera");

        return true;
    };
    
    void Cleanup() override
    {
        ReleaseGraphicsObjects();

        assetUnload(modelAsset);
        assetUnload(modelShaderAsset);

        Engine::Release();

    };
    
    static void ChildTask(uint32 groupIndex, void*)
    {
        PROFILE_ZONE(true);
        
        Thread::Sleep(5);
    }

    static void MainTaskSub()
    {
        PROFILE_ZONE(true);
        Thread::Sleep(3);
        JobsHandle handle;
        handle = Jobs::Dispatch(JobsType::LongTask, ChildTask, nullptr, 1);
        Jobs::WaitForCompletion(handle);
        Thread::Sleep(1);
    }

    static void MainTask(uint32 groupIndex, void*)
    {
        PROFILE_ZONE(true);
        Thread::Sleep(1);
        MainTaskSub();
        Thread::Sleep(7);
    }

    void Update(fl32 dt) override
    {
        PROFILE_ZONE(true);

        cam->HandleMovementKeyboard(dt, 100.0f, 5.0f);

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
                .viewMat = cam->GetViewMat(),
                .projMat = gfxGetClipspaceTransform() * cam->GetPerspectiveMat(width, height)
            };

            gfxCmdUpdateBuffer(uniformBuffer, &ubo, sizeof(ubo));
            gfxCmdBeginSwapchainRenderPass();
            gfxCmdBindPipeline(pipeline);

            // model transform
            // Viewport
            GfxViewport viewport {
                .width = width,
                .height = height,
            };

            gfxCmdSetViewports(0, 1, &viewport, true);

            Recti scissor(0, 0, App::GetFramebufferWidth(), App::GetFramebufferHeight());
            gfxCmdSetScissors(0, 1, &scissor, true);

            Model* model = assetGetModel(modelAsset);

            for (uint32 i = 0; i < model->numNodes; i++) {
                const ModelNode& node = model->nodes[i];
                if (node.meshId) {
                    Mat4 worldMat = modelMat * transform3DToMat4(node.localTransform);
                    gfxCmdPushConstants(pipeline, GfxShaderStage::Vertex, &worldMat, sizeof(worldMat));

                    const ModelMesh& mesh = model->meshes[IdToIndex(node.meshId)];

                    // Buffers
                    uint64* offsets = tmpAlloc.MallocTyped<uint64>(mesh.numVertexBuffers);
                    memset(offsets, 0x0, sizeof(uint64)*mesh.numVertexBuffers);
                    gfxCmdBindVertexBuffers(0, mesh.numVertexBuffers, mesh.gpuBuffers.vertexBuffers, offsets);
                    gfxCmdBindIndexBuffer(mesh.gpuBuffers.indexBuffer, 0, GfxIndexType::Uint32);

                    // DescriptorSets
                    for (uint32 smi = 0; smi < mesh.numSubmeshes; smi++) {
                        const ModelSubmesh& submesh = mesh.submeshes[smi];
                        const ModelMaterial* mtl = model->materials[IdToIndex(submesh.materialId)].Get();
                        
                        GfxDescriptorSet dset = materialToDset.FindAndFetch(Hash::Murmur32(mtl, sizeof(*mtl), 0));
                        gfxCmdBindDescriptorSets(pipeline, 1, &dset);
                        gfxCmdDrawIndexed(mesh.numIndices, 1, 0, 0, 0);
                    }

                }
            }
        }

        {
            DebugDraw::DrawGroundGrid(*cam, width, height, DebugDrawGridProperties { 
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
                Mat4 view = fpsCam.GetViewMat();
                ImGuizmo::ViewManipulate(view.f, 0.1f, ImVec2(5.0f, height - 128.0f - 5.0f), ImVec2(128, 128), 0xff000000);
                fpsCam.SetViewMat(view);
            #endif

            ImGui::DrawFrame();
        }

        // jobsWaitForCompletion(handle);

        gfxCmdEndSwapchainRenderPass();
        gfxEndCommandBuffer();        

        Engine::EndFrame(dt);
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

        GfxPushConstantDesc pushConstant = GfxPushConstantDesc {
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
                .indexBufferUsage = GfxBufferUsage::Immutable,
            };

            modelAsset = assetLoadModel("/data/models/Duck/Duck.gltf", loadParams, b.Barrier());
            modelShaderAsset = assetLoadShader("/shaders/Model.hlsl", ShaderLoadParams {}, b.Barrier());
        }
        if (!assetIsAlive(modelAsset) || !assetIsAlive(modelShaderAsset))
            return false;

        uniformBuffer = gfxCreateBuffer(GfxBufferDesc {
                                        .size = sizeof(FrameTransform),
                                        .type = GfxBufferType::Uniform,
                                        .usage = GfxBufferUsage::Stream
        });

        dsLayout = gfxCreateDescriptorSetLayout(*assetGetShader(modelShaderAsset), bindingLayout, CountOf(bindingLayout), false);

        Model* model = assetGetModel(modelAsset);

        pipeline = gfxCreatePipeline(GfxPipelineDesc {
            .shader = assetGetShader(modelShaderAsset),
            .inputAssemblyTopology = GfxPrimitiveTopology::TriangleList,
            .numDescriptorSetLayouts = 1,
            .descriptorSetLayouts = &dsLayout,
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

        for (uint32 i = 0; i < model->numMeshes; i++) {
            ModelMesh& mesh = model->meshes[i];
            for (uint32 smi = 0; smi < mesh.numSubmeshes; smi++) {
                ModelMaterial* material = model->materials[IdToIndex(mesh.submeshes[smi].materialId)].Get();

                GfxImage albedo = material->pbrMetallicRoughness.baseColorTex.texture.IsValid() ?
                    assetGetImage(material->pbrMetallicRoughness.baseColorTex.texture) :
                    GfxImage();

                GfxDescriptorSet dset = gfxCreateDescriptorSet(dsLayout);
                
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

                materialToDset.Add(Hash::Murmur32(material, sizeof(*material), 0), dset);
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
        gfxDestroyDescriptorSetLayout(dsLayout);
        gfxDestroyPipeline(pipeline);
        gfxDestroyBuffer(uniformBuffer);
        descriptorSets.Free();
        materialToDset.Free();
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
