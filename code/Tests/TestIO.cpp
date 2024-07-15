#include <stdio.h>

#include "../Core/Settings.h"
#include "../Core/Log.h"
#include "../Core/TracyHelper.h"
#include "../Core/Jobs.h"
#include "../Core/MathAll.h"
#include "../Core/System.h"
#include "../Core/Atomic.h"
#include "../Core/Hash.h"
#include "../Core/Allocators.h"
#include "../Core/Pools.h"

#include "../UnityBuild.inl"

#include "../Common/Application.h"
#include "../Common/JunkyardSettings.h"
#include "../Common/VirtualFS.h"

#include "../Engine.h"

#include "../External/dirent/dirent.h"

static RandomContext gRand = Random::CreateContext(666);
static const char* kRootDir = "";

#define WITH_HASHING 0

#if PLATFORM_WINDOWS
#include "../Core/Includewin.h"

bool FlushModifiedData()
{
    static bool prived;
    if(!prived) {
        HANDLE processToken;
        if(OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &processToken) == FALSE)
            return false;

        LUID luid{};
        if(!LookupPrivilegeValueW(nullptr, L"SeProfileSingleProcessPrivilege", &luid)) {
            CloseHandle(processToken);
            return false;
        }
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if(AdjustTokenPrivileges(processToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), static_cast<PTOKEN_PRIVILEGES>(nullptr), static_cast<PDWORD>(nullptr)) == 0) {
            CloseHandle(processToken);
            return false;
        }

        if(GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
            CloseHandle(processToken);
            return false;
        }

        prived = true;
    }

    typedef enum _SYSTEM_INFORMATION_CLASS {
        SystemBasicInformation = 0,
        SystemPerformanceInformation = 2,
        SystemTimeOfDayInformation = 3,
        SystemProcessInformation = 5,
        SystemProcessorPerformanceInformation = 8,
        SystemInterruptInformation = 23,
        SystemExceptionInformation = 33,
        SystemRegistryQuotaInformation = 37,
        SystemLookasideInformation = 45
    } SYSTEM_INFORMATION_CLASS;

    typedef enum _SYSTEM_MEMORY_LIST_COMMAND
    {
        MemoryCaptureAccessedBits,
        MemoryCaptureAndResetAccessedBits,
        MemoryEmptyWorkingSets,
        MemoryFlushModifiedList,
        MemoryPurgeStandbyList,
        MemoryPurgeLowPriorityStandbyList,
        MemoryCommandMax
    } SYSTEM_MEMORY_LIST_COMMAND;  // NOLINT

    HMODULE dll = LoadLibraryA("ntdll.dll");
    if (!dll)
        return false;
    typedef uint32_t (*NtSetSystemInformationFn)(__in SYSTEM_INFORMATION_CLASS SystemInformationClass, __in_bcount_opt(SystemInformationLength) PVOID SystemInformation, __in ULONG SystemInformationLength);

    NtSetSystemInformationFn NtSetSystemInformation = (NtSetSystemInformationFn)GetProcAddress(dll, "NtSetSystemInformation");

    // Write all modified pages to storage
    SYSTEM_MEMORY_LIST_COMMAND command = MemoryPurgeStandbyList;
    uint32_t ntstat = NtSetSystemInformation(SYSTEM_INFORMATION_CLASS(80) /*SystemMemoryListInformation*/, &command, sizeof(SYSTEM_MEMORY_LIST_COMMAND));

    FreeLibrary(dll);
    if(ntstat != 0)
        return false;
    return true;
}

// https://github.com/ned14/llfio/blob/418a2e9312ff0f1760c73c05b4fe476c761cfc22/include/llfio/v2.0/detail/impl/windows/utils.ipp#L165
static bool CleanFileSystemCache()
{
    static bool prived = false;
    if(!prived)    {
        HANDLE processToken;
        if(OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &processToken) == FALSE)
            return false;
        LUID luid{};
        if(!LookupPrivilegeValueW(nullptr, L"SeIncreaseQuotaPrivilege", &luid)) {
            CloseHandle(processToken);
            return false;
        }

        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if(AdjustTokenPrivileges(processToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), static_cast<PTOKEN_PRIVILEGES>(nullptr), static_cast<PDWORD>(nullptr)) == 0) {
            CloseHandle(processToken);
            return false;
        }

        if(GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
            CloseHandle(processToken);
            return false;
        }
        CloseHandle(processToken);

        prived = true;
    }

    // Flush modified data so dropping the cache drops everything
    if (!FlushModifiedData()) 
        return false;

    // Drop filesystem cache
    if(SetSystemFileCacheSize(static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1), 0) == 0)
        return false;

    return true;
}
#endif // PLATFORM_WINDOWS

struct AppImpl final : AppCallbacks
{
    bool Initialize() override
    {
        // Mount file-systems before initializing engine
        if (SettingsJunkyard::Get().engine.connectToServer) {
            Vfs::MountRemote("data", true);
            Vfs::MountRemote("code", true);
        }
        else {        
            Vfs::MountLocal("data", "data", true);
            Vfs::MountLocal("code", "code", true);
        }

        if (!Engine::Initialize())
            return false;

        Engine::RegisterShortcut("ESC", [](void*) { App::Quit(); });


        Path rootDir = kRootDir[0] ? kRootDir : "data/images/TestIO";
        rootDir.ConvertToUnix();
        if (!rootDir.IsDir()) {
            LOG_ERROR("Root directory '%s' does not exist. Run the python script generate-images.py and copy the generated folder", rootDir.CStr());
            return false;
        }

        // scan the directory and gather all the TGA files
        LOG_INFO("Scanning directory: %s", rootDir.CStr());
        dirent** entries;
        MemTempAllocator tmpAlloc;
        Path* paths = nullptr;
        int numFiles = scandir(rootDir.CStr(), &entries, 
                [](const dirent* entry)->int { return strEndsWith(entry->d_name, ".tga") ? 1 : 0; },
                [](const dirent** a, const dirent** b) { return Random::Int(&gRand, -1, 1); });
        if (numFiles > 0) {
            paths = tmpAlloc.MallocZeroTyped<Path>(numFiles);
            for (int i = 0; i < numFiles; i++) {
                paths[i] = Path::JoinUnix(rootDir, entries[i]->d_name);
            }
        }
        else {
            LOG_ERROR("No files found in '%s'", rootDir.CStr());
            return false;
        }

        LOG_INFO("Found %d files", numFiles);
        LOG_INFO("Ready.");
        
        using BenchmarkCallback = void(*)(const Path* paths, int numFiles, size_t& totalSize, AtomicUint64& hashTime);
        auto BenchmarkIO = [paths, numFiles](const char* name, BenchmarkCallback callback) {
            LOG_INFO("Cleaning file-system cache ...");
            if (!CleanFileSystemCache()) {
                LOG_WARNING("Cleaning file-system cache failed. Results may not be accurate. Try running with admin priviliges");
            }

            size_t totalSize = 0;
            AtomicUint64 hashTime = 0;

            LOG_INFO("# %s:", name);

            TimerStopWatch stopWatch;
            callback(paths, numFiles, totalSize, hashTime);

            uint64 elapsed = stopWatch.Elapsed();
            LOG_INFO("  Took: %.2f s (%.0f ms)", Timer::ToSec(elapsed), Timer::ToMS(elapsed));
            LOG_INFO("  Total size: %_$llu", totalSize);
            float bandwith = float(double(totalSize/SIZE_MB)/Timer::ToSec(elapsed));
            LOG_INFO("  Bandwidth: %.0f MB/s (%.2f GB/s)", bandwith, bandwith/1024.0f);
            LOG_INFO("");
        };

        BenchmarkIO("Blocking - Read files one by one", BruteforceBlockMethod);
        BenchmarkIO("Blocking - Read files with jobs", BruteforceTaskBlockMethod);
        BenchmarkIO("Async - Request all files in the main thread and wait", BruteforceAsyncMethod);
        BenchmarkIO("Async - Read files with jobs. Wait for each file", BruteforceTaskAsyncMethod);
        BenchmarkIO("Async - Read files with jobs. Yield jobs until finished", SignalTaskAsyncMethod);
        // BenchmarkIO("Async - NoCache", NoCacheMethod);
        BenchmarkIO("Async - NoCache, CPIO", CPIO_Method);

        LOG_INFO("Done. Press ESC to quit");

        return true;
    };

    static void ProcessAndShowInfo(const char* filename, const void* data, uint32 size, AtomicUint64* hashTime)
    {
        #if WITH_HASHING
            TimerStopWatch watch;
            uint32 hash = Hash::Murmur32(data, size, 666);
            logDebug("File = %s, Hash = 0x%x", filename, hash);
            Atomic::FetchAdd(hashTime, watch.Elapsed());
        #endif
    }

    static void SignalTaskAsyncMethod(const Path* paths, int numFiles, size_t& totalSize, AtomicUint64& hashTime)
    {
        struct IOTask
        {
            MemBumpAllocatorVM vmAlloc;
            MemThreadSafeAllocator alloc;
            uint64 totalSize;
            const Path* paths;
            JobsSignal* signals;
            AtomicUint64* hashTime;
        };
        IOTask task { .paths = paths, .hashTime = &hashTime };

        MemTempAllocator tmpAlloc;
        task.signals = tmpAlloc.MallocZeroTyped<JobsSignal>(numFiles);
        task.vmAlloc.Initialize(SIZE_GB*8, SIZE_GB);
        task.alloc.SetAllocator(&task.vmAlloc);

        JobsHandle handle = Jobs::Dispatch(JobsType::LongTask, [](uint32 groupIndex, void* userData) {
            IOTask* task = (IOTask*)userData;

            auto ReadFileFinished = [](AsyncFile* file, bool failed) {
                JobsSignal* signal = (JobsSignal*)file->userData;
                ASSERT(!failed);
                signal->Increment();
                signal->Raise();
            };

            JobsSignal* signal = &task->signals[groupIndex];

            AsyncFile* file = Async::ReadFile(task->paths[groupIndex].CStr(), { 
                .alloc = &task->alloc, 
                .readFn = ReadFileFinished, 
                .userData = &task->signals[groupIndex] 
            });
            task->signals[groupIndex].Wait();

            ProcessAndShowInfo(file->filepath.CStr(), file->data, file->size, task->hashTime);
            Atomic::FetchAdd(&task->totalSize, file->size);
            Async::Close(file);
        }, &task, numFiles, JobsPriority::Normal, JobsStackSize::Small);
        Jobs::WaitForCompletion(handle);
        totalSize = task.totalSize;
        task.vmAlloc.Release();
    }

    static void BruteforceTaskAsyncMethod(const Path* paths, int numFiles, size_t& totalSize, AtomicUint64& hashTime)
    {
        struct IOTask
        {
            MemBumpAllocatorVM vmAlloc;
            MemThreadSafeAllocator alloc;
            uint64 totalSize;
            const Path* paths;
            AtomicUint64* hashTime;
        };
        IOTask task { .paths = paths, .hashTime = &hashTime };
        task.vmAlloc.Initialize(SIZE_GB*8, SIZE_GB);
        task.alloc.SetAllocator(&task.vmAlloc);

        JobsHandle handle = Jobs::Dispatch(JobsType::LongTask, [](uint32 groupIndex, void* userData) {
            IOTask* task = (IOTask*)userData;

            AsyncFile* file = Async::ReadFile(task->paths[groupIndex].CStr(), { .alloc = &task->alloc });
            ASSERT(file);
            if (Async::Wait(file))
                Atomic::FetchAdd(&task->totalSize, file->size);
            else
                ASSERT(0);

            ProcessAndShowInfo(file->filepath.CStr(), file->data, file->size, task->hashTime);
            Async::Close(file);

        }, &task, numFiles);
        Jobs::WaitForCompletion(handle);
        totalSize = task.totalSize;
        task.vmAlloc.Release();
    }

    static void BruteforceBlockMethod(const Path* paths, int numFiles, size_t& totalSize, AtomicUint64& hashTime)
    {
        for (int i = 0; i < numFiles; i++) {
            File f;
            MemTempAllocator tmpAlloc;
            bool r = f.Open(paths[i].CStr(), FileOpenFlags::Read|FileOpenFlags::SeqScan);
            ASSERT(r);
            uint8* buffer = tmpAlloc.MallocTyped<uint8>((uint32)f.GetSize());
            uint32 bytesRead = f.Read<uint8>(buffer, (uint32)f.GetSize());
            ASSERT(bytesRead == f.GetSize());

            ProcessAndShowInfo(paths[i].CStr(), buffer, bytesRead, &hashTime);
            f.Close();
            Atomic::FetchAdd(&totalSize, bytesRead);
        }
    }

    static void BruteforceTaskBlockMethod(const Path* paths, int numFiles, size_t& totalSize, AtomicUint64& hashTime)
    {
        struct IOTask
        {
            uint64 totalSize;
            const Path* paths;
            AtomicUint64* hashTime;
        };
        IOTask task { .paths = paths, .hashTime = &hashTime };

        JobsHandle handle = Jobs::Dispatch(JobsType::LongTask, [](uint32 groupIndex, void* userData) {
            IOTask* task = (IOTask*)userData;
            File f;
            bool r = f.Open(task->paths[groupIndex].CStr(), FileOpenFlags::Read|FileOpenFlags::SeqScan);
            ASSERT(r);
            MemTempAllocator tmpAlloc;
            uint8* buffer = tmpAlloc.MallocTyped<uint8>((uint32)f.GetSize());
            uint32 bytesRead = f.Read<uint8>(buffer, (uint32)f.GetSize());
            ASSERT(bytesRead == f.GetSize());

            ProcessAndShowInfo(task->paths[groupIndex].CStr(), buffer, bytesRead, task->hashTime);
            f.Close();
            Atomic::FetchAdd(&task->totalSize, bytesRead);
        }, &task, numFiles);
        Jobs::WaitForCompletion(handle);
        totalSize = task.totalSize;
    }

    static void BruteforceAsyncMethod(const Path* paths, int numFiles, size_t& totalSize, AtomicUint64& hashTime)
    {
        MemBumpAllocatorVM myalloc;
        myalloc.Initialize(SIZE_GB*8, SIZE_GB);

        MemTempAllocator tmpAlloc;
        AsyncFile** files = tmpAlloc.MallocTyped<AsyncFile*>(numFiles);

        for (int i = 0; i < numFiles; i++) {
            files[i] = Async::ReadFile(paths[i].CStr(), { .alloc = &myalloc });
            ASSERT(files[i]);
        }

        while (true) {
            bool alldone = true;
            for (int i = 0; i < numFiles; i++) {
                bool error = false;
                if (files[i] && !Async::IsFinished(files[i], &error)) {
                    ASSERT(!error);
                    alldone = false;
                    break;
                }
                else if (files[i]) {
                    totalSize += files[i]->size;
                    ProcessAndShowInfo(paths[i].CStr(), files[i]->data, files[i]->size, &hashTime);
                    Async::Close(files[i]);
                    files[i] = nullptr;
                }
            }

            if (alldone)
                break;
            OS::PauseCPU();
        }

        myalloc.Release();
    }

#if PLATFORM_WINDOWS
    struct CPIO_Request
    {
        OVERLAPPED overlapped;
        HANDLE fileHandle;
        uint64 size;
        void* data;
    };

    struct CPIO_ThreadData
    {
        size_t totalSize;
        uint32 numFilesRead;
        uint32 numFiles;
        HANDLE hCompletionPort;
    };

    static int CPIO_Thread(void* userData)
    {
        ULONG_PTR completionKey;
        OVERLAPPED* pOverlapped;
        CPIO_ThreadData* threadData = (CPIO_ThreadData*)userData;
        DWORD bytesRead;

        while (GetQueuedCompletionStatus(threadData->hCompletionPort, &bytesRead, &completionKey, &pOverlapped, INFINITE)) {
            CPIO_Request* req = reinterpret_cast<CPIO_Request*>(completionKey);
            threadData->totalSize += bytesRead;

            CloseHandle(req->fileHandle);

            ++threadData->numFilesRead;

            if (threadData->numFilesRead == threadData->numFiles)
                break;
        }

        return 0;
    }

    static void CPIO_Method(const Path* paths, int numFiles, size_t& totalSize, AtomicUint64& hashTime)
    {
        MemBumpAllocatorVM fileAlloc;
        fileAlloc.Initialize(8*SIZE_GB, SIZE_GB);

        MemTempAllocator tmpAlloc;
        FixedSizePool<CPIO_Request> reqPool(&tmpAlloc);
        #if 0
        reqPool.Reserve(numFiles);


        uint32 pageSize = (uint32)sysGetPageSize();
        HANDLE hCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (hCompletionPort == nullptr)
            return;

        CPIO_ThreadData threadData = {
            .numFiles = (uint32)numFiles,
            .hCompletionPort = hCompletionPort
        };

        Thread queueThread;
        queueThread.Start(ThreadDesc { .entryFn = CPIO_Thread, .userData = &threadData });

        for (int i = 0; i < numFiles; i++) {
            CPIO_Request* req = reqPool.New();
            req->fileHandle = CreateFileA(paths[i].CStr(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED|FILE_FLAG_NO_BUFFERING, NULL);
            if (req->fileHandle == INVALID_HANDLE_VALUE) {
                LOG_ERROR("Cannot open file: %s", paths[i].CStr());
                continue;
            }

            if (CreateIoCompletionPort(req->fileHandle, hCompletionPort, reinterpret_cast<ULONG_PTR>(req), 0) == nullptr) {
                LOG_ERROR("Error associating file '%s' with completion port", paths[i].CStr());
                continue;
            }
            
            BY_HANDLE_FILE_INFORMATION fileInfo {};
            if (!GetFileInformationByHandle(req->fileHandle, &fileInfo)) {
                LOG_ERROR("Error getting file size: %s", paths[i].CStr());
                continue;
            }

            req->size = (uint64(fileInfo.nFileSizeHigh)<<32) | uint64(fileInfo.nFileSizeLow);
            ASSERT(req->size && req->size < UINT32_MAX);
            req->data = fileAlloc.Malloc(req->size, pageSize);
            if (!ReadFile(req->fileHandle, req->data, AlignValue((uint32)req->size, pageSize), nullptr, &req->overlapped)) {
                if (GetLastError() != ERROR_IO_PENDING) {
                    LOG_ERROR("Error reading file (%u): %s ", GetLastError(), paths[i].CStr());
                    continue;
                }
            }
        }

        queueThread.Stop();
        CloseHandle(hCompletionPort);
        fileAlloc.Release();
        
        totalSize = threadData.totalSize;
        #endif
    }

    struct NoCacheRequest
    {
        OVERLAPPED overlapped;
        HANDLE fileHandle;
        void* data;
        Semaphore* semaphore;
        AtomicUint64* totalSize;
        AtomicUint32* numFiles;
    };

    static void NoCacheMethod(const Path* paths, int numFiles, size_t& totalSize, AtomicUint64& hashTime)
    {
        MemBumpAllocatorVM fileAlloc;
        fileAlloc.Initialize(8*SIZE_GB, SIZE_GB);

        MemTempAllocator tmpAlloc;
        FixedSizePool<NoCacheRequest> reqPool(&tmpAlloc);
        reqPool.Reserve(numFiles);

        uint32 pageSize = (uint32)OS::GetPageSize();
        Semaphore sem;
        sem.Initialize();

        AtomicUint64 atomicTotalSize = 0;
        AtomicUint32 atomicNumFiles = 0;

        auto CompletionCallback = [](DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped)
        {
            NoCacheRequest* req = (NoCacheRequest*)lpOverlapped;
            Atomic::FetchAdd(req->numFiles, 1);
            Atomic::FetchAdd(req->totalSize, dwNumberOfBytesTransfered);
            req->semaphore->Post();
        };

        for (int i = 0; i < numFiles; i++) {
            NoCacheRequest* req = reqPool.New();
            req->fileHandle = CreateFileA(paths[i].CStr(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED|FILE_FLAG_NO_BUFFERING, NULL);
            if (req->fileHandle == INVALID_HANDLE_VALUE) {
                LOG_ERROR("Cannot open file: %s", paths[i].CStr());
                continue;
            }
            req->semaphore = &sem;
            req->numFiles = &atomicNumFiles;
            req->totalSize = &atomicTotalSize;

            BY_HANDLE_FILE_INFORMATION fileInfo {};
            if (!GetFileInformationByHandle(req->fileHandle, &fileInfo)) {
                LOG_ERROR("Error getting file size: %s", paths[i].CStr());
                continue;
            }

            uint64 fileSize = (uint64(fileInfo.nFileSizeHigh)<<32) | uint64(fileInfo.nFileSizeLow);
            ASSERT(fileSize && fileSize < UINT32_MAX);
            req->data = fileAlloc.Malloc(fileSize, pageSize);

            BindIoCompletionCallback(req->fileHandle, CompletionCallback, 0);
            if (!ReadFile(req->fileHandle, req->data, AlignValue((uint32)fileSize, pageSize), nullptr, &req->overlapped)) {
                if (GetLastError() != ERROR_IO_PENDING) {
                    LOG_ERROR("Error reading file (%u): %s ", GetLastError(), paths[i].CStr());
                    continue;
                }
            }
        }

        while (Atomic::Load(&atomicNumFiles) < (uint32)numFiles) {
            sem.Wait();
        }
        fileAlloc.Release();

        totalSize = atomicTotalSize;
    }
#endif // PLATFORM_WINDOWS
    
    void Cleanup() override
    {
        Engine::Release();
    };
    
    void Update(fl32) override
    {
        Thread::Sleep(16);
    }
    
    void OnEvent(const AppEvent&) override
    {
    }
};

int Main(int argc, char* argv[])
{
    SettingsJunkyard::Initialize(SettingsJunkyard {
        .engine = {
            .logLevel = SettingsEngine::LogLevel::Info
        },
        .graphics = {
            .enable = false
        }
    });

    settingsInitializeFromCommandLine(argc, argv);
   
    static AppImpl impl;

    App::Run(AppDesc { 
        .callbacks = &impl, 
        .windowTitle = "Junkyard - TestIO",
    });

    settingsRelease();
    return 0;
}


