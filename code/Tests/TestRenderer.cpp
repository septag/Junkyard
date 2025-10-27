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

static const char* TESTRENDERER_MODELS[] = {
    "/data/Duck/Duck.gltf",
    "/data/DamagedHelmet/DamagedHelmet.gltf",
    "/data/FlightHelmet/FlightHelmet.gltf",
    "/data/Sponza/Sponza.gltf"
};

#define LIGHT_CULL_TILE_SIZE 16
#define LIGHT_CULL_MAX_LIGHTS_PER_TILE 8
#define LIGHT_CULL_MAX_LIGHTS_PER_FRAME 100

struct RZPrepassVertex
{
    Float3 position;
};

struct RZPrepassShaderObjectData
{
    Mat4 localToWorldMat;
};

struct RZPrepassShaderFrameData
{
    Mat4 worldToClipMat;
};

struct RLight
{
    Float3 position;
    float radius;
};

struct RLightCullShaderFrameData
{
    Mat4 worldToViewMat;
    Mat4 clipToViewMat;
    float cameraNear;
    float cameraFar;
    float _reserved1[2];
    uint32 numLights;
    uint32 windowWidth;
    uint32 windowHeight;
    uint32 _reserved2;
};

struct RLightCullDebugShaderFrameData
{
    uint32 tilesCountX;
    uint32 tilesCountY;
    uint32 _reserved[2];
};

struct RForwardPlusContext
{
    AssetHandleShader sZPrepass;
    GfxPipelineHandle pZPrepass;
    GfxPipelineLayoutHandle pZPrepassLayout;
    GfxBufferHandle ubZPrepass;

    RLight* lights;
    uint32 numLights;
    AssetHandleShader sLightCull;
    GfxPipelineHandle pLightCull;
    GfxPipelineLayoutHandle pLightCullLayout;
    GfxBufferHandle ubLightCull;
    GfxBufferHandle bLightsInput;
    GfxBufferHandle bLightsOutput;

    AssetHandleShader sLightCullDebug;
    GfxPipelineHandle pLightCullDebug;
    GfxPipelineLayoutHandle pLightCullDebugLayout;
    AssetHandleImage checkerTex;
};

struct SceneLight
{
    Float4 boundingSphere;
};

RForwardPlusContext gFwdPlus;

namespace R 
{
    void SetLights(uint32 numLights, const SceneLight* lights);
}

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

    Array<SceneLight> mLights;

    float mLightAngle = M_HALFPI;
    float mPointLightRadius = 1.0f;
    bool mEnableLight = false;
    bool mDebugLightCull = false;

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
        Str::ScanFmt(posStr, "%f,%f,%f", &camPos.x, &camPos.y, &camPos.z);
        Str::ScanFmt(targetStr, "%f,%f,%f", &camTarget.x, &camTarget.y, &camTarget.z);
        mCam.SetLookAt(camPos, camTarget);

        GfxBufferDesc bufferDesc {
            .sizeBytes = sizeof(FrameInfo),
            .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Uniform,
            .arena = GfxMemoryArena::PersistentGPU
        };
        mUniformBuffer = GfxBackend::CreateBuffer(bufferDesc);

        mShader = Shader::Load("/shaders/Model.hlsl", ShaderLoadParams{}, initAssetGroup);

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
                .depthWriteEnable = false,
                .depthCompareOp = GfxCompareOp::Equal
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

        mLights.Free();
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
        ImGui::Checkbox("Enable Lights", &mEnableLight);
        ImGui::SliderFloat("Light Angle", &mLightAngle, 0, M_PI, "%0.1f");
        ImGui::SliderFloat("Point Light Radius", &mPointLightRadius, 0.1f, 10.0f, "%.1f");
        if (ImGui::Button("Add Point Light")) {
            AddLightAtCameraPosition();
            R::SetLights(mLights.Count(), mLights.Ptr());
        }

        if (ImGui::Button("Save Lights")) {
            SaveLights();
        }

        ImGui::Checkbox("Debug Light Culling", &mDebugLightCull);
    }

    void SaveLights()
    {
        MemTempAllocator tempAlloc;
        Blob blob(&tempAlloc);
        String<128> line;
        for (SceneLight& light : mLights) {
            line.FormatSelf("%.3f, %.3f, %.3f, %.1f\n", light.boundingSphere.x, light.boundingSphere.y, light.boundingSphere.z, 
                            light.boundingSphere.w);
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
                Str::ScanFmt(line, "%f, %f, %f, %f", &light.boundingSphere.x, &light.boundingSphere.y, &light.boundingSphere.z, 
                             &light.boundingSphere.w);
                mLights.Push(light);
            }
        }

        if (!mLights.IsEmpty()) 
            R::SetLights(mLights.Count(), mLights.Ptr());
    }

    void AddLightAtCameraPosition()
    {
        SceneLight light {
            .boundingSphere = Float4(mCam.Position(), mPointLightRadius)
        };

        mLights.Push(light);
    }

    void Render(GfxCommandBuffer cmd)
    {
        if (!mAssetGroup.IsValid() || !mAssetGroup.IsLoadFinished())
            return;

        cmd.BindPipeline(mPipeline);
        
        // Viewport
        float vwidth = (float)App::GetFramebufferWidth();
        float vheight = (float)App::GetFramebufferHeight();

        cmd.HelperSetFullscreenViewportAndScissor();

        AssetObjPtrScope<ModelData> model(mModel);

        for (uint32 i = 0; i < model->numNodes; i++) {
            const ModelNode& node = model->nodes[i];
            if (node.meshId == 0)
                continue;

            ModelTransform transform {
                .modelMat = Transform3D::ToMat4(node.localTransform)
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

namespace R
{
    void LoadFwdPlusAssets(AssetGroup assetGroup)
    {
        gFwdPlus.sZPrepass = Shader::Load("/shaders/ZPrepass.hlsl", ShaderLoadParams{}, assetGroup);

        {
            ShaderLoadParams loadParams {
                .compileDesc = {
                    .numDefines = 3,
                    .defines = {
                        {
                            .define = "TILE_SIZE",
                            .value = String32::Format("%d", LIGHT_CULL_TILE_SIZE)
                        },
                        {
                            .define = "MAX_LIGHTS_PER_TILE",
                            .value = String32::Format("%d", LIGHT_CULL_MAX_LIGHTS_PER_TILE)
                        },
                        {
                            .define = "MSAA",
                            .value = "0"
                        }
                    }
                }
            };
            gFwdPlus.sLightCull = Shader::Load("/shaders/LightCull.hlsl", loadParams, assetGroup);
            gFwdPlus.sLightCullDebug = Shader::Load("/shaders/LightCullDebug.hlsl", loadParams, assetGroup);
        }

        gFwdPlus.checkerTex = Image::Load("/data/Checker.png", ImageLoadParams{}, assetGroup);
    }

    bool InitializeFwdPlus()
    {

        //--------------------------------------------------------------------------------------------------------------
        // ZPrepass
        {
            // Layout
            ASSERT(gFwdPlus.sZPrepass.IsValid());
            AssetObjPtrScope<GfxShader> shader(gFwdPlus.sZPrepass);
            GfxPipelineLayoutDesc::Binding pBindings[] = {
                {
                    .name = "PerFrameData",
                    .type = GfxDescriptorType::UniformBuffer,
                    .stagesUsed = GfxShaderStage::Vertex
                }
            };

            GfxPipelineLayoutDesc::PushConstant pPushConstants[] = {
                {
                    .name = "PerObjectData",
                    .stagesUsed = GfxShaderStage::Vertex,
                    .size = sizeof(RZPrepassShaderObjectData)
                }
            };

            GfxPipelineLayoutDesc pLayoutDesc {
                .numBindings = CountOf(pBindings),
                .bindings = pBindings,
                .numPushConstants = CountOf(pPushConstants),
                .pushConstants = pPushConstants
            };

            gFwdPlus.pZPrepassLayout = GfxBackend::CreatePipelineLayout(*shader, pLayoutDesc);

            // Pipeline
            GfxVertexInputAttributeDesc vertexInputAttDescs[] = {
                {
                    .semantic = "POSITION",
                    .binding = 0,
                    .format = GfxFormat::R32G32B32_SFLOAT,
                    .offset = offsetof(RZPrepassVertex, position)
                }
            };

            GfxVertexBufferBindingDesc vertexBufferBindingDescs[] = {
                {
                    .binding = 0,
                    .stride = sizeof(ModelScene::Vertex),       // TODO: Fix this, should be a separate vertex stream
                    .inputRate = GfxVertexInputRate::Vertex
                }
            };

            GfxGraphicsPipelineDesc pDesc {
                .numVertexInputAttributes = CountOf(vertexInputAttDescs),
                .vertexInputAttributes = vertexInputAttDescs,
                .numVertexBufferBindings = CountOf(vertexBufferBindingDescs),
                .vertexBufferBindings = vertexBufferBindingDescs,
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
                .numColorAttachments = 0,
                .depthAttachmentFormat = GfxBackend::GetValidDepthStencilFormat(),
                .stencilAttachmentFormat = GfxBackend::GetValidDepthStencilFormat()
            };

            gFwdPlus.pZPrepass = GfxBackend::CreateGraphicsPipeline(*shader, gFwdPlus.pZPrepassLayout, pDesc);

            // Buffers
            GfxBufferDesc bufferDesc {
                .sizeBytes = sizeof(RZPrepassShaderFrameData),
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Uniform,
                .arena = GfxMemoryArena::PersistentGPU
            };
            gFwdPlus.ubZPrepass = GfxBackend::CreateBuffer(bufferDesc);
        }

        //--------------------------------------------------------------------------------------------------------------
        // LightCull
        {
            AssetObjPtrScope<GfxShader> shader(gFwdPlus.sLightCull);
            // Layout
            GfxPipelineLayoutDesc::Binding bindings[] = {
                {
                    .name = "PerFrameData",
                    .type = GfxDescriptorType::UniformBuffer,
                    .stagesUsed = GfxShaderStage::Compute
                },
                {
                    .name = "Lights",
                    .type = GfxDescriptorType::StorageBuffer,
                    .stagesUsed = GfxShaderStage::Compute
                },
                {
                    .name = "VisibleLightIndices",
                    .type = GfxDescriptorType::StorageBuffer,
                    .stagesUsed = GfxShaderStage::Compute
                },
                {
                    .name = "DepthTexture",
                    .type = GfxDescriptorType::SampledImage,
                    .stagesUsed = GfxShaderStage::Compute

                }
            };

            GfxPipelineLayoutDesc layoutDesc { 
                .numBindings = CountOf(bindings),
                .bindings = bindings
            };

            gFwdPlus.pLightCullLayout = GfxBackend::CreatePipelineLayout(*shader, layoutDesc);

            // Pipeline
            gFwdPlus.pLightCull = GfxBackend::CreateComputePipeline(*shader, gFwdPlus.pLightCullLayout);

            // Buffers
            GfxBufferDesc bufferDesc {
                .sizeBytes = sizeof(RLightCullShaderFrameData),
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Uniform
            };
            gFwdPlus.ubLightCull = GfxBackend::CreateBuffer(bufferDesc);

            bufferDesc = {
                .sizeBytes = sizeof(RLight)*LIGHT_CULL_MAX_LIGHTS_PER_FRAME,
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Storage
            };
            gFwdPlus.bLightsInput = GfxBackend::CreateBuffer(bufferDesc);

            uint32 numTilesX = M::CeilDiv(App::GetFramebufferWidth(), LIGHT_CULL_TILE_SIZE);
            uint32 numTilesY = M::CeilDiv(App::GetFramebufferHeight(), LIGHT_CULL_TILE_SIZE);
            bufferDesc = {
                .sizeBytes = sizeof(uint32)*numTilesX*numTilesY*LIGHT_CULL_MAX_LIGHTS_PER_TILE,
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Storage
            };
            gFwdPlus.bLightsOutput = GfxBackend::CreateBuffer(bufferDesc);

        }

        //--------------------------------------------------------------------------------------------------------------
        // LightCull Debug
        {
            AssetObjPtrScope<GfxShader> shader(gFwdPlus.sLightCullDebug);

            // Layout
            GfxPipelineLayoutDesc::Binding bindings[] = {
                {
                    .name = "VisibleLightIndices",
                    .type = GfxDescriptorType::StorageBuffer,
                    .stagesUsed = GfxShaderStage::Fragment
                }
            };

            GfxPipelineLayoutDesc::PushConstant pushConstants[] = {
                {
                    .name = "PerFrameData",
                    .stagesUsed = GfxShaderStage::Fragment,
                    .size = sizeof(RLightCullDebugShaderFrameData)
                }
            };

            GfxPipelineLayoutDesc layoutDesc { 
                .numBindings = CountOf(bindings),
                .bindings = bindings,
                .numPushConstants = CountOf(pushConstants),
                .pushConstants = pushConstants
            };

            gFwdPlus.pLightCullDebugLayout = GfxBackend::CreatePipelineLayout(*shader, layoutDesc);

            GfxGraphicsPipelineDesc pDesc {
                .rasterizer = {
                    .cullMode = GfxCullMode::Back
                },
                .blend = {
                    .numAttachments = 1,
                    .attachments = GfxBlendAttachmentDesc::GetDefault()
                },
                .numColorAttachments = 1,
                .colorAttachmentFormats = {GfxBackend::GetSwapchainFormat()},
                .depthAttachmentFormat = GfxBackend::GetValidDepthStencilFormat(),
                .stencilAttachmentFormat = GfxBackend::GetValidDepthStencilFormat()
            };

            gFwdPlus.pLightCullDebug = GfxBackend::CreateGraphicsPipeline(*shader, gFwdPlus.pLightCullDebugLayout, pDesc);
        }

        return true;
    }

    void ReleaseFwdPlus()
    {
        GfxBackend::DestroyPipeline(gFwdPlus.pZPrepass);
        GfxBackend::DestroyPipelineLayout(gFwdPlus.pZPrepassLayout);
        GfxBackend::DestroyBuffer(gFwdPlus.ubZPrepass);

        GfxBackend::DestroyPipeline(gFwdPlus.pLightCull);
        GfxBackend::DestroyPipelineLayout(gFwdPlus.pLightCullLayout);
        GfxBackend::DestroyBuffer(gFwdPlus.ubLightCull);
        GfxBackend::DestroyBuffer(gFwdPlus.bLightsInput);
        GfxBackend::DestroyBuffer(gFwdPlus.bLightsOutput);

        GfxBackend::DestroyPipeline(gFwdPlus.pLightCullDebug);
        GfxBackend::DestroyPipelineLayout(gFwdPlus.pLightCullDebugLayout);

        Mem::Free(gFwdPlus.lights);
    }

    void UpdateBuffersFwdPlus(GfxCommandBuffer cmd, const Camera& cam)
    {
        float vwidth = (float)App::GetFramebufferWidth();
        float vheight = (float)App::GetFramebufferHeight();

        {
            GfxHelperBufferUpdateScope zprepassBufferUpdater(cmd, gFwdPlus.ubZPrepass, sizeof(RZPrepassShaderFrameData), 
                                                             GfxShaderStage::Vertex|GfxShaderStage::Fragment);
            RZPrepassShaderFrameData* buffer = (RZPrepassShaderFrameData*)zprepassBufferUpdater.mData;

            *buffer = {
                .worldToClipMat = GfxBackend::GetSwapchainTransformMat() * cam.GetPerspectiveMat(vwidth, vheight) * cam.GetViewMat(),
            };
        }

        {
            GfxHelperBufferUpdateScope lightcullUniformUpdater(cmd, gFwdPlus.ubLightCull, sizeof(RLightCullShaderFrameData),
                                                               GfxShaderStage::Compute);
            RLightCullShaderFrameData* buffer = (RLightCullShaderFrameData*)lightcullUniformUpdater.mData;
            buffer->worldToViewMat = cam.GetViewMat();
            buffer->clipToViewMat = Mat4::Inverse(cam.GetPerspectiveMat(vwidth, vheight));
            buffer->cameraNear = cam.Near();
            buffer->cameraFar = cam.Far();
            buffer->numLights = gFwdPlus.numLights;
            buffer->windowWidth = App::GetFramebufferWidth();
            buffer->windowHeight = App::GetFramebufferHeight();
        }

        if (gFwdPlus.numLights) {
            GfxHelperBufferUpdateScope lightcullInputUpdater(cmd, gFwdPlus.bLightsInput, sizeof(RLight)*gFwdPlus.numLights,
                                                             GfxShaderStage::Compute);
            RLight* buffer = (RLight*)lightcullInputUpdater.mData;
            memcpy(buffer, gFwdPlus.lights, sizeof(RLight)*gFwdPlus.numLights);
        }
    }

    void RenderFwdPlus(GfxCommandBuffer cmd, GfxImageHandle depthImageHandle, AssetHandleModel modelHandle)
    {
        AssetObjPtrScope<ModelData> model(modelHandle);
        if (model.IsNull())
            return;

        GPU_PROFILE_ZONE(cmd, "ZPrepass");

        GfxBackendRenderPass pass { 
            .depthAttachment = {
                .image = depthImageHandle,
                .clear = true,
                .clearValue = {
                    .depth = 1.0f
                }
            },
            .hasDepth = true
        };
        cmd.BeginRenderPass(pass);

        cmd.BindPipeline(gFwdPlus.pZPrepass);
        cmd.HelperSetFullscreenViewportAndScissor();
        
        // Viewport
        for (uint32 i = 0; i < model->numNodes; i++) {
            const ModelNode& node = model->nodes[i];
            if (node.meshId == 0)
                continue;

            RZPrepassShaderObjectData perObjData {
                .localToWorldMat = Transform3D::ToMat4(node.localTransform)
            };
            cmd.PushConstants(gFwdPlus.pZPrepassLayout, "PerObjectData", &perObjData, sizeof(perObjData));

            const ModelMesh& mesh = model->meshes[IdToIndex(node.meshId)];

            cmd.BindVertexBuffers(0, model->numVertexBuffers, model->vertexBuffers, mesh.vertexBufferOffsets);
            cmd.BindIndexBuffer(model->indexBuffer, mesh.indexBufferOffset, GfxIndexType::Uint32);

            GfxBindingDesc bindings[] = {
                {
                    .name = "PerFrameData",
                    .buffer = gFwdPlus.ubZPrepass
                }
            };
            cmd.PushBindings(gFwdPlus.pZPrepassLayout, CountOf(bindings), bindings);
            
            uint32 numIndices = 0;
            for (uint32 smi = 0; smi < mesh.numSubmeshes; smi++) {
                const ModelSubmesh& submesh = mesh.submeshes[smi];
                numIndices += submesh.numIndices;
            }

            cmd.DrawIndexed(numIndices, 1, 0, 0, 0);
        }

        cmd.EndRenderPass();

        if (gFwdPlus.numLights) {
            cmd.TransitionImage(depthImageHandle, GfxImageTransition::ShaderRead);

            uint32 numTilesX = M::CeilDiv(App::GetFramebufferWidth(), LIGHT_CULL_TILE_SIZE);
            uint32 numTilesY = M::CeilDiv(App::GetFramebufferHeight(), LIGHT_CULL_TILE_SIZE);

            GfxBindingDesc bindings[] = {
                {
                    .name = "PerFrameData", 
                    .buffer = gFwdPlus.ubLightCull
                },
                {
                    .name = "Lights",
                    .buffer = gFwdPlus.bLightsInput
                },
                {
                    .name = "VisibleLightIndices",
                    .buffer = gFwdPlus.bLightsOutput
                },
                {
                    .name = "DepthTexture",
                    .image = depthImageHandle
                }
            };

            cmd.BindPipeline(gFwdPlus.pLightCull);
            cmd.PushBindings(gFwdPlus.pLightCullLayout, CountOf(bindings), bindings);
            cmd.Dispatch(numTilesX, numTilesY, 1);
        }
    }

    void SetLights(uint32 numLights, const SceneLight* lights)
    {
        ASSERT(numLights);
        ASSERT(lights);

        gFwdPlus.lights = Mem::ReallocTyped<RLight>(gFwdPlus.lights, numLights);   // TODO: alloc
        gFwdPlus.numLights = numLights;
        for (uint32 i = 0; i < numLights; i++) {
            const SceneLight& l = lights[i];
            gFwdPlus.lights[i] = {
                .position = Float3(l.boundingSphere.x, l.boundingSphere.y, l.boundingSphere.z),
                .radius = l.boundingSphere.w
            };
        }
    }

    void DrawLightCullDebug(GfxCommandBuffer cmd)
    {
        cmd.BindPipeline(gFwdPlus.pLightCullDebug);
        cmd.HelperSetFullscreenViewportAndScissor();

        GfxBindingDesc bindings[] = {
            {
                .name = "VisibleLightIndices",
                .buffer = gFwdPlus.bLightsOutput
            }
        };

        cmd.PushBindings(gFwdPlus.pLightCullDebugLayout, CountOf(bindings), bindings);

        RLightCullDebugShaderFrameData perFrameData {
            .tilesCountX = (uint32)M::CeilDiv(App::GetFramebufferWidth(), LIGHT_CULL_TILE_SIZE),
            .tilesCountY = (uint32)M::CeilDiv(App::GetFramebufferHeight(), LIGHT_CULL_TILE_SIZE)
        };
        cmd.PushConstants<RLightCullDebugShaderFrameData>(gFwdPlus.pLightCullDebugLayout, "PerFrameData", perFrameData);

        cmd.Draw(3, 1, 0, 0);
    }
} // R


struct AppImpl final : AppCallbacks
{
    Camera* mCam = nullptr;
    ModelScene mModelScenes[CountOf(TESTRENDERER_MODELS)];
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
                .multisampleFlags = GfxSampleCountFlags::SampleCount1,
                .format = GfxBackend::GetValidDepthStencilFormat(),
                .usageFlags = GfxImageUsageFlags::DepthStencilAttachment|GfxImageUsageFlags::Sampled,
                .arena = GfxMemoryArena::PersistentGPU
            };

            // Note: this won't probably work with tiled GPUs because it's incompatible with Sampled flag
            //       So we probably need to copy the contents of the zbuffer to another one
            #if PLATFORM_MOBILE
                desc.usageFlags |= GfxImageUsageFlags::TransientAttachment;
            #endif

            self->mRenderTargetDepth = GfxBackend::CreateImage(desc);
        }

        R::InitializeFwdPlus();
    }

    bool Initialize() override
    {
        bool isRemote = SettingsJunkyard::Get().engine.connectToServer;

        // For remote mode, you also have to use "-ToolingServerCustomDataMountDir=data/TestAsset" argument for the server tool
        Vfs::HelperMountDataAndShaders(isRemote, isRemote ? "data" : "data/TestBasicGfx");

        if (!Engine::Initialize())
            return false;

        AssetGroup initAssetGroup = Engine::RegisterInitializeResources(InitializeResources, this);
        for (uint32 i = 0; i < CountOf(TESTRENDERER_MODELS); i++)
            mModelScenes[i].Initialize(initAssetGroup, TESTRENDERER_MODELS[i]);
        R::LoadFwdPlusAssets(initAssetGroup);

        mSelectedSceneIdx = (uint32)Str::ToInt(Settings::GetValue("TestRenderer.SelectedScene", "0"));
        mSelectedSceneIdx = Clamp(mSelectedSceneIdx, 0u, CountOf(TESTRENDERER_MODELS)-1);

        mCam = &mModelScenes[mSelectedSceneIdx].mCam;

        if constexpr (PLATFORM_APPLE || PLATFORM_ANDROID)
            mDrawGrid = false;

        return true;
    };

    void Cleanup() override
    {
        Settings::SetValue("TestRenderer.SelectedScene", String32::Format("%u", mSelectedSceneIdx).CStr());

        for (uint32 i = 0; i < CountOf(TESTRENDERER_MODELS); i++)
            mModelScenes[i].Release();

        GfxBackend::DestroyImage(mRenderTargetDepth);

        R::ReleaseFwdPlus();

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
        scene.Update(cmd);
        R::UpdateBuffersFwdPlus(cmd, scene.mCam);

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
                .load = true,
                .clear = false
            },
            .swapchain = true,
            .hasDepth = true
        };

        cmd.TransitionImage(mRenderTargetDepth, GfxImageTransition::RenderTarget, GfxImageTransitionFlags::DepthWrite);

        {
            R::RenderFwdPlus(cmd, mRenderTargetDepth, scene.mModel);
        }

        cmd.TransitionImage(mRenderTargetDepth, GfxImageTransition::RenderTarget, GfxImageTransitionFlags::DepthRead);

        // Draw scene
        {
            GPU_PROFILE_ZONE(cmd, "ModelRender");

            cmd.BeginRenderPass(pass);
            scene.Render(cmd);

            if (scene.mDebugLightCull)
                R::DrawLightCullDebug(cmd);

            cmd.EndRenderPass();
        }

        // DebugDraw
        if (!scene.mDebugLightCull) {
            DebugDraw::BeginDraw(cmd, *mCam, App::GetFramebufferWidth(), App::GetFramebufferHeight());
            if (mDrawGrid) {
                DebugDrawGridProperties gridProps {
                    .distance = 200,
                    .lineColor = Color4u(0x565656),
                    .boldLineColor = Color4u(0xd6d6d6)
                };

                DebugDraw::DrawGroundGrid(*mCam, gridProps);
            }

            for (const SceneLight& l : scene.mLights) {
                DebugDraw::DrawBoundingSphere(l.boundingSphere, COLOR4U_WHITE);
            }
            DebugDraw::EndDraw(cmd, mRenderTargetDepth);
        }

        // ImGui
        if (ImGui::IsEnabled()) {
            GPU_PROFILE_ZONE(cmd, "ImGui");
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
                scene.UpdateImGui();
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
            .appName = "TestRenderer"
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

