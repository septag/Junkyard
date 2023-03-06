#include "System.h"

#if PLATFORM_POSIX

#include <unistd.h>             // sysconf, gettid
#include <dlfcn.h>              // dlopen, dlclose, dlsym
#include <pthread.h>            // pthread_t and family
#include <sys/prctl.h>          // prctl
#include <limits.h> 
#include <stdlib.h>             // realpath
#include <sys/stat.h>           // stat
#include <errno.h>
#include <sys/mman.h>           // mmap/munmap/mprotect/..

#include "../External/tracy/TracyC.h"

#include "String.h"
#include "Atomic.h"
#include "Memory.h"

// "Adaptive" mutex implementation using early spinlock
struct MutexImpl 
{
    pthread_mutex_t handle;
    uint32 spinCount;
    alignas(64) atomicUint32 spinlock;
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
    atomicUint32 stopped;
    bool running;
};

static_assert(sizeof(MutexImpl) <= sizeof(Mutex), "Mutex size mismatch");
static_assert(sizeof(SemaphoreImpl) <= sizeof(Semaphore), "Sempahore size mismatch");
static_assert(sizeof(SignalImpl) <= sizeof(Signal), "Signal size mismatch");
static_assert(sizeof(ThreadImpl) <= sizeof(Thread), "Thread size mismatch");

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

    thrd->sem.Post();

    cast c;
    c.i = thrd->entryFn(thrd->userData);
    return c.ptr;
}

Thread::Thread()
{
    memset(this->data, 0x0, sizeof(Thread));
}

bool Thread::Start(const ThreadDesc& desc)
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(this->data);
    ASSERT(thrd->handle == 0 && !thrd->running);

    thrd->sem.Initialize();
    thrd->entryFn = desc.entryFn;
    thrd->userData = desc.userData;
    thrd->stackSize = Max<uint64>(static_cast<uint64>(desc.stackSize), 64*kKB);
    strCopy(thrd->name, sizeof(thrd->name), desc.name ? desc.name : "");
    thrd->stopped = 0;

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
    thrd->running = true;
    return true;
}

int Thread::Stop()
{
    union {
        void* ptr;
        int32_t i;
    } cast = {};

    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(this->data);

    if (thrd->handle) {
        ASSERT_MSG(thrd->running, "Thread is not running!");
       
        atomicStore32Explicit(&thrd->stopped, 1, AtomicMemoryOrder::Release);
        pthread_join(thrd->handle, &cast.ptr);
    }

    thrd->sem.Release();
    memset(this->data, 0x0, sizeof(Thread));
    return cast.i;
}

bool Thread::IsRunning() const
{
    const ThreadImpl* thrd = reinterpret_cast<const ThreadImpl*>(this->data);
    return thrd->running;
}

bool Thread::IsStopped()
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(this->data);
    return atomicLoad32Explicit(&thrd->stopped, AtomicMemoryOrder::Acquire) == 1;
}

void Thread::SetPriority(ThreadPriority prio)
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(this->data);

    int prioPosix = 0;
    int policy = SCHED_NORMAL;

    switch (prio) {
    case ThreadPriority::Normal:    prioPosix = 0; policy = SCHED_NORMAL; break;
    case ThreadPriority::Idle:      prioPosix = 0; policy = SCHED_IDLE; break;
    case ThreadPriority::Realtime:  prioPosix = 0; policy = SCHED_RR; break;
    case ThreadPriority::High:      prioPosix = sched_get_priority_max(SCHED_NORMAL); policy = SCHED_NORMAL; break;
    case ThreadPriority::Low:       prioPosix = sched_get_priority_min(SCHED_NORMAL); policy = SCHED_NORMAL; break;
    }

    #if PLATFORM_ANDROID
        if (prio == ThreadPriority::Realtime) {
            prioPosix = sched_get_priority_max(SCHED_NORMAL);
            policy = SCHED_NORMAL;
        }
    #endif

    sched_param sParam { .sched_priority = prioPosix };
    pid_t tId = thrd->tId;

    [[maybe_unused]] int r = sched_setscheduler(tId, policy, &sParam);
    ASSERT_ALWAYS(r != -1, "sched_setscheduler failed: %d", errno);
}

void threadSetCurrentThreadName(const char* name)
{
    prctl(PR_SET_NAME, name, 0, 0, 0);

    #ifdef TRACY_ENABLE
        TracyCSetThreadName(name);
    #endif
}

void threadGetCurrentThreadName(char* nameOut, [[maybe_unused]] uint32 nameSize)
{
    ASSERT(nameSize > 16);
    prctl(PR_GET_NAME, nameOut, 0, 0, 0);
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
    int prioPosix = 0;
    int policy = SCHED_NORMAL;

    switch (prio) {
    case ThreadPriority::Normal:    prioPosix = 0; policy = SCHED_NORMAL; break;
    case ThreadPriority::Idle:      prioPosix = 0; policy = SCHED_IDLE; break;
    case ThreadPriority::Realtime:  prioPosix = 0; policy = SCHED_RR; break;
    case ThreadPriority::High:      prioPosix = sched_get_priority_max(SCHED_NORMAL); policy = SCHED_NORMAL; break;
    case ThreadPriority::Low:       prioPosix = sched_get_priority_min(SCHED_NORMAL); policy = SCHED_NORMAL; break;
    }

    #if PLATFORM_ANDROID
        if (prio == ThreadPriority::Realtime) {
            prioPosix = sched_get_priority_max(SCHED_NORMAL);
            policy = SCHED_NORMAL;
        }
    #endif

    sched_param sParam { .sched_priority = prioPosix };

    [[maybe_unused]] int r = sched_setscheduler(0, policy, &sParam);
    ASSERT_ALWAYS(r != -1, "sched_setscheduler failed: %d", errno);
}

//--------------------------------------------------------------------------------------------------
// Mutex
void Mutex::Initialize(uint32 spinCount)
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(this->data);
    
    _m->spinCount = spinCount;
    _m->spinlock = 0;
    
    // Why do we need recursive mutex ?
    pthread_mutexattr_t* _attr = nullptr;
    #if defined(PTHREAD_MUTEX_ADAPTIVE_NP)
        pthread_mutexattr_t attr;
        [[maybe_unused]] int r = pthread_mutexattr_init(&attr);
        ASSERT(r == 0);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ADAPTIVE_NP);
        _attr = &attr;
    #endif

    [[maybe_unused]] int r2 = pthread_mutex_init(&_m->handle, _attr);
    ASSERT_ALWAYS(r2 == 0, "pthread_mutex_init failed");
}

void Mutex::Release()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(this->data);

    pthread_mutex_destroy(&_m->handle);
}

void Mutex::Enter()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(this->data);

    #ifndef PTHREAD_MUTEX_ADAPTIVE_NP
        for (uint32 i = 0, c = _m->spinCount; i < c; i++) {
            if (atomicExchange32Explicit(&_m->spinlock, 1, AtomicMemoryOrder::Acquire) == 0)
                return;
            atomicPauseCpu();
        }
    #endif
    
    pthread_mutex_lock(&_m->handle);
}

void Mutex::Exit()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(this->data);

    pthread_mutex_unlock(&_m->handle);
    #ifndef PTHREAD_MUTEX_ADAPTIVE_NP
        atomicStore32Explicit(&_m->spinlock, 0, AtomicMemoryOrder::Release);
    #endif
}

bool Mutex::TryEnter()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(this->data);

    #ifndef PTHREAD_MUTEX_ADAPTIVE_NP
        if (atomicExchange32Explicit(&_m->spinlock, 1, AtomicMemoryOrder::Acquire) == 0)
            return true;
    #endif
    return pthread_mutex_trylock(&_m->handle) == 0;
}

//------------------------------------------------------------------------
// Semaphore
void Semaphore::Initialize()
{
    SemaphoreImpl* _sem = reinterpret_cast<SemaphoreImpl*>(this->data);

    _sem->count = 0;
    [[maybe_unused]] int r = pthread_mutex_init(&_sem->mutex, NULL);
    ASSERT_ALWAYS(r == 0, "pthread_mutex_init failed");

    r = pthread_cond_init(&_sem->cond, NULL);
    ASSERT_ALWAYS(r == 0, "pthread_cond_init failed");
}

void Semaphore::Release()
{
    SemaphoreImpl* _sem = reinterpret_cast<SemaphoreImpl*>(this->data);

    pthread_cond_destroy(&_sem->cond);
    pthread_mutex_destroy(&_sem->mutex);
}

void Semaphore::Post(uint32 count)
{
    SemaphoreImpl* _sem = reinterpret_cast<SemaphoreImpl*>(this->data);

    [[maybe_unused]] int r = pthread_mutex_lock(&_sem->mutex);
    ASSERT(r == 0);
    for (int ii = 0; ii < count; ii++) {
        r = pthread_cond_signal(&_sem->cond);
        ASSERT(r == 0);
    }

    _sem->count += count;
    [[maybe_unused]] int r2 = pthread_mutex_unlock(&_sem->mutex);
    ASSERT(r2 == 0);
}

bool Semaphore::Wait(uint32 msecs)
{
    SemaphoreImpl* _sem = reinterpret_cast<SemaphoreImpl*>(this->data);

    [[maybe_unused]] int r = pthread_mutex_lock(&_sem->mutex);
    ASSERT(r == 0);

    if (msecs == -1) {
        while (r == 0 && _sem->count <= 0) r = pthread_cond_wait(&_sem->cond, &_sem->mutex);
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        timespecAdd(&ts, msecs);
        while (r == 0 && _sem->count <= 0)
            r = pthread_cond_timedwait(&_sem->cond, &_sem->mutex, &ts);
    }

    bool ok = r == 0;
    if (ok)
        --_sem->count;

    r = pthread_mutex_unlock(&_sem->mutex);
    return ok;
}

//--------------------------------------------------------------------------------------------------
// Signal
// https://github.com/mattiasgustavsson/libs/blob/master/thread.h
void Signal::Initialize()
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);

    _sig->value = 0;
    [[maybe_unused]] int r = pthread_mutex_init(&_sig->mutex, NULL);
    ASSERT_MSG(r == 0, "pthread_mutex_init failed");

    [[maybe_unused]] int r2 = pthread_cond_init(&_sig->cond, NULL);
    ASSERT_MSG(r2 == 0, "pthread_cond_init failed");
}

void Signal::Release()
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);

    pthread_cond_destroy(&_sig->cond);
    pthread_mutex_destroy(&_sig->mutex);
}

void Signal::Raise()
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);

    pthread_cond_signal(&_sig->cond);
}

void Signal::RaiseAll()
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);

    pthread_cond_broadcast(&_sig->cond);
}

bool Signal::Wait(uint32 msecs)
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);

    [[maybe_unused]] int r = pthread_mutex_lock(&_sig->mutex);
    ASSERT(r == 0);
    
    bool timedOut = false;
    while (_sig->value == 0) {
        if (msecs == -1) {
            r = pthread_cond_wait(&_sig->cond, &_sig->mutex);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            timespecAdd(&ts, msecs);
            r = pthread_cond_timedwait(&_sig->cond, &_sig->mutex, &ts);
        }

        ASSERT(r == 0 || r == ETIMEDOUT);
        if (r == ETIMEDOUT) { 
            timedOut = true;
            break;
        }
    }

    if (!timedOut)
        _sig->value = 0;

    pthread_mutex_unlock(&_sig->mutex);
    return !timedOut;
}

bool Signal::WaitOnCondition(bool(*condFn)(int value, int reference), int reference, uint32 msecs)
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);

    [[maybe_unused]] int r = pthread_mutex_lock(&_sig->mutex);
    ASSERT(r == 0);
    
    bool timedOut = false;
    while (condFn(_sig->value, reference)) {
    if (msecs == -1) {
        r = pthread_cond_wait(&_sig->cond, &_sig->mutex);
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        timespecAdd(&ts, msecs);
        r = pthread_cond_timedwait(&_sig->cond, &_sig->mutex, &ts);
        }

        ASSERT(r == 0 || r == ETIMEDOUT);
        if (r == ETIMEDOUT) { 
            timedOut = true;
            break;
        }
    }
    if (!timedOut)
        _sig->value = reference;
    pthread_mutex_unlock(&_sig->mutex);
    return !timedOut;
}

void Signal::Decrement()
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);

    [[maybe_unused]] int r = pthread_mutex_lock(&_sig->mutex);
    ASSERT(r == 0);
    --_sig->value;
    pthread_mutex_unlock(&_sig->mutex);
}

void Signal::Increment()
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);

    [[maybe_unused]] int r = pthread_mutex_lock(&_sig->mutex);
    ASSERT(r == 0);
    ++_sig->value;
    pthread_mutex_unlock(&_sig->mutex);
}

void Signal::Set(int value)
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);

    [[maybe_unused]] int r = pthread_mutex_lock(&_sig->mutex);
    ASSERT(r == 0);
    _sig->value = value;
    pthread_mutex_unlock(&_sig->mutex);
}

//--------------------------------------------------------------------------------------------------
// Timer
struct TimerState
{
    bool init;
    uint64 start;
};
static TimerState gTimer;

void timerInitialize() 
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

#endif // PLATFORM_POSIX
