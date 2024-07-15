#pragma once

#include "Base.h"

#include "External/c89atomic/c89atomic.h"

using AtomicUint32 = c89atomic_uint32;
using AtomicUint64 = c89atomic_uint64;
#if ARCH_32BIT
using AtomicPtr = c89atomic_uint32;
#elif ARCH_64BIT
using AtomicPtr = c89atomic_uint64;
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

namespace Atomic 
{
    FORCE_INLINE void ThreadFence(AtomicMemoryOrder order);
    FORCE_INLINE void SignalFence(AtomicMemoryOrder order);

    FORCE_INLINE uint32 Load(AtomicUint32* a);
    FORCE_INLINE uint64 Load(AtomicUint64* a);
    FORCE_INLINE uint32 LoadExplicit(AtomicUint32* a, AtomicMemoryOrder order);
    FORCE_INLINE uint64 LoadExplicit(AtomicUint64* a, AtomicMemoryOrder order);
    
    FORCE_INLINE void Store(AtomicUint32* a, uint32 b);
    FORCE_INLINE void Store(AtomicUint64* a, uint64 b);
    FORCE_INLINE void StoreExplicit(AtomicUint32* a, uint32 b, AtomicMemoryOrder order);
    FORCE_INLINE void StoreExplicit(AtomicUint64* a, uint64 b, AtomicMemoryOrder order);

    FORCE_INLINE uint32 FetchAdd(AtomicUint32* a, uint32 b);
    FORCE_INLINE uint64 FetchAdd(AtomicUint64* a, uint64 b);
    FORCE_INLINE uint32 FetchAddExplicit(AtomicUint32* a, uint32 b, AtomicMemoryOrder order);
    FORCE_INLINE uint64 FetchAddExplicit(AtomicUint64* a, uint64 b, AtomicMemoryOrder order);
    
    FORCE_INLINE uint32 FetchSub(AtomicUint32* a, uint32 b);
    FORCE_INLINE uint64 FetchSub(AtomicUint64* a, uint64 b);
    FORCE_INLINE uint32 FetchSubExplicit(AtomicUint32* a, uint32 b, AtomicMemoryOrder order);
    FORCE_INLINE uint64 FetchSubExplicit(AtomicUint64* a, uint64 b, AtomicMemoryOrder order);
    
    FORCE_INLINE uint32 FetchOR(AtomicUint32* a, uint32 b);
    FORCE_INLINE uint64 FetchOR(AtomicUint64* a, uint64 b);
    FORCE_INLINE uint32 FetchORExplicit(AtomicUint32* a, uint32 b, AtomicMemoryOrder order);
    FORCE_INLINE uint64 FetchORExplicit(AtomicUint64* a, uint64 b, AtomicMemoryOrder order);
    
    FORCE_INLINE uint32 FetchAND(AtomicUint32* a, uint32 b);
    FORCE_INLINE uint64 FetchAND(AtomicUint64* a, uint64 b);
    FORCE_INLINE uint32 FetchANDExplicit(AtomicUint32* a, uint32 b, AtomicMemoryOrder order);
    FORCE_INLINE uint64 FetchANDExplicit(AtomicUint64* a, uint64 b, AtomicMemoryOrder order);
    
    FORCE_INLINE uint32 Exchange(AtomicUint32* a, uint32 b);
    FORCE_INLINE uint64 Exchange(AtomicUint64* a, uint64 b);
    FORCE_INLINE uint64 ExchangeExplicit(AtomicUint64* a, uint64 b, AtomicMemoryOrder order);
    FORCE_INLINE uint32 ExchangeExplicit(AtomicUint32* a, uint32 b, AtomicMemoryOrder order);

    // If *expected == *a then *a = desired (read-modify-write op), returns true
    // If *expected != *a then *expected = *a (load op), returns false
    FORCE_INLINE bool CompareExchange_Weak(AtomicUint32* a, AtomicUint32* expected, uint32 desired);
    FORCE_INLINE bool CompareExchange_Strong(AtomicUint32* a, AtomicUint32* expected, uint32 desired);
    FORCE_INLINE bool CompareExchange_Weak(AtomicUint64* a, unsigned long long* expected, uint64 desired);
    FORCE_INLINE bool CompareExchange_Strong(AtomicUint64* a, unsigned long long* expected, uint64 desired);

    FORCE_INLINE bool CompareExchangeExplicit_Weak(AtomicUint32* a, uint32* expected, uint32 desired, 
                                                   AtomicMemoryOrder success, AtomicMemoryOrder fail);
    FORCE_INLINE bool CompareExchangeExplicit_Strong(AtomicUint32* a, uint32* expected, uint32 desired,
                                                     AtomicMemoryOrder success, AtomicMemoryOrder fail);
    FORCE_INLINE bool CompareExchangeExplicit_Weak(AtomicUint64* a, unsigned long long* expected, uint64 desired,
                                                   AtomicMemoryOrder success, AtomicMemoryOrder fail);
    FORCE_INLINE bool CompareExchangeExplicit_Strong(AtomicUint64* a, unsigned long long* expected, uint64 desired,
                                                     AtomicMemoryOrder success, AtomicMemoryOrder fail);
} // Atomic

//    ██╗███╗   ██╗██╗     ██╗███╗   ██╗███████╗███████╗
//    ██║████╗  ██║██║     ██║████╗  ██║██╔════╝██╔════╝
//    ██║██╔██╗ ██║██║     ██║██╔██╗ ██║█████╗  ███████╗
//    ██║██║╚██╗██║██║     ██║██║╚██╗██║██╔══╝  ╚════██║
//    ██║██║ ╚████║███████╗██║██║ ╚████║███████╗███████║
//    ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝╚═╝  ╚═══╝╚══════╝╚══════╝
FORCE_INLINE void Atomic::ThreadFence(AtomicMemoryOrder order)
{
    c89atomic_thread_fence(static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE void Atomic::SignalFence(AtomicMemoryOrder order)
{
    c89atomic_signal_fence(static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint32 Atomic::Load(AtomicUint32* a)
{
    return c89atomic_load_32(a);
}

FORCE_INLINE uint64 Atomic::Load(AtomicUint64* a)
{
    return c89atomic_load_64(a);
}

FORCE_INLINE void Atomic::Store(AtomicUint32* a, uint32 b)
{
    c89atomic_store_32(a, b);
}

FORCE_INLINE void Atomic::Store(AtomicUint64* a, uint64 b)
{
    c89atomic_store_64(a, b); 
}

FORCE_INLINE uint32 Atomic::LoadExplicit(AtomicUint32* a, AtomicMemoryOrder order)
{
    return c89atomic_load_explicit_32(a, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint64 Atomic::LoadExplicit(AtomicUint64* a, AtomicMemoryOrder order)
{
    return c89atomic_load_explicit_64(a, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE void Atomic::StoreExplicit(AtomicUint32* a, uint32 b, AtomicMemoryOrder order)
{
    c89atomic_store_explicit_32(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE void Atomic::StoreExplicit(AtomicUint64* a, uint64 b, AtomicMemoryOrder order)
{
    c89atomic_store_explicit_64(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint32 Atomic::FetchAdd(AtomicUint32* a, uint32 b)
{
    return c89atomic_fetch_add_32(a, b);
}

FORCE_INLINE uint32 Atomic::FetchSub(AtomicUint32* a, uint32 b)
{
    return c89atomic_fetch_sub_32(a, b);
}

FORCE_INLINE uint32 Atomic::FetchOR(AtomicUint32* a, uint32 b)
{
    return c89atomic_fetch_or_32(a, b);
}

FORCE_INLINE uint32 Atomic::FetchAND(AtomicUint32* a, uint32 b)
{
    return c89atomic_fetch_and_32(a, b);
}

FORCE_INLINE uint32 Atomic::Exchange(AtomicUint32* a, uint32 b)
{
    return c89atomic_exchange_32(a, b);
}

FORCE_INLINE bool Atomic::CompareExchange_Weak(AtomicUint32* a, AtomicUint32* expected, uint32 desired)
{
    return c89atomic_compare_exchange_weak_32(a, expected, desired);
}

FORCE_INLINE bool Atomic::CompareExchange_Strong(AtomicUint32* a, AtomicUint32* expected, uint32 desired)
{
    return c89atomic_compare_exchange_strong_32(a, expected, desired);
}

FORCE_INLINE uint32 Atomic::FetchAddExplicit(AtomicUint32* a, uint32 b, AtomicMemoryOrder order)
{
    return c89atomic_fetch_add_explicit_32(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint32 Atomic::FetchSubExplicit(AtomicUint32* a, uint32 b, AtomicMemoryOrder order)
{
    return c89atomic_fetch_sub_explicit_32(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint32 Atomic::FetchORExplicit(AtomicUint32* a, uint32 b, AtomicMemoryOrder order)
{
    return c89atomic_fetch_or_explicit_32(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint32 Atomic::FetchANDExplicit(AtomicUint32* a, uint32 b, AtomicMemoryOrder order)
{
    return c89atomic_fetch_and_explicit_32(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint32 Atomic::ExchangeExplicit(AtomicUint32* a, uint32 b, AtomicMemoryOrder order)
{
    return c89atomic_exchange_explicit_32(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE bool Atomic::CompareExchangeExplicit_Weak(
    AtomicUint32* a, uint32* expected, uint32 desired,
    AtomicMemoryOrder success, AtomicMemoryOrder fail)
{
    return c89atomic_compare_exchange_weak_explicit_32(a, expected, desired, 
        static_cast<c89atomic_memory_order>(success), 
        static_cast<c89atomic_memory_order>(fail));
}

FORCE_INLINE bool Atomic::CompareExchangeExplicit_Strong(
    AtomicUint32* a, uint32* expected, uint32 desired,
    AtomicMemoryOrder success, AtomicMemoryOrder fail)
{
    return c89atomic_compare_exchange_strong_explicit_32(a, expected, desired, 
        static_cast<c89atomic_memory_order>(success), 
        static_cast<c89atomic_memory_order>(fail));
}

FORCE_INLINE uint64 Atomic::FetchAdd(AtomicUint64* a, uint64 b)
{
    return c89atomic_fetch_add_64(a, b);
}

FORCE_INLINE uint64 Atomic::FetchSub(AtomicUint64* a, uint64 b)
{
    return c89atomic_fetch_sub_64(a, b);
}

FORCE_INLINE uint64 Atomic::Exchange(AtomicUint64* a, uint64 b)
{
    return c89atomic_exchange_64(a, b);
}

FORCE_INLINE uint64 Atomic::FetchOR(AtomicUint64* a, uint64 b)
{
    return c89atomic_fetch_or_64(a, b);
}

FORCE_INLINE uint64 Atomic::FetchAND(AtomicUint64* a, uint64 b)
{
    return c89atomic_fetch_and_64(a, b);
}

FORCE_INLINE bool Atomic::CompareExchange_Weak(AtomicUint64* a, unsigned long long* expected, uint64 desired)
{
    return c89atomic_compare_exchange_weak_64(a, expected, desired);
}

FORCE_INLINE bool Atomic::CompareExchange_Strong(AtomicUint64* a, unsigned long long* expected, uint64 desired)
{
    return c89atomic_compare_exchange_strong_64(a, expected, desired);
}

FORCE_INLINE uint64 Atomic::FetchAddExplicit(AtomicUint64* a, uint64 b, AtomicMemoryOrder order)
{
    return c89atomic_fetch_add_explicit_64(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint64 Atomic::FetchSubExplicit(AtomicUint64* a, uint64 b, AtomicMemoryOrder order)
{
    return c89atomic_fetch_sub_explicit_64(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint64 Atomic::ExchangeExplicit(AtomicUint64* a, uint64 b, AtomicMemoryOrder order)
{
    return c89atomic_exchange_explicit_64(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint64 Atomic::FetchORExplicit(AtomicUint64* a, uint64 b, AtomicMemoryOrder order)
{
    return c89atomic_fetch_or_explicit_64(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE uint64 Atomic::FetchANDExplicit(AtomicUint64* a, uint64 b, AtomicMemoryOrder order)
{
    return c89atomic_fetch_and_explicit_64(a, b, static_cast<c89atomic_memory_order>(order));
}

FORCE_INLINE bool Atomic::CompareExchangeExplicit_Weak(
    AtomicUint64* a, unsigned long long* expected, uint64 desired,
    AtomicMemoryOrder success, AtomicMemoryOrder fail)
{
    return c89atomic_compare_exchange_weak_explicit_64(a, expected, desired, 
        static_cast<c89atomic_memory_order>(success), 
        static_cast<c89atomic_memory_order>(fail));
}

FORCE_INLINE bool Atomic::CompareExchangeExplicit_Strong(
    AtomicUint64* a, unsigned long long* expected, uint64 desired,
    AtomicMemoryOrder success, AtomicMemoryOrder fail)
{
    return c89atomic_compare_exchange_strong_explicit_64(a, expected, desired, 
        static_cast<c89atomic_memory_order>(success), 
        static_cast<c89atomic_memory_order>(fail));
}

#if ARCH_64BIT
    #define atomicLoadPtr Atomic::Load
    #define atomicStorePtr Atomic::Store
    #define atomicLoadPtrExplicit Atomic::LoadExplicit
    #define atomicStorePtrExplicit Atomic::StoreExplicit
    #define atomicFetchAddPtr Atomic::FetchAdd
    #define atomicFetchSubPtr Atomic::FetchSub
    #define atomicFetchOrPtr Atomic::FetchOR
    #define atomicExchangePtr Atomic::Exchange
    #define atomicCompareExchangePtrWeak Atomic::CompareExchange_Weak
    #define atomicCompareExchangePtrStrong Atomic::CompareExchange_Strong
    #define atomicFetchAddPtrExplicit Atomic::FetchAddExplicit
    #define atomicFetchSubPtrExplicit Atomic::FetchSubExplicit
    #define atomicFetchOrPtrExplicit Atomic::FetchORExplicit
    #define atomicFetchAndPtrExplicit Atomic::FetchANDExplicit
    #define atomicExchangePtrExplicit Atomic::ExchangeExplicit
    #define atomicCompareExchangePtrWeakExplicit Atomic::CompareExchangeExplicit_Weak
    #define atomicCompareExchangePtrStrongExplicit Atomic::CompareExchangeExplicit_Strong
#else
    #define atomicLoadPtr Atomic::Load
    #define atomicStorePtr Atomic::Store
    #define atomicLoadPtrExplicit Atomic::LoadExplicit
    #define atomicStorePtrExplicit Atomic::StoreExplicit
    #define atomicFetchAddPtr Atomic::FetchAdd
    #define atomicFetchSubPtr Atomic::FetchSub
    #define atomicFetchOrPtr Atomic::FetchOR
    #define atomicExchangePtr Atomic::Exchange
    #define atomicCompareExchangePtrWeak Atomic::CompareExchange_Weak
    #define atomicCompareExchangePtrStrong Atomic::CompareExchange_Strong
    #define atomicFetchAddPtrExplicit Atomic::FetchAddExplicit
    #define atomicFetchSubPtrExplicit Atomic::FetchSubExplicit
    #define atomicFetchOrPtrExplicit Atomic::FetchORExplicit
    #define atomicFetchAndPtrExplicit Atomic::FetchANDExplicit
    #define atomicExchangePtrExplicit Atomic::ExchangeExplicit
0    #define atomicCompareExchangePtrWeakExplicit Atomic::CompareExchangeExplicit_Weak
    #define atomicCompareExchangePtrStrongExplicit Atomic::CompareExchangeExplicit_Strong
#endif    // ARCH_64BIT

