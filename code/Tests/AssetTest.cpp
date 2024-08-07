#include <stdio.h>

#include "../Core/Settings.h"
#include "../Core/Log.h"
#include "../Core/TracyHelper.h"
#include "../Core/Jobs.h"
#include "../Core/MathAll.h"

#include "../Common/VirtualFS.h"
#include "../Common/Application.h"
#include "../Common/Camera.h"
#include "../Common/JunkyardSettings.h"

#include "../Assets/AssetManager.h"
#include "../Assets/Model.h"
#include "../Assets/Shader.h"

#include "../DebugTools/DebugDraw.h"
#include "../DebugTools/DebugHud.h"

#include "../ImGui/ImGuiMain.h"
#include "../ImGui/ImGuizmo.h"

#include "../UnityBuild.inl"

#include "../Engine.h"

static constexpr uint32 kNumCubes = 10;

struct AppImpl : AppCallbacks
{
    GfxPipeline pipeline;
    GfxBuffer uniformBuffer;
    GfxDynamicUniformBuffer transformsBuffer;
    GfxDescriptorSetLayout dsLayout;

    AssetHandleModel modelAsset;
    AssetHandleImage testImageAssets[kNumCubes];
    AssetHandleShader modelShaderAsset;
    GfxDescriptorSet descriptorSet;
    CameraFPS   fpsCam;
    CameraOrbit orbitCam;
    Camera*     cam;

    struct Vertex 
    {
        Float3 pos;
        Float2 uv;
    };

    struct WorldTransform
    {
        Mat4 worldMat;
    };

    struct FrameTransform 
    {
        Mat4 viewMat;
        Mat4 projMat;
    };

    bool Initialize() override
    {
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

        for (uint32 i = 0; i < kNumCubes; i++) 
            assetUnload(testImageAssets[i]);
        assetUnload(modelAsset);
        assetUnload(modelShaderAsset);

        Engine::Release();
    };

    void Update(fl32 dt) override
    {
        PROFILE_ZONE(true);

        cam->HandleMovementKeyboard(dt, 10.0f, 5.0f);

        Engine::BeginFrame(dt);
        
        gfxBeginCommandBuffer();
        
        gfxCmdBeginSwapchainRenderPass(Color(100, 100, 100));

        float width = (float)App::GetFramebufferWidth();
        float height = (float)App::GetFramebufferHeight();

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

            Recti scissor(0, 0, App::GetFramebufferWidth(), App::GetFramebufferHeight());
            gfxCmdSetScissors(0, 1, &scissor, true);

            Model* model = assetGetModel(modelAsset);

            for (uint32 inst = 0; inst < kNumCubes; inst++) {
                Mat4 modelMat = mat4Translate(float(inst)*1.5f, 0, 0);
                *((Mat4*)transformsBuffer.Data(inst)) = modelMat;
            }
            transformsBuffer.Flush(0u, kNumCubes);

            for (uint32 inst = 0; inst < kNumCubes; inst++) {
                for (uint32 i = 0; i < model->numNodes; i++) {
                    const ModelNode& node = model->nodes[i];
                    if (node.meshId) {
                        const ModelMesh& mesh = model->meshes[IdToIndex(node.meshId)];
    
                        // Buffers
                        uint64* offsets = (uint64*)alloca(sizeof(uint64)*mesh.numVertexBuffers);
                        memset(offsets, 0x0, sizeof(uint64)*mesh.numVertexBuffers);
                        gfxCmdBindVertexBuffers(0, mesh.numVertexBuffers, mesh.gpuBuffers.vertexBuffers, offsets);
                        gfxCmdBindIndexBuffer(mesh.gpuBuffers.indexBuffer, 0, GfxIndexType::Uint32);

                        uint32 materialData[] = {inst, 0, 0, 0};
                        gfxCmdPushConstants(pipeline, GfxShaderStage::Fragment, materialData, sizeof(materialData));
    
                        // DescriptorSets
                        for (uint32 smi = 0; smi < mesh.numSubmeshes; smi++) {
                            uint32 dynOffset = transformsBuffer.Offset(inst);
    
                            gfxCmdBindDescriptorSets(pipeline, 1, &descriptorSet, &dynOffset, 1);
                            gfxCmdDrawIndexed(mesh.numIndices, 1, 0, 0, 0);
                        }    
                    }  
                }       // foreach (node)     
            }   // foreach (instance)
        }

        {
            DebugDraw::DrawGroundGrid(*cam, width, height, DebugDrawGridProperties { 
                .lineColor = Color(0x565656), 
                .boldLineColor = Color(0xd6d6d6) 
            });
        }

        if (ImGui::IsEnabled()) { // imgui test
            PROFILE_GPU_ZONE_NAME("ImGuiRender", true);
            DebugHud::DrawMemBudgets(dt);
            DebugHud::DrawQuickFrameInfo(dt);

            #if 0
            Mat4 view = fpsCam.GetViewMat();
            ImGuizmo::ViewManipulate(view.f, 0.1f, ImVec2(5.0f, height - 128.0f - 5.0f), ImVec2(128, 128), 0xff000000);
            fpsCam.SetViewMat(view);
            #endif

            ImGui::DrawFrame();
        }

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

        if (!ImGui::IsAnyItemHovered() && !ImGui::GetIO().WantCaptureMouse && !ImGuizmo::IsOver())
            cam->HandleRotationMouse(ev, 0.2f, 0.1f);
    }

    bool CreateGraphicsObjects()
    {
        // Graphics Objects
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
                        {"TEXCOORD", 0, 0, GfxFormat::R32G32_SFLOAT, offsetof(Vertex, uv)}
                    },
                    .vertexBufferStrides = {
                        sizeof(Vertex)
                    }
                },
                .vertexBufferUsage = GfxBufferUsage::Immutable,
                .indexBufferUsage = GfxBufferUsage::Immutable
            };

            modelAsset = assetLoadModel("/data/models/HighPolyBox/HighPolyBox.gltf", loadParams, b.Barrier());
            modelShaderAsset = assetLoadShader("/shaders/Unlit.hlsl", ShaderLoadParams {}, b.Barrier());
            for (uint32 i = 0; i < kNumCubes; i++) {
                char imagePath[128];
                strPrintFmt(imagePath, sizeof(imagePath), "/data/images/gen/%u.png", i+1);
                testImageAssets[i] = assetLoadImage(imagePath, ImageLoadParams {}, b.Barrier());
            }
        }
        if (!assetIsAlive(modelAsset) || !assetIsAlive(modelShaderAsset))
            return false;


        {
            const GfxDescriptorSetLayoutBinding bindingLayout[] = {
                {
                    .name = "ModelTransform",
                    .type = GfxDescriptorType::UniformBufferDynamic,
                    .stages = GfxShaderStage::Vertex,
                },
                {
                    .name = "FrameTransform",
                    .type = GfxDescriptorType::UniformBuffer,
                    .stages = GfxShaderStage::Vertex
                },
                {
                    .name = "BaseColorTextures",
                    .type = GfxDescriptorType::CombinedImageSampler,
                    .stages = GfxShaderStage::Fragment,
                    .arrayCount = kNumCubes
                }
            };

            dsLayout = gfxCreateDescriptorSetLayout(*assetGetShader(modelShaderAsset), bindingLayout, CountOf(bindingLayout));
        }

        uniformBuffer = gfxCreateBuffer(GfxBufferDesc {
                                        .size = sizeof(FrameTransform),
                                        .type = GfxBufferType::Uniform,
                                        .usage = GfxBufferUsage::Stream
        });

        transformsBuffer = gfxCreateDynamicUniformBuffer(kNumCubes, sizeof(WorldTransform));

        GfxPushConstantDesc pushConstant = {
            .name = "Material",
            .stages = GfxShaderStage::Fragment,
            .range = {0, sizeof(uint32)*4}
        };

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

        // TODO: TEMP create descriptor sets and assign them to material userData for later rendering
        GfxImage images[kNumCubes];
        for (uint32 i = 0; i < kNumCubes; i++)
            images[i] = assetGetImage(testImageAssets[i]);
        descriptorSet = gfxCreateDescriptorSet(dsLayout);
        GfxDescriptorBindingDesc descBindings[] = {
            {
                .name = "ModelTransform",
                .type = GfxDescriptorType::UniformBufferDynamic,
                .buffer = {transformsBuffer.buffer, 0, transformsBuffer.stride}
            },
            {
                .name = "FrameTransform",
                .type = GfxDescriptorType::UniformBuffer,
                .buffer = { uniformBuffer, 0, sizeof(FrameTransform) }
            },
            {
                .name = "BaseColorTextures",
                .type = GfxDescriptorType::CombinedImageSampler,
                .imageArrayCount = kNumCubes,
                .imageArray = images
            }
        };
        gfxUpdateDescriptorSet(descriptorSet, CountOf(descBindings), descBindings);
        return true;
    }

    void ReleaseGraphicsObjects()
    {
        gfxWaitForIdle();
        gfxDestroyDescriptorSet(descriptorSet);
        gfxDestroyPipeline(pipeline);
        gfxDestroyDescriptorSetLayout(dsLayout);
        gfxDestroyBuffer(uniformBuffer);
        gfxDestroyDynamicUniformBuffer(transformsBuffer);
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

    LOG_DEBUG("Initializing engine.");
    
    static AppImpl impl;
    App::Run(AppDesc { 
        .callbacks = &impl, 
        .windowTitle = "Junkyard: Asset Loading test",
    });

    Settings::Release();
    return 0;
}
