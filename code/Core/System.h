#pragma once

//--------------------------------------------------------------------------------------------------
// Stuff that are currently in the System.h/cpp
// - Thread: Regular threads implementation, along with helpers like sleep/yield. 'Destroy' waits for the thread to return
// - Mutex/MutexScope: Mutexes (CriticalSection), with scoped RAII helper
// - Semaphore: Semaphore, synchronised queue. 'Post' increases the semaphore count, each 'Wait' decreases the count until it's zero and then waits on it
// - Signal: Signals/Events. 'Raise' sets the signal to 1, 'Wait' waits for the signal until it's 1 then resets it to zero
// - HighRes timer functions (timerXXX): 'timerInitialize' must be called before calling any other functions
// - GeneralOS functions: like Loading DLLs, Getting symbols, PageSize, etc.
// - Path functions: Functions for manipulating paths and working with File-system
// - Virtual memory functions
// - File: Local disk file wrapper
// - SocketTCP: server/client TCP socket
//
#include "StringUtil.h"

//    ████████╗██╗  ██╗██████╗ ███████╗ █████╗ ██████╗ 
//    ╚══██╔══╝██║  ██║██╔══██╗██╔════╝██╔══██╗██╔══██╗
//       ██║   ███████║██████╔╝█████╗  ███████║██║  ██║
//       ██║   ██╔══██║██╔══██╗██╔══╝  ██╔══██║██║  ██║
//       ██║   ██║  ██║██║  ██║███████╗██║  ██║██████╔╝
//       ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝╚═════╝ 
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
    size_t stackSize = SIZE_MB;
    ThreadCreateFlags flags = ThreadCreateFlags::None;
    ThreadPriority priority = ThreadPriority::Normal;
};

struct alignas(64) Thread
{
    Thread();

    bool Start(const ThreadDesc& desc);
    int  Stop();
    bool IsRunning();
    void SetPriority(ThreadPriority prio);

    static void SwitchContext();
    static uint32 GetCurrentId();
    static void SetCurrentThreadPriority(ThreadPriority prio);
    static void SetCurrentThreadName(const char* name);
    static void GetCurrentThreadName(char* nameOut, uint32 nameSize);
    static void Sleep(uint32 msecs);

private:
    uint8 mData[256];
};


//    ███╗   ███╗██╗   ██╗████████╗███████╗██╗  ██╗
//    ████╗ ████║██║   ██║╚══██╔══╝██╔════╝╚██╗██╔╝
//    ██╔████╔██║██║   ██║   ██║   █████╗   ╚███╔╝ 
//    ██║╚██╔╝██║██║   ██║   ██║   ██╔══╝   ██╔██╗ 
//    ██║ ╚═╝ ██║╚██████╔╝   ██║   ███████╗██╔╝ ██╗
//    ╚═╝     ╚═╝ ╚═════╝    ╚═╝   ╚══════╝╚═╝  ╚═╝
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


//    ██████╗ ███████╗ █████╗ ██████╗ ██╗    ██╗██████╗ ██╗████████╗███████╗    ███╗   ███╗██╗   ██╗████████╗███████╗██╗  ██╗
//    ██╔══██╗██╔════╝██╔══██╗██╔══██╗██║    ██║██╔══██╗██║╚══██╔══╝██╔════╝    ████╗ ████║██║   ██║╚══██╔══╝██╔════╝╚██╗██╔╝
//    ██████╔╝█████╗  ███████║██║  ██║██║ █╗ ██║██████╔╝██║   ██║   █████╗      ██╔████╔██║██║   ██║   ██║   █████╗   ╚███╔╝ 
//    ██╔══██╗██╔══╝  ██╔══██║██║  ██║██║███╗██║██╔══██╗██║   ██║   ██╔══╝      ██║╚██╔╝██║██║   ██║   ██║   ██╔══╝   ██╔██╗ 
//    ██║  ██║███████╗██║  ██║██████╔╝╚███╔███╔╝██║  ██║██║   ██║   ███████╗    ██║ ╚═╝ ██║╚██████╔╝   ██║   ███████╗██╔╝ ██╗
//    ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝╚═════╝  ╚══╝╚══╝ ╚═╝  ╚═╝╚═╝   ╚═╝   ╚══════╝    ╚═╝     ╚═╝ ╚═════╝    ╚═╝   ╚══════╝╚═╝  ╚═╝
struct alignas(CACHE_LINE_SIZE) ReadWriteMutex
{
    void Initialize();
    void Release();

    bool TryRead();
    bool TryWrite();

    void EnterRead();
    void ExitRead();

    void EnterWrite();
    void ExitWrite();

private:
    #if !PLATFORM_APPLE
    uint8 mData[64];
    #else
    uint8 mData[256];
    #endif
};

struct ReadWriteMutexReadScope
{
    ReadWriteMutexReadScope() = delete;
    ReadWriteMutexReadScope(const ReadWriteMutexReadScope&) = delete;
    explicit ReadWriteMutexReadScope(ReadWriteMutex& mtx) : mMtx(mtx) { mMtx.EnterRead(); }
    ~ReadWriteMutexReadScope() { mMtx.ExitRead(); }

private:
    ReadWriteMutex& mMtx;
};

struct ReadWriteMutexWriteScope
{
    ReadWriteMutexWriteScope() = delete;
    ReadWriteMutexWriteScope(const ReadWriteMutexWriteScope&) = delete;
    explicit ReadWriteMutexWriteScope(ReadWriteMutex& mtx) : mMtx(mtx) { mMtx.EnterWrite(); }
    ~ReadWriteMutexWriteScope() { mMtx.ExitWrite(); }

private:
    ReadWriteMutex& mMtx;
};


//    ███████╗██████╗ ██╗███╗   ██╗██╗      ██████╗  ██████╗██╗  ██╗    ███╗   ███╗██╗   ██╗████████╗███████╗██╗  ██╗
//    ██╔════╝██╔══██╗██║████╗  ██║██║     ██╔═══██╗██╔════╝██║ ██╔╝    ████╗ ████║██║   ██║╚══██╔══╝██╔════╝╚██╗██╔╝
//    ███████╗██████╔╝██║██╔██╗ ██║██║     ██║   ██║██║     █████╔╝     ██╔████╔██║██║   ██║   ██║   █████╗   ╚███╔╝ 
//    ╚════██║██╔═══╝ ██║██║╚██╗██║██║     ██║   ██║██║     ██╔═██╗     ██║╚██╔╝██║██║   ██║   ██║   ██╔══╝   ██╔██╗ 
//    ███████║██║     ██║██║ ╚████║███████╗╚██████╔╝╚██████╗██║  ██╗    ██║ ╚═╝ ██║╚██████╔╝   ██║   ███████╗██╔╝ ██╗
//    ╚══════╝╚═╝     ╚═╝╚═╝  ╚═══╝╚══════╝ ╚═════╝  ╚═════╝╚═╝  ╚═╝    ╚═╝     ╚═╝ ╚═════╝    ╚═╝   ╚══════╝╚═╝  ╚═╝
struct alignas(CACHE_LINE_SIZE) SpinLockMutex
{
    void Enter();
    void Exit();
    bool TryEnter();

private:
    uint32 mLocked = 0;
    uint8 _padding[CACHE_LINE_SIZE - sizeof(uint32)];
};

struct SpinLockMutexScope
{
    SpinLockMutexScope() = delete;
    SpinLockMutexScope(const SpinLockMutexScope&) = delete;
    inline explicit SpinLockMutexScope(SpinLockMutex& lock) : mLock(lock) { mLock.Enter(); }
    inline ~SpinLockMutexScope() { mLock.Exit(); }
        
private:
    SpinLockMutex& mLock;
};


//    ███████╗███████╗███╗   ███╗ █████╗ ██████╗ ██╗  ██╗ ██████╗ ██████╗ ███████╗
//    ██╔════╝██╔════╝████╗ ████║██╔══██╗██╔══██╗██║  ██║██╔═══██╗██╔══██╗██╔════╝
//    ███████╗█████╗  ██╔████╔██║███████║██████╔╝███████║██║   ██║██████╔╝█████╗  
//    ╚════██║██╔══╝  ██║╚██╔╝██║██╔══██║██╔═══╝ ██╔══██║██║   ██║██╔══██╗██╔══╝  
//    ███████║███████╗██║ ╚═╝ ██║██║  ██║██║     ██║  ██║╚██████╔╝██║  ██║███████╗
//    ╚══════╝╚══════╝╚═╝     ╚═╝╚═╝  ╚═╝╚═╝     ╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═╝╚══════╝
struct alignas(16) Semaphore
{
    void Initialize();
    void Release();

    void Post(uint32 count = 1);
    bool Wait(uint32 msecs = UINT32_MAX);

private:
    uint8 mData[128];
};


//    ███████╗██╗ ██████╗ ███╗   ██╗ █████╗ ██╗     
//    ██╔════╝██║██╔════╝ ████╗  ██║██╔══██╗██║     
//    ███████╗██║██║  ███╗██╔██╗ ██║███████║██║     
//    ╚════██║██║██║   ██║██║╚██╗██║██╔══██║██║     
//    ███████║██║╚██████╔╝██║ ╚████║██║  ██║███████╗
//    ╚══════╝╚═╝ ╚═════╝ ╚═╝  ╚═══╝╚═╝  ╚═╝╚══════╝
struct alignas(16) Signal
{
    void Initialize();
    void Release();

    void Raise();
    void RaiseAll();
    bool Wait(uint32 msecs = UINT32_MAX);
    void Decrement(uint32 count = 1);
    void Increment(uint32 count = 1);
    bool WaitOnCondition(bool(*condFn)(int value, int reference), int reference = 0, uint32 msecs = UINT32_MAX);
    void Set(int value = 1);

private:
    uint8 mData[128];
};


//    ████████╗██╗███╗   ███╗███████╗██████╗ 
//    ╚══██╔══╝██║████╗ ████║██╔════╝██╔══██╗
//       ██║   ██║██╔████╔██║█████╗  ██████╔╝
//       ██║   ██║██║╚██╔╝██║██╔══╝  ██╔══██╗
//       ██║   ██║██║ ╚═╝ ██║███████╗██║  ██║
//       ╚═╝   ╚═╝╚═╝     ╚═╝╚══════╝╚═╝  ╚═╝

namespace Timer
{
    API void Initialize();
    API uint64 GetTicks();
    API uint64 LapTime(uint64* lastTime);
    INLINE uint64 Diff(uint64 newTick, uint64 oldTick);
    INLINE double ToSec(uint64 tick);
    INLINE double ToMS(uint64 tick);
    INLINE double ToUS(uint64 tick);
}

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


//    ███╗   ███╗███████╗███╗   ███╗ ██████╗ ██████╗ ██╗   ██╗
//    ████╗ ████║██╔════╝████╗ ████║██╔═══██╗██╔══██╗╚██╗ ██╔╝
//    ██╔████╔██║█████╗  ██╔████╔██║██║   ██║██████╔╝ ╚████╔╝ 
//    ██║╚██╔╝██║██╔══╝  ██║╚██╔╝██║██║   ██║██╔══██╗  ╚██╔╝  
//    ██║ ╚═╝ ██║███████╗██║ ╚═╝ ██║╚██████╔╝██║  ██║   ██║   
//    ╚═╝     ╚═╝╚══════╝╚═╝     ╚═╝ ╚═════╝ ╚═╝  ╚═╝   ╚═╝   
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

namespace Mem
{
    API void* VirtualReserve(size_t size, MemVirtualFlags flags = MemVirtualFlags::None);
    API void* VirtualCommit(void* ptr, size_t size);
    API void VirtualDecommit(void* ptr, size_t size);
    API void VirtualRelease(void* ptr, size_t size);
    API MemVirtualStats VirtualGetStats();
    API bool VirtualEnableLargePages(size_t* largePageSize);
}

//    ██████╗  █████╗ ████████╗██╗  ██╗
//    ██╔══██╗██╔══██╗╚══██╔══╝██║  ██║
//    ██████╔╝███████║   ██║   ███████║
//    ██╔═══╝ ██╔══██║   ██║   ██╔══██║
//    ██║     ██║  ██║   ██║   ██║  ██║
//    ╚═╝     ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝
enum class PathType : uint32
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

namespace PathUtils
{
    API char* ToUnix(const char* path, char* dst, size_t dstSize);
    API char* ToWin(const char* path, char* dst, size_t dstSize);
    API char* GetFileExtension(const char* path, char* dst, size_t dstSize);
    API char* GetFilenameAndExtension(const char* path, char* dst, size_t dstSize);
    API char* GetFilename(const char* path, char* dst, size_t dstSize);
    API char* GetDirectory(const char* path, char* dst, size_t dstSize);
    API char* Join(char* dst, size_t dstSize, const char* pathA, const char* pathB);
    API char* JoinUnixStyle(char* dst, size_t dstSize, const char* pathA, const char* pathB);
}

struct Path : String<PATH_CHARS_MAX>
{
    Path() = default;
    Path(const char* cstr) : String<PATH_CHARS_MAX>(cstr) {}
    Path(const String<PATH_CHARS_MAX>& str) : String<PATH_CHARS_MAX>(str) {}

    Path& SetToCurrentDir();

    Path& ConvertToUnix();
    Path& ConvertToWin();
    Path& ConvertToAbsolute();

    Path GetAbsolute() const;
    Path GetFileExtension() const;
    Path GetFileNameAndExt() const;
    Path GetFileName() const;
    Path GetDirectory() const;

    inline PathInfo Stat() const;
    inline bool Exists() const;
    inline bool IsFile() const;
    inline bool IsDir() const;

    Path& Join(const Path& path);
    static Path Join(const Path& pathA, const Path& pathB);
    static Path JoinUnix(const Path& pathA, const Path& pathB);
};

//    ███████╗██╗██╗     ███████╗
//    ██╔════╝██║██║     ██╔════╝
//    █████╗  ██║██║     █████╗  
//    ██╔══╝  ██║██║     ██╔══╝  
//    ██║     ██║███████╗███████╗
//    ╚═╝     ╚═╝╚══════╝╚══════╝
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
    void* data;
    void* userData;
    uint64 lastModifiedTime;
    uint32 size;
    Path filepath;
};

// Callback to receive IO Read/Write completion
// After this callback is triggered with failed == false, then you can assume that 'data' member contains valid file data
// Note: [Windows] This function is triggered by kernel's IO thead-pool: https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-bindiocompletioncallback
//                 So the caller is one of kernel's thread and is not owned by application. Take common threading measures when working with userData shared across threads
using AsyncFileCallback = void(*)(AsyncFile* file, bool failed);

struct AsyncFileRequest
{
    MemAllocator* alloc = Mem::GetDefaultAlloc();// Allocator to allocate a continous chunk of file data 
    AsyncFileCallback readFn = nullptr;  // Callback to receive async results. see 'AsyncFileCallback'. If this value is null, then you should use Wait and IsFinished to poll for data
    void* userData = nullptr;            // user-data. Can be allocated by async functions internally as well. See 'userDataAllocatedSize'
    uint32 userDataAllocateSize = 0;     // allocate user-data for this request and copy over the provdided userData instead of using userData pointer directly
    uint32 sizeHint = 0;                 // If you provide a size hint, then file info will not be queried and AsyncRead will allocate this size instead
};

namespace Async
{
    API bool Initialize();
    API void Release();

    API AsyncFile* ReadFile(const char* filepath, const AsyncFileRequest& request = AsyncFileRequest());
    API void Close(AsyncFile* file);
    API bool Wait(AsyncFile* file);
    API bool IsFinished(AsyncFile* file, bool* outError = nullptr);
};

//    ███████╗ ██████╗  ██████╗██╗  ██╗███████╗████████╗
//    ██╔════╝██╔═══██╗██╔════╝██║ ██╔╝██╔════╝╚══██╔══╝
//    ███████╗██║   ██║██║     █████╔╝ █████╗     ██║   
//    ╚════██║██║   ██║██║     ██╔═██╗ ██╔══╝     ██║   
//    ███████║╚██████╔╝╚██████╗██║  ██╗███████╗   ██║   
//    ╚══════╝ ╚═════╝  ╚═════╝╚═╝  ╚═╝╚══════╝   ╚═╝   
struct SocketErrorCode 
{
    enum Enum
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

    static const char* ToStr(Enum code)
    {
        switch (code) {
        case AddressInUse:         return "AddressInUse";
        case AddressNotAvailable:  return "AddressNotAvailable";
        case AddressUnsupported:   return "AddressUnsupported";
        case AlreadyConnected:     return "AlreadyConnected";
        case ConnectionRefused:    return "ConnectionRefused";        
        case Timeout:              return "Timeout";
        case HostUnreachable:      return "HostUnreachable";
        case ConnectionReset:      return "ConnectionReset";
        case SocketShutdown:       return "SocketShutdown";
        case MessageTooLarge:      return "MessageTooLarge";
        case NotConnected:         return "NotConnected";
        default:                   return "Unknown";
        }
    }
};

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
    bool IsConnected() const { return mLive; }
    SocketErrorCode::Enum GetErrorCode() const { return mErrCode; }

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
    static bool ParseUrl(const char* url, char* address, size_t addressSize, char* port, size_t portSize, const char** pResource = nullptr);

private:
    SocketHandle mSock;
    SocketErrorCode::Enum mErrCode;
    uint16 mLive;
};

//    ██╗   ██╗██╗   ██╗██╗██████╗ 
//    ██║   ██║██║   ██║██║██╔══██╗
//    ██║   ██║██║   ██║██║██║  ██║
//    ██║   ██║██║   ██║██║██║  ██║
//    ╚██████╔╝╚██████╔╝██║██████╔╝
//     ╚═════╝  ╚═════╝ ╚═╝╚═════╝ 
struct UniqueID
{
    uint8 data[16];

    bool operator==(const UniqueID& uuid) const;

    static bool Generate(UniqueID* uuid);
    static bool ToString(const UniqueID& uuid, char* str, uint32 size);
    static bool FromString(UniqueID* uuid, const char* str);
};

//     ██████╗ ███████╗███╗   ██╗███████╗██████╗  █████╗ ██╗          ██████╗ ███████╗
//    ██╔════╝ ██╔════╝████╗  ██║██╔════╝██╔══██╗██╔══██╗██║         ██╔═══██╗██╔════╝
//    ██║  ███╗█████╗  ██╔██╗ ██║█████╗  ██████╔╝███████║██║         ██║   ██║███████╗
//    ██║   ██║██╔══╝  ██║╚██╗██║██╔══╝  ██╔══██╗██╔══██║██║         ██║   ██║╚════██║
//    ╚██████╔╝███████╗██║ ╚████║███████╗██║  ██║██║  ██║███████╗    ╚██████╔╝███████║
//     ╚═════╝ ╚══════╝╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝     ╚═════╝ ╚══════╝
using OSDLL = void*;

struct SysInfo
{
    enum class CpuFamily 
    {
        Unknown = 0,
        ARM,
        x86_64,
        ARM64
    };

    char         cpuName[32];
    char         cpuModel[64];
    CpuFamily    cpuFamily;
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

namespace OS
{
    API void PauseCPU();
    uint64 GetCPUClock();

    API [[nodiscard]] OSDLL LoadDLL(const char* filepath, char** pErrorMsg = nullptr);
    API void UnloadDLL(OSDLL dll);
    API void* GetSymbolAddress(OSDLL dll, const char* symbolName);
    API size_t GetPageSize();
    API void GetSysInfo(SysInfo* info);
    API bool IsDebuggerPresent();
    API void GenerateCmdLineFromArgcArgv(int argc, const char* argv[], char** outString, uint32* outStringLen, 
                                             MemAllocator* alloc = Mem::GetDefaultAlloc(), const char* prefixCmd = nullptr);

    // If 'value' is nullptr, then variable will be cleared from the environment
    API bool SetEnvVar(const char* name, const char* value);
    API bool GetEnvVar(const char* name, char* outValue, uint32 valueSize);

    API char* GetMyPath(char* dst, size_t dstSize);
    API char* GetAbsolutePath(const char* path, char* dst, size_t dstSize);
    API char* GetCurrentDir(char* dst, size_t dstSize);
    API void  SetCurrentDir(const char* path);
    API char* GetHomeDir(char* dst, size_t dstSize);
    API char* GetCacheDir(char* dst, size_t dstSize, const char* appName);
    API PathInfo GetPathInfo(const char* path);
    API bool PathExists(const char* path);
    API bool IsPathFile(const char* path);
    API bool IsPathDir(const char* path);
    API bool CreateDir(const char* path);
    API bool MovePath(const char* src, const char* dest);
    API bool MakeTempPath(char* dst, size_t dstSize, const char* namePrefix, const char* dir = nullptr);
    API bool DeleteFilePath(const char* path);
}

#if PLATFORM_PC
enum class OSProcessFlags : uint32
{
    None = 0,
    CaptureOutput = 0x1,
    InheritHandles = 0x2,
    DontCreateConsole = 0x4,
    ForceCreateConsole = 0x8
};
ENABLE_BITMASK(OSProcessFlags);

struct OSProcess
{
    OSProcess();
    ~OSProcess();

    bool Run(const char* cmdline, OSProcessFlags flags, const char* cwd = nullptr);
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

#endif // PLATFORM_PC

// Platform specific 
#if PLATFORM_WINDOWS

// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow
enum class OSWin32ShowWindow : int
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

enum class OSWin32ShellExecuteResult
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

enum class OSWin32Folder
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

namespace OS
{
    API bool Win32IsProcessRunning(const char* execName);
    API bool Win32GetRegisterLocalMachineString(const char* subkey, const char* value, char* dst, size_t dstSize);
    API void Win32PrintToDebugger(const char* text); 
    API bool Win32SetPrivilege(const char* name, bool enable = true);
    // https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecutea
    // For "operation": valid ops are "edit", "explore", "find", "open", "print", "runas"
    API OSWin32ShellExecuteResult Win32ShellExecute(const char* filepath, const char* args = nullptr, 
                                                         const char* cwd = nullptr, 
                                                         OSWin32ShowWindow showFlag = OSWin32ShowWindow::Default, 
                                                         const char* operation = nullptr,
                                                         void** pInstance = nullptr);
    API char* Win32GetFolder(OSWin32Folder folder, char* dst, size_t dstSize);
    API void Win32EnableProgramConsoleCoding();
}

#elif PLATFORM_ANDROID
struct _JNIEnv;
typedef _JNIEnv JNIEnv;

typedef struct ANativeActivity ANativeActivity; // <android/native_activity.h>

enum class OSAndroidLogType 
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

namespace OS
{
    API void AndroidPrintToLog(OSAndroidLogType logType, const char* tag, const char* text);
    API JNIEnv* AndroidAcquireJniEnv(ANativeActivity* activity);
    API void AndroidReleaseJniEnv(ANativeActivity* activity);    
    API JNIEnv* AndroidGetJniEnv();
    API Path AndroidGetCacheDirectory(ANativeActivity* activity);
}
#endif

//----------------------------------------------------------------------------------------------------------------------
// Used internally by system platform source files. Defined in System.cpp
namespace _private
{
    void CountersAddThread(size_t stackSize);
    void CountersRemoveThread(size_t stackSize);
    void CountersAddMutex();
    void CountersRemoveMutex();
    void CountersAddSignal();
    void CountersRemoveSignal();
    void CountersAddSemaphore();
    void CountersRemoveSemaphore();
}

struct SysPrimitiveStats
{
    uint32 numMutexes;
    uint32 numSignals;
    uint32 numSemaphores;
    uint32 numThreads;
    uint64 threadStackSize;
};

SysPrimitiveStats GetSystemPrimitiveStats();

//    ██╗███╗   ██╗██╗     ██╗███╗   ██╗███████╗███████╗
//    ██║████╗  ██║██║     ██║████╗  ██║██╔════╝██╔════╝
//    ██║██╔██╗ ██║██║     ██║██╔██╗ ██║█████╗  ███████╗
//    ██║██║╚██╗██║██║     ██║██║╚██╗██║██╔══╝  ╚════██║
//    ██║██║ ╚████║███████╗██║██║ ╚████║███████╗███████║
//    ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝╚═╝  ╚═══╝╚══════╝╚══════╝
inline Path& Path::SetToCurrentDir()
{
    OS::GetCurrentDir(mStr, sizeof(mStr));
    mLen = Str::Len(mStr);
    return *this;
}

inline Path& Path::ConvertToUnix()
{
    PathUtils::ToUnix(mStr, mStr, sizeof(mStr));
    return *this;
}

inline Path& Path::ConvertToWin()
{
    PathUtils::ToWin(mStr, mStr, sizeof(mStr));
    return *this;
}

inline Path& Path::ConvertToAbsolute()
{
    char abspath[PATH_CHARS_MAX];
    OS::GetAbsolutePath(mStr, abspath, sizeof(abspath));
    return *this = abspath;
}

inline Path Path::GetAbsolute() const 
{
    Path p;
    OS::GetAbsolutePath(mStr, p.mStr, sizeof(p.mStr));
    p.mLen = Str::Len(p.mStr);
    return p;
}

inline Path Path::GetFileExtension() const
{
    Path p;
    PathUtils::GetFileExtension(mStr, p.mStr, sizeof(p.mStr));
    p.mLen = Str::Len(p.mStr);
    return p;
}

inline Path Path::GetFileNameAndExt() const
{
    Path p;
    PathUtils::GetFilenameAndExtension(mStr, p.mStr, sizeof(p.mStr));
    p.mLen = Str::Len(p.mStr);
    return p;
}

inline Path Path::GetFileName() const
{
    Path p;
    PathUtils::GetFilename(mStr, p.mStr, sizeof(p.mStr));
    p.mLen = Str::Len(p.mStr);
    return p;
}

inline Path Path::GetDirectory() const
{
    Path p;
    PathUtils::GetDirectory(mStr, p.mStr, sizeof(p.mStr));
    p.mLen = Str::Len(p.mStr);
    return p;
}

inline Path& Path::Join(const Path& path)
{
    PathUtils::Join(mStr, sizeof(mStr), mStr, path.mStr);
    mLen = Str::Len(mStr);
    return *this;
}

inline Path Path::Join(const Path& pathA, const Path& pathB)
{
    Path p;
    PathUtils::Join(p.mStr, sizeof(p.mStr), pathA.mStr, pathB.mStr);
    p.mLen = Str::Len(p.mStr);
    return p;
}

inline Path Path::JoinUnix(const Path& pathA, const Path& pathB)
{
    Path p;
    PathUtils::JoinUnixStyle(p.mStr, sizeof(p.mStr), pathA.mStr, pathB.mStr);
    p.mLen = Str::Len(p.mStr);
    return p;
}

inline PathInfo Path::Stat() const
{
    return OS::GetPathInfo(mStr);
}

inline bool Path::Exists() const
{
    return OS::PathExists(mStr);
}

inline bool Path::IsFile() const
{
    return OS::GetPathInfo(mStr).type == PathType::File;
}

inline bool Path::IsDir() const
{
    return OS::GetPathInfo(mStr).type == PathType::Directory;
}

inline bool OS::PathExists(const char* path)
{
    return OS::GetPathInfo(path).type != PathType::Invalid;
}

inline bool OS::IsPathFile(const char* path)
{
    return OS::GetPathInfo(path).type == PathType::File;
}

inline bool OS::IsPathDir(const char* path)
{
    return OS::GetPathInfo(path).type == PathType::Directory;
}

INLINE uint64 Timer::Diff(uint64 newTick, uint64 oldTick)
{
    return (newTick > oldTick) ? (newTick - oldTick) : 1;
}

INLINE double Timer::ToSec(uint64 tick)
{
    return (double)tick / 1000000000.0;
}

INLINE double Timer::ToMS(uint64 tick)
{
    return (double)tick / 1000000.0;
}

INLINE double Timer::ToUS(uint64 tick)
{
    return (double)tick / 1000.0;
}

inline TimerStopWatch::TimerStopWatch()
{
    mStart = Timer::GetTicks();
}

inline void TimerStopWatch::Reset()
{
    mStart = Timer::GetTicks();
}

inline uint64 TimerStopWatch::Elapsed() const
{
    return Timer::Diff(Timer::GetTicks(), mStart);
}

inline double TimerStopWatch::ElapsedSec() const
{
    return Timer::ToSec(Elapsed());
}

inline double TimerStopWatch::ElapsedMS() const
{
    return Timer::ToMS(Elapsed());
}

inline double TimerStopWatch::ElapsedUS() const
{
    return Timer::ToUS(Elapsed());
}

template <typename _T> inline uint32 File::Read(_T* dst, uint32 count)
{
    return static_cast<uint32>(Read((void*)dst, sizeof(_T)*count)/sizeof(_T));
}

template <typename _T> inline uint32 File::Write(_T* dst, uint32 count)
{
    return static_cast<uint32>(Write((const void*)dst, sizeof(_T)*count)/sizeof(_T));
}

