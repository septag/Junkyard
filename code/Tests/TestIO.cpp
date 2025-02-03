#include "../Core/Log.h"
#include "../Core/System.h"
#include "../Core/Atomic.h"
#include "../Core/Jobs.h"
#include "../Core/TracyHelper.h"

#include "../Common/Application.h"
#include "../Common/JunkyardSettings.h"
#include "../Common/VirtualFS.h"

#include "../ImGui/ImGuiMain.h"

#include "../Assets/AssetManager.h"
#include "../Assets/Image.h"

#include "../UnityBuild.inl"
#include "../Engine.h"

struct AppImpl;
struct ReadFileData
{
    AppImpl* app;
    uint64 startTime;
    const char* filepath;
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


    bool Initialize() override
    {
        Vfs::HelperMountDataAndShaders(SettingsJunkyard::Get().engine.connectToServer, "data/TestIO");

        Async::Initialize();
        if (!Engine::Initialize())
            return false;

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
            mFilePaths[i] = Path::JoinUnix("/data", filePaths[i]);
        }
        LOG_INFO("Ready. Total %u files", mNumFilePaths);

        mFileAlloc.Initialize(5*SIZE_GB, SIZE_MB*64);
        // mFileAlloc.WarmUp(); // Makes a big difference

        return true;
    };

    void Cleanup() override
    {
        mFileAlloc.Release();
        Mem::Free(mFilePaths);
        Async::Release();
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

        GfxCommandBuffer cmd = GfxBackend::BeginCommandBuffer(GfxQueueType::Graphics);
        ImGui::DrawFrame(cmd);
        GfxBackend::EndCommandBuffer(cmd);

        GfxBackend::SubmitQueue(GfxQueueType::Graphics);

        Engine::EndFrame();
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


