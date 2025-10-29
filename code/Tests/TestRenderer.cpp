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

static inline constexpr uint32 R_LIGHT_CULL_TILE_SIZE = 16;
static inline constexpr uint32 R_LIGHT_CULL_MAX_LIGHTS_PER_TILE = 8;
static inline constexpr uint32 R_LIGHT_CULL_MAX_LIGHTS_PER_FRAME = 100;

struct RLightBounds
{
    Float3 position;
    float radius;
};

struct RLightProps
{
    Float4 color;  
};

enum class RDebugMode
{
    None = 0,
    LightCull
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

struct RLightShaderFrameData
{
    Mat4 worldToClipMat;
    Float3 sunLightDir;
    float _reserved1;
    Float4 sunLightColor;
    Float4 skyAmbientColor;
    Float4 groundAmbientColor;
    uint32 tilesCountX;
    uint32 tilesCountY;
    uint32 _reserved2[2];
};

struct RForwardPlusContext
{
    AssetHandleShader sZPrepass;
    GfxPipelineHandle pZPrepass;
    GfxPipelineLayoutHandle pZPrepassLayout;
    GfxBufferHandle ubZPrepass;

    RLightBounds* lightBounds;
    RLightProps* lightProps;
    uint32 numLights;

    GfxBufferHandle bVisibleLightIndices;
    GfxBufferHandle bLightBounds; 
    GfxBufferHandle bLightProps;

    AssetHandleShader sLightCull;
    GfxPipelineHandle pLightCull;
    GfxPipelineLayoutHandle pLightCullLayout;
    GfxBufferHandle ubLightCull;

    AssetHandleShader sLightCullDebug;
    GfxPipelineHandle pLightCullDebug;
    GfxPipelineLayoutHandle pLightCullDebugLayout;

    AssetHandleShader sLight;
    GfxPipelineHandle pLight;
    GfxPipelineLayoutHandle pLightLayout;
    GfxBufferHandle ubLight;

    AssetHandleImage checkerTex;

    RLightShaderFrameData lightPerFrameData;
};

struct SceneLight
{
    Float4 boundingSphere;
    Float4 color;
};

RForwardPlusContext gFwdPlus;

namespace R 
{
    void SetLocalLights(uint32 numLights, const SceneLight* lights);
    void SetAmbientLight(Float4 skyAmbientColor, Float4 groundAmbientColor);
    void SetSunLight(Float3 direction, Float4 color);
}

struct ModelScene
{
    String32 mName;
    Path mModelFilepath;

    CameraFPS mCam;

    AssetHandleModel mModel;

    AssetGroup mAssetGroup;

    Array<SceneLight> mLights;

    float mSunlightAngle = M_HALFPI;
    Float4 mSunlightColor = Color4u::ToFloat4(Color4u(251,250,204,8)); 
    float mPointLightRadius = 1.0f;
    Float4 mLightColor = Float4(1.0f, 1.0f, 1.0f, 1.0f);
    Float4 mSkyAmbient = Color4u::ToFloat4(Color4u(36,54,81,26));
    Float4 mGroundAmbient = Color4u::ToFloat4(Color4u(216,199,172,8));
    bool mDebugLightCull = false;
    bool mDebugLightBounds = false;

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
    }

    void Load()
    {
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
        mLights.Free();
    }

    void UpdateImGui()
    {
        if (ImGui::ColorEdit4("Sky Ambient Color", mSkyAmbient.f, ImGuiColorEditFlags_Float))
            R::SetAmbientLight(mSkyAmbient, mGroundAmbient);
        if (ImGui::ColorEdit4("Ground Ambient Color", mGroundAmbient.f, ImGuiColorEditFlags_Float)) 
            R::SetAmbientLight(mSkyAmbient, mGroundAmbient);
        ImGui::Separator();

        if (ImGui::SliderFloat("Sun Light Angle", &mSunlightAngle, 0, M_PI, "%0.1f"))
            R::SetSunLight(Float3(-0.2f, M::Cos(mSunlightAngle), -M::Sin(mSunlightAngle)), mSunlightColor);
        if (ImGui::ColorEdit4("Sun Light Color", mSunlightColor.f, ImGuiColorEditFlags_Float))
            R::SetSunLight(Float3(-0.2f, M::Cos(mSunlightAngle), -M::Sin(mSunlightAngle)), mSunlightColor);

        ImGui::SliderFloat("Point Light Radius", &mPointLightRadius, 0.1f, 10.0f, "%.1f");
        ImGui::ColorEdit4("Light Color", mLightColor.f, ImGuiColorEditFlags_Float);
        if (ImGui::Button("Add Point Light")) {
            AddLightAtCameraPosition();
            R::SetLocalLights(mLights.Count(), mLights.Ptr());
        }

        if (ImGui::Button("Save Lights")) {
            SaveLights();
        }
        ImGui::Separator();

        ImGui::Checkbox("Debug Light Culling", &mDebugLightCull);
        ImGui::Checkbox("Debug Light Bounds", &mDebugLightBounds);
    }

    void SaveLights()
    {
        MemTempAllocator tempAlloc;
        Blob blob(&tempAlloc);
        String<128> line;
        for (SceneLight& light : mLights) {
            line.FormatSelf("%.3f, %.3f, %.3f, %.1f, %.2f, %.2f, %.2f, %.2f\n", 
                            light.boundingSphere.x, light.boundingSphere.y, light.boundingSphere.z, light.boundingSphere.w,
                            light.color.x, light.color.y, light.color.z, light.color.w);
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
                Str::ScanFmt(line, "%f, %f, %f, %f, %f, %f, %f, %f", 
                             &light.boundingSphere.x, &light.boundingSphere.y, &light.boundingSphere.z, &light.boundingSphere.w,
                             &light.color.x, &light.color.y, &light.color.z, &light.color.w);
                mLights.Push(light);
            }
        }

        if (!mLights.IsEmpty()) 
            R::SetLocalLights(mLights.Count(), mLights.Ptr());
        R::SetAmbientLight(mSkyAmbient, mGroundAmbient);
        R::SetSunLight(Float3(-0.2f, M::Cos(mSunlightAngle), -M::Sin(mSunlightAngle)), mSunlightColor);
    }

    void AddLightAtCameraPosition()
    {
        SceneLight light {
            .boundingSphere = Float4(mCam.Position(), mPointLightRadius),
            .color = mLightColor
        };

        mLights.Push(light);
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
                            .value = String32::Format("%d", R_LIGHT_CULL_TILE_SIZE)
                        },
                        {
                            .define = "MAX_LIGHTS_PER_TILE",
                            .value = String32::Format("%d", R_LIGHT_CULL_MAX_LIGHTS_PER_TILE)
                        },
                        {
                            .define = "MSAA",
                            .value = "0"
                        }
                    }
                }
            };
            gFwdPlus.sLightCull = Shader::Load("/shaders/LightCull.hlsl", loadParams, assetGroup);
            gFwdPlus.sLight = Shader::Load("/shaders/FwdPlusLight.hlsl", loadParams, assetGroup);
            gFwdPlus.sLightCullDebug = Shader::Load("/shaders/LightCullDebug.hlsl", loadParams, assetGroup);
        }

        gFwdPlus.checkerTex = Image::Load("/data/Checker.png", ImageLoadParams{}, assetGroup);
    }

    bool InitializeFwdPlus()
    {
        //--------------------------------------------------------------------------------------------------------------
        // Common buffers
        {
            GfxBufferDesc bufferDesc = {
                .sizeBytes = sizeof(RLightBounds)*R_LIGHT_CULL_MAX_LIGHTS_PER_FRAME,
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Storage
            };
            gFwdPlus.bLightBounds = GfxBackend::CreateBuffer(bufferDesc);

            uint32 numTilesX = M::CeilDiv((uint32)App::GetFramebufferWidth(), R_LIGHT_CULL_TILE_SIZE);
            uint32 numTilesY = M::CeilDiv((uint32)App::GetFramebufferHeight(), R_LIGHT_CULL_TILE_SIZE);
            bufferDesc = {
                .sizeBytes = sizeof(uint32)*numTilesX*numTilesY*R_LIGHT_CULL_MAX_LIGHTS_PER_TILE,
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Storage
            };
            gFwdPlus.bVisibleLightIndices = GfxBackend::CreateBuffer(bufferDesc);

            bufferDesc = {
                .sizeBytes = sizeof(RLightProps)*R_LIGHT_CULL_MAX_LIGHTS_PER_FRAME,
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Storage
            };
            gFwdPlus.bLightProps = GfxBackend::CreateBuffer(bufferDesc);
        }

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
                    .size = sizeof(Mat4)
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
                    .offset = 0 
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
                .sizeBytes = sizeof(Mat4),
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
        }

        //--------------------------------------------------------------------------------------------------------------
        // Lighting
        {
            AssetObjPtrScope<GfxShader> shader(gFwdPlus.sLight);

            // Layout
            GfxPipelineLayoutDesc::Binding bindings[] = {
                {
                    .name = "PerFrameData",
                    .type = GfxDescriptorType::UniformBuffer,
                    .stagesUsed = GfxShaderStage::Fragment|GfxShaderStage::Vertex
                },
                {
                    .name = "BaseColorTexture",
                    .type = GfxDescriptorType::CombinedImageSampler,
                    .stagesUsed = GfxShaderStage::Fragment
                },
                {
                    .name = "VisibleLightIndices",
                    .type = GfxDescriptorType::StorageBuffer,
                    .stagesUsed = GfxShaderStage::Fragment
                },
                {
                    .name = "LocalLights",
                    .type = GfxDescriptorType::StorageBuffer,
                    .stagesUsed = GfxShaderStage::Fragment
                },
                {
                    .name = "LocalLightBounds",
                    .type = GfxDescriptorType::StorageBuffer,
                    .stagesUsed = GfxShaderStage::Fragment
                }
            };

            GfxPipelineLayoutDesc::PushConstant pushConstants[] = {
                {
                    .name = "PerObjectData",
                    .stagesUsed = GfxShaderStage::Vertex,
                    .size = sizeof(Mat4)
                }
            };

            GfxPipelineLayoutDesc layoutDesc {
                .numBindings = CountOf(bindings),
                .bindings = bindings,
                .numPushConstants = CountOf(pushConstants),
                .pushConstants = pushConstants
            };

            gFwdPlus.pLightLayout = GfxBackend::CreatePipelineLayout(*shader, layoutDesc);

            // Pipeline
            GfxVertexBufferBindingDesc vertexBufferBindingDesc {
                .binding = 0,
                .stride = sizeof(ModelScene::Vertex),   // TODO: use renderer specific vertex 
                .inputRate = GfxVertexInputRate::Vertex
            };

            GfxVertexInputAttributeDesc vertexInputAttDescs[] = {
                {
                    .semantic = "POSITION",
                    .binding = 0,
                    .format = GfxFormat::R32G32B32_SFLOAT,
                    .offset = offsetof(ModelScene::Vertex, pos)
                },
                {
                    .semantic = "NORMAL",
                    .binding = 0,
                    .format = GfxFormat::R32G32B32_SFLOAT,
                    .offset = offsetof(ModelScene::Vertex, normal)
                },
                {
                    .semantic = "TEXCOORD",
                    .binding = 0,
                    .format = GfxFormat::R32G32_SFLOAT,
                    .offset = offsetof(ModelScene::Vertex, uv)
                }
            };

            GfxGraphicsPipelineDesc pDesc {
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

            gFwdPlus.pLight = GfxBackend::CreateGraphicsPipeline(*shader, gFwdPlus.pLightLayout, pDesc);
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

            // Uniform Buffers
            GfxBufferDesc ubDesc {
                .sizeBytes = sizeof(RLightShaderFrameData),
                .usageFlags = GfxBufferUsageFlags::TransferDst | GfxBufferUsageFlags::Uniform,
            };

            gFwdPlus.ubLight = GfxBackend::CreateBuffer(ubDesc);
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

        GfxBackend::DestroyPipeline(gFwdPlus.pLightCullDebug);
        GfxBackend::DestroyPipelineLayout(gFwdPlus.pLightCullDebugLayout);

        GfxBackend::DestroyPipeline(gFwdPlus.pLight);
        GfxBackend::DestroyPipelineLayout(gFwdPlus.pLightLayout);
        GfxBackend::DestroyBuffer(gFwdPlus.ubLight);

        GfxBackend::DestroyBuffer(gFwdPlus.bLightBounds);
        GfxBackend::DestroyBuffer(gFwdPlus.bVisibleLightIndices);
        GfxBackend::DestroyBuffer(gFwdPlus.bLightProps);

        Mem::Free(gFwdPlus.lightBounds);
        Mem::Free(gFwdPlus.lightProps);
    }

    void UpdateBuffersFwdPlus(GfxCommandBuffer& cmd, const Camera& cam)
    {
        float vwidth = (float)App::GetFramebufferWidth();
        float vheight = (float)App::GetFramebufferHeight();

        Mat4 worldToClipMat = cam.GetPerspectiveMat(vwidth, vheight) * cam.GetViewMat();
        if (cmd.mDrawsToSwapchain) // TODO: this is not gonna detect swapchain properly
            worldToClipMat = GfxBackend::GetSwapchainTransformMat() * worldToClipMat;

        gFwdPlus.lightPerFrameData.worldToClipMat = worldToClipMat;
        gFwdPlus.lightPerFrameData.tilesCountX = M::CeilDiv((uint32)App::GetFramebufferWidth(), R_LIGHT_CULL_TILE_SIZE);
        gFwdPlus.lightPerFrameData.tilesCountY = M::CeilDiv((uint32)App::GetFramebufferHeight(), R_LIGHT_CULL_TILE_SIZE);
        uint32 numTiles = gFwdPlus.lightPerFrameData.tilesCountX * gFwdPlus.lightPerFrameData.tilesCountY;

        {
            GfxHelperBufferUpdateScope updater(cmd, gFwdPlus.ubZPrepass, -1, GfxShaderStage::Vertex|GfxShaderStage::Fragment);
            memcpy(updater.mData, &worldToClipMat, sizeof(Mat4));
        }

        // Per-frame light culling data
        {
            GfxHelperBufferUpdateScope updater(cmd, gFwdPlus.ubLightCull, -1, GfxShaderStage::Compute);
            RLightCullShaderFrameData* buffer = (RLightCullShaderFrameData*)updater.mData;
            buffer->worldToViewMat = cam.GetViewMat();
            buffer->clipToViewMat = Mat4::Inverse(cam.GetPerspectiveMat(vwidth, vheight));
            buffer->cameraNear = cam.Near();
            buffer->cameraFar = cam.Far();
            buffer->numLights = gFwdPlus.numLights;
            buffer->windowWidth = App::GetFramebufferWidth();
            buffer->windowHeight = App::GetFramebufferHeight();
        }

        // Per-frame lighting data
        {
            GfxHelperBufferUpdateScope updater(cmd, gFwdPlus.ubLight, -1, GfxShaderStage::Fragment);
            memcpy(updater.mData, &gFwdPlus.lightPerFrameData, sizeof(RLightShaderFrameData));
        }

        if (gFwdPlus.numLights) {
            GfxHelperBufferUpdateScope lightBoundsUpdater(cmd, gFwdPlus.bLightBounds, sizeof(RLightBounds)*gFwdPlus.numLights,
                                                          GfxShaderStage::Compute);
            RLightBounds* bounds = (RLightBounds*)lightBoundsUpdater.mData;
            memcpy(bounds, gFwdPlus.lightBounds, sizeof(RLightBounds)*gFwdPlus.numLights);

            GfxHelperBufferUpdateScope lightPropsUpdater(cmd, gFwdPlus.bLightProps, sizeof(RLightProps)*gFwdPlus.numLights,
                                                         GfxShaderStage::Fragment);
            RLightProps* props = (RLightProps*)lightPropsUpdater.mData;
            memcpy(props, gFwdPlus.lightProps, sizeof(RLightProps)*gFwdPlus.numLights);
        }
        else {
            // Fill visible light indices buffer with sentinels (empty state)
            GfxHelperBufferUpdateScope updater(cmd, gFwdPlus.bVisibleLightIndices, -1, GfxShaderStage::Fragment);
            uint32* indices = (uint32*)updater.mData;
            for (uint32 i = 0; i < numTiles; i++)
                indices[i*R_LIGHT_CULL_MAX_LIGHTS_PER_TILE] = 0xffffffff;
        }
    }

    bool RenderFwdPlus(GfxCommandBuffer& cmd, GfxImageHandle depthImageHandle, AssetHandleModel modelHandle, RDebugMode debugMode = RDebugMode::None)
    {
        AssetObjPtrScope<ModelData> model(modelHandle);
        if (model.IsNull()) {
            return false;
        }

        // Z-Prepass
        {
            // cmd.TransitionImage(depthImageHandle, GfxImageTransition::RenderTarget, GfxImageTransitionFlags::DepthWrite);

            GfxBackendRenderPass zprepass { 
                .depthAttachment = {
                    .image = depthImageHandle,
                    .clear = true,
                    .clearValue = {
                        .depth = 1.0f
                    }
                },
                .hasDepth = true
            };
            cmd.BeginRenderPass(zprepass);

            cmd.BindPipeline(gFwdPlus.pZPrepass);
            cmd.HelperSetFullscreenViewportAndScissor();
        
            for (uint32 i = 0; i < model->numNodes; i++) {
                const ModelNode& node = model->nodes[i];
                if (node.meshId == 0)
                    continue;

                Mat4 localToWorldMat = Transform3D::ToMat4(node.localTransform);
                cmd.PushConstants(gFwdPlus.pZPrepassLayout, "PerObjectData", &localToWorldMat, sizeof(Mat4));

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
        }

        // Light culling
        if (gFwdPlus.numLights) {
            cmd.TransitionImage(depthImageHandle, GfxImageTransition::ShaderRead);
            cmd.TransitionBuffer(gFwdPlus.bVisibleLightIndices, GfxBufferTransition::ComputeWrite);

            GfxBindingDesc bindings[] = {
                {
                    .name = "PerFrameData", 
                    .buffer = gFwdPlus.ubLightCull
                },
                {
                    .name = "Lights",
                    .buffer = gFwdPlus.bLightBounds
                },
                {
                    .name = "VisibleLightIndices",
                    .buffer = gFwdPlus.bVisibleLightIndices
                },
                {
                    .name = "DepthTexture",
                    .image = depthImageHandle
                }
            };

            cmd.BindPipeline(gFwdPlus.pLightCull);
            cmd.PushBindings(gFwdPlus.pLightCullLayout, CountOf(bindings), bindings);
            cmd.Dispatch(gFwdPlus.lightPerFrameData.tilesCountX, gFwdPlus.lightPerFrameData.tilesCountY, 1);

            cmd.TransitionBuffer(gFwdPlus.bVisibleLightIndices, GfxBufferTransition::FragmentRead);
        }

        cmd.TransitionImage(depthImageHandle, GfxImageTransition::RenderTarget, GfxImageTransitionFlags::DepthRead);

        // Light pass
        if (debugMode == RDebugMode::None) {
            GfxBackendRenderPass pass { 
                .colorAttachments = {{ 
                    .clear = true,
                    .clearValue = {
                        .color = gFwdPlus.lightPerFrameData.skyAmbientColor
                    }
                }},
                .depthAttachment = {
                    .image = depthImageHandle,
                    .load = true,
                    .clear = false
                },
                .swapchain = true,  // TODO: Change to offscreen
                .hasDepth = true
            };

            cmd.BeginRenderPass(pass);
            cmd.BindPipeline(gFwdPlus.pLight);
        
            cmd.HelperSetFullscreenViewportAndScissor();

            for (uint32 i = 0; i < model->numNodes; i++) {
                const ModelNode& node = model->nodes[i];
                if (node.meshId == 0)
                    continue;

                Mat4 localToWorldMat = Transform3D::ToMat4(node.localTransform);
                cmd.PushConstants<Mat4>(gFwdPlus.pLightLayout, "PerObjectData", localToWorldMat);

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
                            .name = "PerFrameData",
                            .buffer = gFwdPlus.ubLight
                        },
                        {
                            .name = "BaseColorTexture",
                            .image = imgHandle.IsValid() ? imgHandle : Image::GetWhite1x1()
                        },
                        {
                            .name = "VisibleLightIndices",
                            .buffer = gFwdPlus.bVisibleLightIndices
                        },
                        {
                            .name = "LocalLights",
                            .buffer = gFwdPlus.bLightProps
                        },
                        {
                            .name = "LocalLightBounds",
                            .buffer = gFwdPlus.bLightBounds
                        }
                    };
                    cmd.PushBindings(gFwdPlus.pLightLayout, CountOf(bindings), bindings);

                    cmd.DrawIndexed(submesh.numIndices, 1, submesh.startIndex, 0, 0);
                }
            }

            cmd.EndRenderPass();
        }
        else if (debugMode == RDebugMode::LightCull) {
            GfxBackendRenderPass pass { 
                .swapchain = true,
            };

            cmd.BeginRenderPass(pass);
            cmd.BindPipeline(gFwdPlus.pLightCullDebug);
            cmd.HelperSetFullscreenViewportAndScissor();

            GfxBindingDesc bindings[] = {
                {
                    .name = "VisibleLightIndices",
                    .buffer = gFwdPlus.bVisibleLightIndices
                }
            };

            cmd.PushBindings(gFwdPlus.pLightCullDebugLayout, CountOf(bindings), bindings);

            RLightCullDebugShaderFrameData perFrameData {
                .tilesCountX = gFwdPlus.lightPerFrameData.tilesCountX,
                .tilesCountY = gFwdPlus.lightPerFrameData.tilesCountY
            };
            cmd.PushConstants<RLightCullDebugShaderFrameData>(gFwdPlus.pLightCullDebugLayout, "PerFrameData", perFrameData);

            cmd.Draw(3, 1, 0, 0);

            cmd.EndRenderPass();
        }

        return true;
    }

    void SetLocalLights(uint32 numLights, const SceneLight* lights)
    {
        ASSERT(numLights);
        ASSERT(lights);

        gFwdPlus.lightBounds = Mem::ReallocTyped<RLightBounds>(gFwdPlus.lightBounds, numLights);   // TODO: alloc
        gFwdPlus.lightProps = Mem::ReallocTyped<RLightProps>(gFwdPlus.lightProps, numLights);
        gFwdPlus.numLights = numLights;
        for (uint32 i = 0; i < numLights; i++) {
            const SceneLight& l = lights[i];
            gFwdPlus.lightBounds[i] = {
                .position = Float3(l.boundingSphere.x, l.boundingSphere.y, l.boundingSphere.z),
                .radius = l.boundingSphere.w
            };

            gFwdPlus.lightProps[i] = {
                .color = Color4u::ToFloat4Linear(l.color)
            };
        }
    }

    void SetAmbientLight(Float4 skyAmbientColor, Float4 groundAmbientColor)
    {
        gFwdPlus.lightPerFrameData.skyAmbientColor = Color4u::ToFloat4Linear(skyAmbientColor);
        gFwdPlus.lightPerFrameData.groundAmbientColor = Color4u::ToFloat4Linear(groundAmbientColor);
    }

    void SetSunLight(Float3 direction, Float4 color)
    {
        gFwdPlus.lightPerFrameData.sunLightDir = Float3::Norm(direction);
        gFwdPlus.lightPerFrameData.sunLightColor = Color4u::ToFloat4Linear(color);
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
        R::UpdateBuffersFwdPlus(cmd, scene.mCam);

        // Render
        bool sceneRendered = R::RenderFwdPlus(cmd, mRenderTargetDepth, scene.mModel, scene.mDebugLightCull ? RDebugMode::LightCull : RDebugMode::None);

        // Draw empty scene (Clear framebuffer)
        if (!sceneRendered) {
            GfxBackendRenderPass pass { 
                .colorAttachments = {{ 
                    .clear = true,
                    .clearValue = {
                        .color = Color4u::ToFloat4(COLOR4U_BLACK)
                    }
                }},
                .swapchain = true,
                .hasDepth = false
            };

            cmd.BeginRenderPass(pass);
            cmd.EndRenderPass();
        }

        // DebugDraw
        if (sceneRendered && !scene.mDebugLightCull) {
            DebugDraw::BeginDraw(cmd, *mCam, App::GetFramebufferWidth(), App::GetFramebufferHeight());
            if (mDrawGrid) {
                DebugDrawGridProperties gridProps {
                    .distance = 200,
                    .lineColor = Color4u(0x565656),
                    .boldLineColor = Color4u(0xd6d6d6)
                };

                DebugDraw::DrawGroundGrid(*mCam, gridProps);
            }

            if (scene.mDebugLightBounds) {
                for (const SceneLight& l : scene.mLights) {
                    DebugDraw::DrawBoundingSphere(l.boundingSphere, COLOR4U_WHITE);
                }
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
        },
        .graphics = {
            .surfaceSRGB = true
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

