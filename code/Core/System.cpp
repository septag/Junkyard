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
    AtomicUint32 numThreads;
    AtomicUint32 numMutexes;
    AtomicUint32 numSemaphores;
    AtomicUint32 numSignals;
    AtomicUint64 threadStackSize;
};

static SysCounters gSysCounters;

// We are doing static initilization for timers
// We don't do much of that because of many pitfalls of this approach. 
// But here can be a safe exception for conveniency. Because timer initialization does not involve allocations or any sensitive init code
struct TimerInitializer
{
    TimerInitializer() { _private::InitializeTimer(); }
};

static TimerInitializer gTimerInit;

char* PathUtils::ToUnix(const char *path, char *dst, size_t dstSize)
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

char* PathUtils::ToWin(const char *path, char *dst, size_t dstSize)
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

char* PathUtils::GetFileExtension(const char *path, char *dst, size_t dstSize)
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

char* PathUtils::GetFilenameAndExtension(const char *path, char *dst, size_t dstSize)
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

char* PathUtils::GetFilename(const char* path, char* dst, size_t dstSize)
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

char* PathUtils::GetDirectory(const char *path, char *dst, size_t dstSize)
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

namespace PathUtils
{
    static char* Join(char *dst, size_t dstSize, const char *sep, const char *pathA, const char *pathB)
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
}

char* PathUtils::Join(char *dst, size_t dstSize, const char *pathA, const char *pathB)
{
    #if PLATFORM_WINDOWS
    const char *kSep = "\\";
    #else
    const char *kSep = "/";
    #endif

    return PathUtils::Join(dst, dstSize, kSep, pathA, pathB);
}

char* PathUtils::JoinUnixStyle(char *dst, size_t dstSize, const char *pathA, const char *pathB)
{
    return PathUtils::Join(dst, dstSize, "/", pathA, pathB);
}

uint64 Timer::LapTime(uint64* lastTime)
{
    ASSERT(lastTime);
    uint64 dt = 0;
    uint64 now = Timer::GetTicks();
    if (*lastTime != 0) 
        dt = Timer::Diff(now, *lastTime);
    *lastTime = now;
    return dt;
}

void OS::GenerateCmdLineFromArgcArgv(int argc, const char* argv[], char** outString, uint32* outStringLen, 
                                    MemAllocator* alloc, const char* prefixCmd)
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

void OS::PauseCPU()
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
uint64 OS::GetCPUClock()
{
#if PLATFORM_APPLE  // TODO: maybe we can get rid of this and use the asm one
    return mach_absolute_time();
#elif PLATFORM_WINDOWS && CPU_X86
    return __rdtsc();
#elif CPU_ARM && ARCH_64BIT
    uint64 vtm;
    asm volatile("mrs %0, cntvct_el0" : "=r"(vtm));
    return vtm;
#elif CPU_X86 && ARCH_64BIT
    uint64 low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return (high << 32) | low;
#else
    #error "Not implemented for this platform"
#endif
}

bool SocketTCP::ParseUrl(const char* url, char* address, size_t addressSize, char* port, size_t portSize, const char** pResource)
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

void _private::CountersAddThread(size_t stackSize)
{
    Atomic::FetchAddExplicit(&gSysCounters.numThreads, 1, AtomicMemoryOrder::Relaxed);
    Atomic::FetchAddExplicit(&gSysCounters.threadStackSize, stackSize, AtomicMemoryOrder::Relaxed);
}

void _private::CountersRemoveThread(size_t stackSize)
{
    Atomic::FetchSubExplicit(&gSysCounters.numThreads, 1, AtomicMemoryOrder::Relaxed);
    Atomic::FetchSubExplicit(&gSysCounters.threadStackSize, stackSize, AtomicMemoryOrder::Relaxed);
}

void _private::CountersAddMutex()
{
    Atomic::FetchAddExplicit(&gSysCounters.numMutexes, 1, AtomicMemoryOrder::Relaxed);
}

void _private::CountersRemoveMutex()
{
    Atomic::FetchSubExplicit(&gSysCounters.numMutexes, 1, AtomicMemoryOrder::Relaxed);
}

void _private::CountersAddSignal()
{
    Atomic::FetchAddExplicit(&gSysCounters.numSignals, 1, AtomicMemoryOrder::Relaxed);
}

void _private::CountersRemoveSignal()
{
    Atomic::FetchSubExplicit(&gSysCounters.numSignals, 1, AtomicMemoryOrder::Relaxed);
}

void _private::CountersAddSemaphore()
{
    Atomic::FetchAddExplicit(&gSysCounters.numSemaphores, 1, AtomicMemoryOrder::Relaxed);
}

void _private::CountersRemoveSemaphore()
{
    Atomic::FetchSubExplicit(&gSysCounters.numSemaphores, 1, AtomicMemoryOrder::Relaxed);
}

SysPrimitiveStats GetSystemPrimitiveStats()
{
    return SysPrimitiveStats {
        .numMutexes = gSysCounters.numMutexes,
        .numSignals = gSysCounters.numSignals,
        .numSemaphores = gSysCounters.numSemaphores,
        .numThreads = gSysCounters.numThreads,
        .threadStackSize = gSysCounters.threadStackSize
    };
}

//    ███████╗██████╗ ██╗███╗   ██╗██╗      ██████╗  ██████╗██╗  ██╗    ███╗   ███╗██╗   ██╗████████╗███████╗██╗  ██╗
//    ██╔════╝██╔══██╗██║████╗  ██║██║     ██╔═══██╗██╔════╝██║ ██╔╝    ████╗ ████║██║   ██║╚══██╔══╝██╔════╝╚██╗██╔╝
//    ███████╗██████╔╝██║██╔██╗ ██║██║     ██║   ██║██║     █████╔╝     ██╔████╔██║██║   ██║   ██║   █████╗   ╚███╔╝ 
//    ╚════██║██╔═══╝ ██║██║╚██╗██║██║     ██║   ██║██║     ██╔═██╗     ██║╚██╔╝██║██║   ██║   ██║   ██╔══╝   ██╔██╗ 
//    ███████║██║     ██║██║ ╚████║███████╗╚██████╔╝╚██████╗██║  ██╗    ██║ ╚═╝ ██║╚██████╔╝   ██║   ███████╗██╔╝ ██╗
//    ╚══════╝╚═╝     ╚═╝╚═╝  ╚═══╝╚══════╝ ╚═════╝  ╚═════╝╚═╝  ╚═╝    ╚═╝     ╚═╝ ╚═════╝    ╚═╝   ╚══════╝╚═╝  ╚═╝

// Reference: https://rigtorp.se/spinlock/
// TODO (consider): https://www.intel.com/content/www/us/en/developer/articles/technical/a-common-construct-to-avoid-the-contention-of-threads-architecture-agnostic-spin-wait-loops.html
// Another good reference code: https://github.dev/concurrencykit/ck
void SpinLockMutex::Enter()
{
    while (Atomic::ExchangeExplicit(&mLocked, 1, AtomicMemoryOrder::Acquire) == 1) {
        uint32 spinCount = 1;
        do {
            if (spinCount++ & 1023)
                OS::PauseCPU();
            else
                Thread::SwitchContext();
        } while (Atomic::LoadExplicit(&mLocked, AtomicMemoryOrder::Relaxed));
    }
}

void SpinLockMutex::Exit()
{
    Atomic::StoreExplicit(&mLocked, 0, AtomicMemoryOrder::Release);
}

bool SpinLockMutex::TryEnter()
{
    return Atomic::LoadExplicit(&mLocked, AtomicMemoryOrder::Relaxed) == 0 &&
           Atomic::ExchangeExplicit(&mLocked, 1, AtomicMemoryOrder::Acquire) == 0;
}


