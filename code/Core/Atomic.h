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

enum class AtomicMemoryOrder : uint32
{
    Relaxed = c89atomic_memory_order_relaxed,
    Consume = c89atomic_memory_order_consume,
    Acquire = c89atomic_memory_order_acquire,
    Release = c89atomic_memory_order_release,
    Acqrel  = c89atomic_memory_order_acq_rel,
    Seqcst  = c89atomic_memory_order_seq_cst
};

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


//    ██╗███╗   ██╗██╗     ██╗███╗   ██╗███████╗███████╗
//    ██║████╗  ██║██║     ██║████╗  ██║██╔════╝██╔════╝
//    ██║██╔██╗ ██║██║     ██║██╔██╗ ██║█████╗  ███████╗
//    ██║██║╚██╗██║██║     ██║██║╚██╗██║██╔══╝  ╚════██║
//    ██║██║ ╚████║███████╗██║██║ ╚████║███████╗███████║
//    ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝╚═╝  ╚═══╝╚══════╝╚══════╝
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
0    #define atomicCompareExchangePtrWeakExplicit atomicCompareExchange32WeakExplicit
    #define atomicCompareExchangePtrStrongExplicit atomicCompareExchange32StrongExplicit
#endif    // ARCH_64BIT

