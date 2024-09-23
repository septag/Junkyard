#include <stdio.h>

#include "../Core/Log.h"
#include "../Core/System.h"
#include "../Core/Atomic.h"
#include "../Core/Jobs.h"
#include "../Core/TracyHelper.h"
#include "../Core/MathAll.h"

#include "../Common/Application.h"
#include "../Common/JunkyardSettings.h"
#include "../Common/VirtualFS.h"

#include "../ImGui/ImGuiMain.h"

#include "../Graphics/Graphics.h"

#include "../Assets/AssetManager.h"
#include "../Assets/Image.h"

#include "../UnityBuild.inl"
#include "../Engine.h"

inline constexpr uint32 CELL_SIZE = 50*SIZE_MB;

struct AppImpl;
struct ReadFileData
{
    AppImpl* app;
    uint64 startTime;
    const char* filepath;
};

struct Cell
{
    String<32> name;
    uint32 row;
    uint32 col;
    Array<uint32> files;
    AssetHandle* handles;   // count = files.Count
    uint32 selectedFile = uint32(-1);
    AssetGroup assetGroup;
};

struct Grid
{
    Cell* cells;
    uint32 numCells;
    uint32 dim;
    uint32 selectedCell = uint32(-1);
};

struct AppImpl final : AppCallbacks
{
    uint32 mNumFilePaths = 0;
    Path* mFilePaths = nullptr;
    AtomicUint64 mTotalBytesRead = 0;
    AtomicUint32 mTotalFilesRead = 0;
    uint64 mStartTime = 0;
    uint64 mDuration = 0;
    uint64 mAccumReadTime = 0;
    MemBumpAllocatorVM mFileAlloc;
    ReadFileData* mFileDatas = nullptr;
    bool mReadFinished = false;
    AssetHandle mTexture;

    Grid mGrid;

    void CreateGrid()
    {
        ASSERT(mNumFilePaths);

        MemTempAllocator tempAlloc;
        Array<Cell> cells(&tempAlloc);

        // Calculate the total size of our assets and divide it up to cells
        uint64 totalAssetSize = 0;
        uint64 cellAssetSize = 0;
        Cell* curCell = cells.Push();

        for (uint32 i = 0; i < mNumFilePaths; i++) {
            uint64 fileSize = Vfs::GetFileSize(mFilePaths[i].CStr());
            cellAssetSize += fileSize;
            totalAssetSize += fileSize;

            curCell->files.Push(i);

            if (cellAssetSize >= CELL_SIZE) {
                curCell = cells.Push();
                cellAssetSize = 0;
            }
        }

        uint32 dimSize = (uint32)mathCeil(mathSqrt(float(cells.Count())));
        for (uint32 i = 0; i < cells.Count(); i++) {
            cells[i].col = i % dimSize;    
            cells[i].row = i / dimSize;    
            cells[i].name = String32::Format("%02u,%02u", cells[i].row, cells[i].col);  // row, col
            cells[i].assetGroup = Asset::CreateGroup();
            if (!cells[i].files.IsEmpty())
                cells[i].handles = Mem::AllocZeroTyped<AssetHandle>(cells[i].files.Count());
        }

        cells.Detach(&mGrid.cells, &mGrid.numCells);
        mGrid.dim = dimSize;

        LOG_INFO("Total asset size: %$llu", totalAssetSize);        
        LOG_INFO("Total cells: %d", mGrid.numCells);
    }

    void DestroyGrid()
    {
        for (uint32 i = 0; i < mGrid.numCells; i++) {
            mGrid.cells[i].assetGroup.Unload();
        }
    }


    bool Initialize() override
    {
        Vfs::HelperMountDataAndShaders(SettingsJunkyard::Get().engine.connectToServer, "test");
        Engine::Initialize();

        LOG_INFO("Reading file list ...");
        MemTempAllocator tempAlloc;
        Blob fileListBlob = Vfs::ReadFile("./test/file_list.txt", VfsFlags::TextFile|VfsFlags::AbsolutePath, &tempAlloc);
        if (!fileListBlob.IsValid()) {
            LOG_ERROR("Could not load file_list.txt");
            return true;
        }

        Span<char*> filePaths = strSplitWhitespace((const char*)fileListBlob.Data(), &tempAlloc);
        mNumFilePaths = filePaths.Count();
        mFilePaths = Mem::AllocZeroTyped<Path>(mNumFilePaths);
        for (uint32 i = 0; i < filePaths.Count(); i++) {
            mFilePaths[i] = Path::JoinUnix("/data", filePaths[i] + 1);
        }
        LOG_INFO("Ready. Total %u files", mNumFilePaths);

        mFileAlloc.Initialize(5*SIZE_GB, SIZE_MB*64);
        // mFileAlloc.WarmUp(); // Makes a big difference

        CreateGrid();

        return true;
    };

    void Cleanup() override
    {
        DestroyGrid();
        mFileAlloc.Release();
        Mem::Free(mFilePaths);
        Engine::Release();
    };

    void LoadTextures()
    {
        AssetGroup group = Asset::CreateGroup();

        for (uint32 i = 0; i < mNumFilePaths; i++) {
            Path fileExt = mFilePaths[i].GetFileExtension();
            if (fileExt != ".tga")
                continue;

            ImageLoadParams imageParams {};
            AssetParams params {
                .typeId = IMAGE_ASSET_TYPE,
                .path = mFilePaths[i].CStr(),
                .typeSpecificParams = &imageParams
            };
            mTexture = group.AddToLoadQueue(params);
        }

        group.Load();
//        group.Unload();
//        group.WaitForLoadFinish();
//        
//        Asset::DestroyGroup(group);
    }

    void Start()
    {
        auto FileReadCallback = [](AsyncFile* file, bool failed)
        {
            ReadFileData* data = (ReadFileData*)file->userData;
            if (!failed) {
                uint64 tm = Timer::Diff(Timer::GetTicks(), data->startTime);
                LOG_DEBUG("File: %s (%.2f ms)", file->filepath.CStr(), Timer::ToMS(tm));
                Atomic::FetchAdd(&data->app->mTotalBytesRead, file->size);
                Atomic::FetchAdd(&data->app->mTotalFilesRead, 1);
                Atomic::FetchAdd(&data->app->mAccumReadTime, tm);
            }
            else {
                LOG_ERROR("Reading file '%s' failed", file->filepath.CStr());
            }
            
            Async::Close(file);
        };

        mFileAlloc.Reset();
        mTotalFilesRead = 0;
        mTotalBytesRead = 0;
        mAccumReadTime = 0;
        mDuration = 0;
        mReadFinished = false;
        mStartTime = Timer::GetTicks();

        PROFILE_ZONE();
        for (uint32 i = 0; i < mNumFilePaths; i++) {
            Path absPath = Vfs::ResolveFilepath(mFilePaths[i].CStr());
            ReadFileData data {
                .app = this,
                .startTime = Timer::GetTicks()
            };
            AsyncFileRequest req {
                .alloc = &mFileAlloc,
                .readFn = FileReadCallback,
                .userData = &data,
                .userDataAllocateSize = sizeof(data)
            };

            Async::ReadFile(absPath.CStr(), req);
        }
    }

    void StartSynchronous()
    {
        mFileAlloc.Reset();
        mTotalFilesRead = 0;
        mTotalBytesRead = 0;
        mAccumReadTime = 0;
        mDuration = 0;
        mReadFinished = false;
        mStartTime = Timer::GetTicks();

        auto FileReadCallback = [](uint32 groupIdx, void* userData)
        {
            const ReadFileData* fileDatas = (const ReadFileData*)userData;
            const ReadFileData& data = fileDatas[groupIdx];
            Blob fileData = Vfs::ReadFile(data.filepath, VfsFlags::None, &data.app->mFileAlloc);
            if (fileData.IsValid()) {
                LOG_DEBUG("File: %s", data.filepath);
                Atomic::FetchAdd(&data.app->mTotalBytesRead, fileData.Size());
                Atomic::FetchAdd(&data.app->mTotalFilesRead, 1);
            }
            else {
                LOG_ERROR("Reading file '%s' failed", data.filepath);
            }
            fileData.Free();
        };

        mFileDatas = Mem::ReallocTyped<ReadFileData>(mFileDatas, mNumFilePaths);

        PROFILE_ZONE();
        for (uint32 i = 0; i < mNumFilePaths; i++) {
            mFileDatas[i] = {
                .app = this,
                .startTime = Timer::GetTicks(),
                .filepath = mFilePaths[i].CStr()
            };
        }

        Jobs::DispatchAndForget(JobsType::LongTask, FileReadCallback, mFileDatas, mNumFilePaths);
    }
    
    void Update(float dt) override
    {
        Engine::BeginFrame(dt);

        uint32 totalFilesRead = Atomic::Load(&mTotalFilesRead);
        uint64 totalBytesRead = Atomic::Load(&mTotalBytesRead);
        if (totalFilesRead == mNumFilePaths && !mReadFinished) {
            mDuration = Timer::Diff(Timer::GetTicks(), mStartTime);
            mReadFinished = true;
        }

        gfxBeginCommandBuffer();
        gfxCmdBeginSwapchainRenderPass();

        if (ImGui::Begin("TestIO")) {
            if (ImGui::Button("Start")) {
                Start();
            }

            ImGui::SameLine();
            if (ImGui::Button("Start Synchronous")) {
                StartSynchronous();
            }

            ImGui::ProgressBar(float(totalFilesRead)/float(mNumFilePaths));
            double totalMegsRead = double(totalBytesRead)/double(SIZE_MB);
            ImGui::Text("Count: %u", totalFilesRead);
            ImGui::Text("Read: %$llu", mTotalBytesRead);

            if (mReadFinished) {
                ImGui::Text("Bandwidth: %.1f MB/s", (totalMegsRead / Timer::ToSec(mDuration)));
                ImGui::Text("Time: %.1f ms, AccumTime: %.1f ms", Timer::ToMS(mDuration), Timer::ToMS(mAccumReadTime));
            }

            ImGui::Separator();

            if (ImGui::Button("LoadAllTextures")) {
                LoadTextures();
            }

            if (mTexture.IsValid()) {
                AssetObjPtrScope<GfxImage> image(mTexture);
                if (image) 
                    ImGui::Image((ImTextureID)IntToPtr(image->handle.mId), ImVec2(256, 256));
            }
        }
        ImGui::End();

        auto GetCellStateColor = [](AssetGroupState state)->ImU32
        {
            switch (state) {
            //case AssetGroupState::Idle: return ImColor(200, 200, 0, 255);
            case AssetGroupState::Loading: return ImColor(200, 0, 0, 255);
            case AssetGroupState::Loaded: return ImColor(0, 200, 0, 255);
            default: return ImColor(ImGui::GetStyleColorVec4(ImGuiCol_Button));
            }
        };

        //ImGui::SetNextWindowSizeConstraints(ImVec2(400, 400), ImVec2(2048, 2048));
        if (ImGui::Begin("Cells")) {
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
                                    MemTempAllocator paramsAlloc;
                                    ImageLoadParams imageParams {};
                                    AssetParams* params = paramsAlloc.MallocTyped<AssetParams>(cell.files.Count());
                                    for (uint32 i = 0; i < cell.files.Count(); i++) {
                                        params[i].typeId = IMAGE_ASSET_TYPE;
                                        params[i].path = mFilePaths[cell.files[i]];
                                        params[i].typeSpecificParams = &imageParams;
                                    }   
                                    cell.assetGroup.AddToLoadQueue(params, cell.files.Count(), cell.handles);
                                    cell.assetGroup.Load();
                                }
                            }
                            else if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
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
                        if (cell.handles[cell.selectedFile].IsValid()) {
                            AssetObjPtrScope<GfxImage> image(cell.handles[cell.selectedFile]);
                            if (image) 
                                ImGui::Image((ImTextureID)IntToPtr(image->handle.mId), ImVec2(256, 256));
                        }
                    }

                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();

        ImGui::DrawFrame();

        gfxCmdEndSwapchainRenderPass();

        gfxEndCommandBuffer();

        Engine::EndFrame(dt);
    }
    
    void OnEvent(const AppEvent& e) override
    {
    }
};

int Main(int argc, char* argv[])
{
    SettingsJunkyard::Initialize(SettingsJunkyard {
        .engine = {
            .logLevel = SettingsEngine::LogLevel::Debug  
        }
    });

    Settings::InitializeFromCommandLine(argc, argv);
   
    static AppImpl impl;

    App::Run(AppDesc { 
        .callbacks = &impl, 
        .windowTitle = "Junkyard - TestIO",
    });

    Settings::Release();
    return 0;
}


