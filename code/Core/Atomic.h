#pragma once

#include "Base.h"

#include "External/c89atomic/c89atomic.h"

using atomicUint32 = c89atomic_uint32;
using atomicUint64 = c89atomic_uint64;
#if ARCH_32BIT
using atomicPtr = c89atomic_uint32;
#elif ARCH_64BIT
using atomicPtr = c89atomic_uint64;
#endif

// Note: AtomicLock is declared in Base.h

enum class AtomicMemoryOrder : uint32
{
    Relaxed = c89atomic_memory_order_relaxed,
    Consume = c89atomic_memory_order_consume,
    Acquire = c89atomic_memory_order_acquire,
    Release = c89atomic_memory_order_release,
    Acqrel  = c89atomic_memory_order_acq_rel,
    Seqcst  = c89atomic_memory_order_seq_cst
};

// Atomic SpinLock
FORCE_INLINE void atomicLockEnter(AtomicLock* lock);
FORCE_INLINE void atomicLockExit(AtomicLock* lock);
FORCE_INLINE bool atomicLockTryEnter(AtomicLock* lock);

#ifdef __cplusplus
struct AtomicLockScope
{
    AtomicLockScope() = delete;
    AtomicLockScope(const AtomicLockScope&) = delete;
    inline explicit AtomicLockScope(AtomicLock& lock) : mLock(lock) { atomicLockEnter(&mLock); }
    inline ~AtomicLockScope() { atomicLockExit(&mLock); }
        
private:
    AtomicLock& mLock;
};
#endif

FORCE_INLINE void atomicPauseCpu(void);
FORCE_INLINE uint64 atomicCycleClock(void);

FORCE_INLINE void atomicThreadFence(AtomicMemoryOrder order);
FORCE_INLINE void atomicSignalFence(AtomicMemoryOrder order);

FORCE_INLINE uint32 atomicLoad32(atomicUint32* a);
FORCE_INLINE uint64 atomicLoad64(atomicUint64* a);
FORCE_INLINE void atomicStore32(atomicUint32* a, uint32 b);
FORCE_INLINE void atomicStore64(atomicUint64* a, uint64 b);

FORCE_INLINE uint32 atomicLoad32Explicit(atomicUint32* a, AtomicMemoryOrder order);
FORCE_INLINE uint64 atomicLoad64Explicit(atomicUint64* a, AtomicMemoryOrder order);
FORCE_INLINE void atomicStore32Explicit(atomicUint32* a, uint32 b, AtomicMemoryOrder order);
FORCE_INLINE void atomicStore64Explicit(atomicUint64* a, uint64 b, AtomicMemoryOrder order);

FORCE_INLINE uint32 atomicFetchAdd32(atomicUint32* a, uint32 b);
FORCE_INLINE uint32 atomicFetchSub32(atomicUint32* a, uint32 b);
FORCE_INLINE uint32 atomicFetchOr32(atomicUint32* a, uint32 b);
FORCE_INLINE uint32 atomicFetchAnd32(atomicUint32* a, uint32 b);
FORCE_INLINE uint32 atomicExchange32(atomicUint32* a, uint32 b);
FORCE_INLINE bool atomicCompareExchange32Weak(atomicUint32* a, atomicUint32* expected, uint32 desired);
FORCE_INLINE bool atomicCompareExchange32Strong(atomicUint32* a, atomicUint32* expected, uint32 desired);

FORCE_INLINE uint32 atomicFetchAdd32Explicit(atomicUint32* a, uint32 b, AtomicMemoryOrder order);
FORCE_INLINE uint32 atomicFetchSub32Explicit(atomicUint32* a, uint32 b, AtomicMemoryOrder order);
FORCE_INLINE uint32 atomicFetchOr32Explicit(atomicUint32* a, uint32 b, AtomicMemoryOrder order);
FORCE_INLINE uint32 atomicFetchAnd32Explicit(atomicUint32* a, uint32 b, AtomicMemoryOrder order);
FORCE_INLINE uint32 atomicExchange32Explicit(atomicUint32* a, uint32 b, AtomicMemoryOrder order);
FORCE_INLINE bool atomicCompareExchange32WeakExplicit(
    atomicUint32* a, uint32* expected, uint32 desired,
    AtomicMemoryOrder success, AtomicMemoryOrder fail);
FORCE_INLINE bool atomicCompareExchange32StrongExplicit(
    atomicUint32* a, uint32* expected, uint32 desired,
    AtomicMemoryOrder success, AtomicMemoryOrder fail);

FORCE_INLINE uint64 atomicFetchAdd64(atomicUint64* a, uint64 b);
FORCE_INLINE uint64 atomicFetchSub64(atomicUint64* a, uint64 b);
FORCE_INLINE uint64 atomicExchange64(atomicUint64* a, uint64 b);
FORCE_INLINE uint64 atomicFetchOr64(atomicUint64* a, uint64 b);
FORCE_INLINE uint64 atomicFetchAnd64(atomicUint64* a, uint64 b);
FORCE_INLINE bool atomicCompareExchange64Weak(atomicUint64* a, unsigned long long* expected, uint64 desired);
FORCE_INLINE bool atomicCompareExchange64Strong(atomicUint64* a, unsigned long long* expected, uint64 desired);

FORCE_INLINE uint64 atomicFetchAdd64Explicit(atomicUint64* a, uint64 b, AtomicMemoryOrder order);
FORCE_INLINE uint64 atomicFetchSub64Explicit(atomicUint64* a, uint64 b, AtomicMemoryOrder order);
FORCE_INLINE uint64 atomicExchange64Explicit(atomicUint64* a, uint64 b, AtomicMemoryOrder order);
FORCE_INLINE uint64 atomicFetchOr64Explicit(atomicUint64* a, uint64 b, AtomicMemoryOrder order);
FORCE_INLINE uint64 atomicFetchAnd64Explicit(atomicUint64* a, uint64 b, AtomicMemoryOrder order);
FORCE_INLINE bool atomicCompareExchange64WeakExplicit(
    atomicUint64* a, unsigned long long* expected, uint64 desired,
    AtomicMemoryOrder success, AtomicMemoryOrder fail);
FORCE_INLINE bool atomicCompareExchange64StrongExplicit(
    atomicUint64* a, unsigned long long* expected, uint64 desired,
    AtomicMemoryOrder success, AtomicMemoryOrder fail);

////////////////////////////////////////////////////////////////////////////////////////////////////
#if PLATFORM_WINDOWS
#    if ARCH_32BIT && CPU_X86
#       if !COMPILER_MSVC
#          include <x86intrin.h>
#       endif
#    endif
#    include <intrin.h>
#    if COMPILER_MSVC
#        pragma intrinsic(_mm_pause)
#        pragma intrinsic(__rdtsc)
#    endif
#elif PLATFORM_APPLE
#    include <mach/mach_time.h>
#endif

#if !PLATFORM_WINDOWS
#   include <sys/time.h>
#endif

#if defined(__SSE2__)
#    include <emmintrin.h>    // _mm_pause
#endif

#if CPU_ARM
    #include <arm_acle.h>     // __yield
#endif 

FORCE_INLINE void atomicPauseCpu()
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
FORCE_INLINE uint64 atomicCycleClock()
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
#   if (__ARM_ARCH >= 6)
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
#   endif // (__ARM_ARCH >= 6)
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

FORCE_INLINE void atomicThreadFence(AtomicMemoryOrder order)
{
    c89atomic_thread_fence(static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE void atomicSignalFence(AtomicMemoryOrder order)
{
    c89atomic_signal_fence(static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint32 atomicLoad32(atomicUint32* a)
{
    return c89atomic_load_32(a);
}

FORCE_INLINE uint64 atomicLoad64(atomicUint64* a)
{
    return c89atomic_load_64(a);
}

FORCE_INLINE void atomicStore32(atomicUint32* a, uint32 b)
{
    c89atomic_store_32(a, b);
}

FORCE_INLINE void atomicStore64(atomicUint64* a, uint64 b)
{
    c89atomic_store_64(a, b); 
}

FORCE_INLINE uint32 atomicLoad32Explicit(atomicUint32* a, AtomicMemoryOrder order)
{
    return c89atomic_load_explicit_32(a, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint64 atomicLoad64Explicit(atomicUint64* a, AtomicMemoryOrder order)
{
    return c89atomic_load_explicit_64(a, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE void atomicStore32Explicit(atomicUint32* a, uint32 b, AtomicMemoryOrder order)
{
    c89atomic_store_explicit_32(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE void atomicStore64Explicit(atomicUint64* a, uint64 b, AtomicMemoryOrder order)
{
    c89atomic_store_explicit_64(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint32 atomicFetchAdd32(atomicUint32* a, uint32 b)
{
    return c89atomic_fetch_add_32(a, b);
}

FORCE_INLINE uint32 atomicFetchSub32(atomicUint32* a, uint32 b)
{
    return c89atomic_fetch_sub_32(a, b);
}

FORCE_INLINE uint32 atomicFetchOr32(atomicUint32* a, uint32 b)
{
    return c89atomic_fetch_or_32(a, b);
}

FORCE_INLINE uint32 atomicFetchAnd32(atomicUint32* a, uint32 b)
{
    return c89atomic_fetch_and_32(a, b);
}

FORCE_INLINE uint32 atomicExchange32(atomicUint32* a, uint32 b)
{
    return c89atomic_exchange_32(a, b);
}

FORCE_INLINE bool atomicCompareExchange32Weak(atomicUint32* a, atomicUint32* expected, uint32 desired)
{
    return c89atomic_compare_exchange_weak_32(a, expected, desired);
}

FORCE_INLINE bool atomicCompareExchange32Strong(atomicUint32* a, atomicUint32* expected, uint32 desired)
{
    return c89atomic_compare_exchange_strong_32(a, expected, desired);
}

FORCE_INLINE uint32 atomicFetchAdd32Explicit(atomicUint32* a, uint32 b, AtomicMemoryOrder order)
{
    return c89atomic_fetch_add_explicit_32(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint32 atomicFetchSub32Explicit(atomicUint32* a, uint32 b, AtomicMemoryOrder order)
{
    return c89atomic_fetch_sub_explicit_32(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint32 atomicFetchOr32Explicit(atomicUint32* a, uint32 b, AtomicMemoryOrder order)
{
    return c89atomic_fetch_or_explicit_32(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint32 atomicFetchAnd32Explicit(atomicUint32* a, uint32 b, AtomicMemoryOrder order)
{
    return c89atomic_fetch_and_explicit_32(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint32 atomicExchange32Explicit(atomicUint32* a, uint32 b, AtomicMemoryOrder order)
{
    return c89atomic_exchange_explicit_32(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE bool atomicCompareExchange32WeakExplicit(
    atomicUint32* a, uint32* expected, uint32 desired,
    AtomicMemoryOrder success, AtomicMemoryOrder fail)
{
    return c89atomic_compare_exchange_weak_explicit_32(a, expected, desired, 
        static_cast<c89atomic_memory_order>(success), 
        static_cast<c89atomic_memory_order>(fail));
}

FORCE_INLINE bool atomicCompareExchange32StrongExplicit(
    atomicUint32* a, uint32* expected, uint32 desired,
    AtomicMemoryOrder success, AtomicMemoryOrder fail)
{
    return c89atomic_compare_exchange_strong_explicit_32(a, expected, desired, 
        static_cast<c89atomic_memory_order>(success), 
        static_cast<c89atomic_memory_order>(fail));
}

FORCE_INLINE uint64 atomicFetchAdd64(atomicUint64* a, uint64 b)
{
    return c89atomic_fetch_add_64(a, b);
}

FORCE_INLINE uint64 atomicFetchSub64(atomicUint64* a, uint64 b)
{
    return c89atomic_fetch_sub_64(a, b);
}

FORCE_INLINE uint64 atomicExchange64(atomicUint64* a, uint64 b)
{
    return c89atomic_exchange_64(a, b);
}

FORCE_INLINE uint64 atomicFetchOr64(atomicUint64* a, uint64 b)
{
    return c89atomic_fetch_or_64(a, b);
}

FORCE_INLINE uint64 atomicFetchAnd64(atomicUint64* a, uint64 b)
{
    return c89atomic_fetch_and_64(a, b);
}

FORCE_INLINE bool atomicCompareExchange64Weak(atomicUint64* a, unsigned long long* expected, uint64 desired)
{
    return c89atomic_compare_exchange_weak_64(a, expected, desired);
}

FORCE_INLINE bool atomicCompareExchange64Strong(atomicUint64* a, unsigned long long* expected, uint64 desired)
{
    return c89atomic_compare_exchange_strong_64(a, expected, desired);
}

FORCE_INLINE uint64 atomicFetchAdd64Explicit(atomicUint64* a, uint64 b, AtomicMemoryOrder order)
{
    return c89atomic_fetch_add_explicit_64(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint64 atomicFetchSub64Explicit(atomicUint64* a, uint64 b, AtomicMemoryOrder order)
{
    return c89atomic_fetch_sub_explicit_64(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint64 atomicExchange64Explicit(atomicUint64* a, uint64 b, AtomicMemoryOrder order)
{
    return c89atomic_exchange_explicit_64(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint64 atomicFetchOr64Explicit(atomicUint64* a, uint64 b, AtomicMemoryOrder order)
{
    return c89atomic_fetch_or_explicit_64(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint64 atomicFetchAnd64Explicit(atomicUint64* a, uint64 b, AtomicMemoryOrder order)
{
    return c89atomic_fetch_and_explicit_64(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE bool atomicCompareExchange64WeakExplicit(
    atomicUint64* a, unsigned long long* expected, uint64 desired,
    AtomicMemoryOrder success, AtomicMemoryOrder fail)
{
    return c89atomic_compare_exchange_weak_explicit_64(a, expected, desired, 
        static_cast<c89atomic_memory_order>(success), 
        static_cast<c89atomic_memory_order>(fail));
}

FORCE_INLINE bool atomicCompareExchange64StrongExplicit(
    atomicUint64* a, unsigned long long* expected, uint64 desired,
    AtomicMemoryOrder success, AtomicMemoryOrder fail)
{
    return c89atomic_compare_exchange_strong_explicit_64(a, expected, desired, 
        static_cast<c89atomic_memory_order>(success), 
        static_cast<c89atomic_memory_order>(fail));
}

#if ARCH_64BIT
    #define atomicLoadPtr atomicLoad64
    #define atomicStorePtr atomicStore64
    #define atomicLoadPtrExplicit atomicLoad64Explicit
    #define atomicStorePtrExplicit atomicStore64Explicit
    #define atomicFetchAddPtr atomicFetchAdd64
    #define atomicFetchSubPtr atomicFetchSub64
    #define atomicFetchOrPtr atomicFetchOr64
    #define atomicExchangePtr atomicExchange64
    #define atomicCompareExchangePtrWeak atomicCompareExchange64Weak
    #define atomicCompareExchangePtrStrong atomicCompareExchange64Strong
    #define atomicFetchAddPtrExplicit atomicFetchAdd64Explicit
    #define atomicFetchSubPtrExplicit atomicFetchSub64Explicit
    #define atomicFetchOrPtrExplicit atomicFetchOr64Explicit
    #define atomicFetchAndPtrExplicit atomicFetchAnd64Explicit
    #define atomicExchangePtrExplicit atomicExchange64Explicit
    #define atomicCompareExchangePtrWeakExplicit atomicCompareExchange64WeakExplicit
    #define atomicCompareExchangePtrStrongExplicit atomicCompareExchange64StrongExplicit
#else
    #define atomicLoadPtr atomicLoad32
    #define atomicStorePtr atomicStore32
    #define atomicLoadPtrExplicit atomicLoad32Explicit
    #define atomicStorePtrExplicit atomicStore32Explicit
    #define atomicFetchAddPtr atomicFetchAdd32
    #define atomicFetchSubPtr atomicFetchSub32
    #define atomicFetchOrPtr atomicFetchOr32
    #define atomicExchangePtr atomicExchange32
    #define atomicCompareExchangePtrWeak atomicCompareExchange32Weak
    #define atomicCompareExchangePtrStrong atomicCompareExchange32Strong
    #define atomicFetchAddPtrExplicit atomicFetchAdd32Explicit
    #define atomicFetchSubPtrExplicit atomicFetchSub32Explicit
    #define atomicFetchOrPtrExplicit atomicFetchOr32Explicit
    #define atomicFetchAndPtrExplicit atomicFetchAnd32Explicit
    #define atomicExchangePtrExplicit atomicExchange32Explicit
    #define atomicCompareExchangePtrWeakExplicit atomicCompareExchange32WeakExplicit
    #define atomicCompareExchangePtrStrongExplicit atomicCompareExchange32StrongExplicit
#endif    // ARCH_64BIT

// Reference: https://rigtorp.se/spinlock/
// TODO (consider): https://www.intel.com/content/www/us/en/developer/articles/technical/a-common-construct-to-avoid-the-contention-of-threads-architecture-agnostic-spin-wait-loops.html
// Another good reference code: https://github.dev/concurrencykit/ck
void threadYield(); // System.h
FORCE_INLINE void atomicLockEnter(AtomicLock* lock)
{
    while (atomicExchange32Explicit(&lock->locked, 1, AtomicMemoryOrder::Acquire) == 1) {
        uint32 spinCount = !PLATFORM_MOBILE;    // On mobile hardware, we start from yielding then proceed with Pause
        do {
            if (spinCount++ & 1023)
                atomicPauseCpu();
            else
                threadYield();
        } while (atomicLoad32Explicit(&lock->locked, AtomicMemoryOrder::Relaxed));
    }
}

FORCE_INLINE void atomicLockExit(AtomicLock* lock)
{
    atomicStore32Explicit(&lock->locked, 0, AtomicMemoryOrder::Release);
}

FORCE_INLINE bool atomicLockTryEnter(AtomicLock* lock)
{
    return atomicLoad32Explicit(&lock->locked, AtomicMemoryOrder::Relaxed) == 0 &&
           atomicExchange32Explicit(&lock->locked, 1, AtomicMemoryOrder::Acquire) == 0;
}

//----------------------------------------------------------------------------------------------------------------------
// Anderson's lock: Implementation is a modified form of: https://github.com/concurrencykit/ck/blob/master/include/spinlock/anderson.h
struct alignas(CACHE_LINE_SIZE) AtomicALockThread
{
    uint32 locked;
    uint8 _padding1[CACHE_LINE_SIZE - sizeof(uint32)];
    uint32 position;
    uint8 _padding2[CACHE_LINE_SIZE - sizeof(uint32)];
};

struct alignas(CACHE_LINE_SIZE) AtomicALock
{
    AtomicALockThread* slots;
    uint32 count;
    uint32 wrap;
    uint32 mask;
    char _padding1[CACHE_LINE_SIZE - sizeof(uint32)*3 - sizeof(void*)];
    uint32 next;
    char _padding2[CACHE_LINE_SIZE - sizeof(uint32)];
};

FORCE_INLINE void atomicALockInitialize(AtomicALock* lock, uint32 numThreads, AtomicALockThread* threads)
{
    ASSERT(threads);
    ASSERT(numThreads);

    memset(threads, 0x0, sizeof(AtomicALockThread)*numThreads);
    lock->slots = threads;

    for (uint32 i = 1; i < numThreads; i++) {
        lock->slots[i].locked = 1;
        lock->slots[i].position = i;
    }

    lock->count = numThreads;
    lock->mask = numThreads - 1;

    if (numThreads & (numThreads - 1)) 
        lock->wrap = (UINT32_MAX % numThreads) + 1;
    else
        lock->wrap = 0;

    c89atomic_compiler_fence();
}

FORCE_INLINE uint32 atomicALockEnter(AtomicALock* lock)
{
    uint32 position, next;
    uint32 count = lock->count;

    if (lock->wrap) {
        position = c89atomic_load_explicit_32(&lock->next, c89atomic_memory_order_acquire);

        do {
            if (position == UINT32_MAX)
                next = lock->wrap;
            else 
                next = position + 1;
        } while (c89atomic_compare_exchange_strong_32(&lock->next, &position, next) == false);

        position %= count;
    } else {
        position = c89atomic_fetch_add_32(&lock->next, 1);
        position &= lock->mask;
    }

    c89atomic_thread_fence(c89atomic_memory_order_acq_rel);

    while (c89atomic_load_explicit_32(&lock->slots[position].locked, c89atomic_memory_order_acquire))
        atomicPauseCpu();

    c89atomic_store_explicit_32(&lock->slots[position].locked, 1, c89atomic_memory_order_release);

    return position;
}

FORCE_INLINE void atomicALockExit(AtomicALock* lock, uint32 slot)
{
    uint32 position;

    c89atomic_thread_fence(c89atomic_memory_order_acq_rel);

    if (lock->wrap == 0)
        position = (lock->slots[slot].position + 1) & lock->mask;
    else
        position = (lock->slots[slot].position + 1) % lock->count;

    c89atomic_store_explicit_32(&lock->slots[position].locked, 0, c89atomic_memory_order_release);
}

struct AtomicALockScope
{
    AtomicALockScope() = delete;
    AtomicALockScope(const AtomicALockScope& _lock) = delete;
    inline explicit AtomicALockScope(AtomicALock& _lock) : lock(_lock) { slot = atomicALockEnter(&lock); }
    inline ~AtomicALockScope() { atomicALockExit(&lock, slot); }
        
private:
    AtomicALock& lock;
    uint32 slot;
};
