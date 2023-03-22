#pragma once

#include "Base.h"

#include <memory.h>		// memset, memcpy

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
    Frame,      // Linear-based frame allocator. Grows by page. Resets every frame. Only works within a single thread context.
    Budget,     // Linear-based budget allocator. Fixed capacity. Persists in memory in a higher lifetime
    Tlsf        // TLSF dynamic allocator. Fixed capacity. Persists in memory and usually used for subsystems with unknown memory allocation pattern.
};

struct NO_VTABLE Allocator
{
    virtual void* Malloc(size_t size, uint32 align) = 0;
    virtual void* Realloc(void* ptr, size_t size, uint32 align) = 0;
    virtual void  Free(void* ptr, uint32 align) = 0;
    virtual AllocatorType GetType() const = 0;
};

using MemFailCallback = void(*)(void* userData);

API void memSetFailCallback(MemFailCallback callback, void* userdata);
API void memRunFailCallback();
API void* memAlignPointer(void* ptr, size_t extra, uint32 align);
API Allocator* memDefaultAlloc();
API void memSetDefaultAlloc(Allocator* alloc);

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
[[nodiscard]] MemTempId memTempPushId();
void memTempPopId(MemTempId id);
void memTempSetDebugMode(bool enable);
void memTempGetStats(Allocator* alloc, MemTransientAllocatorStats** outStats, uint32* outCount);

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
    inline operator MemTempId() const { return _id; }

    [[nodiscard]] void* Malloc(size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    [[nodiscard]] void* Realloc(void* ptr, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    void Free(void* ptr, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    AllocatorType GetType() const override { return AllocatorType::Temp; }

    size_t GetOffset() const;
    size_t GetPointerOffset(void* ptr) const;

    template <typename _T>
    [[nodiscard]] _T* MallocTyped(uint32 count = 1, uint32 align = CONFIG_MACHINE_ALIGNMENT) 
        { return reinterpret_cast<_T*>(memAllocTemp(_id, count*sizeof(_T), align)); }
    template <typename _T>
    [[nodiscard]] _T* MallocZeroTyped(uint32 count = 1, uint32 align = CONFIG_MACHINE_ALIGNMENT) 
        { return reinterpret_cast<_T*>(memAllocTempZero(_id, count*sizeof(_T), align)); }
    template <typename _T>
    [[nodiscard]] _T* ReallocTyped(_T* ptr, uint32 count = 1, uint32 align = CONFIG_MACHINE_ALIGNMENT)
        { return reinterpret_cast<_T*>(memReallocTemp(_id, ptr, count*sizeof(_T), align)); }

private:
    MemTempId _id = 0;
    uint16 _fiberProtectorId = 0;
    bool _ownsId = false;
};

//------------------------------------------------------------------------
// Frame Allocator: Linear-based frame allocator. Grows by page. Resets every frame. 
//                  Unlike temp allocator, allocations are thread-safe and available within all threads
void memFrameSetDebugMode(bool enable);
Allocator* memFrameAlloc();
MemTransientAllocatorStats memFrameGetStats();

namespace _private 
{
    void memFrameReset();
}

//------------------------------------------------------------------------
// Budget allocator: Linear-based budget allocator. Fixed capacity. Persists in memory in a higher lifetime
struct MemBudgetAllocator final : Allocator
{
    explicit MemBudgetAllocator(const char* name);

    void Initialize(size_t sizeBudget, size_t pageSize, bool commitAll, bool debugMode = false);
    void Release();

    [[nodiscard]] void* Malloc(size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    [[nodiscard]] void* Realloc(void* ptr, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    void  Free(void* ptr, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    AllocatorType GetType() const override { return AllocatorType::Budget; }

    size_t GetCommitedSize() const { return _commitSize; }
    size_t GetTotalSize() const { return _maxSize; }
    size_t GetOffset() const { return _offset; }

private:
    uint8* _buffer = nullptr;
    size_t _maxSize = 0;
    size_t _commitSize = 0;
    size_t _offset = 0;
    size_t _pageSize = 0;   
    char   _name[32];
    bool   _debugMode = false;
    Array<_private::MemDebugPointer, 8>* _debugPointers;
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

    size_t GetAllocatedSize() const { return _allocatedSize; }

    float CalculateFragmentation();
    bool Validate();
    bool IsDebugMode() const { return _debugMode; }

protected:
    size_t _allocatedSize = 0;
    void*  _tlsf = nullptr;
    size_t _tlsfSize = 0;
    bool   _debugMode = false;
};

struct MemTlsfAllocator_ThreadSafe final : MemTlsfAllocator
{
    [[nodiscard]] void* Malloc(size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    [[nodiscard]] void* Realloc(void* ptr, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    void  Free(void* ptr, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;

protected:
    AtomicLock _lock;    
};

namespace _private
{
    void memTempReset(float dt);
}

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

inline void* operator new(size_t, _private::PlacementNewTag, void* _ptr) { return _ptr; }
inline void  operator delete(void*, _private::PlacementNewTag, void*) throw() {}

