#pragma once

#include "Memory.h"
#include "BlitSort.h"

// Growable array
template <typename _T, uint32 _Reserve = 8>
struct Array
{
    // Constructors never allocates anything, you either have to call Reserve explicitly or Add something
    // However, destructor Frees memory if it's not Freed before, but it's recommended to Free manually
    Array() : Array(memDefaultAlloc()) {}
    explicit Array(Allocator* alloc) : _alloc(alloc) {}
    explicit Array(const void* buffer, size_t size);

    void SetAllocator(Allocator* alloc);
    void Reserve(uint32 capacity);
    void Reserve(uint32 capacity, void* buffer, size_t size);
    void Free();
    static size_t GetMemoryRequirement(uint32 capacity = _Reserve);

    [[nodiscard]] _T* Push();
    _T* Push(const _T& item);
    void RemoveAndSwap(uint32 index);
    uint32 Count() const;
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
    void Detach(_T** outBuffer, uint32* outCount);
    void ShiftLeft(uint32 count);

    // _Cmp = [capture](const _T& a, const _T& b)->int 
    template <typename _Cmp> void Sort(_Cmp cmpFunc);

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

    Iterator begin()    { return Iterator(&_buffer[0]); }
    Iterator end()      { return Iterator(&_buffer[_count]); }

    Iterator begin() const    { return Iterator(&_buffer[0]); }
    Iterator end() const     { return Iterator(&_buffer[_count]); }

private:
    Allocator* 	    _alloc = nullptr;
    uint32 			_capacity = 0;
    uint32 			_count = 0;
    _T*				_buffer = nullptr;
};

// Fixed stack array
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

    // _Cmp = [capture](const _T& a, const _T& b)->int 
    template <typename _Cmp> void Sort(_Cmp cmpFunc);

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
    
    Iterator begin()    { return Iterator(&_buffer[0]); }
    Iterator end()      { return Iterator(&_buffer[_count]); }

private:
    uint32	_count = 0;
    _T		_buffer[_MaxCount];
};

//------------------------------------------------------------------------
template <typename _T, uint32 _Reserve>
inline Array<_T,_Reserve>::Array(const void* buffer, size_t size)
{
    ASSERT_MSG(size > _Reserve*sizeof(_T), "Buffer should have at least %u bytes long", _Reserve*sizeof(_T));

    _capacity = size / sizeof(_T);
    _buffer = buffer;
}

template <typename _T, uint32 _Reserve>
inline void Array<_T, _Reserve>::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(_buffer == nullptr, "buffer should be freed/uninitialized before setting allocator");
    _alloc = alloc;
}    

template <typename _T, uint32 _Reserve>
inline _T* Array<_T,_Reserve>::Push()
{
    if (_count >= _capacity) {
        if (_alloc) {
            Reserve(_capacity ? (_capacity << 1) : _Reserve);
        } 
        else {
            ASSERT(_buffer);
            ASSERT_MSG(_count < _capacity, "Array overflow, capacity=%u", _capacity);
            return nullptr;
        }
    }
    
    return &_buffer[_count++];
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
    ASSERT(_buffer);
    #ifdef CONFIG_CHECK_OUTOFBOUNDS
        ASSERT_MSG(index < _count, "Index out of bounds (count: %u, index: %u)", _count, index);
    #endif
    --_count;
    if (index < _count)
        Swap<_T>(_buffer[index], _buffer[_count]);
}

template <typename _T, uint32 _Reserve>
inline uint32 Array<_T,_Reserve>::Count() const
{
    return _count;
}

template <typename _T, uint32 _Reserve>
inline void Array<_T,_Reserve>::Reserve(uint32 capacity)
{
    ASSERT(_alloc);
    _capacity = Max(capacity, _capacity);
    _buffer = memReallocTyped<_T>(_buffer, _capacity, _alloc);
    ASSERT(_buffer);
}

template <typename _T, uint32 _Reserve>
inline void Array<_T,_Reserve>::Reserve(uint32 capacity, void* buffer, [[maybe_unused]] size_t size)
{
    capacity = Max(capacity, _Reserve);

    ASSERT(buffer);
    ASSERT_MSG(_buffer == nullptr, "Array should not be initialized before reserve by pointer");
    ASSERT_MSG(size >= capacity*sizeof(_T), "Buffer should have at least %u bytes long (size=%u)", capacity*sizeof(_T), size);
    
    _alloc = nullptr;
    _capacity = capacity;
    _buffer = (_T*)buffer;
}    

template <typename _T, uint32 _Reserve>
inline void Array<_T,_Reserve>::Clear()
{
    _count = 0;
}

template <typename _T, uint32 _Reserve>
inline _T& Array<_T,_Reserve>::Last()
{
    ASSERT(_count > 0);
    return _buffer[_count - 1];
}

template <typename _T, uint32 _Reserve>
inline _T Array<_T,_Reserve>::PopLast()
{
    ASSERT(_count > 0);
    return _buffer[--_count];
}

template <typename _T, uint32 _Reserve>
inline _T Array<_T,_Reserve>::PopFirst()
{
    ASSERT(_count > 0);
    _T first = _buffer[0];
    // shuffle all items to the left
    for (uint32 i = 1, c = _count; i < c; i++) {
        _buffer[i-1] = _buffer[i];
    }
    --_count;
    return first;
}

template <typename _T, uint32 _Reserve>
inline _T Array<_T,_Reserve>::Pop(uint32 index)
{
    ASSERT(_count > 0);
    #ifdef CONFIG_CHECK_OUTOFBOUNDS
        ASSERT_MSG(index <= _count, "Index out of bounds (count: %u, index: %u)", _count, index);
    #endif

    _T item = _buffer[index];
    // shuffle all items to the left
    for (uint32 i = index+1, c = _count; i < c; i++) {
        _buffer[i-1] = _buffer[i];
    }
    --_count;
    return item;
}

template <typename _T, uint32 _Reserve>
inline void Array<_T,_Reserve>::Extend(const Array<_T>& arr)
{
    if (arr.Count()) {
        uint32 newCount = _count + arr._count;
        uint32 newCapacity = Max(newCount, Min(_capacity, arr._capacity));
        if (newCapacity > _capacity)
            Reserve(newCapacity);
        memcpy(&_buffer[_count], arr._buffer, sizeof(_T)*arr._count);
        _count = newCount;
    }
}

template <typename _T, uint32 _Reserve>
inline void Array<_T,_Reserve>::ShiftLeft(uint32 count)
{
    ASSERT(count <= _count);
    
    _count -= count;
    if (_count)
        memmove(_buffer, _buffer + sizeof(_T)*count, sizeof(_T)*_count);
}

template <typename _T, uint32 _Reserve>
inline const _T& Array<_T,_Reserve>::operator[](uint32 index) const
{
    #ifdef CONFIG_CHECK_OUTOFBOUNDS
        ASSERT_MSG(index <= _count, "Index out of bounds (count: %u, index: %u)", _count, index);
    #endif
    return _buffer[index];
}

template <typename _T, uint32 _Reserve>
inline _T& Array<_T,_Reserve>::operator[](uint32 index)
{
    #ifdef CONFIG_CHECK_OUTOFBOUNDS
        ASSERT_MSG(index <= _count, "Index out of bounds (count: %u, index: %u)", _count, index);
    #endif
    return _buffer[index];
}

template <typename _T, uint32 _Reserve>
inline const _T* Array<_T,_Reserve>::Ptr() const
{
    return _buffer;
}

template <typename _T, uint32 _Reserve>
inline void Array<_T,_Reserve>::Detach(_T** outBuffer, uint32* outCount)
{
    ASSERT(outBuffer);
    ASSERT(outCount);

    *outBuffer = _buffer;
    *outCount = _count;

    _buffer = nullptr;
    _count = 0;
    _capacity = 0;
}

template<typename _T, uint32 _Reserve>
inline bool Array<_T,_Reserve>::IsFull() const
{
    return _count >= _capacity;
}    

template <typename _T, uint32 _Reserve>
inline void Array<_T, _Reserve>::Free()
{
    _count = 0;

    if (_alloc) {
        memFree(_buffer, _alloc);
        _capacity = 0;
        _buffer = nullptr;
    }
}

template <typename _T, uint32 _Reserve>
inline void Array<_T, _Reserve>::Shrink()
{
    ASSERT(_alloc);
    _capacity = Max(_count, _Reserve);
    Reserve(_capacity);
}


template<typename _T, uint32 _Reserve>
template <typename _Cmp> inline void Array<_T, _Reserve>::Sort(_Cmp cmpFunc)
{
    if (_count > 1)
        BlitSort<_T, _Cmp>(_buffer, _count, cmpFunc);
}

template<typename _T, uint32 _Reserve>
template<typename _Func> inline uint32 Array<_T, _Reserve>::FindIf(_Func findFunc)
{
    for (uint32 i = 0, c = _count; i < c; i++) {
        if (findFunc(_buffer[i]))
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

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
template<typename _T, uint32 _MaxCount>
inline _T* StaticArray<_T, _MaxCount>::Add()
{
    ASSERT_MSG(_count < _MaxCount, "Trying to add more than _MaxCount=%u", _MaxCount);
    return &_buffer[_count++];
}

template<typename _T, uint32 _MaxCount>
inline _T* StaticArray<_T, _MaxCount>::Add(const _T& item)
{
    ASSERT_MSG(_count < _MaxCount, "Trying to add more than _MaxCount=%u", _MaxCount);
    uint32 index = _count++;
    _buffer[index] = item;
    return &_buffer[index];
}

template<typename _T, uint32 _MaxCount>
inline void StaticArray<_T, _MaxCount>::RemoveAndSwap(uint32 index)
{
    ASSERT(_buffer);
    #ifdef CONFIG_CHECK_OUTOFBOUNDS
        ASSERT_MSG(index <= _count, "Index out of bounds (count: %u, index: %u)", _count, index);
    #endif
    Swap<_T>(_buffer[index], _buffer[--_count]);
}

template<typename _T, uint32 _MaxCount>
inline uint32 StaticArray<_T, _MaxCount>::Count() const
{
    return _count;
}

template<typename _T, uint32 _MaxCount>
inline void StaticArray<_T, _MaxCount>::Clear()
{
    _count = 0;
}

template<typename _T, uint32 _MaxCount>
inline _T& StaticArray<_T, _MaxCount>::Last()
{
    ASSERT(_count > 0);
    return _buffer[_count - 1];
}

template<typename _T, uint32 _MaxCount>
inline _T& StaticArray<_T, _MaxCount>::RemoveLast()
{
    ASSERT(_count > 0);
    return _buffer[--_count];
}

template<typename _T, uint32 _MaxCount>
inline const _T& StaticArray<_T, _MaxCount>::operator[](uint32 index) const
{
    #ifdef CONFIG_CHECK_OUTOFBOUNDS
        ASSERT_MSG(index <= _count, "Index out of bounds (count: %u, index: %u)", _count, index);
    #endif
    return _buffer[index];
}

template<typename _T, uint32 _MaxCount>
inline _T& StaticArray<_T, _MaxCount>::operator[](uint32 index)
{
    #ifdef CONFIG_CHECK_OUTOFBOUNDS
        ASSERT_MSG(index <= _count, "Index out of bounds (count: %u, index: %u)", _count, index);
    #endif
    return _buffer[index];
}

template<typename _T, uint32 _MaxCount>
inline const _T* StaticArray<_T, _MaxCount>::Ptr() const
{
    return reinterpret_cast<const _T*>(_buffer);
}

template<typename _T, uint32 _MaxCount>
template <typename _Cmp> inline void StaticArray<_T, _MaxCount>::Sort(_Cmp cmpFunc)
{
    if (_count > 1)
        BlitSort<_T, _Cmp>(_buffer, _count, cmpFunc);
}

template<typename _T, uint32 _MaxCount>
template<typename _Func> inline uint32 StaticArray<_T, _MaxCount>::FindIf(_Func findFunc)
{
    for (uint32 i = 0, c = _count; i < c; i++) {
        if (findFunc(_buffer[i])) {
            return i;
        }
    }
    
    return UINT32_MAX;
}

