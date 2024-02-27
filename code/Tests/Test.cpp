#include <stdio.h>

#include "../Core/Settings.h"
#include "../Core/Log.h"
#include "../Core/TracyHelper.h"
#include "../Core/Jobs.h"
#include "../Core/MathAll.h"
#include "../Core/System.h"

#include "../Common/VirtualFS.h"
#include "../Common/Application.h"
#include "../Common/Camera.h"
#include "../Common/JunkyardSettings.h"

#include "../Assets/AssetManager.h"

#include "../DebugTools/DebugDraw.h"
#include "../DebugTools/FrameInfoHud.h"
#include "../DebugTools/BudgetViewer.h"

#include "../ImGui/ImGuiWrapper.h"
#include "../ImGui/ImGuizmo.h"

#include "../Assets/Model.h"
#include "../Assets/Shader.h"

#include "../UnityBuild.inl"

#include "../Tool/Console.h"

#include "../Engine.h"

#define TEST_IO 0

#if TEST_IO
#include <dirent.h>
#include "../Core/Atomic.h"

static RandomContext gRand = randomCreateContext(666);

static void BruteforceBlockMethod(const Path* paths, int numFiles, atomicUint64& totalSize, atomicUint64& hashTime)
{
    for (int i = 0; i < numFiles; i++) {
        File f;
        MemTempAllocator tmpAlloc;
        bool r = f.Open(paths[i].CStr(), FileOpenFlags::Read|FileOpenFlags::SeqScan);
        ASSERT(r);
        uint8* buffer = tmpAlloc.MallocTyped<uint8>((uint32)f.GetSize());
        uint32 bytesRead = f.Read<uint8>(buffer, (uint32)f.GetSize());
        ASSERT(bytesRead == f.GetSize());

        // ProcessAndShowInfo(paths[i].CStr(), buffer, bytesRead, &hashTime);
        f.Close();
        atomicFetchAdd64(&totalSize, (uint64)bytesRead);
    }
}

static void BruteforceTaskBlockMethod(const Path* paths, int numFiles, atomicUint64& totalSize, atomicUint64& hashTime)
{
    struct IOTask
    {
        atomicUint64 totalSize;
        const Path* paths;
        atomicUint64* hashTime;
    };
    IOTask task { .paths = paths, .hashTime = &hashTime };

    JobsHandle handle = jobsDispatch(JobsType::LongTask, [](uint32 groupIndex, void* userData) {
        IOTask* task = (IOTask*)userData;
        File f;
        bool r = f.Open(task->paths[groupIndex].CStr(), FileOpenFlags::Read|FileOpenFlags::SeqScan);
        ASSERT(r);
        MemTempAllocator tmpAlloc;
        uint8* buffer = tmpAlloc.MallocTyped<uint8>((uint32)f.GetSize());
        uint32 bytesRead = f.Read<uint8>(buffer, (uint32)f.GetSize());
        ASSERT(bytesRead == f.GetSize());

        // ProcessAndShowInfo(task->paths[groupIndex].CStr(), buffer, bytesRead, task->hashTime);
        f.Close();
        atomicFetchAdd64(&task->totalSize, bytesRead);
    }, &task, numFiles);
    jobsWaitForCompletion(handle);
    totalSize = task.totalSize;
}

static void TestIO()
{
    // scan the directory and gather all the TGA files
    Path rootDir = "/sdcard/Documents/junkyard/TestIO";
    MemTempAllocator tmpAlloc;
    Array<Path> pathsArr(&tmpAlloc);
    char filename[32];
    for (uint32 i = 0; i < 1024; i++) {
        strPrintFmt(filename, sizeof(filename), "%u.tga", i+1);
        pathsArr.Push(Path::Join(rootDir, filename));
    }

    Path* paths = nullptr;
    int numFiles;
    pathsArr.Detach(&paths, (uint32*)&numFiles);

    logInfo("Found %d files", numFiles);
    logInfo("Ready.");
        
    using BenchmarkCallback = void(*)(const Path* paths, int numFiles, atomicUint64& totalSize, atomicUint64& hashTime);
    auto BenchmarkIO = [paths, numFiles](const char* name, BenchmarkCallback callback) {
        logInfo("Cleaning file-system cache ...");

        atomicUint64 totalSize = 0;
        atomicUint64 hashTime = 0;

        logInfo("# %s:", name);

        TimerStopWatch stopWatch;
        callback(paths, numFiles, totalSize, hashTime);

        uint64 elapsed = stopWatch.Elapsed();
        logInfo("  Took: %.2f s (%.0f ms)", timerToSec(elapsed), timerToMS(elapsed));
        logInfo("  Total size: %_$llu", totalSize);
        float bandwith = float(double(totalSize/kMB)/timerToSec(elapsed));
        logInfo("  Bandwidth: %.0f MB/s (%.2f GB/s)", bandwith, bandwith/1024.0f);
        logInfo("");
    };

    BenchmarkIO("Blocking - Read files one by one", BruteforceBlockMethod);
    BenchmarkIO("Blocking - Read files with jobs", BruteforceTaskBlockMethod);
}
#endif

struct AppImpl final : AppCallbacks
{
    GfxPipeline pipeline;
    GfxBuffer uniformBuffer;
    GfxDescriptorSetLayout dsLayout;

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
        memTempSetCaptureStackTrace(true);
        // Mount file-systems before initializing engine
        if (settingsGet().engine.connectToServer) {
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

        #if TEST_IO
        TestIO();
        #endif

        return true;
    };
    
    void Cleanup() override
    {
        ReleaseGraphicsObjects();

        assetUnload(modelAsset);
        assetUnload(modelShaderAsset);

        engineRelease();
    };
    
    static void ChildTask(uint32 groupIndex, void*)
    {
        PROFILE_ZONE(true);
        
        threadSleep(5);
    }

    static void MainTaskSub()
    {
        PROFILE_ZONE(true);
        threadSleep(3);
        JobsHandle handle;
        handle = jobsDispatch(JobsType::LongTask, ChildTask, nullptr, 1);
        jobsWaitForCompletion(handle);
        threadSleep(1);
    }

    static void MainTask(uint32 groupIndex, void*)
    {
        PROFILE_ZONE(true);
        threadSleep(1);
        MainTaskSub();
        threadSleep(7);
    }

    void Update(fl32 dt) override
    {
        PROFILE_ZONE(true);

        cam->HandleMovementKeyboard(dt, 100.0f, 5.0f);

        engineBeginFrame(dt);
        
        gfxBeginCommandBuffer();
        
        gfxCmdBeginSwapchainRenderPass();

        float width = (float)appGetFramebufferWidth();
        float height = (float)appGetFramebufferHeight();

        static Mat4 modelMat = kMat4Ident;

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
                    uint64* offsets = tmpAlloc.MallocTyped<uint64>(mesh.numVertexBuffers);
                    memset(offsets, 0x0, sizeof(uint64)*mesh.numVertexBuffers);
                    gfxCmdBindVertexBuffers(0, mesh.numVertexBuffers, mesh.gpuBuffers.vertexBuffers, offsets);
                    gfxCmdBindIndexBuffer(mesh.gpuBuffers.indexBuffer, 0, GfxIndexType::Uint32);

                    // DescriptorSets
                    for (uint32 smi = 0; smi < mesh.numSubmeshes; smi++) {
                        const ModelSubmesh& submesh = mesh.submeshes[smi];
                        const ModelMaterial* mtl = model->materials[IdToIndex(submesh.materialId)].Get();
                        GfxDescriptorSet dset(PtrToInt<uint32>(mtl->userData));
                        gfxCmdBindDescriptorSets(pipeline, 1, &dset);
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
            budgetViewerRender(dt);
            frameInfoRender(dt);

            #if 0
                Mat4 view = fpsCam.GetViewMat();
                ImGuizmo::ViewManipulate(view.f, 0.1f, ImVec2(5.0f, height - 128.0f - 5.0f), ImVec2(128, 128), 0xff000000);
                fpsCam.SetViewMat(view);
            #endif

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
            modelShaderAsset = assetLoadShader("/code/shaders/Model.hlsl", ShaderLoadParams {}, b.Barrier());
        }
        if (!assetIsAlive(modelAsset) || !assetIsAlive(modelShaderAsset))
            return false;

        uniformBuffer = gfxCreateBuffer(GfxBufferDesc {
                                        .size = sizeof(FrameTransform),
                                        .type = GfxBufferType::Uniform,
                                        .usage = GfxBufferUsage::Stream
        });

        dsLayout = gfxCreateDescriptorSetLayout(*assetGetShader(modelShaderAsset), bindingLayout, CountOf(bindingLayout));

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
                .attachments = gfxBlendAttachmentDescGetDefault()
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

                material->userData = IntToPtr(uint32(dset));
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
    }
};

int Main(int argc, char* argv[])
{
    settingsInitializeJunkyard({});

    #if PLATFORM_ANDROID
        settingsInitializeFromAndroidAsset(appAndroidGetAssetManager(), "Settings.ini");
    #else
        settingsInitializeFromCommandLine(argc, argv);
    #endif
   
    static AppImpl impl;
    appInitialize(AppDesc { 
        .callbacks = &impl, 
        .windowTitle = "Junkyard"
    });

    settingsRelease();
    return 0;
}
