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


//     █████╗ ██████╗ ██████╗  █████╗ ██╗   ██╗
//    ██╔══██╗██╔══██╗██╔══██╗██╔══██╗╚██╗ ██╔╝
//    ███████║██████╔╝██████╔╝███████║ ╚████╔╝ 
//    ██╔══██║██╔══██╗██╔══██╗██╔══██║  ╚██╔╝  
//    ██║  ██║██║  ██║██║  ██║██║  ██║   ██║   
//    ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝   ╚═╝   
template <typename _T>
struct Array
{
    Array() : Array(Mem::GetDefaultAlloc()) {}
    explicit Array(MemAllocator* alloc) : mAlloc(alloc) {}
    explicit Array(const void* buffer, size_t size);

    void SetAllocator(MemAllocator* alloc);
    void Reserve(uint32 capacity);
    void Reserve(uint32 capacity, void* buffer, size_t size);
    void Free();
    static size_t GetMemoryRequirement(uint32 capacity);

    [[nodiscard]] _T* Push();
    _T* Push(const _T& item);
    _T* PushBatch(const _T* items, uint32 numItems);

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
    void Extend(const _T* arr, uint32 numArrItems);
    const _T& operator[](uint32 index) const;
    _T& operator[](uint32 index);
    void Shrink();
    bool IsFull() const;
    const _T* Ptr() const;
    _T* Ptr();
    
    void Detach(_T** outBuffer, uint32* outCount);
    Span<_T> Detach();

    void ShiftLeft(uint32 count);
    void CopyTo(Array<_T>* otherArray) const;

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
    MemAllocator* mAlloc = nullptr;
    uint32 mCapacity = 0;
    uint32 mCount = 0;
    _T* mBuffer = nullptr;
};


//    ███████╗████████╗ █████╗ ████████╗██╗ ██████╗     █████╗ ██████╗ ██████╗  █████╗ ██╗   ██╗
//    ██╔════╝╚══██╔══╝██╔══██╗╚══██╔══╝██║██╔════╝    ██╔══██╗██╔══██╗██╔══██╗██╔══██╗╚██╗ ██╔╝
//    ███████╗   ██║   ███████║   ██║   ██║██║         ███████║██████╔╝██████╔╝███████║ ╚████╔╝ 
//    ╚════██║   ██║   ██╔══██║   ██║   ██║██║         ██╔══██║██╔══██╗██╔══██╗██╔══██║  ╚██╔╝  
//    ███████║   ██║   ██║  ██║   ██║   ██║╚██████╗    ██║  ██║██║  ██║██║  ██║██║  ██║   ██║   
//    ╚══════╝   ╚═╝   ╚═╝  ╚═╝   ╚═╝   ╚═╝ ╚═════╝    ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝   ╚═╝   
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


//    ██╗███╗   ██╗██╗     ██╗███╗   ██╗███████╗███████╗
//    ██║████╗  ██║██║     ██║████╗  ██║██╔════╝██╔════╝
//    ██║██╔██╗ ██║██║     ██║██╔██╗ ██║█████╗  ███████╗
//    ██║██║╚██╗██║██║     ██║██║╚██╗██║██╔══╝  ╚════██║
//    ██║██║ ╚████║███████╗██║██║ ╚████║███████╗███████║
//    ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝╚═╝  ╚═══╝╚══════╝╚══════╝
template <typename _T>
inline Array<_T>::Array(const void* buffer, size_t size)
{
    mCapacity = size / sizeof(_T);
    ASSERT(mCapacity);
    mBuffer = buffer;
}

template <typename _T>
inline void Array<_T>::SetAllocator(MemAllocator* alloc)
{
    ASSERT_MSG(mBuffer == nullptr, "buffer should be freed/uninitialized before setting allocator");
    mAlloc = alloc;
}    

template <typename _T>
inline _T* Array<_T>::Push()
{
    if (mCount >= mCapacity) {
        if (mAlloc) {
            Reserve(mCapacity ? (mCapacity << 1) : 8);
        } 
        else {
            ASSERT(mBuffer);
            ASSERT_MSG(mCount < mCapacity, "Array overflow, capacity=%u", mCapacity);
            return nullptr;
        }
    }
    
    return PLACEMENT_NEW(&mBuffer[mCount++], _T) {};
}

template <typename _T>
inline _T* Array<_T>::Push(const _T& item)
{
    if (mCount >= mCapacity) {
        if (mAlloc) {
            Reserve(mCapacity ? (mCapacity << 1) : 8);
        } 
        else {
            ASSERT(mBuffer);
            ASSERT_MSG(mCount < mCapacity, "Array overflow, capacity=%u", mCapacity);
            return nullptr;
        }
    }
    
    _T* dest = &mBuffer[mCount++];
    *dest = item;
    return dest;
}

template <typename _T>
inline _T* Array<_T>::PushBatch(const _T* items, uint32 numItems)
{
    ASSERT(items);
    ASSERT(numItems);

    uint32 targetCount = mCount + numItems;
    if (targetCount > mCapacity) {
        if (mAlloc) {
            Reserve(AlignValue(targetCount, 8u));
        }
        else {
            ASSERT(mBuffer);
            ASSERT_MSG(mCount < mCapacity, "Array overflow, capacity=%u", mCapacity);
            return nullptr;
        }
    }

    _T* dest = &mBuffer[mCount];
    memcpy(dest, items, sizeof(_T)*numItems);
    mCount += numItems;

    return dest;
}

template <typename _T>
inline void Array<_T>::RemoveAndSwap(uint32 index)
{
    ASSERT(mBuffer);
    #ifdef CONFIG_CHECK_OUTOFBOUNDS
    ASSERT_MSG(index < mCount, "Index out of bounds (count: %u, index: %u)", mCount, index);
    #endif
    --mCount;
    if (index < mCount)
        Swap<_T>(mBuffer[index], mBuffer[mCount]);
}

template <typename _T>
inline void Array<_T>::RemoveAndShift(uint32 index)
{
    Pop(index);
}

template <typename _T>
inline void Array<_T>::RemoveRangeAndShift(uint32 index, uint32 endIndex)
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

template <typename _T>
inline uint32 Array<_T>::Count() const
{
    return mCount;
}

template <typename _T>
inline bool Array<_T>::IsEmpty() const
{
    return mCount == 0;
}

template <typename _T>
inline uint32 Array<_T>::Capacity() const
{
    return mCapacity;
}

template <typename _T>
inline void Array<_T>::Reserve(uint32 capacity)
{
    ASSERT(mAlloc);
    if (capacity > mCapacity) {
        mCapacity = capacity;
        mBuffer = Mem::ReallocTyped<_T>(mBuffer, mCapacity, mAlloc);
        ASSERT(mBuffer);
    }
}

template <typename _T>
inline void Array<_T>::Reserve(uint32 capacity, void* buffer, [[maybe_unused]] size_t size)
{
    capacity = Max(capacity, 8u);

    ASSERT(buffer);
    ASSERT_MSG(mBuffer == nullptr, "Array should not be initialized before reserve by pointer");
    ASSERT_MSG(size >= capacity*sizeof(_T), "Buffer should have at least %u bytes long (size=%u)", capacity*sizeof(_T), size);
    
    mAlloc = nullptr;
    mCapacity = capacity;
    mBuffer = (_T*)buffer;
}    

template <typename _T>
inline void Array<_T>::Clear()
{
    mCount = 0;
}

template <typename _T>
inline _T& Array<_T>::Last()
{
    ASSERT(mCount > 0);
    return mBuffer[mCount - 1];
}

template <typename _T>
inline _T Array<_T>::PopLast()
{
    ASSERT(mCount > 0);
    return mBuffer[--mCount];
}

template <typename _T>
inline _T Array<_T>::PopFirst()
{
    return Pop(0);
}

template <typename _T>
inline _T Array<_T>::Pop(uint32 index)
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

template <typename _T>
inline void Array<_T>::Extend(const Array<_T>& arr)
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

template <typename _T>
inline void Array<_T>::Extend(const _T* arr, uint32 numArrItems)
{
    if (numArrItems) {
        uint32 newCount = mCount + numArrItems;
        uint32 newCapacity = Max(newCount, mCapacity);
        if (newCapacity > mCapacity)
            Reserve(newCapacity);
        memcpy(&mBuffer[mCount], arr, sizeof(_T)*numArrItems);
        mCount = newCount;
    }
}

template <typename _T>
inline void Array<_T>::ShiftLeft(uint32 count)
{
    ASSERT(count <= mCount);
    
    mCount -= count;
    if (mCount)
        memmove(mBuffer, mBuffer + sizeof(_T)*count, sizeof(_T)*mCount);
}

template <typename _T>
inline void Array<_T>::CopyTo(Array<_T>* otherArray) const
{
    ASSERT(otherArray);

    if (mCapacity)
        otherArray->Reserve(mCapacity);

    if (mCount) {
        otherArray->mCount = mCount;
        memcpy(otherArray->mBuffer, mBuffer, sizeof(_T)*mCount);
    }
}

template <typename _T>
inline const _T& Array<_T>::operator[](uint32 index) const
{
    #ifdef CONFIG_CHECK_OUTOFBOUNDS
    ASSERT_MSG(index < mCount, "Index out of bounds (count: %u, index: %u)", mCount, index);
    #endif
    return mBuffer[index];
}

template <typename _T>
inline _T& Array<_T>::operator[](uint32 index)
{
    #ifdef CONFIG_CHECK_OUTOFBOUNDS
    ASSERT_MSG(index < mCount, "Index out of bounds (count: %u, index: %u)", mCount, index);
    #endif
    return mBuffer[index];
}

template <typename _T>
inline const _T* Array<_T>::Ptr() const
{
    return mBuffer;
}

template <typename _T>
inline _T* Array<_T>::Ptr()
{
    return mBuffer;
}

template <typename _T>
inline void Array<_T>::Detach(_T** outBuffer, uint32* outCount)
{
    ASSERT(outBuffer);
    ASSERT(outCount);

    *outBuffer = mBuffer;
    *outCount = mCount;

    mBuffer = nullptr;
    mCount = 0;
    mCapacity = 0;
}

template <typename _T>
inline Span<_T> Array<_T>::Detach()
{
    _T* ptr;
    uint32 count;

    Detach(&ptr, &count);
    return Span<_T>(ptr, count);
}

template<typename _T>
inline bool Array<_T>::IsFull() const
{
    return mCount >= mCapacity;
}    

template <typename _T>
inline void Array<_T>::Free()
{
    mCount = 0;

    if (mAlloc) {
        Mem::Free(mBuffer, mAlloc);
        mCapacity = 0;
        mBuffer = nullptr;
    }
}

template <typename _T>
inline void Array<_T>::Shrink()
{
    ASSERT(mAlloc);
    mCapacity = Max(mCount, 8u);
    Reserve(mCapacity);
}

template <typename _T> 
inline uint32 Array<_T>::Find(const _T& value)
{
    for (uint32 i = 0; i < mCount; i++) {
        if (mBuffer[i] == value)
            return i;
    }

    return UINT32_MAX;
}

template<typename _T>
template<typename _Func> inline uint32 Array<_T>::FindIf(_Func findFunc)
{
    for (uint32 i = 0, c = mCount; i < c; i++) {
        if (findFunc(mBuffer[i]))
            return i;
    }

    return UINT32_MAX;
}

template<typename _T>
inline size_t Array<_T>::GetMemoryRequirement(uint32 capacity)
{
    capacity = Max(capacity, 8u);
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



