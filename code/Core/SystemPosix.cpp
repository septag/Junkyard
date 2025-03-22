#include "System.h"

#if PLATFORM_POSIX

#include <unistd.h>             // sysconf, gettid
#include <dlfcn.h>              // dlopen, dlclose, dlsym
#include <pthread.h>            // pthread_t and family
#include <sys/types.h>
#include <sys/socket.h>         // socket funcs
#if PLATFORM_ANDROID || PLATFORM_LINUX
    #include <sys/prctl.h>          // prctl
    #include <semaphore.h>
    #include <sys/syscall.h>
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
#include <poll.h>               // async file poll
#include <spawn.h>
#include <signal.h>             // kill

#if PLATFORM_LINUX
#include <sys/wait.h>
#endif

#if PLATFORM_APPLE || PLATFORM_LINUX
    #include <uuid/uuid.h>
#endif

#include "TracyHelper.h"
#include "StringUtil.h"
#include "Atomic.h"
#include "Allocators.h"
#include "Log.h"
#include "Arrays.h"

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

//    ████████╗██╗  ██╗██████╗ ███████╗ █████╗ ██████╗ 
//    ╚══██╔══╝██║  ██║██╔══██╗██╔════╝██╔══██╗██╔══██╗
//       ██║   ███████║██████╔╝█████╗  ███████║██║  ██║
//       ██║   ██╔══██║██╔══██╗██╔══╝  ██╔══██║██║  ██║
//       ██║   ██║  ██║██║  ██║███████╗██║  ██║██████╔╝
//       ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝╚═════╝ 
struct ThreadImpl
{
    Semaphore sem;
    MemAllocator* alloc;
    ThreadEntryFunc entryFn;
    pthread_t handle;
    char name[32];
    void* userData;
    size_t stackSize;
    pid_t tId;
    AtomicUint32 running;
    bool init;
};
static_assert(sizeof(ThreadImpl) <= sizeof(Thread), "Thread size mismatch");

static void* _ThreadStubFn(void* arg)
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(arg);

    union cast {
        void* ptr;
        int32 i;
    };

    thrd->tId = Thread::GetCurrentId();
    Thread::SetCurrentThreadName(thrd->name);

    ASSERT(thrd->entryFn);
    Atomic::StoreExplicit(&thrd->running, 1, AtomicMemoryOrder::Release);
    thrd->sem.Post();

    cast c;
    c.i = thrd->entryFn(thrd->userData);

    Atomic::StoreExplicit(&thrd->running, 0, AtomicMemoryOrder::Release);
    return c.ptr;
}

Thread::Thread()
{
    memset(mData, 0x0, sizeof(Thread));
}

static void _SetThreadPriority(pthread_t threadHandle, ThreadPriority prio)
{
    sched_param param {};
    int policy;
    
    pthread_getschedparam(threadHandle, &policy, &param);
    if (policy != SCHED_OTHER)
        policy = SCHED_OTHER;
    
    int prioMax = sched_get_priority_max(policy);
    int prioMin = sched_get_priority_min(policy);
    int prioNormal = prioMin + (prioMax - prioMin) / 2;

    #if PLATFORM_APPLE
        int policyIdle = policy;
        int prioIdle = prioMin;
        prioMin = prioMin + (prioNormal - prioMin)/2;
    #else
        int policyIdle = SCHED_IDLE;
        int prioIdle = 0;
    #endif
    
    switch (prio) {
    case ThreadPriority::Normal:    param.sched_priority = prioNormal; break;
    case ThreadPriority::Idle:      policy = policyIdle; param.sched_priority = prioIdle; break;
    case ThreadPriority::Realtime:  policy = SCHED_RR; param.sched_priority = prioMax; break;
    case ThreadPriority::High:      param.sched_priority = prioNormal + (prioMax - prioNormal)/2; break;
    case ThreadPriority::Low:       param.sched_priority = prioMin; break;
    }

    [[maybe_unused]] int r = pthread_setschedparam(threadHandle, policy, &param);
    ASSERT_ALWAYS(r != -1, "pthread_setschedparam failed: %d", errno);
}

bool Thread::Start(const ThreadDesc& desc)
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(mData);
    ASSERT(thrd->handle == 0 && !thrd->init);

    thrd->sem.Initialize();
    thrd->entryFn = desc.entryFn;
    thrd->userData = desc.userData;
    thrd->stackSize = Max<uint64>(static_cast<uint64>(desc.stackSize), 64*SIZE_KB);
    Str::Copy(thrd->name, sizeof(thrd->name), desc.name ? desc.name : "");

    pthread_attr_t attr;
    [[maybe_unused]] int r = pthread_attr_init(&attr);
    ASSERT_MSG(r == 0, "pthread_attr_init failed");
    r = pthread_attr_setstacksize(&attr, thrd->stackSize);
    ASSERT_MSG(r == 0, "pthread_attr_setstacksize failed");

    if ((desc.flags & ThreadCreateFlags::Detached) == ThreadCreateFlags::Detached) {
        r = pthread_attr_setdetachstate(&attr, static_cast<int>(ThreadCreateFlags::Detached));
        ASSERT_MSG(r == 0, "pthread_attr_setdetachstate failed");
    }

    r = pthread_create(&thrd->handle, &attr, _ThreadStubFn, thrd);
    if (r != 0) {
        ASSERT_ALWAYS(r == 0, "pthread_create failed");
        thrd->sem.Release();
        return false;
    }
    
    if (desc.priority != ThreadPriority::Normal)
        _SetThreadPriority(thrd->handle, desc.priority);

    // Ensure that thread callback is running
    thrd->sem.Wait();
    thrd->init = true;

    _private::CountersAddThread(thrd->stackSize);
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

    _private::CountersRemoveThread(thrd->stackSize);

    memset(mData, 0x0, sizeof(Thread));        
    return cast.i;
}

bool Thread::IsRunning()
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(mData);
    return Atomic::LoadExplicit(&thrd->running, AtomicMemoryOrder::Acquire) == 1;
}

void Thread::SetPriority(ThreadPriority prio)
{
    _SetThreadPriority(reinterpret_cast<ThreadImpl*>(mData)->handle, prio);
}

void Thread::SetCurrentThreadName(const char* name)
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

void Thread::GetCurrentThreadName(char* nameOut, [[maybe_unused]] uint32 nameSize)
{
    ASSERT(nameSize > 16);
    
    #if PLATFORM_APPLE
        pthread_getname_np(pthread_self(), nameOut, nameSize);
    #else
        prctl(PR_GET_NAME, nameOut, 0, 0, 0);
    #endif
    
}

void Thread::SwitchContext()
{
    sched_yield();
}

uint32 Thread::GetCurrentId()
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

void Thread::Sleep(uint32 msecs)
{
    struct timespec req = { (time_t)msecs / 1000, (long)((msecs % 1000) * 1000000) };
    struct timespec rem = { 0, 0 };
    nanosleep(&req, &rem);
}

void Thread::SetCurrentThreadPriority(ThreadPriority prio)
{
    _SetThreadPriority(pthread_self(), prio);
}

//    ███╗   ███╗██╗   ██╗████████╗███████╗██╗  ██╗
//    ████╗ ████║██║   ██║╚══██╔══╝██╔════╝╚██╗██╔╝
//    ██╔████╔██║██║   ██║   ██║   █████╗   ╚███╔╝ 
//    ██║╚██╔╝██║██║   ██║   ██║   ██╔══╝   ██╔██╗ 
//    ██║ ╚═╝ ██║╚██████╔╝   ██║   ███████╗██╔╝ ██╗
//    ╚═╝     ╚═╝ ╚═════╝    ╚═╝   ╚══════╝╚═╝  ╚═╝
struct MutexImpl
{
    alignas(CACHE_LINE_SIZE) AtomicUint32 spinlock;
    pthread_mutex_t handle;
    uint32 spinCount;
};
static_assert(sizeof(MutexImpl) <= sizeof(Mutex), "Mutex size mismatch");

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

    _private::CountersAddMutex();
}

void Mutex::Release()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(mData);

    pthread_mutex_destroy(&_m->handle);

    _private::CountersRemoveMutex();
}

void Mutex::Enter()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(mData);

    for (uint32 i = 0, c = _m->spinCount; i < c; i++) {
        if (Atomic::ExchangeExplicit(&_m->spinlock, 1, AtomicMemoryOrder::Acquire) == 0)
            return;
        OS::PauseCPU();
    }
    
    pthread_mutex_lock(&_m->handle);
}

void Mutex::Exit()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(mData);

    pthread_mutex_unlock(&_m->handle);
    Atomic::StoreExplicit(&_m->spinlock, 0, AtomicMemoryOrder::Release);
}

bool Mutex::TryEnter()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(mData);

    if (Atomic::ExchangeExplicit(&_m->spinlock, 1, AtomicMemoryOrder::Acquire) == 0)
        return true;
    return pthread_mutex_trylock(&_m->handle) == 0;
}

//    ██████╗ ███████╗ █████╗ ██████╗ ██╗    ██╗██████╗ ██╗████████╗███████╗    ███╗   ███╗██╗   ██╗████████╗███████╗██╗  ██╗
//    ██╔══██╗██╔════╝██╔══██╗██╔══██╗██║    ██║██╔══██╗██║╚══██╔══╝██╔════╝    ████╗ ████║██║   ██║╚══██╔══╝██╔════╝╚██╗██╔╝
//    ██████╔╝█████╗  ███████║██║  ██║██║ █╗ ██║██████╔╝██║   ██║   █████╗      ██╔████╔██║██║   ██║   ██║   █████╗   ╚███╔╝ 
//    ██╔══██╗██╔══╝  ██╔══██║██║  ██║██║███╗██║██╔══██╗██║   ██║   ██╔══╝      ██║╚██╔╝██║██║   ██║   ██║   ██╔══╝   ██╔██╗ 
//    ██║  ██║███████╗██║  ██║██████╔╝╚███╔███╔╝██║  ██║██║   ██║   ███████╗    ██║ ╚═╝ ██║╚██████╔╝   ██║   ███████╗██╔╝ ██╗
//    ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝╚═════╝  ╚══╝╚══╝ ╚═╝  ╚═╝╚═╝   ╚═╝   ╚══════╝    ╚═╝     ╚═╝ ╚═════╝    ╚═╝   ╚══════╝╚═╝  ╚═╝
struct ReadWriteMutexImpl
{
    pthread_rwlock_t handle;
};
static_assert(sizeof(ReadWriteMutexImpl) <= sizeof(ReadWriteMutex), "ReadWriteMutex mismatch");

void ReadWriteMutex::Initialize()
{
    ReadWriteMutexImpl* m = reinterpret_cast<ReadWriteMutexImpl*>(mData);
    pthread_rwlock_init(&m->handle, nullptr);
}

void ReadWriteMutex::Release()
{
    ReadWriteMutexImpl* m = reinterpret_cast<ReadWriteMutexImpl*>(mData);
    pthread_rwlock_destroy(&m->handle);
}

bool ReadWriteMutex::TryRead()
{
    ReadWriteMutexImpl* m = reinterpret_cast<ReadWriteMutexImpl*>(mData);
    return pthread_rwlock_tryrdlock(&m->handle) == 0;
}

bool ReadWriteMutex::TryWrite()
{
    ReadWriteMutexImpl* m = reinterpret_cast<ReadWriteMutexImpl*>(mData);
    return pthread_rwlock_trywrlock(&m->handle) == 0;
}

void ReadWriteMutex::EnterRead()
{
    ReadWriteMutexImpl* m = reinterpret_cast<ReadWriteMutexImpl*>(mData);
    pthread_rwlock_rdlock(&m->handle);
}

void ReadWriteMutex::ExitRead()
{
    ReadWriteMutexImpl* m = reinterpret_cast<ReadWriteMutexImpl*>(mData);
    pthread_rwlock_unlock(&m->handle);
}

void ReadWriteMutex::EnterWrite()
{
    ReadWriteMutexImpl* m = reinterpret_cast<ReadWriteMutexImpl*>(mData);
    pthread_rwlock_wrlock(&m->handle);
}

void ReadWriteMutex::ExitWrite()
{
    ReadWriteMutexImpl* m = reinterpret_cast<ReadWriteMutexImpl*>(mData);
    pthread_rwlock_unlock(&m->handle);
}

//    ███████╗███████╗███╗   ███╗ █████╗ ██████╗ ██╗  ██╗ ██████╗ ██████╗ ███████╗
//    ██╔════╝██╔════╝████╗ ████║██╔══██╗██╔══██╗██║  ██║██╔═══██╗██╔══██╗██╔════╝
//    ███████╗█████╗  ██╔████╔██║███████║██████╔╝███████║██║   ██║██████╔╝█████╗  
//    ╚════██║██╔══╝  ██║╚██╔╝██║██╔══██║██╔═══╝ ██╔══██║██║   ██║██╔══██╗██╔══╝  
//    ███████║███████╗██║ ╚═╝ ██║██║  ██║██║     ██║  ██║╚██████╔╝██║  ██║███████╗
//    ╚══════╝╚══════╝╚═╝     ╚═╝╚═╝  ╚═╝╚═╝     ╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═╝╚══════╝
#if !PLATFORM_APPLE
struct SemaphoreImpl
{
    sem_t sem;
};
static_assert(sizeof(SemaphoreImpl) <= sizeof(Semaphore), "Sempahore size mismatch");

void Semaphore::Initialize()
{
    SemaphoreImpl* sem = reinterpret_cast<SemaphoreImpl*>(mData);

    [[maybe_unused]] int r = sem_init(&sem->sem, 0, 0);
    ASSERT_MSG(r == 0, "Initialize semaphore failed");

    _private::CountersAddSemaphore();
}

void Semaphore::Release()
{
    SemaphoreImpl* sem = reinterpret_cast<SemaphoreImpl*>(mData);
    sem_destroy(&sem->sem);

    _private::CountersRemoveSemaphore();
}

void Semaphore::Post(uint32 count)
{
    SemaphoreImpl* sem = reinterpret_cast<SemaphoreImpl*>(mData);
    for (uint32 i = 0; i < count; i++) {
        [[maybe_unused]] int r = sem_post(&sem->sem);
        ASSERT(r == 0);
    }
}

bool Semaphore::Wait(uint32 msecs)
{
    SemaphoreImpl* sem = reinterpret_cast<SemaphoreImpl*>(mData);
    [[maybe_unused]] int r;
    if (msecs == UINT32_MAX) {
        r = sem_wait(&sem->sem);
    }
    else {
        struct timespec ts;
        #if defined(__ANDROID_API__) && __ANDROID_API__ >= 28
        clock_gettime(CLOCK_MONOTONIC, &ts);
        timespecAdd(&ts, msecs);
        r = sem_timedwait_monotonic_np(&sem->sem, &ts);
        #else
        clock_gettime(CLOCK_REALTIME, &ts);
        timespecAdd(&ts, msecs);
        r = sem_timedwait(&sem->sem, &ts);
        #endif
    }

    ASSERT(r == 0 || r == ETIMEDOUT);
    return r == 0;
}
#endif // PLATFORM_APPLE

//    ███████╗██╗ ██████╗ ███╗   ██╗ █████╗ ██╗     
//    ██╔════╝██║██╔════╝ ████╗  ██║██╔══██╗██║     
//    ███████╗██║██║  ███╗██╔██╗ ██║███████║██║     
//    ╚════██║██║██║   ██║██║╚██╗██║██╔══██║██║     
//    ███████║██║╚██████╔╝██║ ╚████║██║  ██║███████╗
//    ╚══════╝╚═╝ ╚═════╝ ╚═╝  ╚═══╝╚═╝  ╚═╝╚══════╝

// https://github.com/mattiasgustavsson/libs/blob/master/thread.h
struct SignalImpl 
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int value;
};
static_assert(sizeof(SignalImpl) <= sizeof(Signal), "Signal size mismatch");

void Signal::Initialize()
{
    SignalImpl* sig = reinterpret_cast<SignalImpl*>(mData);

    sig->value = 0;
    [[maybe_unused]] int r = pthread_mutex_init(&sig->mutex, NULL);
    ASSERT_MSG(r == 0, "pthread_mutex_init failed");

    [[maybe_unused]] int r2 = pthread_cond_init(&sig->cond, NULL);
    ASSERT_MSG(r2 == 0, "pthread_cond_init failed");

    _private::CountersAddSignal();
}

void Signal::Release()
{
    SignalImpl* sig = reinterpret_cast<SignalImpl*>(mData);

    pthread_cond_destroy(&sig->cond);
    pthread_mutex_destroy(&sig->mutex);

    _private::CountersRemoveSignal();
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

            #if defined(__ANDROID_API__) && __ANDROID_API__ >= 28
            clock_gettime(CLOCK_MONOTONIC, &ts);
            timespecAdd(&ts, msecs);
            r = pthread_cond_timedwait_monotonic_np(&sig->cond, &sig->mutex, &ts);
            #else
            clock_gettime(CLOCK_REALTIME, &ts);
            timespecAdd(&ts, msecs);
            r = pthread_cond_timedwait(&sig->cond, &sig->mutex, &ts);
            #endif
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
            #if defined(__ANDROID_API__) && __ANDROID_API__ >= 28
            clock_gettime(CLOCK_MONOTONIC, &ts);
            timespecAdd(&ts, msecs);
            r = pthread_cond_timedwait_monotonic_np(&sig->cond, &sig->mutex, &ts);
            #else
            clock_gettime(CLOCK_REALTIME, &ts);
            timespecAdd(&ts, msecs);
            r = pthread_cond_timedwait(&sig->cond, &sig->mutex, &ts);
            #endif
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

void Signal::Decrement(uint32 count)
{
    SignalImpl* sig = reinterpret_cast<SignalImpl*>(mData);

    [[maybe_unused]] int r = pthread_mutex_lock(&sig->mutex);
    ASSERT(r == 0);
    sig->value -= int(count);
    pthread_mutex_unlock(&sig->mutex);
}

void Signal::Increment(uint32 count)
{
    SignalImpl* sig = reinterpret_cast<SignalImpl*>(mData);

    [[maybe_unused]] int r = pthread_mutex_lock(&sig->mutex);
    ASSERT(r == 0);
    sig->value += int(count);
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

//    ████████╗██╗███╗   ███╗███████╗██████╗ 
//    ╚══██╔══╝██║████╗ ████║██╔════╝██╔══██╗
//       ██║   ██║██╔████╔██║█████╗  ██████╔╝
//       ██║   ██║██║╚██╔╝██║██╔══╝  ██╔══██╗
//       ██║   ██║██║ ╚═╝ ██║███████╗██║  ██║
//       ╚═╝   ╚═╝╚═╝     ╚═╝╚══════╝╚═╝  ╚═╝
#if !PLATFORM_APPLE
struct TimerState
{
    bool init;
    uint64 start;
};
static TimerState gTimer;

void Timer::Initialize() 
{
    gTimer.init = true;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    gTimer.start = (uint64)ts.tv_sec*1000000000 + (uint64)ts.tv_nsec;
}

uint64 Timer::GetTicks() 
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64)ts.tv_sec*1000000000 + (uint64)ts.tv_nsec) - gTimer.start;
}
#endif // !PLATFORM_APPLE

//     ██████╗ ███████╗███╗   ██╗███████╗██████╗  █████╗ ██╗          ██████╗ ███████╗
//    ██╔════╝ ██╔════╝████╗  ██║██╔════╝██╔══██╗██╔══██╗██║         ██╔═══██╗██╔════╝
//    ██║  ███╗█████╗  ██╔██╗ ██║█████╗  ██████╔╝███████║██║         ██║   ██║███████╗
//    ██║   ██║██╔══╝  ██║╚██╗██║██╔══╝  ██╔══██╗██╔══██║██║         ██║   ██║╚════██║
//    ╚██████╔╝███████╗██║ ╚████║███████╗██║  ██║██║  ██║███████╗    ╚██████╔╝███████║
//     ╚═════╝ ╚══════╝╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝     ╚═════╝ ╚══════╝
OSDLL OS::LoadDLL(const char* filepath, char** pErrorMsg)
{
    OSDLL dll = dlopen(filepath, RTLD_LOCAL | RTLD_LAZY);
    if (dll == nullptr && pErrorMsg) {
        static char errMsg[64];
        Str::PrintFmt(errMsg, sizeof(errMsg), dlerror());
        *pErrorMsg = errMsg;
    }
    else {
        if (pErrorMsg) 
            *pErrorMsg = nullptr;
    }
    return dll;
}

void OS::UnloadDLL(OSDLL dll)
{
    if (dll)
        dlclose(dll);
}

void* OS::GetSymbolAddress(OSDLL dll, const char* symbolName)
{
    return dlsym(dll, symbolName);
}

size_t OS::GetPageSize()
{
    return static_cast<size_t>(sysconf(_SC_PAGESIZE));
}

bool OS::SetEnvVar(const char* name, const char* value)
{
    return value != nullptr ? setenv(name, value, 1) == 0 : unsetenv(name) == 0;
}

bool OS::GetEnvVar(const char* name, char* outValue, uint32 valueSize)
{
    char* value = getenv(name);
    if (!value)
        return false;
    Str::Copy(outValue, valueSize, value);
    return true;
}

//    ██████╗  █████╗ ████████╗██╗  ██╗
//    ██╔══██╗██╔══██╗╚══██╔══╝██║  ██║
//    ██████╔╝███████║   ██║   ███████║
//    ██╔═══╝ ██╔══██║   ██║   ██╔══██║
//    ██║     ██║  ██║   ██║   ██║  ██║
//    ╚═╝     ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝
char* OS::GetAbsolutePath(const char* path, char* dst, size_t dstSize)
{
    char absPath[PATH_CHARS_MAX];
    if (realpath(path, absPath) != NULL) {
        Str::Copy(dst, (uint32)dstSize, absPath);
    } else {
        dst[0] = '\0';
    }
    return dst;
}

PathInfo OS::GetPathInfo(const char* path)
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

bool OS::CreateDir(const char* path)
{
    return mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0;
}

bool OS::MovePath(const char* src, const char* dest)
{
    return rename(src, dest) == 0;
}

bool OS::DeleteFilePath(const char* path)
{
    return remove(path) == 0;
}

bool OS::MakeTempPath(char* dst, size_t dstSize, const char* namePrefix, const char* dir)
{
    if (dir == nullptr)
        dir = "/tmp";
    PathUtils::Join(dst, dstSize, dir, namePrefix);
    Str::Concat(dst, uint32(dstSize), "XXXXXX");
    mkstemp(dst);
    return dst[0] ? true : false;
}

//    ███╗   ███╗███████╗███╗   ███╗ ██████╗ ██████╗ ██╗   ██╗
//    ████╗ ████║██╔════╝████╗ ████║██╔═══██╗██╔══██╗╚██╗ ██╔╝
//    ██╔████╔██║█████╗  ██╔████╔██║██║   ██║██████╔╝ ╚████╔╝ 
//    ██║╚██╔╝██║██╔══╝  ██║╚██╔╝██║██║   ██║██╔══██╗  ╚██╔╝  
//    ██║ ╚═╝ ██║███████╗██║ ╚═╝ ██║╚██████╔╝██║  ██║   ██║   
//    ╚═╝     ╚═╝╚══════╝╚═╝     ╚═╝ ╚═════╝ ╚═╝  ╚═╝   ╚═╝   
struct MemVirtualStatsAtomic 
{
    AtomicUint64 commitedBytes;
    AtomicUint64 reservedBytes;
};

static MemVirtualStatsAtomic gVMStats;

void* Mem::VirtualReserve(size_t size, MemVirtualFlags flags)
{
    void* ptr = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (!ptr) {
        MEM_FAIL();
    }

    Atomic::FetchAdd(&gVMStats.reservedBytes, size);
    return ptr;
}

void* Mem::VirtualCommit(void* ptr, size_t size)
{
    int r = mprotect(ptr, size, PROT_READ | PROT_WRITE);
    ASSERT(r == 0);
    
    r = madvise(ptr, size, MADV_WILLNEED);
    if (r != 0) {
        if (errno == ENOMEM) {
            MEM_FAIL();
        }
        ASSERT(0);
        return nullptr;
    }
    
    /*
    // Experimental code: I want to hit the pages once in order to fetch them early on
    size_t pageSize = OS::GetPageSize();
    uint8* buff = reinterpret_cast<uint8*>(ptr);
    uintptr dummyCounter = 0;
    for (size_t off = 0; off < size; off += pageSize) {
        dummyCounter += *(uintptr*)(buff + off);
    } 
    */   

    Atomic::FetchAdd(&gVMStats.commitedBytes, size);
    return ptr;
}

void Mem::VirtualDecommit(void* ptr, size_t size)
{
    [[maybe_unused]] int r = madvise(ptr, size, MADV_DONTNEED);
    ASSERT(r == 0);
    Atomic::FetchSub(&gVMStats.commitedBytes, size);
}

void Mem::VirtualRelease(void* ptr, size_t size)
{
    [[maybe_unused]] int r = munmap(ptr, size);
    ASSERT(r == 0);
    Atomic::FetchSub(&gVMStats.reservedBytes, size);
}

MemVirtualStats Mem::VirtualGetStats()
{
    return MemVirtualStats {
        .commitedBytes = gVMStats.commitedBytes,
        .reservedBytes = gVMStats.reservedBytes
    };
}

//    ██╗   ██╗██╗   ██╗██╗██████╗ 
//    ██║   ██║██║   ██║██║██╔══██╗
//    ██║   ██║██║   ██║██║██║  ██║
//    ██║   ██║██║   ██║██║██║  ██║
//    ╚██████╔╝╚██████╔╝██║██████╔╝
//     ╚═════╝  ╚═════╝ ╚═╝╚═════╝ 
#if PLATFORM_APPLE || PLATFORM_LINUX
struct UUIDImpl
{
    uuid_t uuid;
};
static_assert(sizeof(UUIDImpl) <= sizeof(UniqueID), "UUID size mismatch");
#endif

bool UniqueID::Generate(UniqueID* uuid)
{
#if PLATFORM_APPLE || PLATFORM_LINUX
    UUIDImpl* u = reinterpret_cast<UUIDImpl*>(uuid);
    uuid_generate_random(u->uuid);
    return true;
#else
    UNUSED(uuid);
    ASSERT_MSG(0, "Not implemented");
    return false;
#endif
}

bool UniqueID::ToString(const UniqueID& uuid, char* str, uint32 size)
{
#if PLATFORM_APPLE || PLATFORM_LINUX
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

bool UniqueID::FromString(UniqueID* uuid, const char* str)
{
#if PLATFORM_APPLE || PLATFORM_LINUX
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

bool UniqueID::operator==(const UniqueID& uuid) const
{
    return memcmp(&uuid, this, sizeof(UUIDImpl)) == 0;
}

//    ███████╗██╗██╗     ███████╗
//    ██╔════╝██║██║     ██╔════╝
//    █████╗  ██║██║     █████╗  
//    ██╔══╝  ██║██║     ██╔══╝  
//    ██║     ██║███████╗███████╗
//    ╚═╝     ╚═╝╚══════╝╚══════╝
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
            pagesz = OS::GetPageSize();
        ASSERT_ALWAYS((uintptr_t)dst % pagesz == 0, "buffers must be aligned with NoCache flag");
    }
    ssize_t r = read(f->id, dst, size);
    return r != -1 ? r : 0;
}

size_t File::Write(const void* src, size_t size)
{
    ASSERT(size);
    FilePosix* f = (FilePosix*)mData;
    ASSERT(f->id != -1);

    int64_t bytesWritten = write(f->id, src, size);
    if (bytesWritten > -1) {
        f->size += bytesWritten; 
        return bytesWritten;
    }
    else {
        return 0;
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

//    ███████╗ ██████╗  ██████╗██╗  ██╗███████╗████████╗
//    ██╔════╝██╔═══██╗██╔════╝██║ ██╔╝██╔════╝╚══██╔══╝
//    ███████╗██║   ██║██║     █████╔╝ █████╗     ██║   
//    ╚════██║██║   ██║██║     ██╔═██╗ ██╔══╝     ██║   
//    ███████║╚██████╔╝╚██████╗██║  ██╗███████╗   ██║   
//    ╚══════╝ ╚═════╝  ╚═════╝╚═╝  ╚═╝╚══════╝   ╚═╝   
#define SOCKET_INVALID -1
#define SOCKET_ERROR -1

namespace _private
{
    static SocketErrorCode::Enum socketTranslatePlatformErrorCode()
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
        LOG_ERROR("SocketTCP: Opening the socket failed");
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
        LOG_ERROR("SocketTCP: failed binding the socket to port: %d", port);
        return false;
    }

    LOG_VERBOSE("SocketTCP: Listening on port '%d' for incoming connections ...", port);
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
        LOG_ERROR("SocketTCP: failed to accept the new socket");
        return newSock;
    }

    if (clientUrl && clientUrlSize) {
        char ip[256];
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        uint16 port = htons(addr.sin_port);
        
        Str::PrintFmt(clientUrl, clientUrlSize, "%s:%d", ip, port);
    }

    newSock.mLive = true;
    return newSock;
}

SocketTCP SocketTCP::Connect(const char* url)
{
    SocketTCP sock;

    char address[256];
    char port[16];
    if (!ParseUrl(url, address, sizeof(address), port, sizeof(port))) {
        LOG_ERROR("SocketTCP: failed parsing the url: %s", url);
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
        LOG_ERROR("SocketTCP: failed to resolve url: %s", url);
        return sock;
    }

    sock.mSock = socket(addri->ai_family, addri->ai_socktype, addri->ai_protocol);
    if (sock.mSock == SOCKET_INVALID) {
        freeaddrinfo(addri);
        LOG_ERROR("SocketTCP: failed to create socket");
        return sock;
    }

    if (connect(sock.mSock, addri->ai_addr, (int)addri->ai_addrlen) == -1) {
        freeaddrinfo(addri);
        sock.mErrCode = _private::socketTranslatePlatformErrorCode();
        LOG_ERROR("SocketTCP: failed to connect to url: %s", url);
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
                LOG_DEBUG("SocketTCP: socket connection closed forcefully by the peer");
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
            LOG_DEBUG("SocketTCP: socket connection closed forcefully by the peer");
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

//   █████╗ ███████╗██╗   ██╗███╗   ██╗ ██████╗
//  ██╔══██╗██╔════╝╚██╗ ██╔╝████╗  ██║██╔════╝
//  ███████║███████╗ ╚████╔╝ ██╔██╗ ██║██║
//  ██╔══██║╚════██║  ╚██╔╝  ██║╚██╗██║██║
//  ██║  ██║███████║   ██║   ██║ ╚████║╚██████╗
//  ╚═╝  ╚═╝╚══════╝   ╚═╝   ╚═╝  ╚═══╝ ╚═════╝

struct AsyncFilePosix
{
    AsyncFile f;
    MemAllocator* alloc;
    AsyncFileCallback readFn;
    int fd;
    AtomicUint32 done;
};

struct AsyncContext
{
    Mutex mutex;
    Thread* threads;
    Array<AsyncFilePosix*> queue;
    uint32 queueIndex;
    uint32 numThreads;
    Semaphore sem;
    AtomicUint32 quit;
};

static AsyncContext gAsyncCtx;

namespace Async
{
    static int _IOThreadCallback(void* userData)
    {
        while (!Atomic::Load(&gAsyncCtx.quit)) {
            gAsyncCtx.sem.Wait();
            
            AsyncFilePosix* file = nullptr;
            {
                MutexScope lock(gAsyncCtx.mutex);
                if (gAsyncCtx.queue.IsEmpty())
                    continue;
                uint32 index = gAsyncCtx.queueIndex++;
                file = gAsyncCtx.queue[index];
                gAsyncCtx.queue.RemoveAndSwap(index);
                if (gAsyncCtx.queueIndex >= gAsyncCtx.queue.Count())
                    gAsyncCtx.queueIndex = 0;
            }
            
            ASSERT(file->fd != -1);
            pollfd p {};
            p.fd = file->fd;
            p.events = POLLIN;
            [[maybe_unused]] int r = poll(&p, 1, -1);
            ASSERT(r != -1);
            ssize_t bytesRead = read(file->fd, file->f.data, file->f.size);
            ASSERT(bytesRead <= UINT32_MAX);
            
            bool hadError = bytesRead != file->f.size;
            Atomic::StoreExplicit(&file->done, hadError ? 1 : -1, AtomicMemoryOrder::Release);
            file->readFn(&file->f, hadError);
        }
        
        return 0;
    }
} // Async

bool Async::Initialize()
{
    SysInfo info {};
    OS::GetSysInfo(&info);
    ASSERT(info.coreCount);
    
    gAsyncCtx.mutex.Initialize();
    gAsyncCtx.sem.Initialize();
    
    // Create the thread pool for Async IO
    gAsyncCtx.threads = NEW_ARRAY(Mem::GetDefaultAlloc(), Thread, info.coreCount);
    gAsyncCtx.numThreads = info.coreCount;

    for (uint32 i = 0; i < gAsyncCtx.numThreads; i++) {
        String<32> name = String<32>::Format("IO_%u", i+1);
        ThreadDesc tdesc {
            .entryFn = Async::_IOThreadCallback,
            .name = name.CStr(),
            .stackSize = 512*SIZE_KB
        };
        
        gAsyncCtx.threads[i].Start(tdesc);
    }

    LOG_INFO("(init) Initialized %u Async IO Threads", gAsyncCtx.numThreads);

    return true;
}

void Async::Release()
{
    Atomic::Store(&gAsyncCtx.quit, 1);
    gAsyncCtx.sem.Post(gAsyncCtx.numThreads);
    for (uint32 i = 0; i < gAsyncCtx.numThreads; i++)
        gAsyncCtx.threads[i].Stop();
    Mem::Free(gAsyncCtx.threads);
    gAsyncCtx.sem.Release();
    gAsyncCtx.mutex.Release();
}

AsyncFile* Async::ReadFile(const char* filepath, const AsyncFileRequest& request)
{
    int fd = open(filepath, O_RDONLY|O_NONBLOCK, 0);
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
    
    MemSingleShotMalloc<AsyncFilePosix> mallocator;
    uint8* data;
    uint8* userData = nullptr;
    if (request.userDataAllocateSize)
        mallocator.AddExternalPointerField<uint8>(&userData, request.userDataAllocateSize);
    mallocator.AddExternalPointerField<uint8>(&data, fileSize);
    
    AsyncFilePosix* file = mallocator.Malloc(request.alloc);
    memset(file, 0x0, sizeof(*file));
    
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
    
    {
        MutexScope lock(gAsyncCtx.mutex);
        gAsyncCtx.queue.Push(file);
        gAsyncCtx.sem.Post();
    }

    return &file->f;
}

void Async::Close(AsyncFile* file)
{
    AsyncFilePosix* fm = (AsyncFilePosix*)file;
    if (fm->fd)
        close(fm->fd);
    MemSingleShotMalloc<AsyncFilePosix>::Free(fm, fm->alloc);
}

bool Async::Wait(AsyncFile* file)
{
    ASSERT_MSG(0, "Wait not implemented for generic impl");
    return false;
}

bool Async::IsFinished(AsyncFile* file, bool* outError)
{
    ASSERT(file);
    AsyncFilePosix* f = (AsyncFilePosix*)file;
    uint32 r = Atomic::LoadExplicit(&f->done, AtomicMemoryOrder::Acquire);
    if (outError)
        *outError = (r == -1);
    return r != 0;
}

//----------------------------------------------------------------------------------------------------------------------
// OSProcess
OSProcess::OSProcess() :
    mExitCode(-1),
    mTermSignalCode(0)
{
    mProcess = IntToPtr<int>(-1);
    mStdOutPipeRead = IntToPtr<int>(-1);
    mStdErrPipeRead = IntToPtr<int>(-1);
}

OSProcess::~OSProcess()
{
    int stdoutPipeRead = PtrToInt<int32>(mStdOutPipeRead);
    int stderrPipeRead = PtrToInt<int32>(mStdErrPipeRead);
    pid_t pid = PtrToInt<int32>(mProcess);
    
    if (stdoutPipeRead != -1)
        close(stdoutPipeRead);
    if (stderrPipeRead != -1)
        close(stderrPipeRead);
    
    if (pid != -1) {
        int status;
        waitpid(pid, &status, 0);
    }
}

bool OSProcess::Run(const char* cmdline, OSProcessFlags flags, const char* cwd)
{
    int stdoutPipes[2] = {-1, -1};
    int stderrPipes[2] = {-1, -1};
    pid_t pid;
    posix_spawn_file_actions_t fileActions;

    [[maybe_unused]] int r = posix_spawn_file_actions_init(&fileActions);
    ASSERT_MSG(r == 0, "posix_spawn_file_actions_init failed");

    if ((flags & OSProcessFlags::CaptureOutput) == OSProcessFlags::CaptureOutput) {
        r = pipe(stdoutPipes);
        ASSERT_MSG(r == 0, "Creating pipes failed");

        // tell spawned process to close unused read end of pipe
        // without this - spawned process would not receive EOF
        // when read end of the pipe is closed below,
        r = posix_spawn_file_actions_addclose(&fileActions, stdoutPipes[0]);
        ASSERT_MSG(r == 0, "posix_spawn_file_actions_addclose");
        
        r = posix_spawn_file_actions_adddup2(&fileActions, stdoutPipes[1], STDOUT_FILENO);
        ASSERT_MSG(r == 0, "posix_spawn_file_actions_addup2 failed");
        
        // Do the same for stderr
        r = pipe(stderrPipes);
        ASSERT_MSG(r == 0, "Creating pipes failed");

        r = posix_spawn_file_actions_addclose(&fileActions, stderrPipes[0]);
        ASSERT_MSG(r == 0, "posix_spawn_file_actions_addclose");
        
        r = posix_spawn_file_actions_adddup2(&fileActions, stderrPipes[1], STDERR_FILENO);
        ASSERT_MSG(r == 0, "posix_spawn_file_actions_addup2 failed");
    }
    
    if (cwd) {
        if (__builtin_available(macOS 10.15, *))
            posix_spawn_file_actions_addchdir_np(&fileActions, cwd);
        else
            ASSERT_MSG(0, "Not implemented for versions less than macOS 10.15");
    }
    
    // split command-line arguments
    MemTempAllocator tmpAlloc;
    Array<char*> argsArr(&tmpAlloc);

    char* cmdlineCopy = Mem::AllocCopy<char>(cmdline, Str::Len(cmdline)+1, &tmpAlloc);
    char* str = const_cast<char*>(Str::SkipWhitespace(cmdlineCopy));
    while (*str) {
        // Find the next whitespace, or end of string
        char* start = str;
        while (*(++str)) {
            if (Str::IsWhitespace(*str)) {
                *str = 0;
                str = const_cast<char*>(Str::SkipWhitespace(str+1));
                break;
            }
        }
        argsArr.Push(start);
    }
    
    ASSERT(argsArr.Count());
    char** args = nullptr;
    if (argsArr.Count() > 1) {
        args = (char**)tmpAlloc.MallocTyped<char*>(argsArr.Count() + 1);
        for (uint32 i = 0; i < argsArr.Count(); i++)
            args[i] = argsArr[i];
        args[argsArr.Count()] = nullptr;
    }
    
    if (posix_spawn(&pid, argsArr[0], &fileActions, nullptr, args, nullptr) != 0) {
        LOG_ERROR("Running process failed: %s", cmdline);
        posix_spawn_file_actions_destroy(&fileActions);
        if (stdoutPipes[0] != -1)
            close(stdoutPipes[0]);
        if (stdoutPipes[1] != -1)
            close(stdoutPipes[1]);
        return false;
    }
    
    if ((flags & OSProcessFlags::CaptureOutput) == OSProcessFlags::CaptureOutput) {
        close(stdoutPipes[1]);
        close(stderrPipes[1]);
        mStdOutPipeRead = IntToPtr<int>(stdoutPipes[0]);
        mStdErrPipeRead = IntToPtr<int>(stderrPipes[0]);
    }
    
    posix_spawn_file_actions_destroy(&fileActions);
    mProcess = IntToPtr<int32>(pid);
    return true;
}

void OSProcess::Wait() const
{
    pid_t pid = PtrToInt<int32>(mProcess);
    ASSERT(pid != -1);
    int status;
    [[maybe_unused]] int r = waitpid(pid, &status, 0);
    ASSERT(r == pid);
    if (WIFEXITED(status))
        const_cast<OSProcess*>(this)->mExitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        const_cast<OSProcess*>(this)->mTermSignalCode = WTERMSIG(status);
    const_cast<OSProcess*>(this)->mProcess = IntToPtr<int32>(-1);
}

bool OSProcess::IsRunning() const
{
    pid_t pid = PtrToInt<int32>(mProcess);
    ASSERT(pid != -1);
    int status;
    return waitpid(pid, &status, WNOHANG) == 0;
}

int OSProcess::GetExitCode() const
{
    return mExitCode;
}

uint32 OSProcess::ReadStdOut(void* data, uint32 size) const
{
    int pipeId = PtrToInt<int>(mStdOutPipeRead);
    ASSERT(pipeId != -1);
    ssize_t r = read(pipeId, data, size);
    return r > 0 ? (uint32)r : 0;
}

uint32 OSProcess::ReadStdErr(void* data, uint32 size) const
{
    int pipeId = PtrToInt<int>(mStdErrPipeRead);
    ASSERT(pipeId != -1);
    ssize_t r = read(pipeId, data, size);
    return r > 0 ? (uint32)r : 0;
}

void OSProcess::Abort()
{
    pid_t pid = PtrToInt<int32>(mProcess);
    if (pid)
        kill(pid, 1);
}



#endif // PLATFORM_POSIX
