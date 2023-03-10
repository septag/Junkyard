#pragma once

//--------------------------------------------------------------------------------------------------
// Stuff that is currently in System
// - Thread: Regular threads implementation, along with helpers like sleep/yield. 'Destroy' waits for the thread to return
// - Mutex/MutexScope: Mutexes (CriticalSection), with scoped RAII helper
// - Semaphore: Semaphore, 'Post' increases the semaphore count, each 'Wait' decreases the count
//                 until it's zero and then waits on it
// - Signal: Signals/Events. 'Raise' sets the signal to 1, 'Wait' waits for the signal until it's 1 then resets it to zero
// - HighRes timer functions (timerXXX): 'timerInitialize' must be called before calling any other functions
// - GeneralOS functions: like Loading DLLs, Getting symbols, PageSize, etc.
// - Path functions: Functions for manipulating paths and working with File-system
//
#include "String.h"

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

    bool IsRunning() const;
    void SetPriority(ThreadPriority prio);

private:
    uint8 data[256];
};

//--------------------------------------------------------------------------------------------------
// Mutex
struct alignas(64) Mutex
{
    void Initialize(uint32 spinCount = 100);
    void Release();

    void Enter();
    void Exit();
    bool TryEnter();

private:
    uint8 data[128];
};

#ifdef __cplusplus
    struct MutexScope
    {
        MutexScope( ) = delete;
        MutexScope(const MutexScope& mtx) = delete;
        explicit MutexScope(Mutex& mtx) : _mtx(mtx) { _mtx.Enter(); }
        ~MutexScope( ) { _mtx.Exit(); }

      private:
        Mutex& _mtx;
    };
#endif

//--------------------------------------------------------------------------------------------------
// Semaphore
struct alignas(16) Semaphore
{
    void Initialize();
    void Release();

    void Post(uint32 count = 1);
    bool Wait(uint32 msecs = UINT32_MAX);

private:
    uint8 data[128];
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
    uint8 data[128];
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
API void timerInitialize();
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
    uint64 _start;
};

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

API [[nodiscard]] DLLHandle sysLoadDLL(const char* filepath, char** pErrorMsg = nullptr);
API void sysUnloadDLL(DLLHandle dll);
API void* sysSymbolAddress(DLLHandle dll, const char* symbolName);
API size_t sysGetPageSize();
API void sysGetSysInfo(SysInfo* info);
API bool sysIsDebuggerPresent();

// Platform specific 
#if PLATFORM_WINDOWS
    API void* sysWin32RunProcess(int argc, const char* argv[]);
    API bool sysWin32IsProcessRunning(const char* execName);
    API bool sysWin32GetRegisterLocalMachineString(const char* subkey, const char* value, 
                                                   char* dst, size_t dstSize);
    API void sysWin32PrintToDebugger(const char* text);

    enum class SysWin32ConsoleColor : uint16
    {
        Blue      = 0x0001,
        Green     = 0x0002,
        Red       = 0x0004,
        Intensity = 0x0008
    };
    ENABLE_BITMASK(SysWin32ConsoleColor);

    API void sysWin32SetConsoleColor(void* handle, SysWin32ConsoleColor color);
#elif PLATFORM_ANDROID
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
    API JNIEnv* sysAndroidAcquireJniEnv();
    API void sysAndroidReleaseJniEnv();    
    API JNIEnv* sysAndroidGetJniEnv();
#endif  // PLATFORM_ANDROID

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

struct Path : String<kMaxPath>
{
    Path() = default;
    Path(const char* cstr) : String<kMaxPath>(cstr) {}

    Path& SetToCurrentDir();

    Path& ConvertToUnix();
    Path& ConvertToWin();

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

//------------------------------------------------------------------------
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
    pathGetCurrentDir(_str, sizeof(_str));
    _len = strLen(_str);
    return *this;
}

inline Path& Path::ConvertToUnix()
{
    pathToUnix(_str, _str, sizeof(_str));
    return *this;
}

inline Path& Path::ConvertToWin()
{
    pathToWin(_str, _str, sizeof(_str));
    return *this;
}

inline Path Path::GetAbsolute()
{
    Path p;
    pathAbsolute(_str, p._str, sizeof(p._str));
    p._len = strLen(p._str);
    return p;
}

inline Path Path::GetFileExtension()
{
    Path p;
    pathFileExtension(_str, p._str, sizeof(p._str));
    p._len = strLen(p._str);
    return p;
}

inline Path Path::GetFileNameAndExt()
{
    Path p;
    pathFileNameAndExt(_str, p._str, sizeof(p._str));
    p._len = strLen(p._str);
    return p;
}

inline Path Path::GetFileName()
{
    Path p;
    pathFileName(_str, p._str, sizeof(p._str));
    p._len = strLen(p._str);
    return p;
}

inline Path Path::GetDirectory()
{
    Path p;
    pathDirectory(_str, p._str, sizeof(p._str));
    p._len = strLen(p._str);
    return p;
}

inline Path Path::Join(const Path& pathA, const Path& pathB)
{
    Path p;
    pathJoin(p._str, sizeof(p._str), pathA._str, pathB._str);
    p._len = strLen(p._str);
    return p;
}

inline Path Path::JoinUnix(const Path& pathA, const Path& pathB)
{
    Path p;
    pathJoinUnixStyle(p._str, sizeof(p._str), pathA._str, pathB._str);
    p._len = strLen(p._str);
    return p;
}

inline PathInfo Path::Stat() const
{
    return pathStat(_str);
}

inline bool Path::Exists() const
{
    return pathExists(_str);
}

inline bool Path::IsFile() const
{
    return pathStat(_str).type == PathType::File;
}

inline bool Path::IsDir() const
{
    return pathStat(_str).type == PathType::Directory;
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
    _start = timerGetTicks();
}

inline void TimerStopWatch::Reset()
{
    _start = timerGetTicks();
}

inline uint64 TimerStopWatch::Elapsed() const
{
    return timerDiff(timerGetTicks(), _start);
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

