#include "System.h"

#if PLATFORM_APPLE
#include "Allocators.h"
#include "Log.h"
#include "Arrays.h"
#include "Atomic.h"

#include <mach/mach_time.h>
#include <mach-o/dyld.h>        // _NSGetExecutablePath
#include <unistd.h>             // sysconf
#include <dlfcn.h>              // dlopen, dlclose, dlsym
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <pthread.h>            // pthread_t and family
#include <sys/sysctl.h>
#include <pthread.h>
#include <stdio.h>              // puts

struct SemaphoreImpl
{
    dispatch_semaphore_t handle;
};

struct TimerState 
{
    bool init;
    mach_timebase_info_data_t timebase;
    uint64 start;
};

static TimerState gTimer;

static_assert(sizeof(SemaphoreImpl) <= sizeof(Semaphore), "Sempahore size mismatch");

// Semaphore
void Semaphore::Initialize()
{
    SemaphoreImpl* sem = (SemaphoreImpl*)mData;
    sem->handle = dispatch_semaphore_create(0);
    ASSERT_MSG(sem->handle != NULL, "dispatch_semaphore_create failed");

    _private::CountersAddSemaphore();
}

void Semaphore::Release()
{
    SemaphoreImpl* sem = (SemaphoreImpl*)mData;
    if (sem->handle) {
        // ObjC ARC doesn't need dispatch_release (?)
        // dispatch_release(sem->handle);
        sem->handle = NULL;

        _private::CountersRemoveSemaphore();
    }
}

void Semaphore::Post(uint32 count)
{
    SemaphoreImpl* sem = (SemaphoreImpl*)mData;
    if (sem->handle) {
        for (int i = 0; i < count; i++)
            dispatch_semaphore_signal(sem->handle);
    }
}

bool Semaphore::Wait(uint32 msecs)
{
    SemaphoreImpl* sem = (SemaphoreImpl*)mData;
    if (sem->handle) {
        dispatch_time_t dt = msecs < 0 ? DISPATCH_TIME_FOREVER
                                       : dispatch_time(DISPATCH_TIME_NOW, (int64_t)msecs * 1000000ll);
        return !dispatch_semaphore_wait(sem->handle, dt);
    }
    else {
        return false;
    }
}

namespace Timer
{
    // Tip by johaness spohr
    // https://gist.github.com/jspohr/3dc4f00033d79ec5bdaf67bc46c813e3
    FORCE_INLINE int64_t _Int64MulDiv(int64_t value, int64_t numer, int64_t denom)
    {
        int64_t q = value / denom;
        int64_t r = value % denom;
        return q * numer + r * numer / denom;
    }
} // Timer

void Timer::Initialize() 
{
    gTimer.init = true;
    mach_timebase_info(&gTimer.timebase);
    gTimer.start = mach_absolute_time();
}

uint64 Timer::GetTicks() 
{
    ASSERT_MSG(gTimer.init, "Timer not initialized. call Timer::Initialize()");
    const uint64 machNow = mach_absolute_time() - gTimer.start;
    return _Int64MulDiv(machNow, gTimer.timebase.numer, gTimer.timebase.denom);
}

char* OS::GetMyPath(char* dst, size_t dstSize)
{
    uint32 size32 = (uint32)dstSize;
    _NSGetExecutablePath(dst, (uint32_t*)&size32);
    return dst;
}

char* OS::GetCurrentDir(char* dst, size_t dstSize)
{
    return getcwd(dst, dstSize);
}

void OS::SetCurrentDir(const char* path)
{
    chdir(path);
}

void OS::GetSysInfo(SysInfo* info)
{
    memset(info, 0x0, sizeof(*info));
    
    {
        int ncpu;
        size_t ncpuLen = sizeof(ncpu);
        if (sysctlbyname("hw.ncpu", &ncpu, &ncpuLen, nullptr, 0) == 0)
            info->coreCount = (uint32)ncpu;
    }
    
    {
        uint64 physMem;
        size_t physMemSize = sizeof(physMem);
        if (sysctlbyname("hw.memsize", &physMem, &physMemSize, nullptr, 0) == 0)
            info->physicalMemorySize = physMem;
    }
        
    info->pageSize = OS::GetPageSize();
    
    // TODO
}

bool OS::IsDebuggerPresent()
{
    int mib[4];
    struct kinfo_proc info;
    size_t size = sizeof(info);

    // Set the sysctl arguments to request information about the current process
    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PID;
    mib[3] = getpid();

    // Retrieve the process information
    if (sysctl(mib, 4, &info, &size, NULL, 0) == -1) {
        puts("Failed to retrieve process information.");
        return false;
    }

    // Check if the P_TRACED flag is set, indicating a debugger is attached
    return (info.kp_proc.p_flag & P_TRACED) != 0;
}

char* OS::GetHomeDir(char* dst, size_t dstSize)
{
    #if PLATFORM_OSX
        const char* homeDir = getenv("HOME");
        ASSERT(homeDir);
        Str::Copy(dst, (uint32)dstSize, homeDir);
        return dst;
    #else
        ASSERT(0, "Not implemented in iOS");
        return nullptr;
    #endif
}

char* OS::GetCacheDir(char* dst, size_t dstSize, const char* appName)
{
    #if PLATFORM_OSX
        const char* homeDir = getenv("HOME");
        ASSERT(homeDir);
        Str::Copy(dst, (uint32)dstSize, homeDir);
        Str::Concat(dst, (uint32)dstSize, "/Library/Application Support/");
        Str::Concat(dst, (uint32)dstSize, appName);
        return dst;
    #else
        ASSERT(0, "Not implemented");
        return nullptr;
    #endif
}

//----------------------------------------------------------------------------------------------------------------------
#define USE_AIO 0
#define USE_LIBDISPATCH 0
#if USE_AIO
#include <aio.h>
#include <errno.h>

struct AsyncContext
{
};

struct AsyncFileMac
{
    aiocb cb;
    AsyncFile f;
    MemAllocator* alloc;
    int fd;
    AsyncFileCallback readFn;
};

static AsyncContext gAsyncCtx;

bool Async::Initialize()
{
    return true;
}

void Async::Release()
{
}

namespace Async
{
    static void _AIOCompletionHandler(sigval val)
    {
        AsyncFileMac* file = (AsyncFileMac*)val.sival_ptr;
        ASSERT(file->readFn);
        file->readFn(&file->f, aio_error(&file->cb) != 0);
    }

    static inline AsyncFileMac* _GetInternalFilePtr(AsyncFile* f)
    {
        return (AsyncFileMac*)((uint8*)f - offsetof(AsyncFileMac, f));
    }
}

AsyncFile* Async::ReadFile(const char* filepath, const AsyncFileRequest& request)
{
    int fd = open(filepath, O_RDONLY, 0);
    if (fd == -1)
        return nullptr;
    
    uint64 fileSize = request.sizeHint;
    uint64 fileModificationTime = 0;
    
    if (!fileSize) {
        PathInfo info = OS::GetPathInfo(filepath);
        if (info.type != PathType::File) {
            close(fd);
            return nullptr;
        }
        
        fileSize = info.size;
        fileModificationTime = info.lastModified;
    }
    
    MemSingleShotMalloc<AsyncFileMac> mallocator;
    uint8* data;
    uint8* userData = nullptr;
    if (request.userDataAllocateSize)
        mallocator.AddExternalPointerField<uint8>(&userData, request.userDataAllocateSize);
    mallocator.AddExternalPointerField<uint8>(&data, fileSize);
    
    AsyncFileMac* file = mallocator.Malloc(request.alloc);
    memset(file, 0x0, sizeof(*file));
    
    file->cb.aio_fildes = fd;
    file->cb.aio_buf = data;
    file->cb.aio_nbytes = fileSize;
    file->cb.aio_offset = 0;
    file->cb.aio_sigevent.sigev_notify = SIGEV_THREAD;
    file->cb.aio_sigevent.sigev_notify_function = Async::_AIOCompletionHandler;
    file->cb.aio_sigevent.sigev_value.sival_ptr = &file;
    
    file->f.filepath = filepath;
    file->f.data = data;
    file->f.size = uint32(fileSize);
    file->f.lastModifiedTime = fileModificationTime;
    if (request.userData) {
        if (request.userDataAllocateSize) {
            memcpy(userData, request.userData, request.userDataAllocateSize);
            file->f.userData = userData;
        }
        else {
            file->f.userData = request.userData;
        }
    }

    file->fd = fd;
    file->alloc = request.alloc;
    file->readFn = request.readFn;

    if (aio_read(&file->cb) == -1) {
        LOG_ERROR("AIO failed reading file (Code: %u)", errno);
        close(fd);
        MemSingleShotMalloc<AsyncFileMac>::Free(file, request.alloc);
        return nullptr;
        if (aio_error(&file->cb) != EINPROGRESS) {
        }
    }

    return &file->f;
}

void Async::Close(AsyncFile* file)
{
    AsyncFileMac* fm = _GetInternalFilePtr(file);
    if (fm->fd)
        close(fm->fd);
    MemSingleShotMalloc<AsyncFileMac>::Free(fm, fm->alloc);
}

bool Async::Wait(AsyncFile* file)
{
    ASSERT_MSG(0, "Not implemented");
    return false;
}

bool Async::IsFinished(AsyncFile* file, bool* outError)
{
    ASSERT_MSG(0, "Not implemented");
    return false;
}

#elif USE_LIBDISPATCH
struct AsyncContext
{
    dispatch_queue_t queue;
};

struct AsyncFileMac
{
    AsyncFile f;
    MemAllocator* alloc;
    dispatch_io_t io;
    AsyncFileCallback readFn;
    AtomicUint32 done;
};

bool Async::Initialize()
{
    gAsyncCtx.queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
    if (!gAsyncCtx.queue)
        return false;
    
    return true;
}

void Async::Release()
{
}

AsyncFile* Async::ReadFile(const char* filepath, const AsyncFileRequest& request)
{
    // TODO: do a ASIO (posix) implementation as well and compare it with this
    PathInfo info = OS::GetPathInfo(filepath);
    if (info.type != PathType::File)
        return nullptr;
    
    dispatch_io_t io = dispatch_io_create_with_path(DISPATCH_IO_STREAM, filepath, O_RDONLY, 0, gAsyncCtx.queue, ^(int error) {
        ASSERT_MSG(error == 0, "Unexpected open file: %s (error: %u)", filepath, error);
    });
    
    ASSERT(io);
    ASSERT(request.readFn);

    uint64 fileSize = request.sizeHint;
    if (!fileSize)
        fileSize = info.size;
    
    ASSERT_MSG(fileSize < UINT32_MAX, "Large file sizes are not supported by win32 overlapped API");
    ASSERT_MSG(!request.userDataAllocateSize || (request.userData && request.userDataAllocateSize),
               "`userDataAllocatedSize` should be accompanied with a valid `userData` pointer");
    
    MemSingleShotMalloc<AsyncFileMac> mallocator;
    uint8* data;
    uint8* userData = nullptr;
    if (request.userDataAllocateSize)
        mallocator.AddExternalPointerField<uint8>(&userData, request.userDataAllocateSize);
    mallocator.AddExternalPointerField<uint8>(&data, fileSize);
    
    AsyncFileMac* file = mallocator.Malloc(request.alloc);
    memset(file, 0x0, sizeof(*file));
    file->f.filepath = filepath;
    file->f.data = data;
    file->f.size = uint32(fileSize);
    file->f.lastModifiedTime = info.lastModified;
    if (request.userData) {
        if (request.userDataAllocateSize) {
            memcpy(userData, request.userData, request.userDataAllocateSize);
            file->f.userData = userData;
        }
        else {
            file->f.userData = request.userData;
        }
    }

    file->io = io;
    file->alloc = request.alloc;
    file->readFn = request.readFn;
    
    dispatch_io_read(io, 0, fileSize, gAsyncCtx.queue, ^(bool done, dispatch_data_t data, int error) {
        if (done) {
            if (error == 0) {
                const void* buffer;
                size_t size;
                dispatch_data_t newData = dispatch_data_create_map(data, &buffer, &size);
                ASSERT(buffer);
                memcpy(file->f.data, buffer, size);
                dispatch_release(newData);
            }
            
            file->readFn(&file->f, error == 0);
            dispatch_release(data);
            
            Atomic::StoreExplicit(&file->done, 1, AtomicMemoryOrder::Release);
        }
    });

    return &file->f;
}

void Async::Close(AsyncFile* file)
{
    ASSERT(file);
    AsyncFileMac* f = (AsyncFileMac*)file;
    if (f->io)
        dispatch_release(f->io);
    if (f->alloc)
        Mem::Free(f, f->alloc);
}

bool Async::Wait(AsyncFile* file)
{
    ASSERT_MSG(0, "Not implemented");
    return false;
}

bool Async::IsFinished(AsyncFile* file, bool* outError)
{
    ASSERT(file);
    AsyncFileMac* f = (AsyncFileMac*)file;
    return Atomic::LoadExplicit(&f->done, AtomicMemoryOrder::Acquire);
}
#endif

#endif // PLATFORM_APPLE



