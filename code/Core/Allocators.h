#pragma once

//
// Allocators: includes some common allocators 
//
//      MemTempAllocator: This is a stack-like bump allocator. Covers all Temp allocations that happen within a small scope
//                        https://septag.dev/blog/posts/junkyard-memory-01/#allocations/allocatortypes/tempallocator
//      MemBumpAllocatorBase: This is a simple form of linear allocator. Like temp allocator, the logic is that all allocated memory is layed out continously in memory
//                            https://septag.dev/blog/posts/junkyard-memory-01/#allocations/allocatortypes/bumpallocator
//                            Current implemented backends: MemBumpAllocatorVM
//      MemTlsfAllocator: TLSF is a generic embeddable allocator (MemTlsfAllocator) using Two-Level Segregated Fit memory allocator implementation
//                        https://septag.dev/blog/posts/junkyard-memory-01/#allocations/allocatortypes/tlsfallocator
//      
//      MemSingleShotMalloc: This helps me with allocating several buffers with one allocation call.
//                           https://septag.dev/blog/posts/junkyard-memory-01/#buffersandcontainers/memsingleshotmalloc
//                          

#include "Base.h"

template <typename _T> struct Array; // Fwd from Array.h
using SpinLockFake = uint8[CACHE_LINE_SIZE];

struct MemDebugPointer
{
    void* ptr;
    uint32 align;
};

//    ████████╗███████╗███╗   ███╗██████╗      █████╗ ██╗     ██╗      ██████╗  ██████╗
//    ╚══██╔══╝██╔════╝████╗ ████║██╔══██╗    ██╔══██╗██║     ██║     ██╔═══██╗██╔════╝
//       ██║   █████╗  ██╔████╔██║██████╔╝    ███████║██║     ██║     ██║   ██║██║     
//       ██║   ██╔══╝  ██║╚██╔╝██║██╔═══╝     ██╔══██║██║     ██║     ██║   ██║██║     
//       ██║   ███████╗██║ ╚═╝ ██║██║         ██║  ██║███████╗███████╗╚██████╔╝╚██████╗
//       ╚═╝   ╚══════╝╚═╝     ╚═╝╚═╝         ╚═╝  ╚═╝╚══════╝╚══════╝ ╚═════╝  ╚═════╝
struct MemTempAllocator final : MemAllocator
{
    using ID = uint32;

    struct Stats
    {
        size_t curPeak;
        size_t maxPeak;
        uint32 threadId;
        const char* threadName;
    };

    MemTempAllocator();
    explicit MemTempAllocator(ID id);
    ~MemTempAllocator();

    ID GetId() const { return mId; }

    [[nodiscard]] void* Malloc(size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    [[nodiscard]] void* Realloc(void* ptr, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    [[nodiscard]] void* MallocZero(size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT);
    void Free(void* ptr, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;

    template <typename _T>
    [[nodiscard]] _T* MallocTyped(uint32 count = 1, uint32 align = CONFIG_MACHINE_ALIGNMENT) { return reinterpret_cast<_T*>(Malloc(count*sizeof(_T), align)); }
   
    template <typename _T>
    [[nodiscard]] _T* MallocZeroTyped(uint32 count = 1, uint32 align = CONFIG_MACHINE_ALIGNMENT)  { return reinterpret_cast<_T*>(MallocZero(count*sizeof(_T), align)); }
    
    template <typename _T>
    [[nodiscard]] _T* ReallocTyped(_T* ptr, uint32 count = 1, uint32 align = CONFIG_MACHINE_ALIGNMENT) { return reinterpret_cast<_T*>(Realloc(ptr, count*sizeof(_T), align)); }

    MemAllocatorType GetType() const override { return MemAllocatorType::Temp; }
    size_t GetOffset() const;
    size_t GetPointerOffset(void* ptr) const;

    API [[nodiscard]] static ID PushId();
    API static void PopId(ID id);
    API static void EnableDebugMode(bool enable);
    API static void EnableCallstackCapture(bool capture);
    API static void GetStats(MemAllocator* alloc, Stats** outStats, uint32* outCount);
    API static void Reset(float dt, bool resetValidation = true);

private:
    ID mId = 0;
    uint16 mFiberProtectorId = 0;
    bool mOwnsId = false;
};

//    ██████╗ ██╗   ██╗███╗   ███╗██████╗      █████╗ ██╗     ██╗      ██████╗  ██████╗
//    ██╔══██╗██║   ██║████╗ ████║██╔══██╗    ██╔══██╗██║     ██║     ██╔═══██╗██╔════╝
//    ██████╔╝██║   ██║██╔████╔██║██████╔╝    ███████║██║     ██║     ██║   ██║██║     
//    ██╔══██╗██║   ██║██║╚██╔╝██║██╔═══╝     ██╔══██║██║     ██║     ██║   ██║██║     
//    ██████╔╝╚██████╔╝██║ ╚═╝ ██║██║         ██║  ██║███████╗███████╗╚██████╔╝╚██████╗
//    ╚═════╝  ╚═════╝ ╚═╝     ╚═╝╚═╝         ╚═╝  ╚═╝╚══════╝╚══════╝ ╚═════╝  ╚═════╝
                                                                                     
struct MemBumpAllocatorBase : MemAllocator
{
    void Initialize(size_t reserveSize, size_t pageSize, bool debugMode = false);
    void Release();
    void Reset();
    void CommitAll();
    bool IsInitialized() const;

    [[nodiscard]] void* Malloc(size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    [[nodiscard]] void* Realloc(void* ptr, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    void  Free(void* ptr, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    MemAllocatorType GetType() const override { return MemAllocatorType::Bump; }

    size_t GetReservedSize() const { return mReserveSize; }
    size_t GetAllocatedSize() const { return mOffset; }
    size_t GetCommitedSize() const { return mCommitSize; }
    size_t GetOffset() const { return mOffset; }

protected:
    virtual void* BackendReserve(size_t size) = 0;
    virtual void* BackendCommit(void* ptr, size_t size) = 0;
    virtual void  BackendDecommit(void* ptr, size_t size) = 0;
    virtual void  BackendRelease(void* ptr, size_t size) = 0;

    uint8* mBuffer = nullptr;
    size_t mCommitSize = 0;
    size_t mOffset = 0;
    size_t mPageSize = 0;
    size_t mReserveSize = 0;
    void* mLastAllocatedPtr = 0;
    Array<MemDebugPointer>* mDebugPointers = nullptr;
    bool mDebugMode = false;
};

struct MemBumpAllocatorVM final : MemBumpAllocatorBase
{
private:
    void* BackendReserve(size_t size) override;
    void* BackendCommit(void* ptr, size_t size) override;
    void  BackendDecommit(void* ptr, size_t size) override;
    void  BackendRelease(void* ptr, size_t size) override;
};

struct alignas(CACHE_LINE_SIZE) MemThreadSafeAllocator final : MemAllocator
{
    MemThreadSafeAllocator() {}
    explicit MemThreadSafeAllocator(MemAllocator* alloc);
    void SetAllocator(MemAllocator* alloc);

    [[nodiscard]] void* Malloc(size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    [[nodiscard]] void* Realloc(void* ptr, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    void Free(void* ptr, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    MemAllocatorType GetType() const override;

private:
    MemAllocator* mAlloc = nullptr;
    [[maybe_unused]] uint8 _padding1[CACHE_LINE_SIZE - sizeof(MemAllocator) - sizeof(MemAllocator*)];
    SpinLockFake mLock;
};


//    ████████╗██╗     ███████╗███████╗     █████╗ ██╗     ██╗      ██████╗  ██████╗
//    ╚══██╔══╝██║     ██╔════╝██╔════╝    ██╔══██╗██║     ██║     ██╔═══██╗██╔════╝
//       ██║   ██║     ███████╗█████╗      ███████║██║     ██║     ██║   ██║██║     
//       ██║   ██║     ╚════██║██╔══╝      ██╔══██║██║     ██║     ██║   ██║██║     
//       ██║   ███████╗███████║██║         ██║  ██║███████╗███████╗╚██████╔╝╚██████╗
//       ╚═╝   ╚══════╝╚══════╝╚═╝         ╚═╝  ╚═╝╚══════╝╚══════╝ ╚═════╝  ╚═════╝
struct MemTlsfAllocator final : MemAllocator
{
    static size_t GetMemoryRequirement(size_t poolSize);

    void Initialize(size_t poolSize, void* buffer, size_t size, bool debugMode = false);
    void Release();

    [[nodiscard]] void* Malloc(size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    [[nodiscard]] void* Realloc(void* ptr, size_t size, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    void  Free(void* ptr, uint32 align = CONFIG_MACHINE_ALIGNMENT) override;
    MemAllocatorType GetType() const override { return MemAllocatorType::Tlsf; }

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


//
//    ███████╗██╗███╗   ██╗ ██████╗ ██╗     ███████╗    ███████╗██╗  ██╗ ██████╗ ████████╗     █████╗ ██╗     ██╗      ██████╗  ██████╗
//    ██╔════╝██║████╗  ██║██╔════╝ ██║     ██╔════╝    ██╔════╝██║  ██║██╔═══██╗╚══██╔══╝    ██╔══██╗██║     ██║     ██╔═══██╗██╔════╝
//    ███████╗██║██╔██╗ ██║██║  ███╗██║     █████╗      ███████╗███████║██║   ██║   ██║       ███████║██║     ██║     ██║   ██║██║     
//    ╚════██║██║██║╚██╗██║██║   ██║██║     ██╔══╝      ╚════██║██╔══██║██║   ██║   ██║       ██╔══██║██║     ██║     ██║   ██║██║     
//    ███████║██║██║ ╚████║╚██████╔╝███████╗███████╗    ███████║██║  ██║╚██████╔╝   ██║       ██║  ██║███████╗███████╗╚██████╔╝╚██████╗
//    ╚══════╝╚═╝╚═╝  ╚═══╝ ╚═════╝ ╚══════╝╚══════╝    ╚══════╝╚═╝  ╚═╝ ╚═════╝    ╚═╝       ╚═╝  ╚═╝╚══════╝╚══════╝ ╚═════╝  ╚═════╝
                                                                                                                                     
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

    _T* Calloc(MemAllocator* alloc = Mem::GetDefaultAlloc());
    _T* Calloc(void* buff, size_t size);

    // Free can be called as a static function, since it just calls malloc with alignof
    static void Free(_T* p, MemAllocator* alloc = Mem::GetDefaultAlloc());
    
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


//    ██╗███╗   ██╗██╗     ██╗███╗   ██╗███████╗███████╗
//    ██║████╗  ██║██║     ██║████╗  ██║██╔════╝██╔════╝
//    ██║██╔██╗ ██║██║     ██║██╔██╗ ██║█████╗  ███████╗
//    ██║██║╚██╗██║██║     ██║██║╚██╗██║██╔══╝  ╚════██║
//    ██║██║ ╚████║███████╗██║██║ ╚████║███████╗███████║
//    ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝╚═╝  ╚═══╝╚══════╝╚══════╝
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
inline _T* MemSingleShotMalloc<_T, _MaxFields>::Calloc(MemAllocator* alloc)
{
    void* mem = Mem::AllocAligned(mSize, alignof(_T), alloc);
    return Calloc(mem, mSize);
}

template <typename _T, uint32 _MaxFields>
inline size_t MemSingleShotMalloc<_T, _MaxFields>::GetMemoryRequirement() const
{
    return mSize;
}

template <typename _T, uint32 _MaxFields>
inline void MemSingleShotMalloc<_T, _MaxFields>::Free(_T* p, MemAllocator* alloc)
{
    ASSERT(alloc);
    if (p)
        Mem::FreeAligned(p, alignof(_T), alloc);
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
