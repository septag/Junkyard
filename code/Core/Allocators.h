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
FORCE_INLINE void* memAllocAlignedZero(size_t size, uint32 align, Allocator* alloc = memDefaultAlloc());
FORCE_INLINE void* memReallocAligned(void* ptr, size_t size, uint32 align, Allocator* alloc = memDefaultAlloc());
FORCE_INLINE void  memFreeAligned(void* ptr, uint32 align, Allocator* alloc = memDefaultAlloc());

template<typename _T> _T* memAllocTyped(uint32 count = 1, Allocator* alloc = memDefaultAlloc());
template<typename _T> _T* memAllocZeroTyped(uint32 count = 1, Allocator* alloc = memDefaultAlloc());
template<typename _T> _T* memAllocAlignedTyped(uint32 count = 1, uint32 align = CONFIG_MACHINE_ALIGNMENT, Allocator* alloc = memDefaultAlloc());
template<typename _T> _T* memAllocAlignedZeroTyped(uint32 count = 1, uint32 align = CONFIG_MACHINE_ALIGNMENT, Allocator* alloc = memDefaultAlloc());
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
struct MemLinearVMAllocator : Allocator
{
    void Initialize(size_t reserveSize, size_t pageSize, bool debugMode = false);
    void Release();
    void Reset();

    [[nodiscard]] void* Malloc(size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    [[nodiscard]] void* Realloc(void* ptr, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    void  Free(void* ptr, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    AllocatorType GetType() const override { return AllocatorType::LinearVM; }

    size_t GetReservedSize() const { return mReserveSize; }
    size_t GetAllocatedSize() const { return mOffset; }
    size_t GetCommitedSize() const { return mCommitSize; }

protected:
    uint8* myBuffer = nullptr;
    size_t mCommitSize = 0;
    size_t mOffset = 0;
    size_t mPageSize = 0;
    size_t mReserveSize = 0;
    void* mLastAllocatedPtr = 0;
    Array<_private::MemDebugPointer, 8>* mDebugPointers = nullptr;
    bool mDebugMode = false;
};

struct MemLinearVMAllocator_ThreadSafe final : MemLinearVMAllocator
{
    [[nodiscard]] void* Malloc(size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    [[nodiscard]] void* Realloc(void* ptr, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    void Free(void* ptr, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;

private:
    AtomicLock mLock;
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

protected:
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

//----------------------------------------------------------------------------------------------------------------------
// MemSingleShotMalloc: POD struct continous allocator. If you have POD structs that contain some buffers and arrays
//      You can create all of the buffers in one malloc call with the help of this allocator
//      Relative pointers are also supported for member variables, as shown in the example below.
//      Example:
//          struct SomeObject {
//              uint32 a;
//              uint32 b;
//          };
//
//          struct SomeStruct {
//              int count;
//              RelativePtr<SomeObject> objects;
//              uint32* someOtherArray;
//          };
//
//          MemSingleShotMalloc<SomeStruct> mallocator;
//          SomeStruct* s = mallocator.AddMemberField<SomeObject>(offsetof(SomeStruct, objects), 100, true)
//                                    .AddMemberField<uint32>(offsetof(SomeStruct, someOtherArray), 100)
//                                    .Calloc();
//          s->count = 100;
//          // Arrays in the struct are allocated and assigned, do whatever you want
//          // For free, it's recommended to use the allocator, or you can call `memFreeAligned` with alignof(SomeStruct)
//          mallocator.Free(s);
//
template <typename _T, uint32 _MaxFields = 8>
struct MemSingleShotMalloc
{
    MemSingleShotMalloc();

    template <typename _FieldType> MemSingleShotMalloc& AddMemberField(uint32 offsetInStruct, size_t arrayCount, 
                                                                       bool relativePtr = false,
                                                                       uint32 align = CONFIG_MACHINE_ALIGNMENT);
    template <typename _PodAllocType> MemSingleShotMalloc& AddMemberChildPODField(const _PodAllocType& podAlloc, 
                                                                                  uint32 offsetInStruct, size_t arrayCount, 
                                                                                  bool relativePtr = false,
                                                                                  uint32 align = CONFIG_MACHINE_ALIGNMENT);
    template <typename _FieldType> MemSingleShotMalloc& AddExternalPointerField(_FieldType** pPtr, size_t arrayCount, 
                                                                                uint32 align = CONFIG_MACHINE_ALIGNMENT);

    _T* Calloc(Allocator* alloc = memDefaultAlloc());
    _T* Calloc(void* buff, size_t size);

    // Free can be called as a static function, since it just calls malloc with alignof
    static void Free(_T* p, Allocator* alloc = memDefaultAlloc());
    
    size_t GetMemoryRequirement() const;
    size_t GetSize() const;

private:
    struct Field
    {
        void** pPtr;
        size_t offset;
        uint32 offsetInStruct;
        bool   relativePtr;
    };

    Field  mFields[_MaxFields];
    size_t mSize;
    uint32 mNumFields;
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
    align = Max(align, CONFIG_MACHINE_ALIGNMENT);
    void* ptr = alloc->Malloc(AlignValue<size_t>(size, align), align);
    if (ptr == NULL) {
        MEMORY_FAIL();
    }
    return ptr;
}

[[nodiscard]] FORCE_INLINE void* memAllocAlignedZero(size_t size, uint32 align, Allocator* alloc)
{
    ASSERT(alloc);
    align = Max(align, CONFIG_MACHINE_ALIGNMENT);
    void* ptr = alloc->Malloc(AlignValue<size_t>(size, align), align);
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
    align = Max(align, CONFIG_MACHINE_ALIGNMENT);
    ptr = alloc->Realloc(ptr, AlignValue<size_t>(size, align), align);
    if (ptr == NULL) {
        MEMORY_FAIL();
    }
    return ptr;

}

FORCE_INLINE void memFreeAligned(void* ptr, uint32 align, Allocator* alloc)
{
    ASSERT(alloc);
    alloc->Free(ptr, Max(align, CONFIG_MACHINE_ALIGNMENT));
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
[[nodiscard]] inline _T* memAllocAlignedTyped(uint32 count, uint32 align, Allocator* alloc)
{
    return reinterpret_cast<_T*>(memAllocAligned(sizeof(_T)*count, align, alloc));
}

template<typename _T>
[[nodiscard]] inline _T* memAllocAlignedZeroTyped(uint32 count, uint32 align, Allocator* alloc)
{
    return reinterpret_cast<_T*>(memAllocAlignedZero(sizeof(_T)*count, align, alloc));
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

//------------------------------------------------------------------------
// @impl MemSingleShotMalloc
template <typename _T, uint32 _MaxFields>
inline MemSingleShotMalloc<_T, _MaxFields>::MemSingleShotMalloc()
{
    mSize = sizeof(_T);

    mFields[0].pPtr = nullptr;
    mFields[0].offset = 0;
    mFields[0].offsetInStruct = UINT32_MAX;
    mNumFields = 1;
}

template <typename _T, uint32 _MaxFields>
template <typename _FieldType> inline MemSingleShotMalloc<_T, _MaxFields>& 
    MemSingleShotMalloc<_T, _MaxFields>::AddMemberField(uint32 offsetInStruct, size_t arrayCount, bool relativePtr, uint32 align)
{
    uint32 index = mNumFields;
    ASSERT_MSG(index < _MaxFields, "Cannot add more fields, increase the _MaxFields");
    
    align = Max(CONFIG_MACHINE_ALIGNMENT, align);
    size_t size = sizeof(_FieldType) * arrayCount;
    size = AlignValue<size_t>(size, align);

    size_t offset = mSize;
    if (offset % align != 0)
        offset = AlignValue<size_t>(offset, align);

    Field& buff = mFields[index];
    buff.pPtr = nullptr;
    buff.offset = offset;
    buff.offsetInStruct = offsetInStruct;
    buff.relativePtr = relativePtr;

    mSize += size;
    ++mNumFields;

    return *this;
}

template <typename _T, uint32 _MaxFields>
template <typename _PodAllocType> inline MemSingleShotMalloc<_T, _MaxFields>& 
MemSingleShotMalloc<_T, _MaxFields>::AddMemberChildPODField(const _PodAllocType& podAlloc, uint32 offsetInStruct, 
                                                        size_t arrayCount, bool relativePtr, uint32 align)
{
    uint32 index = mNumFields;
    ASSERT_MSG(index < _MaxFields, "Cannot add more fields, increase the _MaxFields");
    
    align = Max(CONFIG_MACHINE_ALIGNMENT, align);
    size_t size = podAlloc.GetMemoryRequirement() * arrayCount;
    size = AlignValue<size_t>(size, align);

    size_t offset = mSize;
    if (offset % align != 0) {
        offset = AlignValue<size_t>(offset, align);
    }

    Field& buff = mFields[index];
    buff.pPtr = nullptr;
    buff.offset = offset;
    buff.offsetInStruct = offsetInStruct;
    buff.relativePtr = relativePtr;

    mSize += size;
    ++mNumFields;

    return *this;
}


template <typename _T, uint32 _MaxFields>
template <typename _FieldType> inline MemSingleShotMalloc<_T, _MaxFields>& 
    MemSingleShotMalloc<_T, _MaxFields>::AddExternalPointerField(_FieldType** pPtr, size_t arrayCount, uint32 align)
{
    ASSERT(pPtr);

    uint32 index = mNumFields;
    ASSERT_MSG(index < _MaxFields, "Cannot add more fields, increase the _MaxFields");
    
    align = Max(CONFIG_MACHINE_ALIGNMENT, align);
    size_t size = sizeof(_FieldType) * arrayCount;
    size = AlignValue<size_t>(size, align);
    
    size_t offset = mSize;
    if (offset % align != 0) {
        offset = AlignValue<size_t>(offset, align);
    }
    
    Field& buff = mFields[index];
    buff.pPtr = (void**)pPtr;
    buff.offset = offset;
    buff.offsetInStruct = UINT32_MAX;
    buff.relativePtr = false;
    
    mSize += size;
    ++mNumFields;

    return *this;
}

template <typename _T, uint32 _MaxFields>
inline _T* MemSingleShotMalloc<_T, _MaxFields>::Calloc(Allocator* alloc)
{
    void* mem = memAllocAligned(mSize, alignof(_T), alloc);
    return Calloc(mem, mSize);
}

template <typename _T, uint32 _MaxFields>
inline size_t MemSingleShotMalloc<_T, _MaxFields>::GetMemoryRequirement() const
{
    return mSize;
}

template <typename _T, uint32 _MaxFields>
inline void MemSingleShotMalloc<_T, _MaxFields>::Free(_T* p, Allocator* alloc)
{
    ASSERT(alloc);
    if (p)
        memFreeAligned(p, alignof(_T), alloc);
}

template <typename _T, uint32 _MaxFields>
inline size_t MemSingleShotMalloc<_T, _MaxFields>::GetSize() const
{
    return mSize;
}

template <typename _T, uint32 _MaxFields>
inline _T*  MemSingleShotMalloc<_T, _MaxFields>::Calloc(void* buff, [[maybe_unused]] size_t size)
{
    ASSERT(buff);
    ASSERT(size == 0 || size >= GetMemoryRequirement());

    memset(buff, 0x0, mSize);
    
    uint8* tmp = (uint8*)buff;
    
    // Assign buffer pointers
    for (int i = 1, c = mNumFields; i < c; i++) {
        if (mFields[i].offsetInStruct != UINT32_MAX) {
            ASSERT(mFields[i].pPtr == NULL);
            if (!mFields[i].relativePtr) 
                *((void**)(tmp + mFields[i].offsetInStruct)) = tmp + mFields[i].offset;
            else
                *((uint32*)(tmp + mFields[i].offsetInStruct)) = (uint32)mFields[i].offset - mFields[i].offsetInStruct;
        } else {
            ASSERT(mFields[i].offsetInStruct == -1);
            *mFields[i].pPtr = tmp + mFields[i].offset;
        }
    }

    return (_T*)buff;
}
