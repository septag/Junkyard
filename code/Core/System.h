#pragma once

//--------------------------------------------------------------------------------------------------
// Stuff that are currently in the System.h/cpp
// - Thread: Regular threads implementation, along with helpers like sleep/yield. 'Destroy' waits for the thread to return
// - Mutex/MutexScope: Mutexes (CriticalSection), with scoped RAII helper
// - Semaphore: Semaphore, 'Post' increases the semaphore count, each 'Wait' decreases the count
//                 until it's zero and then waits on it
// - Signal: Signals/Events. 'Raise' sets the signal to 1, 'Wait' waits for the signal until it's 1 then resets it to zero
// - HighRes timer functions (timerXXX): 'timerInitialize' must be called before calling any other functions
// - GeneralOS functions: like Loading DLLs, Getting symbols, PageSize, etc.
// - Path functions: Functions for manipulating paths and working with File-system
// - Virtual memory functions
// - File: Local disk file wrapper
// - SocketTCP: server/client TCP socket
//
#include "StringUtil.h"

// fwd
struct NO_VTABLE Allocator;
Allocator* memDefaultAlloc();

#if PLATFORM_ANDROID
    struct _JNIEnv;
    typedef _JNIEnv JNIEnv;
#endif

//--------------------------------------------------------------------------------------------------
// Thread
enum class ThreadCreateFlags : uint32
{
    None = 0,
    Detached = 0x1
};
ENABLE_BITMASK(ThreadCreateFlags);

enum class ThreadPriority 
{
    Normal = 0,
    Idle,
    Realtime,
    High,
    Low
};

using ThreadEntryFunc = int(*)(void* userData);

struct ThreadDesc
{
    ThreadEntryFunc entryFn;
    void* userData;
    const char* name;
    size_t stackSize = kMB;
    ThreadCreateFlags flags = ThreadCreateFlags::None;
};

struct alignas(64) Thread
{
    Thread();

    bool Start(const ThreadDesc& desc);
    int  Stop();

    bool IsRunning();

    void SetPriority(ThreadPriority prio);

private:
    uint8 mData[256];
};

//--------------------------------------------------------------------------------------------------
// Mutex
struct alignas(CACHE_LINE_SIZE) Mutex
{
    void Initialize(uint32 spinCount = 32);
    void Release();

    void Enter();
    void Exit();
    bool TryEnter();

private:
    uint8 mData[128];
};

struct MutexScope
{
    MutexScope( ) = delete;
    MutexScope(const MutexScope& mtx) = delete;
    explicit MutexScope(Mutex& mtx) : mMtx(mtx) { mMtx.Enter(); }
    ~MutexScope( ) { mMtx.Exit(); }

private:
    Mutex& mMtx;
};

//--------------------------------------------------------------------------------------------------
// Semaphore
struct alignas(16) Semaphore
{
    void Initialize();
    void Release();

    void Post(uint32 count = 1);
    bool Wait(uint32 msecs = UINT32_MAX);

private:
    uint8 mData[128];
};

//--------------------------------------------------------------------------------------------------
// Signal
struct alignas(16) Signal
{
    void Initialize();
    void Release();

    void Raise();
    void RaiseAll();
    bool Wait(uint32 msecs = UINT32_MAX);
    void Decrement();
    void Increment();
    bool WaitOnCondition(bool(*condFn)(int value, int reference), int reference = 0, uint32 msecs = UINT32_MAX);
    void Set(int value = 1);

private:
    uint8 mData[128];
};

//------------------------------------------------------------------------
// Thread
API void    threadYield();
API uint32  threadGetCurrentId();
API void    threadSetCurrentThreadPriority(ThreadPriority prio);
API void    threadSetCurrentThreadName(const char* name);
API void    threadGetCurrentThreadName(char* nameOut, uint32 nameSize);
API void    threadSleep(uint32 msecs);

//--------------------------------------------------------------------------------------------------
// Timer
API uint64 timerGetTicks();
API uint64 timerLapTime(uint64* lastTime);
INLINE uint64 timerDiff(uint64 newTick, uint64 oldTick);
INLINE double timerToSec(uint64 tick);
INLINE double timerToMS(uint64 tick);
INLINE double timerToUS(uint64 tick);

struct TimerStopWatch
{
    TimerStopWatch();

    void Reset();

    uint64 Elapsed() const;
    double ElapsedSec() const;
    double ElapsedMS() const;
    double ElapsedUS() const;

private:
    uint64 mStart;
};

namespace _private 
{
    void timerInitialize(); // Called by TimerInitializer
}

//------------------------------------------------------------------------
// Memory
enum class MemVirtualFlags : uint32
{
    None = 0,
    Watch = 0x1
};
ENABLE_BITMASK(MemVirtualFlags);

struct MemVirtualStats
{
    uint64 commitedBytes;
    uint64 reservedBytes;
};

void* memVirtualReserve(size_t size, MemVirtualFlags flags = MemVirtualFlags::None);
void* memVirtualCommit(void* ptr, size_t size);
void memVirtualDecommit(void* ptr, size_t size);
void memVirtualRelease(void* ptr, size_t size);
MemVirtualStats memVirtualGetStats();
bool memVirtualEnableLargePages(size_t* largePageSize);

//--------------------------------------------------------------------------------------------------
// Path 
enum class PathType
{
    Invalid = 0,
    File,
    Directory
};

struct PathInfo
{
    PathType type;
    uint64 size;
    uint64 lastModified;
};

API char* pathGetMyPath(char* dst, size_t dstSize);
API char* pathAbsolute(const char* path, char* dst, size_t dstSize);
API char* pathGetCurrentDir(char* dst, size_t dstSize);
API void  pathSetCurrentDir(const char* path);
API char* pathGetHomeDir(char* dst, size_t dstSize);
API char* pathGetCacheDir(char* dst, size_t dstSize, const char* appName);
API char* pathToUnix(const char* path, char* dst, size_t dstSize);
API char* pathToWin(const char* path, char* dst, size_t dstSize);
API char* pathFileExtension(const char* path, char* dst, size_t dstSize);
API char* pathFileNameAndExt(const char* path, char* dst, size_t dstSize);
API char* pathFileName(const char* path, char* dst, size_t dstSize);
API char* pathDirectory(const char* path, char* dst, size_t dstSize);
API char* pathJoin(char* dst, size_t dstSize, const char* pathA, const char* pathB);
API char* pathJoinUnixStyle(char* dst, size_t dstSize, const char* pathA, const char* pathB);
API PathInfo pathStat(const char* path);
INLINE bool pathExists(const char* path);
INLINE bool pathIsFile(const char* path);
INLINE bool pathIsDir(const char* path);
API bool pathCreateDir(const char* path);
API bool pathMove(const char* src, const char* dest);

struct Path : String<kMaxPath>
{
    Path() = default;
    Path(const char* cstr) : String<kMaxPath>(cstr) {}
    Path(const String<kMaxPath>& str) : String<kMaxPath>(str) {}

    Path& SetToCurrentDir();

    Path& ConvertToUnix();
    Path& ConvertToWin();
    Path& ConvertToAbsolute();

    Path GetAbsolute();
    Path GetFileExtension();
    Path GetFileNameAndExt();
    Path GetFileName();
    Path GetDirectory();

    static Path Join(const Path& pathA, const Path& pathB);
    static Path JoinUnix(const Path& pathA, const Path& pathB);

    PathInfo Stat() const;
    bool Exists() const;
    bool IsFile() const;
    bool IsDir() const;
};

//--------------------------------------------------------------------------------------------------
// File
enum class FileOpenFlags : uint32
{
    None         = 0,
    Read         = 0x01, // Open for reading
    Write        = 0x02, // Open for writing
    Append       = 0x03, // Append to the end of the file (write-mode only)
    NoCache      = 0x08, // Disable IO cache, suitable for very large files, remember to align buffers to virtual memory pages
    Writethrough = 0x10, // Write-through writes meta information to disk immediately
    SeqScan      = 0x20, // Optimize cache for sequential read (not to be used with NOCACHE)
    RandomAccess = 0x40, // Optimize cache for random access read (not to be used with NOCACHE)
    Temp         = 0x80, // Indicate that the file is temperary
};
ENABLE_BITMASK(FileOpenFlags);

enum class FileSeekMode
{
    Start = 0,
    Current,
    End 
};

struct File
{
    File();

    bool Open(const char* filepath, FileOpenFlags flags);
    void Close();

    size_t Read(void* dst, size_t size);
    size_t Write(const void* src, size_t size);
    size_t Seek(size_t offset, FileSeekMode mode = FileSeekMode::Start);

    template <typename _T> uint32 Read(_T* dst, uint32 count);
    template <typename _T> uint32 Write(_T* dst, uint32 count);

    size_t GetSize() const;
    uint64 GetLastModified() const;
    bool IsOpen() const;

private:
    uint8 mData[64];
};

// Async file
// TODO: (experimental) Currently, not implemented in platforms other than windows
struct AsyncFile
{
    Path filepath;
    void* data;
    uint64 lastModifiedTime;
    void* userData;
    uint32 size;
};

// Callback to receive IO Read/Write completion
// After this callback is triggered with failed == false, then you can assume that 'data' member contains valid file data
// Note: [Windows] This function is triggered by kernel's IO thead-pool: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-bindiocompletioncallback
//                 So the caller is one of kernel's thread and is not owned by application. Take common threading measures when working with userData shared across threads
using AsyncFileCallback = void(*)(AsyncFile* file, bool failed);

struct AsyncFileRequest
{
    Allocator* alloc = memDefaultAlloc();// Allocator to allocate a continous chunk of file data 
    AsyncFileCallback readFn = nullptr;  // Callback to receive async results. see 'AsyncFileCallback'. If this value is null, then you should use Wait and IsFinished to poll for data
    void* userData = nullptr;            // user-data. Can be allocated by async functions internally as well. See 'userDataAllocatedSize'
    uint32 userDataAllocateSize = 0;     // allocate user-data for this request and copy over the provdided userData instead of using userData pointer directly
};

API AsyncFile* asyncReadFile(const char* filepath, const AsyncFileRequest& request = AsyncFileRequest());
API void asyncClose(AsyncFile* file);
API bool asyncWait(AsyncFile* file);
API bool asyncIsFinished(AsyncFile* file, bool* outError = nullptr);

//----------------------------------------------------------------------------------------------------------------------
// SocketTCP
enum class SocketErrorCode : uint16
{
    None = 0,
    AddressInUse,
    AddressNotAvailable,
    AddressUnsupported,
    AlreadyConnected,
    ConnectionRefused,
    Timeout,
    HostUnreachable,
    ConnectionReset,
    SocketShutdown,
    MessageTooLarge,
    NotConnected,
    Unknown
};
INLINE const char* socketErrorCodeGetStr(SocketErrorCode code);

#if PLATFORM_WINDOWS
using SocketHandle = uint64;
#else 
using SocketHandle = int;
#endif

struct SocketTCP
{
    SocketTCP();

    void Close();
    bool IsValid() const;
    bool IsConnected() const { return this->mLive; }
    SocketErrorCode GetErrorCode() const { return this->mErrCode; }

    // Returns number of bytes written/read
    // Returns 0 if connection is closed gracefully
    // Returns UINT32_MAX if there was an error. check socketGetError()
    uint32 Write(const void* src, uint32 size);
    uint32 Read(void* dst, uint32 dstSize);

    static SocketTCP CreateListener();
    SocketTCP Accept(char* clientUrl = nullptr, uint32 clientUrlSize = 0);
    bool Listen(uint16 port, uint32 maxConnections = UINT32_MAX);

    static SocketTCP Connect(const char* url);

private:
    SocketHandle mSock;
    SocketErrorCode mErrCode;
    uint16 mLive;
};

//--------------------------------------------------------------------------------------------------
// General OS
using DLLHandle = void*;

enum class SysCpuFamily 
{
    Unknown = 0,
    ARM,
    x86_64,
    ARM64
};

struct SysInfo
{
    char         cpuName[32];
    char         cpuModel[64];
    SysCpuFamily cpuFamily;
    size_t       pageSize;
    size_t       physicalMemorySize;
    uint32       coreCount;
    uint32       cpuCapsSSE : 1;
    uint32       cpuCapsSSE2 : 1;
    uint32       cpuCapsSSE3 : 1;
    uint32       cpuCapsSSE41 : 1;
    uint32       cpuCapsSSE42: 1;
    uint32       cpuCapsAVX : 1;
    uint32       cpuCapsAVX2 : 1;
    uint32       cpuCapsAVX512 : 1;
    uint32       cpuCapsNeon : 1;
};

struct SysUUID
{
    uint8 data[16];

    bool operator==(const SysUUID& uuid) const;
};

API [[nodiscard]] DLLHandle sysLoadDLL(const char* filepath, char** pErrorMsg = nullptr);
API void sysUnloadDLL(DLLHandle dll);
API void* sysSymbolAddress(DLLHandle dll, const char* symbolName);
API size_t sysGetPageSize();
API void sysGetSysInfo(SysInfo* info);
API bool sysIsDebuggerPresent();
API void sysGenerateCmdLineFromArgcArgv(int argc, const char* argv[], char** outString, uint32* outStringLen, 
                                        Allocator* alloc = memDefaultAlloc(), const char* prefixCmd = nullptr);

API bool sysUUIDGenerate(SysUUID* uuid);
API bool sysUUIDToString(const SysUUID& uuid, char* str, uint32 size);
API bool sysUUIDFromString(SysUUID* uuid, const char* str);

// If 'value' is nullptr, then variable will be cleared from the environment
API bool sysSetEnvVar(const char* name, const char* value);
API bool sysGetEnvVar(const char* name, char* outValue, uint32 valueSize);

#if PLATFORM_DESKTOP
enum class SysProcessFlags : uint32
{
    None = 0,
    CaptureOutput = 0x1,
    InheritHandles = 0x2,
    DontCreateConsole = 0x4,
    ForceCreateConsole = 0x8
};
ENABLE_BITMASK(SysProcessFlags);

struct SysProcess
{
    SysProcess();
    ~SysProcess();

    bool Run(const char* cmdline, SysProcessFlags flags, const char* cwd = nullptr);
    void Wait() const;
    bool IsRunning() const;
    void Abort();
    bool IsValid() const;

    int GetExitCode() const;
    uint32 ReadStdOut(void* data, uint32 size) const;
    uint32 ReadStdErr(void* data, uint32 size) const;

private:
    void* mProcess;
    void* mStdOutPipeRead;
    void* mStdErrPipeRead;
    
#if PLATFORM_POSIX
    int mExitCode;
    int mTermSignalCode;
#endif
};

#endif // PLATFORM_DESKTOP

// Platform specific 
#if PLATFORM_WINDOWS

// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow
enum class SysWin32ShowWindow : int
{
    Hide = 0,
    Normal = 1,
    Minimized = 2,
    Maximized = 3,
    NotActive = 4,
    Show = 5,
    Minimize = 6,
    MinimizedNotActive = 7,
    Restore = 9,
    Default = 10,
    ForceMinimize = 11
};

enum class SysWin32ShellExecuteResult
{
    Ok,
    OutOfMemory,
    FileNotFound,
    PathNotFound,
    BadFormat,
    AccessDenied,
    NoAssociation,
    UnknownError
};

API bool sysWin32IsProcessRunning(const char* execName);
API bool sysWin32GetRegisterLocalMachineString(const char* subkey, const char* value, char* dst, size_t dstSize);
API void sysWin32PrintToDebugger(const char* text); 
API bool sysWin32SetPrivilege(const char* name, bool enable = true);

// https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecutea
// For "operation": valid ops are "edit", "explore", "find", "open", "print", "runas"
API SysWin32ShellExecuteResult sysWin32ShellExecute(const char* filepath, const char* args = nullptr, 
                                                    const char* cwd = nullptr, 
                                                    SysWin32ShowWindow showFlag = SysWin32ShowWindow::Default, 
                                                    const char* operation = nullptr,
                                                    void** pInstance = nullptr);

enum class SysWin32Folder
{
    Documents = 0,  // %USERPROFILE%\My Documents
    Fonts,          // %windir%\Fonts
    Downloads,      // %USERPROFILE%\Downloads
    AppData,        // %USERPROFILE%\AppData\Roaming
    LocalAppData,   // %USERPROFILE%\AppData\Local
    ProgramFiles,   // %SystemDrive%\Program Files
    System,         // %windir%\system32
    Startup,        // %APPDATA%\Microsoft\Windows\Start Menu\Programs\StartUp
    Desktop,        // %USERPROFILE%\Desktop
    _Count
};
API char* pathWin32GetFolder(SysWin32Folder folder, char* dst, size_t dstSize);

#elif PLATFORM_ANDROID
typedef struct ANativeActivity ANativeActivity; // <android/native_activity.h>

enum class SysAndroidLogType 
{
    Unknown = 0,
    Default,
    Verbose,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
    Silent,
};

API void sysAndroidPrintToLog(SysAndroidLogType logType, const char* tag, const char* text);
API JNIEnv* sysAndroidAcquireJniEnv(ANativeActivity* activity);
API void sysAndroidReleaseJniEnv(ANativeActivity* activity);    
API JNIEnv* sysAndroidGetJniEnv();
API Path sysAndroidGetCacheDirectory(ANativeActivity* activity);
#endif

//----------------------------------------------------------------------------------------------------------------------
INLINE bool pathExists(const char* path)
{
    return pathStat(path).type != PathType::Invalid;
}

INLINE bool pathIsFile(const char* path)
{
    return pathStat(path).type == PathType::File;
}

INLINE bool pathIsDir(const char* path)
{
    return pathStat(path).type == PathType::Directory;
}

inline Path& Path::SetToCurrentDir()
{
    pathGetCurrentDir(mStr, sizeof(mStr));
    mLen = strLen(mStr);
    return *this;
}

inline Path& Path::ConvertToUnix()
{
    pathToUnix(mStr, mStr, sizeof(mStr));
    return *this;
}

inline Path& Path::ConvertToWin()
{
    pathToWin(mStr, mStr, sizeof(mStr));
    return *this;
}

inline Path& Path::ConvertToAbsolute()
{
    char abspath[kMaxPath];
    pathAbsolute(mStr, abspath, sizeof(abspath));
    return *this = abspath;
}

inline Path Path::GetAbsolute()
{
    Path p;
    pathAbsolute(mStr, p.mStr, sizeof(p.mStr));
    p.mLen = strLen(p.mStr);
    return p;
}

inline Path Path::GetFileExtension()
{
    Path p;
    pathFileExtension(mStr, p.mStr, sizeof(p.mStr));
    p.mLen = strLen(p.mStr);
    return p;
}

inline Path Path::GetFileNameAndExt()
{
    Path p;
    pathFileNameAndExt(mStr, p.mStr, sizeof(p.mStr));
    p.mLen = strLen(p.mStr);
    return p;
}

inline Path Path::GetFileName()
{
    Path p;
    pathFileName(mStr, p.mStr, sizeof(p.mStr));
    p.mLen = strLen(p.mStr);
    return p;
}

inline Path Path::GetDirectory()
{
    Path p;
    pathDirectory(mStr, p.mStr, sizeof(p.mStr));
    p.mLen = strLen(p.mStr);
    return p;
}

inline Path Path::Join(const Path& pathA, const Path& pathB)
{
    Path p;
    pathJoin(p.mStr, sizeof(p.mStr), pathA.mStr, pathB.mStr);
    p.mLen = strLen(p.mStr);
    return p;
}

inline Path Path::JoinUnix(const Path& pathA, const Path& pathB)
{
    Path p;
    pathJoinUnixStyle(p.mStr, sizeof(p.mStr), pathA.mStr, pathB.mStr);
    p.mLen = strLen(p.mStr);
    return p;
}

inline PathInfo Path::Stat() const
{
    return pathStat(mStr);
}

inline bool Path::Exists() const
{
    return pathExists(mStr);
}

inline bool Path::IsFile() const
{
    return pathStat(mStr).type == PathType::File;
}

inline bool Path::IsDir() const
{
    return pathStat(mStr).type == PathType::Directory;
}

INLINE uint64 timerDiff(uint64 newTick, uint64 oldTick)
{
    return (newTick > oldTick) ? (newTick - oldTick) : 1;
}

INLINE double timerToSec(uint64 tick)
{
    return (double)tick / 1000000000.0;
}

INLINE double timerToMS(uint64 tick)
{
    return (double)tick / 1000000.0;
}

INLINE double timerToUS(uint64 tick)
{
    return (double)tick / 1000.0;
}

inline TimerStopWatch::TimerStopWatch()
{
    mStart = timerGetTicks();
}

inline void TimerStopWatch::Reset()
{
    mStart = timerGetTicks();
}

inline uint64 TimerStopWatch::Elapsed() const
{
    return timerDiff(timerGetTicks(), mStart);
}

inline double TimerStopWatch::ElapsedSec() const
{
    return timerToSec(Elapsed());
}

inline double TimerStopWatch::ElapsedMS() const
{
    return timerToMS(Elapsed());
}

inline double TimerStopWatch::ElapsedUS() const
{
    return timerToUS(Elapsed());
}

template <typename _T> inline uint32 File::Read(_T* dst, uint32 count)
{
    return static_cast<uint32>(Read((void*)dst, sizeof(_T)*count)/sizeof(_T));
}

template <typename _T> inline uint32 File::Write(_T* dst, uint32 count)
{
    return static_cast<uint32>(Write((const void*)dst, sizeof(_T)*count)/sizeof(_T));
}

INLINE const char* socketErrorCodeGetStr(SocketErrorCode errCode)
{
    switch (errCode) {
    case SocketErrorCode::AddressInUse:         return "AddressInUse";
    case SocketErrorCode::AddressNotAvailable:  return "AddressNotAvailable";
    case SocketErrorCode::AddressUnsupported:   return "AddressUnsupported";
    case SocketErrorCode::AlreadyConnected:     return "AlreadyConnected";
    case SocketErrorCode::ConnectionRefused:    return "ConnectionRefused";        
    case SocketErrorCode::Timeout:              return "Timeout";
    case SocketErrorCode::HostUnreachable:      return "HostUnreachable";
    case SocketErrorCode::ConnectionReset:      return "ConnectionReset";
    case SocketErrorCode::SocketShutdown:       return "SocketShutdown";
    case SocketErrorCode::MessageTooLarge:      return "MessageTooLarge";
    case SocketErrorCode::NotConnected:         return "NotConnected";
    default:                                    return "Unknown";
    }
}
