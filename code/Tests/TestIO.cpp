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

        const char* rootDir = Settings::GetValue("RootDir");
        if (!rootDir[0]) {
            LOG_ERROR("-RootDir argument is not specified");
            return false;
        }

        Path scanPath = Vfs::ResolveFilepath(rootDir);
        if (!scanPath.IsDir()) {
            LOG_ERROR("RootDir (%s) is not a directory", scanPath.CStr());
            return false;
        }

        // scan the directory and gather all the TGA files
        LOG_INFO("Scanning directory: %s", scanPath.CStr());
        dirent** entries;
        MemTempAllocator tmpAlloc;
        Path* paths = nullptr;
        int numFiles = scandir(rootDir.CStr(), &entries, 
                [](const dirent* entry)->int { return strEndsWith(entry->d_name, ".tga") ? 1 : 0; },
                [](const dirent** a, const dirent** b) { return Random::Int(&gRand, -1, 1); });
        if (numFiles > 0) {
            paths = tmpAlloc.MallocZeroTyped<Path>(numFiles);
            for (int i = 0; i < numFiles; i++) {
                paths[i] = Path::JoinUnix(scanPath, entries[i]->d_name);
            }
        }
        else {
            LOG_ERROR("No files found in '%s'", rootDir.CStr());
            return false;
        }

        LOG_INFO("Found %d files", numFiles);
        LOG_INFO("Ready.");
        
        return true;
    };

    void Cleanup() override
    {
        Engine::Release();
    };
    
    void Update(fl32) override
    {
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


