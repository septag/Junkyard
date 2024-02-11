#pragma once
//
// Contains memory containers and useful buffer manipulation classes
// All objects can be initialized either with allocator or with user provided buffer/size pair. But doesn't actually allocate anything in ctor or deallocate in dtor.
// In the case of latter, use `GetMemoryRequirement` to get needed memory size before creating the objects.
//
// Array: Regular growable array, very much like std::vector but with some essential differences.
//        Arrays are meant to be used with POD (plain-old-data) structs only, so it doesn't handle ctor/dtor and all that stuff that comes with OO
//        It can only grow if you assign allocator to the array, otherwise, in case of buffer/size pair, growing is not supported.
//        Array does not allocate anything in ctor or deallocate anything in dtor. 
//        So you should either allocate explicitly with `Reserve` method, or implicitly by calling `Write` method. 
//        And deallocate explictly with `Free` or implicitly with `Detach`
//        Removing elements is provided by swaping the element with the last one. 
//        So beware not to export pointers or indexes of objects from Array if you are about to delete elements. For that purpose, use `HandlePool`.
//
// StaticArray: Very much like Array but of course static and on the stack
//
//  RingBuffer: Regular ring-buffer
//      Writing to ring-buffer: use `ExpectWrite` method to determine how much memory is available in the ring-buffer before writing.
//      Example:
//          if (buffer.ExpectWrite() >= sizeof(float))
//              buffer.Write<float>(value);
//      Reading: There are two methods for Reading. `Read` progreses the ring-buffer offset. `Peek` just reads a buffer and doesn't move the offset
//
//  Blob: Readable and Writable blob of memory
//        Blob can contain static buffer (by explicitly prodiving a buffer pointer and size), or Dynamic growable buffer (by providing allocator)
//        In the case of dynamic growable memory, you should provide Growing policy with `SetGrowPolicy` method.
//        Blobs don't allocate anything in ctor or deallocate anything in dtor. 
//        So you should either allocate explicitly with `Reserve` method, or implicitly by calling `Write` method.
//        And deallocate explictly with `Free` or implicitly with `Detach`
//        
//  PoolBuffer: Fast pool memory allocator with Fixed sized elements
//              Pools can grow by adding pages implicitly when calling `New`. For that, you need to provide allocator to the pool-buffer instead of pre-allocated pointer/size pair.
//

// TODO: This is a very commonly used header. If would be cool if we can remove the dependency to Allocators.h here
#include "Allocators.h"

//------------------------------------------------------------------------
template <typename _T, uint32 _Reserve = 8>
struct Array
{
    Array() : Array(memDefaultAlloc()) {}
    explicit Array(Allocator* alloc) : mAlloc(alloc) {}
    explicit Array(const void* buffer, size_t size);

    void SetAllocator(Allocator* alloc);
    void Reserve(uint32 capacity);
    void Reserve(uint32 capacity, void* buffer, size_t size);
    void Free();
    static size_t GetMemoryRequirement(uint32 capacity = _Reserve);

    [[nodiscard]] _T* Push();
    _T* Push(const _T& item);
    void RemoveAndSwap(uint32 index);
    void RemoveAndShift(uint32 index);
    uint32 Count() const;
    uint32 Capacity() const;
    void Clear();
    _T& Last();
    _T PopLast();
    _T PopFirst();
    _T Pop(uint32 index);
    void Extend(const Array<_T>& arr);
    const _T& operator[](uint32 index) const;
    _T& operator[](uint32 index);
    void Shrink();
    bool IsFull() const;
    const _T* Ptr() const;
    _T* Ptr();
    
    void Detach(_T** outBuffer, uint32* outCount);
    Span<_T> Detach();

    void ShiftLeft(uint32 count);
    void CopyTo(Array<_T, _Reserve>* otherArray) const;

    uint32 Find(const _T& value);
    // _Func = [capture](const _T& item)->bool
    template <typename _Func> uint32 FindIf(_Func findFunc);
    
    // C++ stl crap compatibility. it's mainly `for(auto t : array)` syntax sugar
    struct Iterator 
    {
        Iterator(_T* ptr) : _ptr(ptr) {}
        _T& operator*() { return *_ptr; }
        void operator++() { ++_ptr; }
        bool operator!=(Iterator it) { return _ptr != it._ptr; }
        _T* _ptr;
    };

    Iterator begin()    { return Iterator(&mBuffer[0]); }
    Iterator end()      { return Iterator(&mBuffer[mCount]); }

    Iterator begin() const    { return Iterator(&mBuffer[0]); }
    Iterator end() const     { return Iterator(&mBuffer[mCount]); }

private:
    Allocator* mAlloc = nullptr;
    uint32 mCapacity = 0;
    uint32 mCount = 0;
    _T* mBuffer = nullptr;
};

//------------------------------------------------------------------------
// Fixed sized array on stack
template <typename _T, uint32 _MaxCount>
struct StaticArray
{
    _T* Add();
    _T* Add(const _T& item);
    void RemoveAndSwap(uint32 index);
    uint32 Count() const;
    void Clear();
    _T& Last();
    _T& RemoveLast();
    const _T& operator[](uint32 index) const;
    _T& operator[](uint32 index);
    const _T* Ptr() const;
    _T* Ptr();

    uint32 Find(const _T& value);
    // _Func = [capture](const _T& item)->bool
    template <typename _Func> uint32 FindIf(_Func findFunc);

    // C++ stl crap compatibility. we just want to use for(auto t : array) syntax sugar
    struct Iterator 
    {
        Iterator(_T* ptr) : _ptr(ptr) {}
        _T operator*() { return *_ptr; }
        void operator++() { ++_ptr; }
        bool operator!=(Iterator it) { return _ptr != it._ptr; }
        _T* _ptr;
    };
    
    Iterator begin()    { return Iterator(&mBuffer[0]); }
    Iterator end()      { return Iterator(&mBuffer[mCount]); }

private:
    uint32 mCount = 0;
    _T mBuffer[_MaxCount];
};

//------------------------------------------------------------------------
// HandlePool
namespace _private 
{
    // change number of kHandleGenBits to have more generation range
    // Whatever the GenBits is, max gen would be 2^GenBits-1 and max index would be 2^(32-GenBits)-1
    // Handle = [<--- high-bits: Generation --->][<--- low-bits: Index -->]
    static inline constexpr uint32 kHandleGenBits = 14;
    static inline constexpr uint32 kHandleIndexMask = (1 << (32 - kHandleGenBits)) - 1;
    static inline constexpr uint32 kHandleGenMask = (1 << kHandleGenBits) - 1;
    static inline constexpr uint32 kHandleGenShift  = 32 - kHandleGenBits;
} // _private

// TODO: Apple declares "Handle" type in MacTypes.h
#ifndef __OBJC__
template <typename _T>
struct Handle
{
    Handle() = default;
    Handle(const Handle<_T>&) = default;
    explicit Handle(uint32 _id) : mId(_id) {}
    Handle<_T>& operator=(const Handle<_T>&) = default;

    void Set(uint32 gen, uint32 index) { mId = ((gen & _private::kHandleGenMask)<<_private::kHandleGenShift) | (index&_private::kHandleIndexMask); }
    explicit operator uint32() const { return mId; }
    uint32 GetSparseIndex() { return mId & _private::kHandleIndexMask; }
    uint32 GetGen() { return (mId >> _private::kHandleGenShift) & _private::kHandleGenMask; }
    bool IsValid() const { return mId != 0; }
    bool operator==(const Handle<_T>& v) const { return mId == v.mId; }
    bool operator!=(const Handle<_T>& v) const { return mId != v.mId; }

    uint32 mId = 0;
};

#define DEFINE_HANDLE(_Name) struct _Name##T; using _Name = Handle<_Name##T>

namespace _private
{
    struct alignas(16) HandlePoolTable
    {
        uint32  count;
        uint32  capacity;
        uint32* dense;          // actual handles are stored in 'dense' array [0..arrayCount]
        uint32* sparse;         // indices to dense for removal lookup [0..arrayCapacity]
        uint8   padding[sizeof(void*)];
    };

    API HandlePoolTable* handleCreatePoolTable(uint32 capacity, Allocator* alloc);
    API void handleDestroyPoolTable(HandlePoolTable* tbl, Allocator* alloc);
    API bool handleGrowPoolTable(HandlePoolTable** pTbl, Allocator* alloc);
    API HandlePoolTable* handleClone(HandlePoolTable* tbl, Allocator* alloc);

    API uint32 handleNew(HandlePoolTable* tbl);
    API void   handleDel(HandlePoolTable* tbl, uint32 handle);
    API void   handleResetPoolTable(HandlePoolTable* tbl);
    API bool   handleIsValid(const HandlePoolTable* tbl, uint32 handle);
    API uint32 handleAt(const HandlePoolTable* tbl, uint32 index);
    API bool   handleFull(const HandlePoolTable* tbl);

    API size_t handleGetMemoryRequirement(uint32 capacity);
    API HandlePoolTable* handleCreatePoolTableWithBuffer(uint32 capacity, void* buff, size_t size);
    API bool handleGrowPoolTableWithBuffer(HandlePoolTable** pTbl, void* buff, size_t size);
} // _private

template <typename _HandleType, typename _DataType, uint32 _Reserve = 32>
struct HandlePool
{
    HandlePool() : HandlePool(memDefaultAlloc()) {}
    explicit HandlePool(Allocator* alloc) : mAlloc(alloc), mItems(alloc) {}
    explicit HandlePool(void* data, size_t size); 

    void CopyTo(HandlePool<_HandleType, _DataType, _Reserve>* otherPool) const;

    [[nodiscard]] _HandleType Add(const _DataType& item, _DataType* prevItem = nullptr);
    void Remove(_HandleType handle);
    uint32 Count() const;
    void Clear();
    bool IsValid(_HandleType handle);
    _HandleType HandleAt(uint32 index);
    _DataType& Data(uint32 index);
    _DataType& Data(_HandleType handle);
    bool IsFull() const;
    uint32 Capacity() const;

    void Reserve(uint32 capacity, void* buffer, size_t size);
    void SetAllocator(Allocator* alloc);
    void Free();

    static size_t GetMemoryRequirement(uint32 capacity = _Reserve);
    bool Grow();
    bool Grow(void* data, size_t size);

    // _Func = [](const _DataType&)->bool
    template <typename _Func> _HandleType FindIf(_Func findFunc);

    // C++ stl crap compatibility. we just want to use for(auto t : array) syntax sugar
    struct Iterator 
    {
        using HandlePool_t = HandlePool<_HandleType, _DataType, _Reserve>;

        Iterator(HandlePool_t* pool, uint32 index) : _pool(pool), mIndex(index) {}
        _DataType& operator*() { return _pool->Data(mIndex); }
        void operator++() { ++mIndex; }
        bool operator!=(Iterator it) { return mIndex != it.mIndex; }
        HandlePool_t* _pool;
        uint32 mIndex;
    };
    
    Iterator begin()    { return Iterator(this, 0); }
    Iterator end()      { return Iterator(this, mHandles ? mHandles->count : 0); }

private:
    Allocator*                  mAlloc = nullptr;
    _private::HandlePoolTable*  mHandles = nullptr;
    Array<_DataType, _Reserve>  mItems;
};
#endif // __OBJC__

//------------------------------------------------------------------------
struct RingBuffer
{
    RingBuffer() : RingBuffer(memDefaultAlloc()) {}
    explicit RingBuffer(Allocator* alloc) : mAlloc(alloc) {}
    explicit RingBuffer(void* buffer, size_t size);
    
    void SetAllocator(Allocator* alloc);
    void Reserve(size_t capacity);
    void Reserve(void* buffer, size_t size);
    void Free();
    static size_t GetMemoryRequirement(size_t capacity);
    
    size_t ExpectWrite() const;

    void Write(const void* src, size_t size);
    size_t Read(void* dst, size_t size);
    size_t Peek(void* dst, size_t size, size_t* pOffset = nullptr);

    template <typename _T> void Write(const _T& src);
    template <typename _T> size_t Read(_T* dst);

    size_t Capacity() const;

private:
    Allocator* mAlloc = nullptr;
    uint8* mBuffer = nullptr;
    size_t mCapacity = 0;
    size_t mSize = 0;
    size_t mStart = 0;
    size_t mEnd = 0;
};

//------------------------------------------------------------------------
struct Blob
{
    enum class GrowPolicy : uint32
    {
        None = 0,
        Linear,
        Multiply
    };

    inline Blob() : Blob(memDefaultAlloc()) {}
    inline explicit Blob(Allocator* alloc) : mAlloc(alloc) {}
    inline explicit Blob(void* buffer, size_t size);
    inline Blob& operator=(const Blob&) = default;
    inline Blob(const Blob&) = default;

    inline void Attach(void* data, size_t size, Allocator* alloc);
    inline void Detach(void** outData, size_t* outSize);

    inline void SetAllocator(Allocator* alloc);
    inline void SetGrowPolicy(GrowPolicy policy, uint32 amount = 0);
    inline void SetAlignment(uint8 align);
    inline void SetSize(size_t size);
    inline void Reserve(size_t capacity);
    inline void Reserve(void* buffer, size_t size);
    inline void Free();
    inline void ResetRead();
    inline void ResetWrite();
    inline void Reset();
    inline void SetOffset(size_t offset);
    inline void CopyTo(Blob* otherBlob) const;

    inline size_t Write(const void* src, size_t size);
    inline size_t Read(void* dst, size_t size) const;
    template <typename _T> size_t Write(const _T& src);
    template <typename _T> size_t Read(_T* dst) const;
    size_t WriteStringBinary(const char* str, uint32 len = 0);
    size_t ReadStringBinary(char* outStr, uint32 outStrSize) const;
    size_t WriteStringBinary16(const char* str, uint32 len = 0);
    size_t ReadStringBinary16(char* outStr, uint32 outStrSize) const;
    
    inline size_t Size() const;
    inline size_t Capacity() const;
    inline size_t ReadOffset() const;
    inline const void* Data() const;
    inline bool IsValid() const;

private:
    Allocator* mAlloc = nullptr;
    void*      mBuffer = nullptr;
    size_t     mSize = 0;
    size_t     mOffset = 0;
    size_t     mCapacity = 0;
    uint32     mAlign = CONFIG_MACHINE_ALIGNMENT;
    GrowPolicy mGrowPolicy = GrowPolicy::None;
    uint32     mGrowCount = 4096u;
};

//------------------------------------------------------------------------
template <typename _T, uint32 _Align = CONFIG_MACHINE_ALIGNMENT>
struct PoolBuffer
{
    PoolBuffer() : PoolBuffer(memDefaultAlloc()) {}
    explicit PoolBuffer(Allocator* alloc) : mAlloc(alloc) {}
    explicit PoolBuffer(void* buffer, size_t size);
    
    void SetAllocator(Allocator* alloc);
    void Reserve(uint32 pageSize);
    void Reserve(void* buffer, size_t size, uint32 pageSize);
    void Free();
    static size_t GetMemoryRequirement(uint32 pageSize);

    _T* New();
    void Delete(_T* item);
    bool IsFull() const;

private:
    struct Page
    {
        _T**    ptrs;
        _T*     data;
        Page*   next;
        uint32  index;
    };

    Page* CreatePage(void* buffer, size_t size);

public:
    // To Iterate over all items in the pool
    struct Iterator 
    {
        Iterator(Page* page, uint32 index, uint32 pageSize) : mPage(page), mIndex(index), mPageSize(pageSize) {}
        _T& operator*() { return mPage->data[mIndex]; }
        void operator++() 
        { 
            ASSERT(mPage); 
            if (mIndex < mPageSize) 
                mIndex++; 
            else { 
                mPage = mPage->next; 
                mIndex = 0; 
            } 
        }
        bool operator!=(Iterator it) { return mPage != it.mPage || mIndex != it.mIndex; }

        Page* mPage;
        uint32 mIndex;
        uint32 mPageSize;
    };

    Iterator begin()    { return Iterator(mPages, 0, mPageSize); }
    Iterator end()      
    { 
        Page* page = mPages;
        while (page && page->index == 0 && page->next)
            page = page->next;

        return Iterator(page, 0, mPageSize); 
    }

    Iterator begin() const    { return Iterator(mPages, 0, mPageSize); }
    Iterator end() const     
    { 
        Page* page = mPages;
        while (page && page->index == 0 && page->next)
            page = page->next;

        return Iterator(page, 0, mPageSize); 
    }

private:
    Allocator*  mAlloc = nullptr;
    uint32      mPageSize = 32;      // maximum number of items that a page can hold
    Page*       mPages = nullptr;
};

//------------------------------------------------------------------------
// @impl Array
template <typename _T, uint32 _Reserve>
inline Array<_T,_Reserve>::Array(const void* buffer, size_t size)
{
    ASSERT_MSG(size > _Reserve*sizeof(_T), "Buffer should have at least %u bytes long", _Reserve*sizeof(_T));

    mCapacity = size / sizeof(_T);
    mBuffer = buffer;
}

template <typename _T, uint32 _Reserve>
inline void Array<_T, _Reserve>::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(mBuffer == nullptr, "buffer should be freed/uninitialized before setting allocator");
    mAlloc = alloc;
}    

template <typename _T, uint32 _Reserve>
inline _T* Array<_T,_Reserve>::Push()
{
    if (mCount >= mCapacity) {
        if (mAlloc) {
            Reserve(mCapacity ? (mCapacity << 1) : _Reserve);
        } 
        else {
            ASSERT(mBuffer);
            ASSERT_MSG(mCount < mCapacity, "Array overflow, capacity=%u", mCapacity);
            return nullptr;
        }
    }
    
    return &mBuffer[mCount++];
}

template <typename _T, uint32 _Reserve>
inline _T* Array<_T,_Reserve>::Push(const _T& item)
{
    _T* newItem = Push();
    if (newItem)
        *newItem = item;
    return newItem;
}

template <typename _T, uint32 _Reserve>
inline void Array<_T,_Reserve>::RemoveAndSwap(uint32 index)
{
    ASSERT(mBuffer);
    #ifdef CONFIG_CHECK_OUTOFBOUNDS
        ASSERT_MSG(index < mCount, "Index out of bounds (count: %u, index: %u)", mCount, index);
    #endif
    --mCount;
    if (index < mCount)
        Swap<_T>(mBuffer[index], mBuffer[mCount]);
}

template <typename _T, uint32 _Reserve>
inline void Array<_T,_Reserve>::RemoveAndShift(uint32 index)
{
    ASSERT(mBuffer);
#ifdef CONFIG_CHECK_OUTOFBOUNDS
    ASSERT_MSG(index < mCount, "Index out of bounds (count: %u, index: %u)", mCount, index);
#endif
    --mCount;
    for (uint32 i = index; i < mCount; i++)
        mBuffer[i] = mBuffer[i+1];
}


template <typename _T, uint32 _Reserve>
inline uint32 Array<_T,_Reserve>::Count() const
{
    return mCount;
}

template <typename _T, uint32 _Reserve>
inline uint32 Array<_T,_Reserve>::Capacity() const
{
    return mCapacity;
}

template <typename _T, uint32 _Reserve>
inline void Array<_T,_Reserve>::Reserve(uint32 capacity)
{
    ASSERT(mAlloc);
    mCapacity = Max(capacity, mCapacity);
    mBuffer = memReallocTyped<_T>(mBuffer, mCapacity, mAlloc);
    ASSERT(mBuffer);
}

template <typename _T, uint32 _Reserve>
inline void Array<_T,_Reserve>::Reserve(uint32 capacity, void* buffer, [[maybe_unused]] size_t size)
{
    capacity = Max(capacity, _Reserve);

    ASSERT(buffer);
    ASSERT_MSG(mBuffer == nullptr, "Array should not be initialized before reserve by pointer");
    ASSERT_MSG(size >= capacity*sizeof(_T), "Buffer should have at least %u bytes long (size=%u)", capacity*sizeof(_T), size);
    
    mAlloc = nullptr;
    mCapacity = capacity;
    mBuffer = (_T*)buffer;
}    

template <typename _T, uint32 _Reserve>
inline void Array<_T,_Reserve>::Clear()
{
    mCount = 0;
}

template <typename _T, uint32 _Reserve>
inline _T& Array<_T,_Reserve>::Last()
{
    ASSERT(mCount > 0);
    return mBuffer[mCount - 1];
}

template <typename _T, uint32 _Reserve>
inline _T Array<_T,_Reserve>::PopLast()
{
    ASSERT(mCount > 0);
    return mBuffer[--mCount];
}

template <typename _T, uint32 _Reserve>
inline _T Array<_T,_Reserve>::PopFirst()
{
    ASSERT(mCount > 0);
    _T first = mBuffer[0];
    // shuffle all items to the left
    for (uint32 i = 1, c = mCount; i < c; i++) {
        mBuffer[i-1] = mBuffer[i];
    }
    --mCount;
    return first;
}

template <typename _T, uint32 _Reserve>
inline _T Array<_T,_Reserve>::Pop(uint32 index)
{
    ASSERT(mCount > 0);
    #ifdef CONFIG_CHECK_OUTOFBOUNDS
        ASSERT_MSG(index < mCount, "Index out of bounds (count: %u, index: %u)", mCount, index);
    #endif

    _T item = mBuffer[index];
    // shuffle all items to the left
    for (uint32 i = index+1, c = mCount; i < c; i++) {
        mBuffer[i-1] = mBuffer[i];
    }
    --mCount;
    return item;
}

template <typename _T, uint32 _Reserve>
inline void Array<_T,_Reserve>::Extend(const Array<_T>& arr)
{
    if (arr.Count()) {
        uint32 newCount = mCount + arr.mCount;
        uint32 newCapacity = Max(newCount, Min(mCapacity, arr.mCapacity));
        if (newCapacity > mCapacity)
            Reserve(newCapacity);
        memcpy(&mBuffer[mCount], arr.mBuffer, sizeof(_T)*arr.mCount);
        mCount = newCount;
    }
}

template <typename _T, uint32 _Reserve>
inline void Array<_T,_Reserve>::ShiftLeft(uint32 count)
{
    ASSERT(count <= mCount);
    
    mCount -= count;
    if (mCount)
        memmove(mBuffer, mBuffer + sizeof(_T)*count, sizeof(_T)*mCount);
}

template <typename _T, uint32 _Reserve>
inline void Array<_T, _Reserve>::CopyTo(Array<_T, _Reserve>* otherArray) const
{
    ASSERT(otherArray);

    if (this->mCapacity)
        otherArray->Reserve(this->mCapacity);

    if (this->mCount) {
        otherArray->mCount = this->mCount;
        memcpy(otherArray->mBuffer, this->mBuffer, sizeof(_T)*this->mCount);
    }
}

template <typename _T, uint32 _Reserve>
inline const _T& Array<_T,_Reserve>::operator[](uint32 index) const
{
    #ifdef CONFIG_CHECK_OUTOFBOUNDS
        ASSERT_MSG(index < mCount, "Index out of bounds (count: %u, index: %u)", mCount, index);
    #endif
    return mBuffer[index];
}

template <typename _T, uint32 _Reserve>
inline _T& Array<_T,_Reserve>::operator[](uint32 index)
{
    #ifdef CONFIG_CHECK_OUTOFBOUNDS
        ASSERT_MSG(index < mCount, "Index out of bounds (count: %u, index: %u)", mCount, index);
    #endif
    return mBuffer[index];
}

template <typename _T, uint32 _Reserve>
inline const _T* Array<_T,_Reserve>::Ptr() const
{
    return mBuffer;
}

template <typename _T, uint32 _Reserve>
inline _T* Array<_T,_Reserve>::Ptr()
{
    return mBuffer;
}

template <typename _T, uint32 _Reserve>
inline void Array<_T,_Reserve>::Detach(_T** outBuffer, uint32* outCount)
{
    ASSERT(outBuffer);
    ASSERT(outCount);

    *outBuffer = mBuffer;
    *outCount = mCount;

    mBuffer = nullptr;
    mCount = 0;
    mCapacity = 0;
}

template <typename _T, uint32 _Reserve>
inline Span<_T> Array<_T,_Reserve>::Detach()
{
    _T* ptr;
    uint32 count;

    Detach(&ptr, &count);
    return Span<_T>(ptr, count);
}

template<typename _T, uint32 _Reserve>
inline bool Array<_T,_Reserve>::IsFull() const
{
    return mCount >= mCapacity;
}    

template <typename _T, uint32 _Reserve>
inline void Array<_T, _Reserve>::Free()
{
    mCount = 0;

    if (mAlloc) {
        memFree(mBuffer, mAlloc);
        mCapacity = 0;
        mBuffer = nullptr;
    }
}

template <typename _T, uint32 _Reserve>
inline void Array<_T, _Reserve>::Shrink()
{
    ASSERT(mAlloc);
    mCapacity = Max(mCount, _Reserve);
    Reserve(mCapacity);
}

template <typename _T, uint32 _Reserve> 
inline uint32 Array<_T, _Reserve>::Find(const _T& value)
{
    for (uint32 i = 0; i < mCount; i++) {
        if (mBuffer[i] == value)
            return i;
    }

    return UINT32_MAX;
}

template<typename _T, uint32 _Reserve>
template<typename _Func> inline uint32 Array<_T, _Reserve>::FindIf(_Func findFunc)
{
    for (uint32 i = 0, c = mCount; i < c; i++) {
        if (findFunc(mBuffer[i]))
            return i;
    }

    return UINT32_MAX;
}

template<typename _T, uint32 _Reserve>
inline size_t Array<_T, _Reserve>::GetMemoryRequirement(uint32 capacity)
{
    capacity = Max(capacity, _Reserve);
    return capacity * sizeof(_T);
}

//------------------------------------------------------------------------
// @impl StaticArray
template<typename _T, uint32 _MaxCount>
inline _T* StaticArray<_T, _MaxCount>::Add()
{
    ASSERT_MSG(mCount < _MaxCount, "Trying to add more than _MaxCount=%u", _MaxCount);
    return &mBuffer[mCount++];
}

template<typename _T, uint32 _MaxCount>
inline _T* StaticArray<_T, _MaxCount>::Add(const _T& item)
{
    ASSERT_MSG(mCount < _MaxCount, "Trying to add more than _MaxCount=%u", _MaxCount);
    uint32 index = mCount++;
    mBuffer[index] = item;
    return &mBuffer[index];
}

template<typename _T, uint32 _MaxCount>
inline void StaticArray<_T, _MaxCount>::RemoveAndSwap(uint32 index)
{
    ASSERT(mBuffer);
    #ifdef CONFIG_CHECK_OUTOFBOUNDS
        ASSERT_MSG(index <= mCount, "Index out of bounds (count: %u, index: %u)", mCount, index);
    #endif
    Swap<_T>(mBuffer[index], mBuffer[--mCount]);
}

template<typename _T, uint32 _MaxCount>
inline uint32 StaticArray<_T, _MaxCount>::Count() const
{
    return mCount;
}

template<typename _T, uint32 _MaxCount>
inline void StaticArray<_T, _MaxCount>::Clear()
{
    mCount = 0;
}

template<typename _T, uint32 _MaxCount>
inline _T& StaticArray<_T, _MaxCount>::Last()
{
    ASSERT(mCount > 0);
    return mBuffer[mCount - 1];
}

template<typename _T, uint32 _MaxCount>
inline _T& StaticArray<_T, _MaxCount>::RemoveLast()
{
    ASSERT(mCount > 0);
    return mBuffer[--mCount];
}

template<typename _T, uint32 _MaxCount>
inline const _T& StaticArray<_T, _MaxCount>::operator[](uint32 index) const
{
    #ifdef CONFIG_CHECK_OUTOFBOUNDS
        ASSERT_MSG(index <= mCount, "Index out of bounds (count: %u, index: %u)", mCount, index);
    #endif
    return mBuffer[index];
}

template<typename _T, uint32 _MaxCount>
inline _T& StaticArray<_T, _MaxCount>::operator[](uint32 index)
{
    #ifdef CONFIG_CHECK_OUTOFBOUNDS
        ASSERT_MSG(index <= mCount, "Index out of bounds (count: %u, index: %u)", mCount, index);
    #endif
    return mBuffer[index];
}

template<typename _T, uint32 _MaxCount>
inline const _T* StaticArray<_T, _MaxCount>::Ptr() const
{
    return reinterpret_cast<const _T*>(mBuffer);
}

template<typename _T, uint32 _MaxCount>
inline _T* StaticArray<_T, _MaxCount>::Ptr()
{
    return reinterpret_cast<_T*>(mBuffer);
}

template <typename _T, uint32 _MaxCount> 
inline uint32 StaticArray<_T, _MaxCount>::Find(const _T& value)
{
    for (uint32 i = 0; i < mCount; i++) {
        if (mBuffer[i] == value)
            return i;
    }
    return UINT32_MAX;
}

template <typename _T, uint32 _MaxCount>
template <typename _Func> inline uint32 StaticArray<_T, _MaxCount>::FindIf(_Func findFunc)
{
    for (uint32 i = 0, c = mCount; i < c; i++) {
        if (findFunc(mBuffer[i])) {
            return i;
        }
    }
    
    return UINT32_MAX;
}

//------------------------------------------------------------------------
// @impl HandlePool
#ifndef __OBJC__
template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline HandlePool<_HandleType, _DataType, _Reserve>::HandlePool(void* data, size_t size) :
    mItems((uint8*)data + GetMemoryRequirement(), size - GetMemoryRequirement())
{
    mHandles = _private::handleCreatePoolTableWithBuffer(_Reserve, data, GetMemoryRequirement());
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
void HandlePool<_HandleType, _DataType, _Reserve>::Reserve(uint32 capacity, void* buffer, size_t size)
{
    capacity = Max(capacity, _Reserve);
    ASSERT_MSG(mHandles == nullptr, "pool should be freed/uninitialized before reserve by pointer");
    mAlloc = nullptr;

    size_t tableSize = _private::handleGetMemoryRequirement(capacity);
    ASSERT(tableSize <= size);
    mHandles = _private::handleCreatePoolTableWithBuffer(capacity, buffer, tableSize);

    void* arrayBuffer = reinterpret_cast<uint8*>(buffer) + tableSize;
    ASSERT(reinterpret_cast<uintptr_t>(arrayBuffer)%CONFIG_MACHINE_ALIGNMENT == 0);
    mItems.Reserve(capacity, arrayBuffer, size - tableSize);
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
void HandlePool<_HandleType, _DataType, _Reserve>::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(mHandles == nullptr, "pool should be freed/uninitialized before setting allocator");
    mAlloc = alloc;
    mItems.SetAllocator(mAlloc);
}

template <typename _HandleType, typename _DataType, uint32 _Reserve>
void HandlePool<_HandleType, _DataType, _Reserve>::CopyTo(HandlePool<_HandleType, _DataType, _Reserve>* otherPool) const
{
    ASSERT_MSG(otherPool->mHandles == nullptr, "other pool should be uninitialized before cloning");
    otherPool->mHandles = _private::handleClone(mHandles, otherPool->mAlloc);
    mItems.CopyTo(&otherPool->mItems);
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline _HandleType HandlePool<_HandleType, _DataType, _Reserve>::Add(const _DataType& item, _DataType* prevItem)
{
    if (mHandles == nullptr) {
        ASSERT(mAlloc);
        mHandles = _private::handleCreatePoolTable(_Reserve, mAlloc);
    } 
    else if (mHandles->count == mHandles->capacity) {
        if (mAlloc) {
           Grow();
        }
        else {
            ASSERT_MSG(0, "HandlePool overflow, capacity=%u", mHandles->capacity);
        }
    }

    _HandleType handle(_private::handleNew(mHandles));
    uint32 index = handle.GetSparseIndex();
    if (index >= mItems.Count()) {
        mItems.Push(item);
        if (prevItem)
            *prevItem = _DataType {};
    }
    else {
        if (prevItem) 
            *prevItem = mItems[index];
        mItems[index] = item;
    }

    return handle;
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline void HandlePool<_HandleType, _DataType, _Reserve>::Remove(_HandleType handle)
{
    ASSERT(mHandles);
    _private::handleDel(mHandles, static_cast<uint32>(handle));
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline uint32 HandlePool<_HandleType, _DataType, _Reserve>::Count() const
{
    return mHandles ? mHandles->count : 0;
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline void HandlePool<_HandleType, _DataType, _Reserve>::Clear()
{
    if (mHandles)
        _private::handleResetPoolTable(mHandles);
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline bool HandlePool<_HandleType, _DataType, _Reserve>::IsValid(_HandleType handle)
{
    ASSERT(mHandles);
    return _private::handleIsValid(mHandles, static_cast<uint32>(handle));
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline _HandleType HandlePool<_HandleType, _DataType, _Reserve>::HandleAt(uint32 index)
{
    ASSERT(mHandles);
    return _HandleType(_private::handleAt(mHandles, index));
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline _DataType& HandlePool<_HandleType, _DataType, _Reserve>::Data(uint32 index)
{
    _HandleType handle = HandleAt(index);
    return mItems[handle.GetSparseIndex()];
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline _DataType& HandlePool<_HandleType, _DataType, _Reserve>::Data(_HandleType handle)
{
    ASSERT(mHandles);
    ASSERT_MSG(IsValid(handle), "Invalid handle (%u): Generation=%u, SparseIndex=%u", 
               uint32(handle), handle.GetGen(), handle.GetSparseIndex());
    return mItems[handle.GetSparseIndex()];
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline void HandlePool<_HandleType, _DataType, _Reserve>::Free()
{
    if (mAlloc) {
        if (mHandles) 
            _private::handleDestroyPoolTable(mHandles, mAlloc);
        mItems.Free();
        mHandles = nullptr;
    }
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
template<typename _Func> inline _HandleType HandlePool<_HandleType, _DataType, _Reserve>::FindIf(_Func findFunc)
{
    if (mHandles) {
        for (uint32 i = 0, c = mHandles->count; i < c; i++) {
            _HandleType h = _HandleType(_private::handleAt(mHandles, i));
            if (findFunc(mItems[h.GetSparseIndex()]))
                return h;
        }
    }
    
    return _HandleType();
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline bool HandlePool<_HandleType, _DataType, _Reserve>::IsFull() const
{
    return !mHandles && mHandles->count == mHandles->capacity;
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline uint32 HandlePool<_HandleType, _DataType, _Reserve>::Capacity() const
{
    return mHandles ? mHandles->capacity : _Reserve;
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline size_t HandlePool<_HandleType, _DataType, _Reserve>::GetMemoryRequirement(uint32 capacity)
{
    return _private::handleGetMemoryRequirement(capacity) + Array<_DataType>::GetMemoryRequirement(capacity);
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline bool HandlePool<_HandleType, _DataType, _Reserve>::Grow()
{
    ASSERT(mAlloc);
    ASSERT(mHandles);
       
    mItems.Reserve(mHandles->capacity << 1);
    return _private::handleGrowPoolTable(&mHandles, mAlloc);
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline bool HandlePool<_HandleType, _DataType, _Reserve>::Grow(void* data, size_t size)
{
    ASSERT(!mAlloc);
    ASSERT(mHandles);

    uint32 newCapacity = mHandles->capacity << 1;
    size_t handleTableSize = GetMemoryRequirement(newCapacity);
    ASSERT(handleTableSize < size);

    mItems.Reserve(mHandles->capacity << 1, (uint8*)data + handleTableSize, size - handleTableSize);
    return _private::handleGrowPoolTableWithBuffer(&mHandles, data, handleTableSize);
}
#endif // !__OBJC__

//------------------------------------------------------------------------
// @impl RingBuffer
inline RingBuffer::RingBuffer(void* buffer, size_t size)
{
    ASSERT(buffer);
    ASSERT(size);

    mCapacity = size;
    mBuffer = reinterpret_cast<uint8*>(buffer);
}

inline void RingBuffer::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(mBuffer == nullptr, "buffer should be freed/uninitialized before setting allocator");
    mAlloc = alloc;
}

inline void RingBuffer::Reserve(size_t capacity)
{
    ASSERT(mAlloc);
    mCapacity = Max(capacity, mCapacity);
    mBuffer = reinterpret_cast<uint8*>(memRealloc(mBuffer, mCapacity, mAlloc));
    ASSERT(mBuffer);
}

inline void RingBuffer::Reserve(void* buffer, size_t size)
{
    ASSERT_MSG(mBuffer == nullptr, "RingBuffer must not get used before setting buffer pointer");
    ASSERT(buffer);
    
    mCapacity = size;
    mBuffer = reinterpret_cast<uint8*>(buffer);
    mAlloc = nullptr;
}

inline void RingBuffer::Free()
{
    if (mAlloc) {
        memFree(mBuffer, mAlloc);
        mCapacity = mSize = mStart = mEnd = 0;
        mBuffer = nullptr;
    }
}

inline size_t RingBuffer::GetMemoryRequirement(size_t capacity)
{
    return capacity;
}

inline size_t RingBuffer::ExpectWrite() const
{
    return mCapacity - mSize;
}

inline void RingBuffer::Write(const void* src, size_t size)
{
    ASSERT(size <= ExpectWrite());
    
    uint8* buff = mBuffer;
    const uint8* udata = reinterpret_cast<const uint8*>(src);
    size_t remain = mCapacity - mEnd;
    if (remain >= size) {
        memcpy(&buff[mEnd], udata, size);
    } else {
        memcpy(&buff[mEnd], udata, remain);
        memcpy(buff, &udata[remain], size - remain);
    }
    
    mEnd = (mEnd + size) % mCapacity;
    mSize += size;
}

inline size_t RingBuffer::Read(void* dst, size_t size)
{
    ASSERT(size > 0);
    
    size = Min(size, mSize);
    if (size == 0)
        return 0;
    
    if (dst) {
        uint8* buff = mBuffer;
        uint8* udata = reinterpret_cast<uint8*>(dst);
        size_t remain = mCapacity - mStart;
        if (remain >= size) {
            memcpy(udata, &buff[mStart], size);
        } else {
            memcpy(udata, &buff[mStart], remain);
            memcpy(&udata[remain], buff, size - remain);
        }
    }
    
    mStart = (mStart + size) % mCapacity;
    mSize -= size;
    return size;
}

inline size_t RingBuffer::Peek(void* dst, size_t size, size_t* pOffset)
{
    ASSERT(size > 0);
    
    size = Min(size, mSize);
    if (size == 0)
        return 0;
    
    ASSERT(dst);
    uint8* buff = mBuffer;
    uint8* udata = reinterpret_cast<uint8*>(dst);
    size_t _offset = pOffset ? *pOffset : mStart;
    size_t remain = mCapacity - _offset;
    if (remain >= size) {
        memcpy(udata, &buff[_offset], size);
    } else {
        memcpy(udata, &buff[_offset], remain);
        memcpy(&udata[remain], buff, (size_t)size - (size_t)remain);
    }
    
    if (pOffset)
        *pOffset = (*pOffset + size) % mCapacity;

    return size;
}

template <typename _T> inline void RingBuffer::Write(const _T& src)
{
    Write(&src, sizeof(_T));
}

template <typename _T> inline size_t RingBuffer::Read(_T* dst)
{
    return Read(dst, sizeof(_T));
}

inline size_t RingBuffer::Capacity() const
{
    return mCapacity;
}

//------------------------------------------------------------------------
// @impl Blob
inline Blob::Blob(void* buffer, size_t size)
{
    ASSERT(buffer && size);
    mBuffer = buffer;
    mCapacity = size;
}

inline void Blob::Attach(void* data, size_t size, Allocator* alloc)
{
    ASSERT(data);
    ASSERT_MSG(!mBuffer, "buffer should be freed before attach");
    mAlloc = alloc;
    mGrowPolicy = GrowPolicy::None;
    mBuffer = data;
    mOffset = 0;
    mSize = size;
    mCapacity = size;
}

inline void Blob::Detach(void** outData, size_t* outSize)
{
    ASSERT(outData);
    ASSERT(outSize);

    *outData = mBuffer;
    *outSize = mSize;

    mBuffer = nullptr;
    mSize = 0;
    mOffset = 0;
    mCapacity = 0;
}

inline void Blob::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(!mBuffer, "SetAllocator must be called before using/initializing the Blob");
    mAlloc = alloc;
}

inline void Blob::SetSize(size_t size)
{
    ASSERT_MSG(size <= mCapacity, "Size cannot be larger than capacity");
    mSize = size;
}

inline void Blob::Reserve(size_t capacity)
{
    ASSERT_MSG(mAlloc, "Allocator must be set for dynamic Reserve");
    ASSERT(capacity > mSize);

    mBuffer = memReallocAligned(mBuffer, capacity, mAlign, mAlloc);
    mCapacity = capacity;
}

inline void Blob::Reserve(void* buffer, size_t size)
{
    ASSERT(size > mSize);
    ASSERT(PtrToInt<uint64>(buffer) % mAlign == 0);
    ASSERT(mBuffer == nullptr);

    mBuffer = buffer;
    mCapacity = size;
    mAlloc = nullptr;
}

inline void Blob::Free()
{
    if (mAlloc)
        memFreeAligned(mBuffer, mAlign, mAlloc);
    mBuffer = nullptr;
    mSize = 0;
    mCapacity = 0;
    mAlloc = nullptr;
}

inline void Blob::ResetRead() 
{
    mOffset = 0;
}

inline void Blob::ResetWrite()
{
    mSize = 0;
}

inline void Blob::Reset()
{
    mOffset = 0;
    mSize = 0;
}

inline void Blob::SetOffset(size_t offset) 
{
    ASSERT(mOffset < mSize);
    mOffset = offset;
}

inline size_t Blob::Write(const void* src, size_t size)
{
    ASSERT(src);
    ASSERT(size);

    size_t writeBytes = Min(mCapacity - mSize, size);
    if (writeBytes < size) {
        ASSERT_MSG(mAlloc, "Growable blobs should have allocator");
        ASSERT_MSG(mGrowPolicy != GrowPolicy::None, "Growable blobs should have a grow policy");
        ASSERT(mGrowCount);

        if (mGrowPolicy == GrowPolicy::Linear) {
            mCapacity += mGrowCount;
            mBuffer = memReallocAligned(mBuffer, mCapacity, mAlign, mAlloc);
        }
        else if (mGrowPolicy == GrowPolicy::Multiply) {
            if (!mCapacity)
                mCapacity = mGrowCount;
            else
                mCapacity <<= 1;
            mBuffer = memReallocAligned(mBuffer, mCapacity, mAlign, mAlloc);
        }

        return Write(src, size);
    }

    if (writeBytes) {
        uint8* buff = reinterpret_cast<uint8*>(mBuffer);
        memcpy(buff + mSize, src, writeBytes);
        mSize += writeBytes;
    }

    #if CONFIG_VALIDATE_IO_READ_WRITES
       ASSERT(writeBytes == size);
    #endif
    return writeBytes;
}

inline size_t Blob::Read(void* dst, size_t size) const
{
    ASSERT(dst);
    ASSERT(size);

    size_t readBytes = Min(mSize - mOffset, size);
    if (readBytes) {
        uint8* buff = reinterpret_cast<uint8*>(mBuffer);
        memcpy(dst, buff + mOffset, readBytes);
        const_cast<Blob*>(this)->mOffset += readBytes;
    }

    #if CONFIG_VALIDATE_IO_READ_WRITES
       ASSERT(size == readBytes);
    #endif

    return readBytes;
}

template <typename _T> 
inline size_t Blob::Write(const _T& src)
{
    return static_cast<uint32>(Write(&src, sizeof(_T)));
}

template <typename _T> 
inline size_t Blob::Read(_T* dst) const
{
    return static_cast<uint32>(Read(dst, sizeof(_T)));
}

inline size_t Blob::Size() const
{
    return mSize;
}

inline size_t Blob::ReadOffset() const
{
    return mOffset;
}

inline size_t Blob::Capacity() const
{
    return mCapacity;
}

inline const void* Blob::Data() const
{
    return mBuffer;
}

inline bool Blob::IsValid() const
{
    return mBuffer && mSize;
}

inline void Blob::CopyTo(Blob* otherBlob) const
{
    ASSERT(mSize);
    otherBlob->Reserve(mSize);
    otherBlob->SetSize(mSize);
    memcpy(otherBlob->mBuffer, mBuffer, mSize); 
}

inline void Blob::SetAlignment(uint8 align)
{
    if (align < CONFIG_MACHINE_ALIGNMENT)
        align = CONFIG_MACHINE_ALIGNMENT;
    mAlign = align;
}

inline void Blob::SetGrowPolicy(GrowPolicy policy, uint32 amount)
{
    mGrowPolicy = policy;
    mGrowCount = amount == 0 ? 4096u : AlignValue(amount, CACHE_LINE_SIZE);
}

//------------------------------------------------------------------------
// @impl PoolBuffer
template <typename _T, uint32 _Align>
inline PoolBuffer<_T, _Align>::PoolBuffer(void* buffer, size_t size)
{
    ASSERT(buffer);
    ASSERT(size > sizeof(Page));

    mPageSize = (size - sizeof(Page))/sizeof(_T);
    ASSERT_MSG(mPageSize, "Buffer size is too small");
    mPages = CreatePage(buffer, size);
}

template <typename _T, uint32 _Align>
inline void PoolBuffer<_T, _Align>::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(!mPages, "SetAllocator must be called before using/initializing the Blob");
    mAlloc = alloc;
}

template <typename _T, uint32 _Align>
inline void PoolBuffer<_T, _Align>::Reserve(uint32 pageSize)
{
    ASSERT(mAlloc);
    ASSERT(pageSize);

    mPageSize = pageSize;
    mPages = CreatePage(nullptr, 0);
}

template <typename _T, uint32 _Align>
inline void PoolBuffer<_T, _Align>::Reserve(void* buffer, size_t size, uint32 pageSize)
{
    ASSERT(buffer);
    ASSERT(size > sizeof(Page));
    ASSERT(mPages == nullptr);
    ASSERT(pageSize);
    
    mPageSize = pageSize;
    mAlloc = nullptr;
    mPages = CreatePage(buffer, size);
}

template <typename _T, uint32 _Align>
inline void PoolBuffer<_T, _Align>::Free()
{
    if (mAlloc) {
        Page* page = mPages;
        while (page) {
            Page* next = page->next; 
            if (mAlloc)
                MemSingleShotMalloc<Page>::Free(page, mAlloc);
            page = next;
        }
    }

    mPageSize = 0;
    mPages = nullptr;
}

template <typename _T, uint32 _Align>
inline size_t PoolBuffer<_T, _Align>::GetMemoryRequirement(uint32 pageSize)
{
    MemSingleShotMalloc<Page> pageBuffer;
    pageBuffer.template AddMemberField<_T*>(offsetof(Page, ptrs), pageSize);
    pageBuffer.template AddMemberField<_T>(offsetof(Page, data), pageSize, false, _Align);
    return pageBuffer.GetMemoryRequirement();
}

template <typename _T, uint32 _Align>
inline _T* PoolBuffer<_T, _Align>::New()
{
    Page* page = mPages;
    while (page && page->index == 0 && page->next)
        page = page->next;
    
    // Grow if necassory 
    if (!page || page->index == 0) {
        if (!mAlloc) {
            ASSERT_MSG(0, "Cannot allocate anymore new objects. Pool is full");
            return nullptr;
        }

        page = CreatePage(nullptr, 0);
        if (mPages) {
            Page* lastPage = mPages;
            while (lastPage->next)
                lastPage = lastPage->next;
            lastPage->next = page;
        }
        else {
            mPages = page;
        }
    }

    ASSERT(page->index);
    return page->ptrs[--page->index];
}

template <typename _T, uint32 _Align>
inline void PoolBuffer<_T, _Align>::Delete(_T* item)
{
    uint64 uptr = PtrToInt<uint64>(item);
    Page* page = mPages;
    uint32 pageSize = mPageSize;

    while (page) {
        if (uptr >= PtrToInt<uint64>(page->data) && uptr < PtrToInt<uint64>(page->data + pageSize)) {
            ASSERT_MSG(page->index != pageSize, "Cannot delete more objects from this page, possible double delete");

            page->ptrs[page->index++] = item;
            return;
        }

        page = page->next;
    }

    ASSERT_MSG(0, "Pointer doesn't belong to this pool");
}

template <typename _T, uint32 _Align>
inline bool PoolBuffer<_T, _Align>::IsFull() const
{
    Page* page = mPages;
    while (page && page->index == 0 && page->next)
        page = page->next;
    
    return !page || page->index == 0;
}

template <typename _T, uint32 _Align>
inline typename PoolBuffer<_T, _Align>::Page* PoolBuffer<_T, _Align>::CreatePage(void* buffer, size_t size)
{
    ASSERT(mPageSize);

    MemSingleShotMalloc<Page> mallocator;
    mallocator.template AddMemberField<_T*>(offsetof(Page, ptrs), mPageSize);
    mallocator.template AddMemberField<_T>(offsetof(Page, data), mPageSize, false, _Align); // Only align data buffer

    Page* page = (buffer && size) ? mallocator.Calloc(buffer, size) : page = mallocator.Calloc(mAlloc);
    page->index = mPageSize;
    for (uint32 i = 0, c = mPageSize; i < c; i++)
        page->ptrs[c - i - 1] = page->data + i;
    return page;
}
