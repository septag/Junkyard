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

static constexpr uint32 NUM_CUBES = 10;

struct AppImpl : AppCallbacks
{
    GfxPipelineHandle mPipeline;
    GfxBufferHandle mUniformBuffer;
    GfxDynamicUniformBuffer mTransformBuffer;
    GfxDescriptorSetLayoutHandle mDSLayout;
    GfxDescriptorSetHandle mDescriptorSet;
    AssetHandleShader mUnlitShader;

    CameraFPS   mFpsCam;
    CameraOrbit mOrbitCam;
    Camera*     mCam;

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
        ASSERT_MSG(0, "Not fully implemented yet");

        Vfs::HelperMountDataAndShaders(SettingsJunkyard::Get().engine.connectToServer);

        if (!Engine::Initialize())
            return false;

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

        {
            AssetGroup group = Engine::RegisterInitializeResources(AppImpl::CreateGraphicsResources, this);
            mUnlitShader = Asset::LoadShader("/shaders/Unlit.hlsl", {}, group);
        }


        return true;
    };
    
    void Cleanup() override
    {
        ReleaseGraphicsObjects();


        Engine::Release();
    };

    void Update(fl32 dt) override
    {
        PROFILE_ZONE();

        mCam->HandleMovementKeyboard(dt, 10.0f, 5.0f);

        Engine::BeginFrame(dt);
        
        gfxBeginCommandBuffer();
        
        gfxCmdBeginSwapchainRenderPass(Color(100, 100, 100));

        float width = (float)App::GetFramebufferWidth();
        float height = (float)App::GetFramebufferHeight();

        { // draw something
            PROFILE_ZONE_NAME("DrawSomething");
            PROFILE_GPU_ZONE_NAME("DrawSomething", true);

            // We are drawing to swapchain, so we need ClipSpaceTransform
            FrameTransform ubo {
                .viewMat = mCam->GetViewMat(),
                .projMat = gfxGetClipspaceTransform() * mCam->GetPerspectiveMat(width, height)
            };

            gfxCmdUpdateBuffer(mUniformBuffer, &ubo, sizeof(ubo));
            gfxCmdBindPipeline(mPipeline);

            // model transform
            // Viewport
            GfxViewport viewport {
                .width = width,
                .height = height,
            };

            gfxCmdSetViewports(0, 1, &viewport, true);

            Recti scissor(0, 0, App::GetFramebufferWidth(), App::GetFramebufferHeight());
            gfxCmdSetScissors(0, 1, &scissor, true);

            Model* model = nullptr;
            ASSERT(model);

            for (uint32 inst = 0; inst < NUM_CUBES; inst++) {
                Mat4 modelMat = mat4Translate(float(inst)*1.5f, 0, 0);
                *((Mat4*)mTransformBuffer.Data(inst)) = modelMat;
            }
            mTransformBuffer.Flush(0u, NUM_CUBES);

            for (uint32 inst = 0; inst < NUM_CUBES; inst++) {
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
                        gfxCmdPushConstants(mPipeline, GfxShaderStage::Fragment, materialData, sizeof(materialData));
    
                        // DescriptorSets
                        for (uint32 smi = 0; smi < mesh.numSubmeshes; smi++) {
                            uint32 dynOffset = mTransformBuffer.Offset(inst);
    
                            gfxCmdBindDescriptorSets(mPipeline, 1, &mDescriptorSet, &dynOffset, 1);
                            gfxCmdDrawIndexed(mesh.numIndices, 1, 0, 0, 0);
                        }    
                    }  
                }       // foreach (node)     
            }   // foreach (instance)
        }

        {
            DebugDraw::DrawGroundGrid(*mCam, width, height, DebugDrawGridProperties { 
                .lineColor = Color(0x565656), 
                .boldLineColor = Color(0xd6d6d6) 
            });
        }

        if (ImGui::IsEnabled()) { // imgui test
            PROFILE_GPU_ZONE_NAME("ImGuiRender", true);
            DebugHud::DrawMemBudgets(dt);
            DebugHud::DrawQuickFrameInfo(dt);

            #if 0
            Mat4 view = mFpsCam.GetViewMat();
            ImGuizmo::ViewManipulate(view.f, 0.1f, ImVec2(5.0f, height - 128.0f - 5.0f), ImVec2(128, 128), 0xff000000);
            mFpsCam.SetViewMat(view);
            #endif

            ImGui::DrawFrame();
        }

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

        if (!ImGui::IsAnyItemHovered() && !ImGui::GetIO().WantCaptureMouse && !ImGuizmo::IsOver())
            mCam->HandleRotationMouse(ev, 0.2f, 0.1f);
    }

    static void CreateGraphicsResources(void* userData)
    {
        AppImpl* self = (AppImpl*)userData;

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
        }

        {
            AssetObjPtrScope<GfxShader> shader(self->mUnlitShader);
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
                    .arrayCount = NUM_CUBES
                }
            };

            self->mDSLayout = gfxCreateDescriptorSetLayout(*shader, bindingLayout, CountOf(bindingLayout), GfxDescriptorSetLayoutFlags::None);
        }

        GfxBufferDesc uniformBufferDesc {
            .size = sizeof(FrameTransform),
            .type = GfxBufferType::Uniform,
            .usage = GfxBufferUsage::Stream
        };

        self->mUniformBuffer = gfxCreateBuffer(uniformBufferDesc);

        self->mTransformBuffer = gfxCreateDynamicUniformBuffer(NUM_CUBES, sizeof(WorldTransform));

        GfxPushConstantDesc pushConstant = {
            .name = "Material",
            .stages = GfxShaderStage::Fragment,
            .range = {0, sizeof(uint32)*4}
        };

        AssetObjPtrScope<GfxShader> unlitShader(self->mUnlitShader);
        GfxPipelineDesc pipelineDesc {
            .shader = unlitShader,
            .inputAssemblyTopology = GfxPrimitiveTopology::TriangleList,
            .numDescriptorSetLayouts = 1,
            .descriptorSetLayouts = &self->mDSLayout,
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
        };

        self->mPipeline = gfxCreatePipeline(pipelineDesc);

        // TODO: TEMP create descriptor sets and assign them to material userData for later rendering
        GfxImageHandle images[NUM_CUBES];
        for (uint32 i = 0; i < NUM_CUBES; i++)
            images[i] = GfxImageHandle();
        self->mDescriptorSet = gfxCreateDescriptorSet(self->mDSLayout);
        GfxDescriptorBindingDesc descBindings[] = {
            {
                .name = "ModelTransform",
                .type = GfxDescriptorType::UniformBufferDynamic,
                .buffer = {self->mTransformBuffer.buffer, 0, self->mTransformBuffer.stride}
            },
            {
                .name = "FrameTransform",
                .type = GfxDescriptorType::UniformBuffer,
                .buffer = { self->mUniformBuffer, 0, sizeof(FrameTransform) }
            },
            {
                .name = "BaseColorTextures",
                .type = GfxDescriptorType::CombinedImageSampler,
                .imageArrayCount = NUM_CUBES,
                .imageArray = images
            }
        };
        gfxUpdateDescriptorSet(self->mDescriptorSet, CountOf(descBindings), descBindings);
    }

    void ReleaseGraphicsObjects()
    {
        gfxWaitForIdle();
        gfxDestroyDescriptorSet(mDescriptorSet);
        gfxDestroyPipeline(mPipeline);
        gfxDestroyDescriptorSetLayout(mDSLayout);
        gfxDestroyBuffer(mUniformBuffer);
        gfxDestroyDynamicUniformBuffer(mTransformBuffer);
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
