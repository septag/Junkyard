#include "System.h"

#if PLATFORM_APPLE
#include "String.h"

#include <mach/mach_time.h>
#include <mach-o/dyld.h>        // _NSGetExecutablePath
#include <unistd.h>             // sysconf
#include <dlfcn.h>              // dlopen, dlclose, dlsym
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <pthread.h>            // pthread_t and family

struct MutexImpl 
{
    pthread_mutex_t handle;
};

struct SemaphoreImpl 
{
    dispatch_semaphore_t handle;
};

struct SignalImpl 
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int value;
};

struct Thread 
{
    Semaphore sem;
    Allocator* alloc;
    int (*threadFn)(void* userData);
    pthread_t handle;
    char name[32];
    void* userData;
    size_t stackSize;
    bool running;
};

struct TimerState 
{
    bool init;
    mach_timebase_info_data_t timebase;
    uint64 start;
};

static TimerState gTimer;

static_assert(sizeof(MutexImpl) <= sizeof(Mutex), "Mutex size mismatch");
static_assert(sizeof(SemaphoreImpl) <= sizeof(Semaphore), "Sempahore size mismatch");
static_assert(sizeof(SignalImpl) <= sizeof(Signal), "Signal size mismatch");

//--------------------------------------------------------------------------------------------------
// Mutex
void mutexInitialize(Mutex* mutex)
{
    auto _m = (MutexImpl*)mutex->data;
    pthread_mutexattr_t attr;
    int r = pthread_mutexattr_init(&attr);
    ASSERT(r == 0);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    [[maybe_unused]] r = pthread_mutex_init(&_m->handle, &attr);
    ASSERT_MSG(r == 0, "pthread_mutex_init failed");
}

void mutexRelease(Mutex* mutex)
{
    auto _m = (MutexImpl*)mutex->data;
    pthread_mutex_destroy(&_m->handle);
}

void mutexEnter(Mutex* mutex)
{
    auto _m = (MutexImpl*)mutex->data;
    pthread_mutex_lock(&_m->handle);
}

void mutexExit(Mutex* mutex)
{
    auto _m = (MutexImpl*)mutex->data;
    pthread_mutex_unlock(&_m->handle);
}

bool mutexTryLock(Mutex* mutex)
{
    auto _m = (MutexImpl*)mutex->data;
    return pthread_mutex_trylock(&_m->handle) == 0;
}

// Semaphore
void semaphoreInitialize(Semaphore* sem)
{
    auto _sem = (SemaphoreImpl*)sem->data;
    _sem->handle = dispatch_semaphore_create(0);
    ASSERT_MSG(_sem->handle != NULL, "dispatch_semaphore_create failed");
}

void semaphoreRelease(Semaphore* sem)
{
    auto _sem = (SemaphoreImpl*)sem->data;
    if (_sem->handle) {
        dispatch_release(_sem->handle);
        _sem->handle = NULL;
    }
}

void semaphorePost(Semaphore* sem, uint32 count)
{
    auto _sem = (SemaphoreImpl*)sem->data;
    for (int i = 0; i < count; i++) {
        dispatch_semaphore_signal(_sem->handle);
    } 
}

bool semaphoreWait(Semaphore* sem, uint32 msecs)
{
    auto _sem = (SemaphoreImpl*)sem->data;
    dispatch_time_t dt = msecs < 0 ? DISPATCH_TIME_FOREVER
                                   : dispatch_time(DISPATCH_TIME_NOW, (int64_t)msecs * 1000000ll);
    return !dispatch_semaphore_wait(_sem->handle, dt);
}

//--------------------------------------------------------------------------------------------------
// Signal
// https://github.com/mattiasgustavsson/libs/blob/master/thread.h
void signalInitialize(Signal* sig)
{
    auto _sig = (SignalImpl*)sig->data;
    _sig->value = 0;
    int r = pthread_mutex_init(&_sig->mutex, NULL);
    ASSERT_MSG(r == 0, "pthread_mutex_init failed");

    r = pthread_cond_init(&_sig->cond, NULL);
    ASSERT_MSG(r == 0, "pthread_cond_init failed");

    UNUSED(r);
}

void signalRelease(Signal* sig)
{
    auto _sig = (SignalImpl*)sig->data;
    pthread_cond_destroy(&_sig->cond);
    pthread_mutex_destroy(&_sig->mutex);
}

void signalRaise(Signal* sig)
{
    auto _sig = (SignalImpl*)sig->data;
    [[maybe_unused]] int r = pthread_mutex_lock(&_sig->mutex);
    ASSERT(r == 0);
    _sig->value = 1;
    pthread_mutex_unlock(&_sig->mutex);
    pthread_cond_signal(&_sig->cond);
}

bool signalWait(Signal* sig, uint32 msecs)
{
    auto toNs = [](const struct timespec* _ts)->uint64
    {
        return _ts->tv_sec * UINT64_C(1000000000) + _ts->tv_nsec;
    };

    auto timespecNs = [](struct timespec* _ts, uint64 _nsecs)
    {
        _ts->tv_sec = _nsecs / UINT64_C(1000000000);
        _ts->tv_nsec = _nsecs % UINT64_C(1000000000);
    };

    auto tmAdd = [timespecNs, toNs](struct timespec* _ts, int32_t _msecs)
    {
        uint64 ns = toNs(_ts);
        timespecNs(_ts, ns + (uint64)(_msecs)*1000000);
    };

    auto _sig = (SignalImpl*)sig->data;
    int r = pthread_mutex_lock(&_sig->mutex);
    ASSERT(r == 0);

    if (msecs == -1) {
        r = pthread_cond_wait(&_sig->cond, &_sig->mutex);
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        tmAdd(&ts, msecs);
        r = pthread_cond_timedwait(&_sig->cond, &_sig->mutex, &ts);
    }

    bool ok = r == 0;
    if (ok)
        _sig->value = 0;
    r = pthread_mutex_unlock(&_sig->mutex);
    UNUSED(r);
    return ok;
}

//--------------------------------------------------------------------------------------------------
// Thread
static void* threadStubFn(void* arg)
{
    auto thrd = (thread*)arg;
    union {
        void* ptr;
        int32 i;
    } cast;

    if (thrd->name[0])
        threadSetName(thrd, thrd->name);

    semaphorePost(&thrd->sem, 1);
    cast.i = thrd->threadFn(thrd->userData);
    return cast.ptr;
}

NODISCARD thread* threadCreate(int(*threadFn)(void* userData), void* userData, const char* name, 
                                         size_t stackSize, Allocator* alloc)
{
    auto thrd = (thread*)MALLOC(alloc, sizeof(thread));

    thrd->alloc = alloc;
    semaphoreInitialize(&thrd->sem);
    thrd->threadFn = threadFn;
    thrd->userData = userData;
    thrd->stackSize = Max((uint64)stackSize, 32768llu);
    thrd->running = true;
    if (name)
        strCopy(thrd->name, sizeof(thrd->name), name);
    else
        thrd->name[0] = 0;

    pthread_attr_t attr;
    [[maybe_unused]] int r = pthread_attr_init(&attr);
    ASSERT_MSG(r == 0, "pthread_attr_init failed");
    r = pthread_attr_setstacksize(&attr, thrd->stackSize);
    ASSERT_MSG(r == 0, "pthread_attr_setstacksize failed");

    r = pthread_create(&thrd->handle, &attr, threadStubFn, thrd);
    ASSERT_MSG(r == 0, "pthread_create failed");

    // Ensure that thread callback is running
    semaphoreWait(&thrd->sem, WAIT_FOREVER);

    if (name)
        threadSetName(thrd, name);

    return thrd;
}

int threadDestroy(thread* thrd)
{
    ASSERT(thrd);
    ASSERT_MSG(thrd->running, "Thread is not running!");

    union {
        void* ptr;
        int32_t i;
    } cast;

    pthread_join(thrd->handle, &cast.ptr);

    semaphoreRelease(&thrd->sem);

    thrd->handle = 0;
    thrd->running = false;

    FREE(thrd->alloc, thrd);
    return cast.i;
}

bool threadIsRunning(thread* thrd)
{
    return thrd->running;
}

void threadYield(void)
{
    sched_yield();
}

void threadSetName(thread* thrd, const char* name)
{
    UNUSED(thrd);
    pthread_setname_np(name);
}

uint32 threadGetCurrentId(void)
{
    return (mach_port_t)pthread_mach_thread_np(pthread_self());
}

void threadSleep(uint32 msecs)
{
    struct timespec req = { (time_t)msecs / 1000, (long)((msecs % 1000) * 1000000) };
    struct timespec rem = { 0, 0 };
    nanosleep(&req, &rem);
}

// Tip by johaness spohr
// https://gist.github.com/jspohr/3dc4f00033d79ec5bdaf67bc46c813e3
static int64_t timerInt64MulDiv(int64_t value, int64_t numer, int64_t denom) 
{
    int64_t q = value / denom;
    int64_t r = value % denom;
    return q * numer + r * numer / denom;
}

void timerInitialize() 
{
    gTimer.init = true;
    mach_timebase_info(&gTimer.timebase);
    gTimer.start = mach_absolute_time();
}

uint64 timerGetTicks() 
{
    ASSERT_MSG(gTimer.init, "Timer not initialized. call timerInit()");
    const uint64 machNow = mach_absolute_time() - gTimer.start;
    return timerInt64MulDiv(machNow, gTimer.timebase.numer, gTimer.timebase.denom);
}

dLL loadDLL(const char* filepath, char** pErrorMsg)
{
    dLL dll = dlopen(filepath, RTLD_LOCAL | RTLD_LAZY);
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

void unloadDLL(dLL dll)
{
    if (dll)
        dlclose(dll);
}

void* symbolAddress(dLL dll, const char* symbolName)
{
    return dlsym(dll, symbolName);
}

NODISCARD size_t getPageSize(void)
{
    return (size_t)sysconf(_SC_PAGESIZE);
}

char* pathGetMyPath(char* dst, size_t dstSize)
{
    uint32 size32 = (uint32)dstSize;
    _NSGetExecutablePath(dst, (uint32_t*)&size32);
    return dst;
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

char* pathGetCurrentDir(char* dst, size_t dstSize)
{
    return getcwd(dst, dstSize);
}

void pathSetCurrentDir(const char* path)
{
    chdir(path);
}

#endif // PLATFORM_APPLE

