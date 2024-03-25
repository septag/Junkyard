#include "System.h"

#include "Atomic.h"
#include "StringUtil.h"
#include "Blobs.h"

#ifdef BUILD_UNITY
    #if PLATFORM_WINDOWS
        #include "SystemWin.cpp"
    #elif PLATFORM_ANDROID
        #include "SystemPosix.cpp"
        #include "SystemAndroid.cpp"
    #elif PLATFORM_OSX
        #include "SystemMac.cpp"
    #else
        #error "Not implemented"
    #endif
#endif

#if defined(__SSE2__)
#include <emmintrin.h>    // _mm_pause
#endif

#if CPU_ARM
#include <arm_acle.h>     // __yield
#endif 

#if PLATFORM_WINDOWS
    #if ARCH_32BIT && CPU_X86
        #if !COMPILER_MSVC
            #include <x86intrin.h>
        #endif
    #endif
    #include <intrin.h>
    #if COMPILER_MSVC
        #pragma intrinsic(_mm_pause)
        #pragma intrinsic(__rdtsc)
    #endif
#elif PLATFORM_APPLE
    #include <mach/mach_time.h>
#endif

#if !PLATFORM_WINDOWS
    #include <sys/time.h>
#endif

struct SysCounters
{
    atomicUint32 numThreads;
    atomicUint32 numMutexes;
    atomicUint32 numSemaphores;
    atomicUint32 numSignals;
    atomicUint64 threadStackSize;
};

static SysCounters gSysCounters;

// We are doing static initilization for timers
// We don't do much of that because of many pitfalls of this approach. 
// But here can be a safe exception for conveniency. Because timer initialization does not involve allocations or any sensitive init code
struct TimerInitializer
{
    TimerInitializer() { _private::timerInitialize(); }
};

static TimerInitializer gTimerInit;

char* pathToUnix(const char *path, char *dst, size_t dstSize)
{
    size_t len = strLen(path);
    len = Min<size_t>(len, dstSize - 1);

    for (int i = 0; i < len; i++) {
        if (path[i] != '\\')
            dst[i] = path[i];
        else
            dst[i] = '/';
    }
    dst[len] = '\0';
    return dst;
}

char* pathToWin(const char *path, char *dst, size_t dstSize)
{
    size_t len = strLen(path);
    len = Min<size_t>(len, dstSize - 1);

    for (int i = 0; i < len; i++) {
        if (path[i] != '/')
            dst[i] = path[i];
        else
            dst[i] = '\\';
    }
    dst[len] = '\0';
    return dst;
}

char* pathFileExtension(const char *path, char *dst, size_t dstSize)
{
    ASSERT(dstSize > 0);

    int len = strLen(path);
    if (len > 0) {
        const char *start = strrchr(path, '/');
        #if PLATFORM_WINDOWS
        if (!start)
            start = strrchr(path, '\\');
        #endif
        if (!start)
            start = path;
        const char *end = &path[len - 1];
        for (const char *e = start; e < end; ++e) {
            if (*e != '.')
                continue;
            strCopy(dst, (uint32)dstSize, e);
            return dst;
        }
    }

    dst[0] = '\0'; // no extension
    return dst;
}

char* pathFileNameAndExt(const char *path, char *dst, size_t dstSize)
{
    const char *r = strrchr(path, '/');
    #if PLATFORM_WINDOWS
    if (!r)
        r = strrchr(path, '\\');
    #endif
    if (r) {
        strCopy(dst, (uint32)dstSize, r + 1);
    }
    else if (dst != path) {
        strCopy(dst, (uint32)dstSize, path);
    }
    return dst;
}

char* pathFileName(const char* path, char* dst, size_t dstSize)
{
    const char *r = strrchr(path, '/');
    #if PLATFORM_WINDOWS
    if (!r)
        r = strrchr(path, '\\');
    #endif
    if (r) {
        strCopy(dst, (uint32)dstSize, r + 1);
    }
    else if (dst != path) {
        strCopy(dst, (uint32)dstSize, path);
    }

    char* dot = strrchr(dst, '.');
    if (dot) 
        *dot = '\0';

    return dst;
}

char* pathDirectory(const char *path, char *dst, size_t dstSize)
{
    const char *r = strrchr(path, '/');
    #if PLATFORM_WINDOWS
    if (!r)
        r = strrchr(path, '\\');
    #endif
    if (r) {
        int o = (int)(intptr_t)(r - path);
        if (dst == path) {
            dst[o] = '\0';
        }
        else {
            strCopyCount(dst, (uint32)dstSize, path, o);
        }

        #if PLATFORM_WINDOWS
        // if (o > 0 && dst[o-1] == ':')
        //    strConcat(dst, (uint32)dstSize, "\\");
        #endif
    }
    else if (dst != path) {
        *dst = '\0';
    }
    return dst;
}

static char* pathJoin(char *dst, size_t dstSize, const char *sep, const char *pathA, const char *pathB)
{
    ASSERT(dst != pathB);
    int len = strLen(pathA);
    if (dst != pathA) {
        if (len > 0 && pathA[len - 1] == sep[0]) {
            strCopy(dst, (uint32)dstSize, pathA);
        }
        else if (len > 0) {
            strCopy(dst, (uint32)dstSize, pathA);
            strConcat(dst, (uint32)dstSize, sep);
        }
        else {
            dst[0] = '\0';
        }
    }
    else if (len > 0 && pathA[len - 1] != sep[0]) {
        strConcat(dst, (uint32)dstSize, sep);
    }

    if (pathB[0] == sep[0])
        ++pathB;
    strConcat(dst, (uint32)dstSize, pathB);
    return dst;
}

char* pathJoin(char *dst, size_t dstSize, const char *pathA, const char *pathB)
{
    #if PLATFORM_WINDOWS
    const char *kSep = "\\";
    #else
    const char *kSep = "/";
    #endif

    return pathJoin(dst, dstSize, kSep, pathA, pathB);
}

char* pathJoinUnixStyle(char *dst, size_t dstSize, const char *pathA, const char *pathB)
{
    return pathJoin(dst, dstSize, "/", pathA, pathB);
}

uint64 timerLapTime(uint64* lastTime)
{
    ASSERT(lastTime);
    uint64 dt = 0;
    uint64 now = timerGetTicks();
    if (*lastTime != 0) 
        dt = timerDiff(now, *lastTime);
    *lastTime = now;
    return dt;
}

void sysGenerateCmdLineFromArgcArgv(int argc, const char* argv[], char** outString, uint32* outStringLen, 
                                    Allocator* alloc, const char* prefixCmd)
{
    ASSERT(outString);
    ASSERT(outStringLen);

    Blob blob(alloc);
    blob.SetGrowPolicy(Blob::GrowPolicy::Linear, 256);

    // If we have a prefix command, append to the beginning
    if (prefixCmd) {
        blob.Write(prefixCmd, strLen(prefixCmd));
        blob.Write<char>(32);
    }

    // TODO: perform escaping on the strings
    for (int i = 0; i < argc; i++) {
        blob.Write(argv[i], strLen(argv[i]));
        if (i != argc - 1)
            blob.Write<char>(32);
    }
    blob.Write<char>(0);

    size_t len;
    blob.Detach((void**)outString, &len);
    *outStringLen = static_cast<uint32>(len);
}

bool _private::socketParseUrl(const char* url, char* address, size_t addressSize, char* port, size_t portSize, const char** pResource)
{
    uint32 urlLen = strLen(url);
    
    // skip the 'protocol://' part
    if (const char* addressBegin = strFindStr(url, "://"); addressBegin)
        url = addressBegin + 2;
    
    // find end of address part of url
    char const* addressEnd = strFindChar(url, ':');
    if (!addressEnd) addressEnd = strFindChar(url, '/');
    if (!addressEnd) addressEnd = url + urlLen;
        
    // extract address
    uint32 addressLen = PtrToInt<uint32>((void*)(addressEnd - url));
    if(addressLen >= addressSize) 
        return false;
    memcpy(address, url, addressLen);
    address[addressLen] = '\0';
        
    // check if there's a port defined
    char const* portEnd = addressEnd;
    if (*addressEnd == ':') {
        ++addressEnd;
        portEnd = strFindChar(addressEnd, '/');
        if (!portEnd) 
            portEnd = addressEnd + strLen(addressEnd);
        uint32 portLen = PtrToInt<uint32>((void*)(portEnd - addressEnd));
        if (portLen >= portSize) 
            return false;
        memcpy(port, addressEnd, portLen);
        port[portLen] = '\0';
    }
    else {
        return false;
    }    
    
    if (pResource)
        *pResource = portEnd;    
    return true;    
}

void _private::sysCountersAddThread(size_t stackSize)
{
    atomicFetchAdd32Explicit(&gSysCounters.numThreads, 1, AtomicMemoryOrder::Relaxed);
    atomicFetchAdd64Explicit(&gSysCounters.threadStackSize, stackSize, AtomicMemoryOrder::Relaxed);
}

void _private::sysCountersRemoveThread(size_t stackSize)
{
    atomicFetchSub32Explicit(&gSysCounters.numThreads, 1, AtomicMemoryOrder::Relaxed);
    atomicFetchSub64Explicit(&gSysCounters.threadStackSize, stackSize, AtomicMemoryOrder::Relaxed);
}

void _private::sysCountersAddMutex()
{
    atomicFetchAdd32Explicit(&gSysCounters.numMutexes, 1, AtomicMemoryOrder::Relaxed);
}

void _private::sysCountersRemoveMutex()
{
    atomicFetchSub32Explicit(&gSysCounters.numMutexes, 1, AtomicMemoryOrder::Relaxed);
}

void _private::sysCountersAddSignal()
{
    atomicFetchAdd32Explicit(&gSysCounters.numSignals, 1, AtomicMemoryOrder::Relaxed);
}

void _private::sysCountersRemoveSignal()
{
    atomicFetchSub32Explicit(&gSysCounters.numSignals, 1, AtomicMemoryOrder::Relaxed);
}

void _private::sysCountersAddSemaphore()
{
    atomicFetchAdd32Explicit(&gSysCounters.numSemaphores, 1, AtomicMemoryOrder::Relaxed);
}

void _private::sysCountersRemoveSemaphore()
{
    atomicFetchSub32Explicit(&gSysCounters.numSemaphores, 1, AtomicMemoryOrder::Relaxed);
}

SysPrimitiveStats sysGetPrimitiveStats()
{
    return SysPrimitiveStats {
        .numMutexes = gSysCounters.numMutexes,
        .numSignals = gSysCounters.numSignals,
        .numSemaphores = gSysCounters.numSemaphores,
        .numThreads = gSysCounters.numThreads,
        .threadStackSize = gSysCounters.threadStackSize
    };
}

// Reference: https://rigtorp.se/spinlock/
// TODO (consider): https://www.intel.com/content/www/us/en/developer/articles/technical/a-common-construct-to-avoid-the-contention-of-threads-architecture-agnostic-spin-wait-loops.html
// Another good reference code: https://github.dev/concurrencykit/ck
void SpinLockMutex::Enter()
{
    while (atomicExchange32Explicit(&mLocked, 1, AtomicMemoryOrder::Acquire) == 1) {
        uint32 spinCount = 1;
        do {
            if (spinCount++ & 1023)
                sysPauseCpu();
            else
                threadYield();
        } while (atomicLoad32Explicit(&mLocked, AtomicMemoryOrder::Relaxed));
    }
}

void SpinLockMutex::Exit()
{
    atomicStore32Explicit(&mLocked, 0, AtomicMemoryOrder::Release);
}

bool SpinLockMutex::TryEnter()
{
    return atomicLoad32Explicit(&mLocked, AtomicMemoryOrder::Relaxed) == 0 &&
           atomicExchange32Explicit(&mLocked, 1, AtomicMemoryOrder::Acquire) == 0;
}

void sysPauseCpu()
{
#if CPU_X86
    _mm_pause();
#elif CPU_ARM 
    __yield();
#else
    #error "Not implemented"
#endif
}

// https://github.com/google/benchmark/blob/v1.1.0/src/cycleclock.h
uint64 sysGetCpuClock()
{
#if PLATFORM_APPLE
    return mach_absolute_time();
#elif PLATFORM_WINDOWS
    return __rdtsc();
#elif CPU_ARM && ARCH_64BIT
    uint64 vtm;
    asm volatile("mrs %0, cntvct_el0" : "=r"(vtm));
    return vtm;
#elif CPU_ARM
    #if (__ARM_ARCH >= 6)
        uint32 pmccntr;
        uint32 pmuseren;
        uint32 pmcntenset;
        // Read the user mode perf monitor counter access permissions.
        asm volatile("mrc p15, 0, %0, c9, c14, 0" : "=r"(pmuseren));
        if (pmuseren & 1) {    // Allows reading perfmon counters for user mode code.
            asm volatile("mrc p15, 0, %0, c9, c12, 1" : "=r"(pmcntenset));
            if (pmcntenset & 0x80000000ul) {    // Is it counting?
                asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(pmccntr));
                // The counter is set up to count every 64th cycle
                return (int64_t)pmccntr * 64;    // Should optimize to << 6
            }
        }
    #endif // (__ARM_ARCH >= 6)
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
#elif CPU_X86 && ARCH_32BIT
    int64_t ret;
    __asm__ volatile("rdtsc" : "=A"(ret));
    return ret;
#elif CPU_X86 && ARCH_64BIT
    uint64 low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return (high << 32) | low;
#endif
}

