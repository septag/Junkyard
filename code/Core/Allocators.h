#pragma once

#include "Base.h"

#include <memory.h>		// memset, memcpy
#if PLATFORM_APPLE
#include <string.h>
#endif

template <typename _T, uint32 _Reserve>
struct Array;

// TODO: alloca is not a safe function to be used, change all alloca instances to memAllocTemp at some point
#if PLATFORM_WINDOWS
    #include <malloc.h> // _alloca
    #ifdef alloca	
        #undef alloca
    #endif
    #define alloca(_size) _alloca(_size)
#else
    #include <alloca.h>	// alloca
#endif

enum class AllocatorType
{
    Unknown,
    Heap,       // Normal malloc/free heap allocator
    Temp,       // Stack-based temp allocator. Grows by page. Only works within a single thread context and function scopes.
    LinearVM,   // Linear-based VM backed allocator. Fixed capacity. Grows page by page
    Budget,     // Linear-based budget allocator. Fixed capacity. Persists in memory in a higher lifetime
    Tlsf        // TLSF dynamic allocator. Fixed capacity. Persists in memory and usually used for subsystems with unknown memory allocation pattern.
};

struct NO_VTABLE Allocator
{
    virtual void* Malloc(size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) = 0;
    virtual void* Realloc(void* ptr, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) = 0;
    virtual void  Free(void* ptr, uint32 align = CONFIG_MACHINE_ALIGNMENT) = 0;
    virtual AllocatorType GetType() const = 0;
};

using MemFailCallback = void(*)(void* userData);

API void memSetFailCallback(MemFailCallback callback, void* userdata);
API void memRunFailCallback();
API void* memAlignPointer(void* ptr, size_t extra, uint32 align);
API Allocator* memDefaultAlloc();
API void memSetDefaultAlloc(Allocator* alloc);
API void memEnableMemPro(bool enable);

#define MEMORY_FAIL() do { memRunFailCallback(); ASSERT_ALWAYS(0, "Out of memory"); } while (0)

FORCE_INLINE void* memAlloc(size_t size, Allocator* alloc = memDefaultAlloc());
FORCE_INLINE void* memAllocZero(size_t size, Allocator* alloc = memDefaultAlloc());
FORCE_INLINE void* memRealloc(void* ptr, size_t size, Allocator* alloc = memDefaultAlloc());
FORCE_INLINE void  memFree(void* ptr, Allocator* alloc = memDefaultAlloc());

FORCE_INLINE void* memAllocAligned(size_t size, uint32 align, Allocator* alloc = memDefaultAlloc());
FORCE_INLINE void* memAllocZeroAligned(size_t size, uint32 align, Allocator* alloc = memDefaultAlloc());
FORCE_INLINE void* memReallocAligned(void* ptr, size_t size, uint32 align, Allocator* alloc = memDefaultAlloc());
FORCE_INLINE void  memFreeAligned(void* ptr, uint32 align, Allocator* alloc = memDefaultAlloc());

template<typename _T> _T* memAllocTyped(uint32 count = 1, Allocator* alloc = memDefaultAlloc());
template<typename _T> _T* memAllocZeroTyped(uint32 count = 1, Allocator* alloc = memDefaultAlloc());
template<typename _T> _T* memReallocTyped(void* ptr, uint32 count = 1, Allocator* alloc = memDefaultAlloc());
template<typename _T> _T* memAllocCopy(const _T* src, uint32 count = 1, Allocator* alloc = memDefaultAlloc());
template<typename _T> _T* memAllocCopyRawBytes(const _T* src, size_t sizeBytes, Allocator* alloc = memDefaultAlloc());

namespace _private
{
    struct MemDebugPointer
    {
        void* ptr;
        uint32 align;
    };
} // _private

struct MemTransientAllocatorStats
{
    size_t curPeak;
    size_t maxPeak;
    uint32 threadId;
    const char* threadName;
};

//------------------------------------------------------------------------
// Temp Allocator: Stack-based temp allocator. Grows by page. Only works within a single thread context.
using MemTempId = uint32;
API [[nodiscard]] MemTempId memTempPushId();
API void memTempPopId(MemTempId id);
API void memTempSetDebugMode(bool enable);
API void memTempSetCaptureStackTrace(bool capture);
API void memTempGetStats(Allocator* alloc, MemTransientAllocatorStats** outStats, uint32* outCount);
API void memTempReset(float dt, bool resetValidation = true);

[[nodiscard]] void* memAllocTemp(MemTempId id, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT);
[[nodiscard]] void* memReallocTemp(MemTempId id, void* ptr, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT);
[[nodiscard]] void* memAllocTempZero(MemTempId id, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT);
template<typename _T> _T* memAllocTempTyped(MemTempId id, uint32 count = 1, uint32 align = CONFIG_MACHINE_ALIGNMENT);
template<typename _T> _T* memAllocTempZeroTyped(MemTempId id, uint32 count = 1, uint32 align = CONFIG_MACHINE_ALIGNMENT);

struct MemTempAllocator final : Allocator
{
    MemTempAllocator();
    explicit MemTempAllocator(MemTempId id);
    ~MemTempAllocator();
    inline operator MemTempId() const { return mId; }

    [[nodiscard]] void* Malloc(size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    [[nodiscard]] void* Realloc(void* ptr, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    void Free(void* ptr, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    AllocatorType GetType() const override { return AllocatorType::Temp; }

    size_t GetOffset() const;
    size_t GetPointerOffset(void* ptr) const;

    template <typename _T>
    [[nodiscard]] _T* MallocTyped(uint32 count = 1, uint32 align = CONFIG_MACHINE_ALIGNMENT) 
        { return reinterpret_cast<_T*>(memAllocTemp(mId, count*sizeof(_T), align)); }
    template <typename _T>
    [[nodiscard]] _T* MallocZeroTyped(uint32 count = 1, uint32 align = CONFIG_MACHINE_ALIGNMENT) 
        { return reinterpret_cast<_T*>(memAllocTempZero(mId, count*sizeof(_T), align)); }
    template <typename _T>
    [[nodiscard]] _T* ReallocTyped(_T* ptr, uint32 count = 1, uint32 align = CONFIG_MACHINE_ALIGNMENT)
        { return reinterpret_cast<_T*>(memReallocTemp(mId, ptr, count*sizeof(_T), align)); }

private:
    MemTempId mId = 0;
    uint16 mFiberProtectorId = 0;
    bool mOwnsId = false;
};

//------------------------------------------------------------------------
// Linear virtual-mem allocator: Linear-based allocator backed by VMem. Grows by page size. reserve a large size upfront
struct MemLinearVMAllocator final : Allocator
{
    void Initialize(size_t reserveSize, size_t pageSize, bool debugMode = false);
    void Release();
    void Reset();

    [[nodiscard]] void* Malloc(size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    [[nodiscard]] void* Realloc(void* ptr, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    void  Free(void* ptr, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    AllocatorType GetType() const override { return AllocatorType::Budget; }

    size_t GetReservedSize() const { return mReserveSize; }
    size_t GetAllocatedSize() const { return mOffset; }
    size_t GetCommitedSize() const { return mCommitSize; }

    uint8* myBuffer = nullptr;
    size_t mCommitSize = 0;
    size_t mOffset = 0;
    size_t mPageSize = 0;
    size_t mReserveSize = 0;
    void* mLastAllocatedPtr = 0;
    Array<_private::MemDebugPointer, 8>* mDebugPointers = nullptr;
    bool mDebugMode = false;
};

//------------------------------------------------------------------------
// Budget allocator: Linear-based budget allocator. Fixed capacity. Persists in memory in a higher lifetime
//  NOTE: If you attempt to realloc a pointer with budget allocator, you will get an ASSERT failure, because Budget allocators are not meant for that
struct MemBudgetAllocator final : Allocator
{
    explicit MemBudgetAllocator(const char* name);

    void Initialize(size_t sizeBudget, size_t pageSize, bool commitAll, bool debugMode = false);
    void Release();

    [[nodiscard]] void* Malloc(size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    [[nodiscard]] void* Realloc(void* ptr, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    void  Free(void* ptr, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    AllocatorType GetType() const override { return AllocatorType::Budget; }

    size_t GetCommitedSize() const { return mCommitSize; }
    size_t GetTotalSize() const { return mMaxSize; }
    size_t GetOffset() const { return mOffset; }

private:
    uint8* mBuffer = nullptr;
    size_t mMaxSize = 0;
    size_t mCommitSize = 0;
    size_t mOffset = 0;
    size_t mPageSize = 0;   
    char   mName[32];
    bool   mDebugMode = false;
    Array<_private::MemDebugPointer, 8>* mDebugPointers = nullptr;
};

//------------------------------------------------------------------------
// TLSF dynamic allocator: Fixed capacity. Persists in memory and usually used for subsystems with unknown memory allocation pattern.
struct MemTlsfAllocator : Allocator
{
    static size_t GetMemoryRequirement(size_t poolSize);

    void Initialize(size_t poolSize, void* buffer, size_t size, bool debugMode = false);
    void Release();

    [[nodiscard]] void* Malloc(size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    [[nodiscard]] void* Realloc(void* ptr, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    void  Free(void* ptr, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    AllocatorType GetType() const override { return AllocatorType::Tlsf; }

    size_t GetAllocatedSize() const { return mAllocatedSize; }

    float CalculateFragmentation();
    bool Validate();
    bool IsDebugMode() const { return mDebugMode; }

protected:
    size_t mAllocatedSize = 0;
    void*  mTlsf = nullptr;
    size_t mTlsfSize = 0;
    bool   mDebugMode = false;
};

struct MemTlsfAllocator_ThreadSafe final : MemTlsfAllocator
{
    [[nodiscard]] void* Malloc(size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    [[nodiscard]] void* Realloc(void* ptr, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    void  Free(void* ptr, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;

protected:
    AtomicLock mLock;    
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// inline implementation
[[nodiscard]] FORCE_INLINE void* memAlloc(size_t size, Allocator* alloc)
{
    ASSERT(alloc);
    void* ptr = alloc->Malloc(size, CONFIG_MACHINE_ALIGNMENT);
    if (ptr == NULL) {
        MEMORY_FAIL();
    }
    return ptr;
}

[[nodiscard]] FORCE_INLINE void* memAllocZero(size_t size, Allocator* alloc)
{
    ASSERT(alloc);
    void* ptr = alloc->Malloc(size, CONFIG_MACHINE_ALIGNMENT);
    if (ptr == NULL) {
        MEMORY_FAIL();
    }
    memset(ptr, 0x0, size);
    return ptr;
}

[[nodiscard]] FORCE_INLINE void* memRealloc(void* ptr, size_t size, Allocator* alloc)
{
    ASSERT(alloc);
    ptr = alloc->Realloc(ptr, size, CONFIG_MACHINE_ALIGNMENT);
    if (ptr == NULL) {
        MEMORY_FAIL();
    }
    return ptr;
}

FORCE_INLINE void memFree(void* ptr, Allocator* alloc)
{
    ASSERT(alloc);
    alloc->Free(ptr, CONFIG_MACHINE_ALIGNMENT);
}

[[nodiscard]] FORCE_INLINE void* memAllocAligned(size_t size, uint32 align, Allocator* alloc)
{
    ASSERT(alloc);
    void* ptr = alloc->Malloc(size, align);
    if (ptr == NULL) {
        MEMORY_FAIL();
    }
    return ptr;
}

[[nodiscard]] FORCE_INLINE void* memAllocZeroAligned(size_t size, uint32 align, Allocator* alloc)
{
    ASSERT(alloc);
    void* ptr = alloc->Malloc(size, align);
    if (ptr == NULL) {
        MEMORY_FAIL();
        return nullptr;
    }
    memset(ptr, 0x0, size);
    return ptr;
}

[[nodiscard]] FORCE_INLINE void* memReallocAligned(void* ptr, size_t size, uint32 align, Allocator* alloc)
{
    ASSERT(alloc);
    ptr = alloc->Realloc(ptr, size, align);
    if (ptr == NULL) {
        MEMORY_FAIL();
    }
    return ptr;

}

FORCE_INLINE void memFreeAligned(void* ptr, uint32 align, Allocator* alloc)
{
    ASSERT(alloc);
    alloc->Free(ptr, align);
}

template<typename _T>
[[nodiscard]] inline _T* memAllocTyped(uint32 count, Allocator* alloc)
{
    return reinterpret_cast<_T*>(memAlloc(sizeof(_T)*count, alloc));
}

template<typename _T>
[[nodiscard]] inline _T* memAllocZeroTyped(uint32 count, Allocator* alloc)
{
    return reinterpret_cast<_T*>(memAllocZero(sizeof(_T)*count, alloc));
}

template<typename _T>
[[nodiscard]] inline _T* memReallocTyped(void* ptr, uint32 count, Allocator* alloc)
{
    return reinterpret_cast<_T*>(memRealloc(ptr, sizeof(_T)*count, alloc));
}

template<typename _T> 
[[nodiscard]] inline _T* memAllocCopy(const _T* src, uint32 count, Allocator* alloc)
{
    if (count == 0) {
        ASSERT(0);
        return nullptr;
    }

    auto buff = memAllocTyped<_T>(count, alloc);
    if (buff) {
        memcpy(buff, src, sizeof(_T)*count);
        return buff;
    }
    else {
        return nullptr;
    }
}

template<typename _T> 
[[nodiscard]] inline _T* memAllocCopyRawBytes(const _T* src, size_t sizeBytes, Allocator* alloc)
{
    if (sizeBytes == 0) {
        ASSERT(0);
        return nullptr;
    }

    auto buff = (_T*)memAlloc(sizeBytes, alloc);
    if (buff) {
        memcpy(buff, src, sizeBytes);
        return buff;
    }
    else {
        return nullptr;
    }
}

template<typename _T> 
[[nodiscard]] _T* memAllocTempTyped(MemTempId id, uint32 count, uint32 align)
{
    return reinterpret_cast<_T*>(memAllocTemp(id, count*sizeof(_T), align));
}

template<typename _T> 
[[nodiscard]] _T* memAllocTempZeroTyped(MemTempId id, uint32 count, uint32 align)
{
    return reinterpret_cast<_T*>(memAllocTempZero(id, count*sizeof(_T), align));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// new/delete overrides
namespace _private
{
    struct PlacementNewTag {};
}

#define PLACEMENT_NEW(_ptr, _type) ::new(_private::PlacementNewTag(), _ptr) _type
#define NEW(_alloc, _type) PLACEMENT_NEW(memAlloc(sizeof(_type), _alloc), _type)
#define ALIGNED_NEW(_alloc, _type, _align) PLACEMENT_NEW(memAllocAligned(sizeof(_type), _align, _alloc), _type)

#define PLACEMENT_NEW_ARRAY(_ptr, _type, _n) new(_private::PlacementNewTag(), _ptr) _type[_n]
#define NEW_ARRAY(_alloc, _type, _n) PLACEMENT_NEW_ARRAY(memAlloc(sizeof(_type)*_n, _alloc), _type, _n)

inline void* operator new(size_t, _private::PlacementNewTag, void* _ptr) { return _ptr; }
inline void* operator new[](size_t, _private::PlacementNewTag, void* _ptr) { return _ptr; }
inline void  operator delete(void*, _private::PlacementNewTag, void*) throw() {}

