#include "System.h"

#if PLATFORM_POSIX

#include <unistd.h>             // sysconf, gettid
#include <dlfcn.h>              // dlopen, dlclose, dlsym
#include <pthread.h>            // pthread_t and family
#include <sys/types.h>
#include <sys/socket.h>         // socket funcs
#if PLATFORM_ANDROID || PLATFORM_LINUX
    #include <sys/prctl.h>          // prctl
#else
    #include <sched.h>
#endif
#include <sys/stat.h>           // stat
#include <sys/mman.h>           // mmap/munmap/mprotect/..
#include <limits.h> 
#include <stdlib.h>             // realpath
#include <stdio.h>              // rename
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>              // getaddrinfo, freeaddrinfo
#include <netinet/in.h>         // sockaddr_in
#include <arpa/inet.h>          // inet_ntop

#if !PLATFORM_ANDROID
    #include <uuid/uuid.h>
#else
    #include <linux/uuid.h>
#endif

#include "TracyHelper.h"
#include "StringUtil.h"
#include "Atomic.h"
#include "Allocators.h"
#include "Log.h"

// "Adaptive" mutex implementation using early spinlock
struct alignas(CACHE_LINE_SIZE) MutexImpl
{
    alignas(CACHE_LINE_SIZE) atomicUint32 spinlock;
    pthread_mutex_t handle;
    uint32 spinCount;
};

struct SemaphoreImpl
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    uint32 count;
};

struct SignalImpl 
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int value;
};

struct ThreadImpl
{
    Semaphore sem;
    Allocator* alloc;
    ThreadEntryFunc entryFn;
    pthread_t handle;
    char name[32];
    void* userData;
    size_t stackSize;
    pid_t tId;
    atomicUint32 running;
    bool init;
};

#if !PLATFORM_ANDROID
struct UUIDImpl
{
    uuid_t uuid;
};
#else
struct UUIDImpl
{
    guid_t uuid;
};
#endif

static_assert(sizeof(MutexImpl) <= sizeof(Mutex), "Mutex size mismatch");
static_assert(sizeof(SemaphoreImpl) <= sizeof(Semaphore), "Sempahore size mismatch");
static_assert(sizeof(SignalImpl) <= sizeof(Signal), "Signal size mismatch");
static_assert(sizeof(ThreadImpl) <= sizeof(Thread), "Thread size mismatch");
static_assert(sizeof(UUIDImpl) <= sizeof(SysUUID), "UUID size mismatch");

static inline void timespecAdd(struct timespec* _ts, int32_t _msecs)
{
    auto timespecToNs = [](const struct timespec* _ts)->uint64
    {
        return _ts->tv_sec * UINT64_C(1000000000) + _ts->tv_nsec;
    };

    auto timespecFromNs = [](struct timespec* _ts, uint64 _nsecs)
    {
        _ts->tv_sec = _nsecs / UINT64_C(1000000000);
        _ts->tv_nsec = _nsecs % UINT64_C(1000000000);
    };

    uint64 ns = timespecToNs(_ts);
    timespecFromNs(_ts, ns + (uint64)(_msecs)*1000000);
}

//------------------------------------------------------------------------
// Thread
static void* threadStubFn(void* arg)
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(arg);

    union cast {
        void* ptr;
        int32 i;
    };

    thrd->tId = threadGetCurrentId();
    threadSetCurrentThreadName(thrd->name);

    ASSERT(thrd->entryFn);
    atomicStore32Explicit(&thrd->running, 1, AtomicMemoryOrder::Release);
    thrd->sem.Post();

    cast c;
    c.i = thrd->entryFn(thrd->userData);

    atomicStore32Explicit(&thrd->running, 0, AtomicMemoryOrder::Release);
    return c.ptr;
}

Thread::Thread()
{
    memset(mData, 0x0, sizeof(Thread));
}

bool Thread::Start(const ThreadDesc& desc)
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(mData);
    ASSERT(thrd->handle == 0 && !thrd->init);

    thrd->sem.Initialize();
    thrd->entryFn = desc.entryFn;
    thrd->userData = desc.userData;
    thrd->stackSize = Max<uint64>(static_cast<uint64>(desc.stackSize), 64*kKB);
    strCopy(thrd->name, sizeof(thrd->name), desc.name ? desc.name : "");

    pthread_attr_t attr;
    [[maybe_unused]] int r = pthread_attr_init(&attr);
    ASSERT_MSG(r == 0, "pthread_attr_init failed");
    r = pthread_attr_setstacksize(&attr, thrd->stackSize);
    ASSERT_MSG(r == 0, "pthread_attr_setstacksize failed");

    if ((desc.flags & ThreadCreateFlags::Detached) == ThreadCreateFlags::Detached) {
        r = pthread_attr_setdetachstate(&attr, static_cast<int>(ThreadCreateFlags::Detached));
        ASSERT_MSG(r == 0, "pthread_attr_setdetachstate failed");
    }

    r = pthread_create(&thrd->handle, &attr, threadStubFn, thrd);
    if (r != 0) {
        ASSERT_ALWAYS(r == 0, "pthread_create failed");
        thrd->sem.Release();
        return false;
    }    

    // Ensure that thread callback is running
    thrd->sem.Wait();
    thrd->init = true;
    return true;
}

int Thread::Stop()
{
    union {
        void* ptr;
        int32_t i;
    } cast = {};

    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(mData);

    if (thrd->handle) {
        ASSERT_MSG(thrd->init, "Thread is not init!");
       
        pthread_join(thrd->handle, &cast.ptr);
    }

    thrd->sem.Release();
    memset(mData, 0x0, sizeof(Thread));
    return cast.i;
}

bool Thread::IsRunning()
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(mData);
    return atomicLoad32Explicit(&thrd->running, AtomicMemoryOrder::Acquire) == 1;
}

static void threadSetPriority(pthread_t threadHandle, ThreadPriority prio)
{
    sched_param param {};

    int policy = SCHED_OTHER;
    int prioMax = sched_get_priority_max(SCHED_RR);
    int prioMin = sched_get_priority_min(SCHED_RR);
    int prioNormal = prioMin + (prioMax - prioMin) / 2;

    #if PLATFORM_APPLE
        int policyIdle = SCHED_RR;
        int prioIdle = prioMin;
        prioMin = prioMin + (prioNormal - prioMin)/2;
    #else
        int policyIdle = SCHED_IDLE;
        int prioIdle = 0;
    #endif
    
    switch (prio) {
    case ThreadPriority::Normal:    policy = SCHED_RR; param.sched_priority = prioNormal; break;
    case ThreadPriority::Idle:      policy = policyIdle; param.sched_priority = prioIdle; break;
    case ThreadPriority::Realtime:  policy = SCHED_RR; param.sched_priority = prioMax; break;
    case ThreadPriority::High:      policy = SCHED_RR; param.sched_priority = prioNormal + (prioMax - prioNormal)/2; break;
    case ThreadPriority::Low:       policy = SCHED_RR; param.sched_priority = prioMin; break;
    }

    [[maybe_unused]] int r = pthread_setschedparam(threadHandle, policy, &param);
    ASSERT_ALWAYS(r != -1, "pthread_setschedparam failed: %d", errno);
}

void Thread::SetPriority(ThreadPriority prio)
{
    threadSetPriority(reinterpret_cast<ThreadImpl*>(mData)->handle, prio);
}

void threadSetCurrentThreadName(const char* name)
{
    #if PLATFORM_APPLE
        pthread_setname_np(name);
    #else
        prctl(PR_SET_NAME, name, 0, 0, 0);
    #endif

    #ifdef TRACY_ENABLE
        TracyCSetThreadName(name);
    #endif
}

void threadGetCurrentThreadName(char* nameOut, [[maybe_unused]] uint32 nameSize)
{
    ASSERT(nameSize > 16);
    
    #if PLATFORM_APPLE
        pthread_getname_np(pthread_self(), nameOut, nameSize);
    #else
        prctl(PR_GET_NAME, nameOut, 0, 0, 0);
    #endif
    
}

void threadYield()
{
    sched_yield();
}

uint32 threadGetCurrentId()
{
    #if PLATFORM_LINUX
        return static_cast<uint32>((pid_t)syscall(SYS_gettid));
    #elif PLATFORM_ANDROID
        return static_cast<uint32>(gettid());
    #elif PLATFORM_APPLE
        return pthread_mach_thread_np(pthread_self());
    #else
        #error "Not implemented"
    #endif
}

void threadSleep(uint32 msecs)
{
    struct timespec req = { (time_t)msecs / 1000, (long)((msecs % 1000) * 1000000) };
    struct timespec rem = { 0, 0 };
    nanosleep(&req, &rem);
}

void threadSetCurrentThreadPriority(ThreadPriority prio)
{
    threadSetPriority(pthread_self(), prio);
}

//--------------------------------------------------------------------------------------------------
// Mutex
void Mutex::Initialize(uint32 spinCount)
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(mData);
    
    _m->spinCount = spinCount;
    _m->spinlock = 0;
    
    // Why do we need recursive mutex ?
    pthread_mutexattr_t attr;
    [[maybe_unused]] int r = pthread_mutexattr_init(&attr);
    ASSERT(r == 0);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    
    r = pthread_mutex_init(&_m->handle, &attr);
    ASSERT_ALWAYS(r == 0, "pthread_mutex_init failed");
}

void Mutex::Release()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(mData);

    pthread_mutex_destroy(&_m->handle);
}

void Mutex::Enter()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(mData);

    for (uint32 i = 0, c = _m->spinCount; i < c; i++) {
        if (atomicExchange32Explicit(&_m->spinlock, 1, AtomicMemoryOrder::Acquire) == 0)
            return;
        atomicPauseCpu();
    }
    
    pthread_mutex_lock(&_m->handle);
}

void Mutex::Exit()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(mData);

    pthread_mutex_unlock(&_m->handle);
    atomicStore32Explicit(&_m->spinlock, 0, AtomicMemoryOrder::Release);
}

bool Mutex::TryEnter()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(mData);

    if (atomicExchange32Explicit(&_m->spinlock, 1, AtomicMemoryOrder::Acquire) == 0)
        return true;
    return pthread_mutex_trylock(&_m->handle) == 0;
}

#if !PLATFORM_APPLE
//------------------------------------------------------------------------
// Semaphore
void Semaphore::Initialize()
{
    SemaphoreImpl* sem = reinterpret_cast<SemaphoreImpl*>(mData);

    sem->count = 0;
    [[maybe_unused]] int r = pthread_mutex_init(&sem->mutex, NULL);
    ASSERT_ALWAYS(r == 0, "pthread_mutex_init failed");

    r = pthread_cond_init(&sem->cond, NULL);
    ASSERT_ALWAYS(r == 0, "pthread_cond_init failed");
}

void Semaphore::Release()
{
    SemaphoreImpl* sem = reinterpret_cast<SemaphoreImpl*>(mData);

    pthread_cond_destroy(&sem->cond);
    pthread_mutex_destroy(&sem->mutex);
}

void Semaphore::Post(uint32 count)
{
    SemaphoreImpl* sem = reinterpret_cast<SemaphoreImpl*>(mData);

    [[maybe_unused]] int r = pthread_mutex_lock(&sem->mutex);
    ASSERT(r == 0);
    for (int ii = 0; ii < count; ii++) {
        r = pthread_cond_signal(&sem->cond);
        ASSERT(r == 0);
    }

    sem->count += count;
    [[maybe_unused]] int r2 = pthread_mutex_unlock(&sem->mutex);
    ASSERT(r2 == 0);
}

bool Semaphore::Wait(uint32 msecs)
{
    SemaphoreImpl* sem = reinterpret_cast<SemaphoreImpl*>(mData);

    [[maybe_unused]] int r = pthread_mutex_lock(&sem->mutex);
    ASSERT(r == 0);

    if (msecs == -1) {
        while (r == 0 && sem->count <= 0) r = pthread_cond_wait(&sem->cond, &sem->mutex);
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        timespecAdd(&ts, msecs);
        while (r == 0 && sem->count <= 0)
            r = pthread_cond_timedwait(&sem->cond, &sem->mutex, &ts);
    }

    bool ok = r == 0;
    if (ok)
        --sem->count;

    r = pthread_mutex_unlock(&sem->mutex);
    return ok;
}
#endif // PLATFORM_APPLE

//--------------------------------------------------------------------------------------------------
// Signal
// https://github.com/mattiasgustavsson/libs/blob/master/thread.h
void Signal::Initialize()
{
    SignalImpl* sig = reinterpret_cast<SignalImpl*>(mData);

    sig->value = 0;
    [[maybe_unused]] int r = pthread_mutex_init(&sig->mutex, NULL);
    ASSERT_MSG(r == 0, "pthread_mutex_init failed");

    [[maybe_unused]] int r2 = pthread_cond_init(&sig->cond, NULL);
    ASSERT_MSG(r2 == 0, "pthread_cond_init failed");
}

void Signal::Release()
{
    SignalImpl* sig = reinterpret_cast<SignalImpl*>(mData);

    pthread_cond_destroy(&sig->cond);
    pthread_mutex_destroy(&sig->mutex);
}

void Signal::Raise()
{
    SignalImpl* sig = reinterpret_cast<SignalImpl*>(mData);

    pthread_cond_signal(&sig->cond);
}

void Signal::RaiseAll()
{
    SignalImpl* sig = reinterpret_cast<SignalImpl*>(mData);

    pthread_cond_broadcast(&sig->cond);
}

bool Signal::Wait(uint32 msecs)
{
    SignalImpl* sig = reinterpret_cast<SignalImpl*>(mData);

    [[maybe_unused]] int r = pthread_mutex_lock(&sig->mutex);
    ASSERT(r == 0);
    
    bool timedOut = false;
    while (sig->value == 0) {
        if (msecs == -1) {
            r = pthread_cond_wait(&sig->cond, &sig->mutex);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            timespecAdd(&ts, msecs);
            r = pthread_cond_timedwait(&sig->cond, &sig->mutex, &ts);
        }

        ASSERT(r == 0 || r == ETIMEDOUT);
        if (r == ETIMEDOUT) { 
            timedOut = true;
            break;
        }
    }

    if (!timedOut)
        sig->value = 0;

    pthread_mutex_unlock(&sig->mutex);
    return !timedOut;
}

bool Signal::WaitOnCondition(bool(*condFn)(int value, int reference), int reference, uint32 msecs)
{
    SignalImpl* sig = reinterpret_cast<SignalImpl*>(mData);

    [[maybe_unused]] int r = pthread_mutex_lock(&sig->mutex);
    ASSERT(r == 0);
    
    bool timedOut = false;
    while (condFn(sig->value, reference)) {
    if (msecs == -1) {
        r = pthread_cond_wait(&sig->cond, &sig->mutex);
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        timespecAdd(&ts, msecs);
        r = pthread_cond_timedwait(&sig->cond, &sig->mutex, &ts);
        }

        ASSERT(r == 0 || r == ETIMEDOUT);
        if (r == ETIMEDOUT) { 
            timedOut = true;
            break;
        }
    }
    if (!timedOut)
        sig->value = reference;
    pthread_mutex_unlock(&sig->mutex);
    return !timedOut;
}

void Signal::Decrement()
{
    SignalImpl* sig = reinterpret_cast<SignalImpl*>(mData);

    [[maybe_unused]] int r = pthread_mutex_lock(&sig->mutex);
    ASSERT(r == 0);
    --sig->value;
    pthread_mutex_unlock(&sig->mutex);
}

void Signal::Increment()
{
    SignalImpl* sig = reinterpret_cast<SignalImpl*>(mData);

    [[maybe_unused]] int r = pthread_mutex_lock(&sig->mutex);
    ASSERT(r == 0);
    ++sig->value;
    pthread_mutex_unlock(&sig->mutex);
}

void Signal::Set(int value)
{
    SignalImpl* sig = reinterpret_cast<SignalImpl*>(mData);

    [[maybe_unused]] int r = pthread_mutex_lock(&sig->mutex);
    ASSERT(r == 0);
    sig->value = value;
    pthread_mutex_unlock(&sig->mutex);
}

#if !PLATFORM_APPLE
//--------------------------------------------------------------------------------------------------
// Timer
struct TimerState
{
    bool init;
    uint64 start;
};
static TimerState gTimer;

void _private::timerInitialize() 
{
    gTimer.init = true;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    gTimer.start = (uint64)ts.tv_sec*1000000000 + (uint64)ts.tv_nsec;
}

uint64 timerGetTicks() 
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64)ts.tv_sec*1000000000 + (uint64)ts.tv_nsec) - gTimer.start;
}
#endif

DLLHandle sysLoadDLL(const char* filepath, char** pErrorMsg)
{
    DLLHandle dll = dlopen(filepath, RTLD_LOCAL | RTLD_LAZY);
    if (dll == nullptr && pErrorMsg) {
        static char errMsg[64];
        strPrintFmt(errMsg, sizeof(errMsg), dlerror());
        *pErrorMsg = errMsg;
    }
    else {
        if (pErrorMsg) 
            *pErrorMsg = nullptr;
    }
    return dll;
}

void sysUnloadDLL(DLLHandle dll)
{
    if (dll)
        dlclose(dll);
}

void* sysSymbolAddress(DLLHandle dll, const char* symbolName)
{
    return dlsym(dll, symbolName);
}

size_t sysGetPageSize()
{
    return static_cast<size_t>(sysconf(_SC_PAGESIZE));
}

bool sysSetEnvVar(const char* name, const char* value)
{
    return value != nullptr ? setenv(name, value, 1) == 0 : unsetenv(name) == 0;
}

bool sysGetEnvVar(const char* name, char* outValue, uint32 valueSize)
{
    char* value = getenv(name);
    if (!value)
        return false;
    strCopy(outValue, valueSize, value);
    return true;
}

char* pathAbsolute(const char* path, char* dst, size_t dstSize)
{
    char absPath[kMaxPath];
    if (realpath(path, absPath) != NULL) {
        strCopy(dst, (uint32)dstSize, absPath);
    } else {
        dst[0] = '\0';
    }
    return dst;
}

PathInfo pathStat(const char* path)
{
    struct stat st;
    int result = stat(path, &st);
    if (result)
        return PathInfo {};

    PathType type = PathType::Invalid;
    if (st.st_mode & S_IFREG)
        type = PathType::File;
    else if (st.st_mode & S_IFDIR)
        type = PathType::Directory;
    #if PLATFORM_APPLE
        uint64 lastModified = st.st_mtimespec.tv_sec;
    #else
        uint64 lastModified = st.st_mtim.tv_sec;
    #endif

    return PathInfo {
        .type = type,
        .size = static_cast<uint64>(st.st_size),
        .lastModified = lastModified
    };
}

bool pathCreateDir(const char* path)
{
    return mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0;
}

bool pathMove(const char* src, const char* dest)
{
    return rename(src, dest) == 0;
}

//------------------------------------------------------------------------
// Virtual memory
struct MemVirtualStatsAtomic 
{
    atomicUint64 commitedBytes;
    atomicUint64 reservedBytes;
};

static MemVirtualStatsAtomic gVMStats;

void* memVirtualReserve(size_t size, MemVirtualFlags flags)
{
    void* ptr = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (!ptr) {
        MEMORY_FAIL();
    }

    atomicFetchAdd64(&gVMStats.reservedBytes, size);
    return ptr;
}

void* memVirtualCommit(void* ptr, size_t size)
{
    int r = mprotect(ptr, size, PROT_READ | PROT_WRITE);
    ASSERT(r == 0);
    
    size_t pageSize = sysGetPageSize();
    r = madvise(ptr, size, MADV_WILLNEED);
    if (r != 0) {
        if (errno == ENOMEM) {
            MEMORY_FAIL();
        }
        ASSERT(0);
        return nullptr;
    }

    uint8* buff = reinterpret_cast<uint8*>(ptr);
    uintptr dummyCounter = 0;
    for (size_t off = 0; off < size; off += pageSize) {
        dummyCounter += *(uintptr*)(buff + off);
    }    

    atomicFetchAdd64(&gVMStats.commitedBytes, size);
    return ptr;
}

void memVirtualDecommit(void* ptr, size_t size)
{
    [[maybe_unused]] int r = madvise(ptr, size, MADV_DONTNEED);
    ASSERT(r == 0);
    atomicFetchSub64(&gVMStats.commitedBytes, size);
}

void memVirtualRelease(void* ptr, size_t size)
{
    [[maybe_unused]] int r = munmap(ptr, size);
    ASSERT(r == 0);
    atomicFetchSub64(&gVMStats.reservedBytes, size);
}

MemVirtualStats memVirtualGetStats()
{
    return MemVirtualStats {
        .commitedBytes = gVMStats.commitedBytes,
        .reservedBytes = gVMStats.reservedBytes
    };
}

bool sysUUIDGenerate(SysUUID* uuid)
{
#if !PLATFORM_ANDROID
    UUIDImpl* u = reinterpret_cast<UUIDImpl*>(uuid);
    uuid_generate_random(u->uuid);
    return true;
#else
    UNUSED(uuid);
    ASSERT_MSG(0, "Not implemented");
    return false;
#endif
}

bool sysUUIDToString(const SysUUID& uuid, char* str, uint32 size)
{
#if !PLATFORM_ANDROID
    ASSERT(size >= 36);
    UNUSED(size);
    
    const UUIDImpl& u = reinterpret_cast<const UUIDImpl&>(uuid);
    uuid_unparse(u.uuid, str);
    return true;
#else
    UNUSED(uuid);
    UNUSED(str);
    UNUSED(size);
    ASSERT_MSG(0, "Not implemented");
    return false;
#endif
}

bool sysUUIDFromString(SysUUID* uuid, const char* str)
{
#if !PLATFORM_ANDROID
    UUIDImpl* u = reinterpret_cast<UUIDImpl*>(uuid);

    if (uuid_parse(str, u->uuid) < 0)
        return false;
    return true;
#else
    UNUSED(uuid);
    UNUSED(str);
    ASSERT_MSG(0, "Not implemented");
    return false;
#endif
}

bool SysUUID::operator==(const SysUUID& uuid) const
{
    return memcmp(&uuid, this, sizeof(UUIDImpl)) == 0;
}

//----------------------------------------------------------------------------------------------------------------------
// File
#undef _LARGEFILE64_SOURCE
#ifndef __O_LARGEFILE4
    #define __O_LARGEFILE 0
#endif

struct FilePosix
{
    int         id;
    FileOpenFlags flags;
    uint64      size;  
    uint64      lastModifiedTime;
};
static_assert(sizeof(FilePosix) <= sizeof(File));

File::File()
{
    FilePosix* f = (FilePosix*)mData;
    f->id = -1;
    f->flags = FileOpenFlags::None;
    f->size = 0;
    f->lastModifiedTime = 0;
}

bool File::Open(const char* filepath, FileOpenFlags flags)
{
    ASSERT((flags & (FileOpenFlags::Read|FileOpenFlags::Write)) != (FileOpenFlags::Read|FileOpenFlags::Write));
    ASSERT((flags & (FileOpenFlags::Read|FileOpenFlags::Write)) != FileOpenFlags::None);

    FilePosix* f = (FilePosix*)mData;

    int openFlags = __O_LARGEFILE;
    mode_t mode = 0;

    if ((flags & FileOpenFlags::Read) == FileOpenFlags::Read) {
        openFlags |= O_RDONLY;
    } else if ((flags & FileOpenFlags::Write) == FileOpenFlags::Write) {
        openFlags |= O_WRONLY;
        if ((flags & FileOpenFlags::Append) == FileOpenFlags::Append) {
            openFlags |= O_APPEND;
        } else {
            openFlags |= (O_CREAT | O_TRUNC);
            mode |= (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH); 
        }
    }

    #if (PLATFORM_LINUX || PLATFORM_ANDROID)
        if ((flags & FileOpenFlags::Temp) == FileOpenFlags::Temp) {
            openFlags |= __O_TMPFILE;
        }
    #endif

    int fileId = open(filepath, openFlags, mode);
    if (fileId == -1) 
        return false;

    #if PLATFORM_APPLE
        if ((flags & FileOpenFlags::NoCache) == FileOpenFlags::NoCache) {
            if (fcntl(fileId, F_NOCACHE) != 0) {
                return false;
            }
        }
    #endif

    struct stat _stat;
    int sr = fstat(fileId, &_stat);
    if (sr != 0) {
        ASSERT_MSG(0, "stat failed!");
        return false;
    }

    f->id = fileId;
    f->flags = flags;
    f->size = static_cast<uint64>(_stat.st_size);
    f->lastModifiedTime = static_cast<uint64>(_stat.st_mtime);
    return true;
}

void File::Close()
{
    FilePosix* f = (FilePosix*)mData;

    if (f->id != -1) {
        close(f->id);
        f->id = -1;
    }
}

size_t File::Read(void* dst, size_t size)
{
    FilePosix* f = (FilePosix*)mData;
    ASSERT(f->id != -1);
    
    if ((f->flags & FileOpenFlags::NoCache) == FileOpenFlags::NoCache) {
        static size_t pagesz = 0;
        if (pagesz == 0)
            pagesz = sysGetPageSize();
        ASSERT_ALWAYS((uintptr_t)dst % pagesz == 0, "buffers must be aligned with NoCache flag");
    }
    ssize_t r = read(f->id, dst, size);
    return r != -1 ? r : SIZE_MAX;
}

size_t File::Write(const void* src, size_t size)
{
    FilePosix* f = (FilePosix*)mData;
    ASSERT(f->id != -1);

    int64_t bytesWritten = write(f->id, src, size);
    if (bytesWritten > -1) {
        f->size += bytesWritten; 
        return bytesWritten;
    }
    else {
        return SIZE_MAX;
    }    
}

size_t File::Seek(size_t offset, FileSeekMode mode)
{
    FilePosix* f = (FilePosix*)mData;
    ASSERT(f->id != -1);

    int _whence = 0;
    switch (mode) {
    case FileSeekMode::Current:    _whence = SEEK_CUR; break;
    case FileSeekMode::Start:      _whence = SEEK_SET; break;
    case FileSeekMode::End:        _whence = SEEK_END; break;
    }

    return size_t(lseek(f->id, static_cast<off_t>(offset), _whence));
}

size_t File::GetSize() const
{
    const FilePosix* f = (const FilePosix*)mData;
    return f->size;
}

uint64 File::GetLastModified() const
{
    const FilePosix* f = (const FilePosix*)mData;
    return f->lastModifiedTime;
}

bool File::IsOpen() const
{
    FilePosix* f = (FilePosix*)mData;
    return f->id != -1;
}

//----------------------------------------------------------------------------------------------------------------------
// Socket
#define SOCKET_INVALID -1
#define SOCKET_ERROR -1

namespace _private
{
    static SocketErrorCode socketTranslatePlatformErrorCode()
    {
        switch (errno) {
        case EADDRINUSE:        return SocketErrorCode::AddressInUse;
        case ECONNREFUSED:      return SocketErrorCode::ConnectionRefused;
        case EISCONN:           return SocketErrorCode::AlreadyConnected;
        case EHOSTUNREACH: 
        case ENETUNREACH:       return SocketErrorCode::HostUnreachable;
        case EWOULDBLOCK:
        case ETIMEDOUT:         return SocketErrorCode::Timeout;
        case ECONNRESET:        return SocketErrorCode::ConnectionReset;
        case EADDRNOTAVAIL:     return SocketErrorCode::AddressNotAvailable;
        case EAFNOSUPPORT:      return SocketErrorCode::AddressUnsupported;
        case ESHUTDOWN:         return SocketErrorCode::SocketShutdown;
        case EMSGSIZE:          return SocketErrorCode::MessageTooLarge;
        case ENOTCONN:          return SocketErrorCode::NotConnected;
        default:                return SocketErrorCode::Unknown;
        }
    }

    bool socketParseUrl(const char* url, char* address, size_t addressSize, char* port, size_t portSize, const char** pResource = nullptr);
} // namespace _private

#define SOCKET_INVALID -1
#define SOCKET_ERROR -1

SocketTCP::SocketTCP() :
    mSock(SOCKET_INVALID),
    mErrCode(SocketErrorCode::None),
    mLive(0)
{
}

void SocketTCP::Close()
{
    if (mSock != SOCKET_INVALID) {
        if (mLive)
            shutdown(mSock, SHUT_RDWR);
        close(mSock);

        mSock = SOCKET_INVALID;
        mErrCode = SocketErrorCode::None;
        mLive = false;
    }
}

SocketTCP SocketTCP::CreateListener()
{
    SocketTCP sock;

    sock.mSock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock.mSock == SOCKET_INVALID) {
        sock.mErrCode = _private::socketTranslatePlatformErrorCode();
        logError("SocketTCP: Opening the socket failed");
        return sock;
    }
    return sock;    
}

bool SocketTCP::Listen(uint16 port, uint32 maxConnections)
{
    ASSERT(IsValid());

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(mSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        mErrCode = _private::socketTranslatePlatformErrorCode();
        logError("SocketTCP: failed binding the socket to port: %d", port);
        return false;
    }

    logVerbose("SocketTCP: Listening on port '%d' for incoming connections ...", port);
    int _maxConnections = maxConnections > INT32_MAX ? INT32_MAX : static_cast<int>(maxConnections);
    bool success = listen(mSock, _maxConnections) >= 0;
    
    if (!success) 
        mErrCode = _private::socketTranslatePlatformErrorCode();
    else
        mLive = true;

    return success;
}

SocketTCP SocketTCP::Accept(char* clientUrl, uint32 clientUrlSize)
{
    ASSERT(IsValid());

    SocketTCP newSock;

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    newSock.mSock = accept(mSock, (struct sockaddr*)&addr, &addrlen);
    if (mLive && newSock.mSock == SOCKET_INVALID) {
        newSock.mErrCode = _private::socketTranslatePlatformErrorCode();
        logError("SocketTCP: failed to accept the new socket");
        return newSock;
    }

    if (clientUrl && clientUrlSize) {
        char ip[256];
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        uint16 port = htons(addr.sin_port);
        
        strPrintFmt(clientUrl, clientUrlSize, "%s:%d", ip, port);
    }

    newSock.mLive = true;
    return newSock;
}

SocketTCP SocketTCP::Connect(const char* url)
{
    SocketTCP sock;

    char address[256];
    char port[16];
    if (!_private::socketParseUrl(url, address, sizeof(address), port, sizeof(port))) {
        logError("SocketTCP: failed parsing the url: %s", url);
        return sock;
    }

    struct addrinfo hints;
    memset(&hints, 0x0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* addri = nullptr;
    if (getaddrinfo(address, port, &hints, &addri) != 0) {
        logError("SocketTCP: failed to resolve url: %s", url);
        return sock;
    }

    sock.mSock = socket(addri->ai_family, addri->ai_socktype, addri->ai_protocol);
    if (sock.mSock == SOCKET_INVALID) {
        freeaddrinfo(addri);
        logError("SocketTCP: failed to create socket");
        return sock;
    }

    if (connect(sock.mSock, addri->ai_addr, (int)addri->ai_addrlen) == -1) {
        freeaddrinfo(addri);
        sock.mErrCode = _private::socketTranslatePlatformErrorCode();
        logError("SocketTCP: failed to connect to url: %s", url);
        sock.Close();
        return sock;
    }

    freeaddrinfo(addri);

    sock.mLive = true;
    return sock;
}

uint32 SocketTCP::Write(const void* src, uint32 size)
{
    ASSERT(IsValid());
    ASSERT(mLive);
    uint32 totalBytesSent = 0;

    while (size > 0) {
        int bytesSent = (int)send(mSock, reinterpret_cast<const char*>(src) + totalBytesSent, size, 0);
        if (bytesSent == 0) {
            break;
        }
        else if (bytesSent == -1) {
            mErrCode = _private::socketTranslatePlatformErrorCode();
            if (mErrCode == SocketErrorCode::SocketShutdown ||
                mErrCode == SocketErrorCode::NotConnected)
            {
                logDebug("SocketTCP: socket connection closed forcefully by the peer");
                mLive = false;
            }
            return UINT32_MAX;
        }

        totalBytesSent += static_cast<uint32>(bytesSent);
        size -= static_cast<uint32>(bytesSent);
    }

    return totalBytesSent;
}

uint32 SocketTCP::Read(void* dst, uint32 dstSize)
{
    ASSERT(IsValid());
    ASSERT(mLive);

    int bytesRecv = (int)recv(mSock, reinterpret_cast<char*>(dst), dstSize, 0);
    if (bytesRecv == -1) {
        mErrCode = _private::socketTranslatePlatformErrorCode();
        if (mErrCode == SocketErrorCode::SocketShutdown ||
            mErrCode == SocketErrorCode::NotConnected)
        {
            logDebug("SocketTCP: socket connection closed forcefully by the peer");
            mLive = false;
        }
        return UINT32_MAX;
    }

    return static_cast<uint32>(bytesRecv);
}

bool SocketTCP::IsValid() const
{
    return mSock != SOCKET_INVALID;
}

#endif // PLATFORM_POSIX
