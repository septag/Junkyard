#include <stdio.h>

#include "../Core/Log.h"
#include "../Core/System.h"
#include "../Core/Atomic.h"

#include "../Common/Application.h"
#include "../Common/JunkyardSettings.h"
#include "../Common/VirtualFS.h"

#include "../ImGui/ImGuiMain.h"

#include "../Graphics/Graphics.h"

#include "../UnityBuild.inl"
#include "../Engine.h"

struct AppImpl;
struct ReadFileData
{
    AppImpl* app;
    uint64 startTime;
};


struct AppImpl final : AppCallbacks
{
    uint32 mNumFilePaths = 0;
    Path* mFilePaths = nullptr;
    AtomicUint64 mTotalBytesRead = 0;
    AtomicUint32 mTotalFilesRead = 0;
    TimerStopWatch mStopWatch;
    double mElapsedReadTime;
    uint64 mAccumReadTime = 0;
    MemThreadSafeAllocator mFileAlloc;
    MemBumpAllocatorVM mFileAllocBackend;

    bool Initialize() override
    {
        Vfs::HelperMountDataAndShaders(SettingsJunkyard::Get().engine.connectToServer);
        Engine::Initialize();

        LOG_INFO("Reading file list ...");
        MemTempAllocator tempAlloc;
        Blob fileListBlob = Vfs::ReadFile("/data/file_list.txt", VfsFlags::TextFile, &tempAlloc);
        if (!fileListBlob.IsValid()) {
            LOG_ERROR("Could not load file_list.txt");
            return false;
        }

        Span<char*> filePaths = strSplit((const char*)fileListBlob.Data(), '\n', &tempAlloc);
        mNumFilePaths = filePaths.Count();
        mFilePaths = Mem::AllocZeroTyped<Path>(mNumFilePaths);
        for (uint32 i = 0; i < filePaths.Count(); i++) {
            mFilePaths[i] = Path::JoinUnix("/data", filePaths[i] + 1);
        }
        LOG_INFO("Ready. Total %u files", mNumFilePaths);

        mFileAllocBackend.Initialize(10*SIZE_GB, SIZE_MB*64);
        mFileAlloc.SetAllocator(&mFileAllocBackend);

        Async::Initialize();

        return true;
    };

    void Cleanup() override
    {
        Async::Release();
        mFileAllocBackend.Release();
        Mem::Free(mFilePaths);
        Engine::Release();
    };

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
        };

        mFileAllocBackend.Reset();
        mTotalFilesRead = 0;
        mTotalBytesRead = 0;
        mAccumReadTime = 0;
        mStopWatch.Reset();   

        TimerStopWatch watch;        
        for (uint32 i = 0; i < mNumFilePaths; i++) {
            Path absPath = Vfs::ResolveFilepath(mFilePaths[i].CStr());
            ReadFileData data {
                .app = this,
                .startTime = Timer::GetTicks()
            };
            AsyncFileRequest req {
                .alloc = &mFileAllocBackend,
                .readFn = FileReadCallback,
                .userData = &data,
                .userDataAllocateSize = sizeof(data)
            };

            Async::ReadFile(absPath.CStr(), req);
        }
        LOG_INFO("ReadFile(s) took: %.1f ms", watch.ElapsedMS());
    }
    
    void Update(float dt) override
    {
        Engine::BeginFrame(dt);

        uint32 totalFilesRead = Atomic::Load(&mTotalFilesRead);
        uint64 totalBytesRead = Atomic::Load(&mTotalBytesRead);
        if (totalFilesRead < mNumFilePaths)
            mElapsedReadTime = mStopWatch.ElapsedSec();

        gfxBeginCommandBuffer();
        gfxCmdBeginSwapchainRenderPass();

        if (ImGui::Begin("TestIO")) {
            if (ImGui::Button("Start")) {
                Start();
            }

            ImGui::ProgressBar(float(totalFilesRead)/float(mNumFilePaths));
            double totalMegsRead = double(totalBytesRead)/double(SIZE_MB);
            ImGui::Text("Count: %u", totalFilesRead);
            ImGui::Text("Read: %$llu", mTotalBytesRead);
            ImGui::Text("Bandwidth: %.1f MB/s", (totalMegsRead / mElapsedReadTime));
            ImGui::Text("Time: %.1f ms, AccumTime: %.1f ms", mElapsedReadTime*1000.0f, Timer::ToMS(mAccumReadTime));
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


