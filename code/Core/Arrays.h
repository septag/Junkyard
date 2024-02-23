#pragma once

// Array: Regular growable array, very much like std::vector but with some major design simplifications
//      - Arrays are meant to be used with POD (plain-old-data) structs only, so it doesn't do ctor/dtor/move and all that stuff that comes with OO
//      - It can only grow if you assign allocator to the array, otherwise, in case of buffer/size pair, growing is not supported.
//      - Array does not allocate anything in ctor or deallocate anything in dtor. 
//      - You should either pre-allocate buffer with `Reserve` method, or implicitly by calling `Add` method if allocator is set 
//      - And deallocate with `Free` or implicitly with `Detach` which will return internal pointer and detach itself from it
//      - RemoveAndSwap: Removes element by swaping the element with the last one. FAST
//      - Pop: Removes the element at index by shifting all remaining elements to the left. Preserving the order of items. SLOW.
//      - Beware not to export pointers or indexes of objects from Array if you are about to delete elements. For that purpose, use `HandlePool`
// 
// StaticArray: Pretty much same as array but with fixed capacity. Stays on stack.
// 

#include "Base.h"

//----------------------------------------------------------------------------------------------------------------------
// Array
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
    void RemoveRangeAndShift(uint32 index, uint32 endIndex = INVALID_INDEX);

    uint32 Count() const;
    uint32 Capacity() const;
    bool IsEmpty() const;
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

//----------------------------------------------------------------------------------------------------------------------
// StaticArray
template <typename _T, uint32 _MaxCount>
struct StaticArray
{
    _T* Add();
    _T* Add(const _T& item);
    void RemoveAndSwap(uint32 index);
    uint32 Count() const;
    bool IsEmpty() const;
    bool IsFull() const;
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
        _T& operator*() { return *_ptr; }
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

//----------------------------------------------------------------------------------------------------------------------
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
    Pop(index);
}

template <typename _T, uint32 _Reserve>
inline void Array<_T,_Reserve>::RemoveRangeAndShift(uint32 index, uint32 endIndex)
{
    if (endIndex == INVALID_INDEX)
        endIndex = mCount;

    #ifdef CONFIG_CHECK_OUTOFBOUNDS
    ASSERT(endIndex > index);
    ASSERT(endIndex <= mCount);
    #endif

    uint32 removeCount = endIndex - index;
    for (uint32 i = 0; i < removeCount; i++) {
        uint32 start = i + index;
        uint32 end = i + endIndex;
        if (end < mCount)
            mBuffer[start] = mBuffer[end];
    }

    mCount -= removeCount;
}

template <typename _T, uint32 _Reserve>
inline uint32 Array<_T,_Reserve>::Count() const
{
    return mCount;
}

template <typename _T, uint32 _Reserve>
inline bool Array<_T,_Reserve>::IsEmpty() const
{
    return mCount == 0;
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
    return Pop(0);
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
    for (uint32 i = index+1, c = mCount; i < c; i++)
        mBuffer[i-1] = mBuffer[i];
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

//----------------------------------------------------------------------------------------------------------------------
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
inline bool StaticArray<_T, _MaxCount>::IsEmpty() const
{
    return mCount == 0;
}

template<typename _T, uint32 _MaxCount>
inline bool StaticArray<_T, _MaxCount>::IsFull() const
{
    return mCount == _MaxCount;
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



