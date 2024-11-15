#include <stdio.h>

#include "../Core/Settings.h"
#include "../Core/Log.h"
#include "../Core/TracyHelper.h"
#include "../Core/Jobs.h"
#include "../Core/MathAll.h"
#include "../Core/StringUtil.h"

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

inline constexpr uint32 NUM_CUBES = 10;
inline constexpr uint32 CELL_SIZE_BYTES = 45*SIZE_MB;
inline constexpr float CUBE_UNIT_SIZE = 1.1f;

struct CellItem
{
    AssetHandleModel modelHandle;
    AssetHandleImage imageHandle;
};

struct Cell
{
    String<32> name;
    uint32 row;
    uint32 col;
    Float2 center;
    Array<uint32> files;
    CellItem* items;    // count = files.Count()
    uint32 selectedFile = uint32(-1);
    AssetGroup assetGroup;
    bool loaded;
};

struct Grid
{
    Cell* cells;
    uint32 numCells;
    uint32 dim;
    uint32 selectedCell = uint32(-1);
    float cellDim;      // in world units
};

struct AppImpl : AppCallbacks
{
    GfxPipelineHandle mPipeline;
    GfxBufferHandle mUniformBuffer;
    GfxDescriptorSetLayoutHandle mDSLayout;
    AssetHandleShader mUnlitShader;
    uint32 mNumFilePaths = 0;
    Path* mFilePaths = nullptr;

    CameraFPS   mFpsCam;
    CameraOrbit mOrbitCam;
    Camera*     mCam;

    Grid mGrid;

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

    void CreateGrid()
    {
        using namespace M;
        ASSERT(mNumFilePaths);

        Array<Cell> cells;

        // Calculate the total size of our assets and divide it up to cells
        uint64 totalAssetSize = 0;
        uint64 cellAssetSize = 0;
        Cell* curCell = cells.Push();
        Path imagePath;

        for (uint32 i = 0; i < mNumFilePaths; i++) {
            uint64 fileSize = Vfs::GetFileSize(mFilePaths[i].CStr());

            Path imageFilename = mFilePaths[i].GetFileName();
            imagePath.FormatSelf("/data/Tex%s.tga", imageFilename.CStr());

            fileSize += Vfs::GetFileSize(imagePath.CStr());
            cellAssetSize += fileSize;
            totalAssetSize += fileSize;


            curCell->files.Push(i);

            if (cellAssetSize >= CELL_SIZE_BYTES) {
                curCell = cells.Push();
                cellAssetSize = 0;
            }
        }

        uint32 gridDim = (uint32)Ceil(Sqrt(float(cells.Count())));
        float cellDim = 0;
        for (uint32 i = 0; i < cells.Count(); i++) {
            cells[i].col = i % gridDim;    
            cells[i].row = i / gridDim;    
            cells[i].name = String32::Format("%02u,%02u", cells[i].row, cells[i].col);  // row, col
            cells[i].assetGroup = Asset::CreateGroup();
            if (!cells[i].files.IsEmpty()) {
                cells[i].items = Mem::AllocZeroTyped<CellItem>(cells[i].files.Count()); 
                float numCubesPerDim = Ceil(Sqrt(float(cells[i].files.Count())));
                cellDim = Max(CUBE_UNIT_SIZE * numCubesPerDim, cellDim);
            }
        }

        float start = float(gridDim) * cellDim * -0.5f + cellDim * 0.5f;
        float y = start;
        for (uint32 row = 0; row < gridDim; row++) {
            float x = start;
            for (uint32 col = 0; col < gridDim; col++) {
                uint32 index = col + row*gridDim;
                if (index < cells.Count()) {
                    Cell& cell = cells[index];
                    cell.center = Float2(x, y);
                }
                x += cellDim;
            }
            y += cellDim;
        }

        cells.Detach(&mGrid.cells, &mGrid.numCells);
        mGrid.dim = gridDim;
        mGrid.cellDim = cellDim;

        LOG_INFO("Total asset size: %$llu", totalAssetSize);        
        LOG_INFO("Total cells: %d", mGrid.numCells);
    }

    void DestroyGrid()
    {
        for (uint32 i = 0; i < mGrid.numCells; i++) {
            mGrid.cells[i].assetGroup.Unload();
            Mem::Free(mGrid.cells[i].items);
        }
    }

    void LoadCell(uint32 index)
    {
        Cell& cell = mGrid.cells[index];

        MemTempAllocator paramsAlloc;
        ImageLoadParams imageParams {};

        ModelLoadParams modelParams {
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

        Path imagePath;
        for (uint32 i = 0; i < cell.files.Count(); i++) {
            cell.items[i].modelHandle = Asset::LoadModel(mFilePaths[cell.files[i]].CStr(), modelParams, cell.assetGroup);

            Path imageFilename = mFilePaths[cell.files[i]].GetFileName();
            imagePath.FormatSelf("/data/Tex%s.tga", imageFilename.CStr());
            cell.items[i].imageHandle = Asset::LoadImage(imagePath.CStr(), imageParams, cell.assetGroup);
        }   
        cell.assetGroup.Load();

        cell.loaded = true;
    }

    void LoadAll()
    {
        TimerStopWatch timer;
        for (uint32 i = 0; i < mGrid.numCells; i++) {
            LoadCell(i);            
        }
        Asset::Update();

        while (true) {
            uint32 numLoaded = 0;
            for (uint32 i = 0; i < mGrid.numCells; i++) {
                Cell& cell = mGrid.cells[i];
                if (cell.assetGroup.IsLoadFinished()) 
                    numLoaded++;
            }

            if (numLoaded == mGrid.numCells)
                break;
            else {
                Asset::Update();
            }
        }

        LOG_INFO("Load finished: %0.2f ms", timer.ElapsedMS());
    }

    void DrawCell(uint32 index)
    {
        Cell& cell = mGrid.cells[index];

        float cellDim = mGrid.cellDim;
        Float2 cellCenter = cell.center;
        uint32 cubeIndex = 0;
        Float2 startPt = Float2(cellCenter.x - cellDim*0.5f, cellCenter.y - cellDim*0.5f) + Float2(CUBE_UNIT_SIZE, CUBE_UNIT_SIZE)*0.5f;
        Float2 endPt = Float2(cellCenter.x + cellDim*0.5f, cellCenter.y + cellDim*0.5f);
        
        for (float y = startPt.y; y <= endPt.y; y += CUBE_UNIT_SIZE) {
            for (float x = startPt.x; x <= endPt.x; x += CUBE_UNIT_SIZE) {
                if (cubeIndex >= cell.files.Count()) {
                    return;
                }
                const CellItem& item = cell.items[cubeIndex++];
               
                AssetObjPtrScope<Model> model(item.modelHandle);
                AssetObjPtrScope<GfxImage> image(item.imageHandle);

                if (model.IsNull() || image.IsNull())
                    continue;

                for (uint32 i = 0; i < model->numNodes; i++) {
                    const ModelNode& node = model->nodes[i];
                    if (node.meshId) {
                        const ModelMesh& mesh = model->meshes[IdToIndex(node.meshId)];
    
                        // Buffers
                        uint64* offsets = (uint64*)alloca(sizeof(uint64)*mesh.numVertexBuffers);
                        memset(offsets, 0x0, sizeof(uint64)*mesh.numVertexBuffers);
                        gfxCmdBindVertexBuffers(0, mesh.numVertexBuffers, mesh.gpuBuffers.vertexBuffers, offsets);
                        gfxCmdBindIndexBuffer(mesh.gpuBuffers.indexBuffer, 0, GfxIndexType::Uint32);

                        Mat4 worldMat = Mat4::Translate(x, y, 0.5f);
                        gfxCmdPushConstants(mPipeline, GfxShaderStage::Vertex, &worldMat, sizeof(worldMat));
    
                        GfxDescriptorBindingDesc bindings[] = {
                            {
                                .name = "FrameTransform",
                                .type = GfxDescriptorType::UniformBuffer,
                                .buffer = { mUniformBuffer, 0, sizeof(FrameTransform) }
                            },
                            {
                                .name = "BaseColorTexture",
                                .type = GfxDescriptorType::CombinedImageSampler,
                                .image = image->handle
                            }
                        };
                        gfxCmdPushDescriptorSet(mPipeline, GfxPipelineBindPoint::Graphics, 0, CountOf(bindings), bindings);
                        gfxCmdDrawIndexed(mesh.numIndices, 1, 0, 0, 0);
                    }  
                } // foreach (node)
            }
        }
    }

    bool Initialize() override
    {
        Vfs::HelperMountDataAndShaders(SettingsJunkyard::Get().engine.connectToServer, "data/TestAsset");

        if (!Engine::Initialize())
            return false;

        mFpsCam.SetLookAt(Float3(0, -2.0f, 3.0f), FLOAT3_ZERO);
        mFpsCam.Setup(50.0f, 0.1f, 1000.0f);
        mOrbitCam.SetLookAt(Float3(0, -2.0f, 3.0f), FLOAT3_ZERO);
        mCam = &mFpsCam;

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

        LOG_INFO("Reading file list ...");
        MemTempAllocator tempAlloc;
        Blob fileListBlob = Vfs::ReadFile("/data/file_list.txt", VfsFlags::TextFile, &tempAlloc);
        if (!fileListBlob.IsValid()) {
            LOG_ERROR("Could not load file_list.txt");
            return true;
        }

        Span<char*> filePaths = Str::SplitWhitespace((const char*)fileListBlob.Data(), &tempAlloc);
        mNumFilePaths = filePaths.Count();
        mFilePaths = Mem::AllocZeroTyped<Path>(mNumFilePaths);
        for (uint32 i = 0; i < filePaths.Count(); i++) {
            mFilePaths[i] = Path::JoinUnix("/data/", filePaths[i]);
        }
        LOG_INFO("Ready. Total %u files", mNumFilePaths);

        CreateGrid();

        return true;
    };
    
    void Cleanup() override
    {
        DestroyGrid();

        ReleaseGraphicsObjects();
        Engine::Release();
    };

    void ShowGridGUI()
    {
        auto GetCellStateColor = [](AssetGroupState state)->ImU32
        {
            switch (state) {
            //case AssetGroupState::Idle: return ImColor(200, 200, 0, 255);
            case AssetGroupState::Loading: return ImColor(200, 0, 0, 255);
            case AssetGroupState::Loaded: return ImColor(0, 200, 0, 255);
            default: return ImColor(ImGui::GetStyleColorVec4(ImGuiCol_Button));
            }
        };

        if (ImGui::Begin("Cells")) {
            if (ImGui::Button("Load All")) 
                LoadAll();

            if (ImGui::BeginTable("GridTable", mGrid.dim)) {
                for (uint32 row = 0; row < mGrid.dim; row++) {
                    ImGui::TableNextRow();

                    for (uint32 col = 0; col < mGrid.dim; col++) {
                        ImGui::TableSetColumnIndex(col);

                        uint32 index = col + row*mGrid.dim;
                        if (index < mGrid.numCells) {
                            Cell& cell = mGrid.cells[index];
                            AssetGroupState state = cell.assetGroup.GetState();
                            ImGui::PushStyleColor(ImGuiCol_Button, GetCellStateColor(state));
                            ImGui::SetItemAllowOverlap();
                            if (ImGui::Selectable(String32::Format("##%s", cell.name.CStr()).CStr(), mGrid.selectedCell == index, ImGuiSelectableFlags_None)) 
                                mGrid.selectedCell = index;

                            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                                if (state == AssetGroupState::Idle) {
                                    LoadCell(index);
                                }
                            }
                            else if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                                cell.loaded = false;
                                cell.assetGroup.Unload();
                            }

                            ImGui::SameLine();
                            ImGui::SmallButton(cell.name.CStr());
                            ImGui::PopStyleColor();
                        }
                    }
                }

                ImGui::EndTable();
            }

            ImGui::Separator();
            ImGui::BeginChild("CellDetails");
            if (mGrid.selectedCell != -1) {
                if (ImGui::BeginTable("CellViewTable", 2)) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    Cell& cell = mGrid.cells[mGrid.selectedCell];
                    for (uint32 i = 0; i < cell.files.Count(); i++) {
                        if (ImGui::Selectable(mFilePaths[cell.files[i]].CStr(), i == cell.selectedFile)) {
                            cell.selectedFile = i;
                        }
                    }

                    ImGui::SeparatorVertical();
                    ImGui::TableSetColumnIndex(1);

                    if (cell.selectedFile != -1) {
                        // TODO:
                    }

                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }


    void Update(fl32 dt) override
    {
        PROFILE_ZONE();

        mCam->HandleMovementKeyboard(dt, 40.0f, 20.0f);

        Engine::BeginFrame(dt);
        
        gfxBeginCommandBuffer();
        
        gfxCmdBeginSwapchainRenderPass(Color(100, 100, 100));

        float width = (float)App::GetFramebufferWidth();
        float height = (float)App::GetFramebufferHeight();

        // Viewport
        GfxViewport viewport {
            .width = width,
            .height = height,
        };

        gfxCmdSetViewports(0, 1, &viewport, true);
        RectInt scissor(0, 0, App::GetFramebufferWidth(), App::GetFramebufferHeight());
        gfxCmdSetScissors(0, 1, &scissor, true);

        // We are drawing to swapchain, so we need ClipSpaceTransform
        FrameTransform ubo {
            .viewMat = mCam->GetViewMat(),
            .projMat = gfxGetClipspaceTransform() * mCam->GetPerspectiveMat(width, height)
        };

        gfxCmdUpdateBuffer(mUniformBuffer, &ubo, sizeof(ubo));
        gfxCmdBindPipeline(mPipeline);

        for (uint32 i = 0; i < mGrid.numCells; i++) {
            if (mGrid.cells[i].loaded) 
                DrawCell(i);
        }

        {
            DebugDraw::DrawGroundGrid(*mCam, width, height, DebugDrawGridProperties { 
                .distance = 50.0f,
                .lineColor = Color(0x565656), 
                .boldLineColor = Color(0xd6d6d6)
            });
        }

        if (ImGui::IsEnabled()) { // imgui test
            PROFILE_GPU_ZONE_NAME("ImGuiRender", true);
            DebugHud::DrawDebugHud(dt);

            ShowGridGUI();
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
            AssetObjPtrScope<GfxShader> shader(self->mUnlitShader);
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

            self->mDSLayout = gfxCreateDescriptorSetLayout(*shader, bindingLayout, CountOf(bindingLayout), GfxDescriptorSetLayoutFlags::PushDescriptor);
        }

        GfxBufferDesc uniformBufferDesc {
            .size = sizeof(FrameTransform),
            .type = GfxBufferType::Uniform,
            .usage = GfxBufferUsage::Stream
        };

        self->mUniformBuffer = gfxCreateBuffer(uniformBufferDesc);

        GfxPushConstantDesc pushConstant {
            .name = "ModelTransform",
            .stages = GfxShaderStage::Vertex,
            .range = {0, sizeof(Mat4)}
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
    }

    void ReleaseGraphicsObjects()
    {
        gfxWaitForIdle();
        gfxDestroyPipeline(mPipeline);
        gfxDestroyDescriptorSetLayout(mDSLayout);
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

    LOG_DEBUG("Initializing engine.");
    
    static AppImpl impl;
    App::Run(AppDesc { 
        .callbacks = &impl, 
        .windowTitle = "Junkyard: Asset Loading test",
    });

    Settings::Release();
    return 0;
}
