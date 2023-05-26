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
// BuffersAllocPOD: POD struct continous allocator. If you have POD structs that contain some buffers and arrays
//              	You can create all of the buffers in one malloc call with the help of this allocator
//					Relative pointers are also supported for member variables, as shown in the example below.
//					Example:
//						struct SomeObject
//						{
//						    uint32 a;
//						    uint32 b;
//						};
//						
//						struct SomeStruct
//						{
//						    int count;
//						    RelativePtr<SomeObject> objects;
//						};
//						
//						BuffersAllocPOD<SomeStruct> alloc;
//						SomeStruct* s = alloc.AddMemberField<SomeObject>(offsetof(SomeStruct, objects), 100, true).Calloc();
//						s->count = 100;
//						
//						for (int i = 0; i < s->count; i++) {
//						    s->objects[i].a = (uint32)i;
//						    s->objects[i].b = (uint32)(i*2);
//						}
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
#include "Memory.h"

//------------------------------------------------------------------------
template <typename _T, uint32 _Reserve = 8>
struct Array
{
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
    _T* Ptr();
    void Detach(_T** outBuffer, uint32* outCount);
    void ShiftLeft(uint32 count);
    void CopyTo(Array<_T, _Reserve>* otherArray);

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
    
    Iterator begin()    { return Iterator(&_buffer[0]); }
    Iterator end()      { return Iterator(&_buffer[_count]); }

private:
    uint32	_count = 0;
    _T		_buffer[_MaxCount];
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

template <typename _T>
struct Handle
{
    Handle() = default;
    Handle(const Handle<_T>&) = default;
    explicit Handle(uint32 _id) : id(_id) {}
    Handle<_T>& operator=(const Handle<_T>&) = default;

    void Set(uint32 gen, uint32 index) { id = ((gen & _private::kHandleGenMask)<<_private::kHandleGenShift) | (index&_private::kHandleIndexMask); }
    explicit operator uint32() const { return id; }
    uint32 GetSparseIndex() { return id & _private::kHandleIndexMask; }
    uint32 GetGen() { return (id >> _private::kHandleGenShift) & _private::kHandleGenMask; }
    bool IsValid() const { return id != 0; }
    bool operator==(const Handle<_T>& v) const { return id == v.id; }
    bool operator!=(const Handle<_T>& v) const { return id != v.id; }

    uint32 id = 0;
};

#define DEFINE_HANDLE(_Name) struct _Name##T; using _Name = Handle<_Name##T>

namespace _private
{
    struct HandlePoolTable
    {
        uint32  count;
        uint32  capacity;
        uint32* dense;          // actual handles are stored in 'dense' array [0..arrayCount]
        uint32* sparse;         // indices to dense for removal lookup [0..arrayCapacity]
    };

    API HandlePoolTable* handleCreatePoolTable(uint32 capacity, Allocator* alloc);
    API void handleDestroyPoolTable(HandlePoolTable* tbl, Allocator* alloc);
    API bool handleGrowPoolTable(HandlePoolTable** pTbl, Allocator* alloc);

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
    explicit HandlePool(Allocator* alloc) : _alloc(alloc), _items(alloc) {}
    explicit HandlePool(void* data, size_t size); 

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

        Iterator(HandlePool_t* pool, uint32 index) : _pool(pool), _index(index) {}
        _DataType& operator*() { return _pool->Data(_index); }
        void operator++() { ++_index; }
        bool operator!=(Iterator it) { return _index != it._index; }
        HandlePool_t* _pool;
        uint32 _index;
    };
    
    Iterator begin()    { return Iterator(this, 0); }
    Iterator end()      { return Iterator(this, _handles ? _handles->count : 0); }

private:
    Allocator*                  _alloc = nullptr;
    _private::HandlePoolTable*  _handles = nullptr;
    Array<_DataType>            _items;
};

//------------------------------------------------------------------------
template <typename _T, uint32 _MaxFields = 8>
struct BuffersAllocPOD
{
    BuffersAllocPOD() : BuffersAllocPOD(CONFIG_MACHINE_ALIGNMENT) {}
    explicit BuffersAllocPOD(uint32 align);

    template <typename _FieldType> BuffersAllocPOD& AddMemberField(uint32 offsetInStruct, size_t arrayCount, 
                                                                bool relativePtr = false,
                                                                uint32 align = CONFIG_MACHINE_ALIGNMENT);
    template <typename _PodAllocType> BuffersAllocPOD& AddMemberChildPODField(const _PodAllocType& podAlloc, 
                                                                            uint32 offsetInStruct, size_t arrayCount, 
                                                                            bool relativePtr = false,
                                                                            uint32 align = CONFIG_MACHINE_ALIGNMENT);
    template <typename _FieldType> BuffersAllocPOD& AddExternalPointerField(_FieldType** pPtr, size_t arrayCount, 
                                                                            uint32 align = CONFIG_MACHINE_ALIGNMENT);

    _T* Calloc(Allocator* alloc = memDefaultAlloc());
    _T* Calloc(void* buff, size_t size);

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

    Field  _fields[_MaxFields];
    size_t _size;
    uint32 _numFields;
    uint32 _structAlign;
};

//------------------------------------------------------------------------
struct RingBuffer
{
    RingBuffer() : RingBuffer(memDefaultAlloc()) {}
    explicit RingBuffer(Allocator* alloc) : _alloc(alloc) {}
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
    Allocator*  _alloc = nullptr;
    uint8*      _buffer = nullptr;
    size_t      _capacity = 0;
    size_t      _size = 0;
    size_t      _start = 0;
    size_t      _end = 0;
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
    inline explicit Blob(Allocator* alloc) : _alloc(alloc) {}
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
    inline void ResetOffset();
    inline void SetOffset(size_t offset);
    inline void CopyTo(Blob* otherBlob) const;

    inline size_t Write(const void* src, size_t size);
    inline size_t Read(void* dst, size_t size) const;
    template <typename _T> size_t Write(const _T& src);
    template <typename _T> size_t Read(_T* dst) const;
    inline size_t WriteStringBinary(const char* str, uint32 len);
    inline size_t ReadStringBinary(char* outStr, uint32 outStrSize) const;
    
    inline size_t Size() const;
    inline size_t Capacity() const;
    inline size_t ReadOffset() const;
    inline const void* Data() const;
    inline bool IsValid() const;

private:
    Allocator* _alloc = nullptr;
    void*      _buffer = nullptr;
    size_t     _size = 0;
    size_t     _offset = 0;
    size_t     _capacity = 0;
    uint32     _align = CONFIG_MACHINE_ALIGNMENT;
    GrowPolicy _growPolicy = GrowPolicy::None;
    uint32     _growAmount = 4096u;
};

//------------------------------------------------------------------------
template <typename _T, uint32 _Align = CONFIG_MACHINE_ALIGNMENT>
struct PoolBuffer
{
    PoolBuffer() : PoolBuffer(memDefaultAlloc()) {}
    explicit PoolBuffer(Allocator* alloc) : _alloc(alloc) {}
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
        Iterator(Page* page, uint32 index, uint32 pageSize) : _page(page), _index(index), _pageSize(pageSize) {}
        _T& operator*() { return _page->data[_index]; }
        void operator++() 
        { 
            ASSERT(_page); 
            if (_index < _pageSize) 
                _index++; 
            else { 
                _page = _page->next; 
                _index = 0; 
            } 
        }
        bool operator!=(Iterator it) { return _page != it._page || _index != it._index; }

        Page* _page;
        uint32 _index;
        uint32 _pageSize;
    };

    Iterator begin()    { return Iterator(_pages, 0, _pageSize); }
    Iterator end()      
    { 
        Page* page = _pages;
        while (page && page->index == 0 && page->next)
            page = page->next;

        return Iterator(page, 0, _pageSize); 
    }

    Iterator begin() const    { return Iterator(_pages, 0, _pageSize); }
    Iterator end() const     
    { 
        Page* page = _pages;
        while (page && page->index == 0 && page->next)
            page = page->next;

        return Iterator(page, 0, _pageSize); 
    }

private:
    Allocator*  _alloc = nullptr;
    uint32      _pageSize = 32;      // maximum number of items that a page can hold
    Page*       _pages = nullptr;
};

//------------------------------------------------------------------------
// @impl Array
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
inline void Array<_T, _Reserve>::CopyTo(Array<_T, _Reserve>* otherArray)
{
    ASSERT(otherArray);

    if (this->_capacity)
        otherArray->Reserve(this->_capacity);

    if (this->_count) {
        otherArray->_count = this->_count;
        memcpy(otherArray->_buffer, this->_buffer, sizeof(_T)*this->_count);
    }
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
inline _T* Array<_T,_Reserve>::Ptr()
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

template <typename _T, uint32 _Reserve> 
inline uint32 Array<_T, _Reserve>::Find(const _T& value)
{
    for (uint32 i = 0; i < _count; i++) {
        if (_buffer[i] == value)
            return i;
    }

    return UINT32_MAX;
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

//------------------------------------------------------------------------
// @impl StaticArray
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
inline _T* StaticArray<_T, _MaxCount>::Ptr()
{
    return reinterpret_cast<_T*>(_buffer);
}

template <typename _T, uint32 _MaxCount> 
inline uint32 StaticArray<_T, _MaxCount>::Find(const _T& value)
{
    for (uint32 i = 0; i < _count; i++) {
        if (_buffer[i] == value)
            return i;
    }
    return UINT32_MAX;
}

template <typename _T, uint32 _MaxCount>
template <typename _Func> inline uint32 StaticArray<_T, _MaxCount>::FindIf(_Func findFunc)
{
    for (uint32 i = 0, c = _count; i < c; i++) {
        if (findFunc(_buffer[i])) {
            return i;
        }
    }
    
    return UINT32_MAX;
}

//------------------------------------------------------------------------
// @impl HandlePool
template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline HandlePool<_HandleType, _DataType, _Reserve>::HandlePool(void* data, size_t size) :
    _items((uint8*)data + GetMemoryRequirement(), size - GetMemoryRequirement())
{
    _handles = _private::handleCreatePoolTableWithBuffer(_Reserve, data, GetMemoryRequirement());
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
void HandlePool<_HandleType, _DataType, _Reserve>::Reserve(uint32 capacity, void* buffer, size_t size)
{
    capacity = Max(capacity, _Reserve);
    ASSERT_MSG(_handles == nullptr, "pool should be freed/uninitialized before reserve by pointer");
    _alloc = nullptr;

    size_t tableSize = _private::handleGetMemoryRequirement(capacity);
    ASSERT(tableSize <= size);
    _handles = _private::handleCreatePoolTableWithBuffer(capacity, buffer, tableSize);

    void* arrayBuffer = reinterpret_cast<uint8*>(buffer) + tableSize;
    ASSERT(reinterpret_cast<uintptr_t>(arrayBuffer)%CONFIG_MACHINE_ALIGNMENT == 0);
    _items.Reserve(capacity, arrayBuffer, size - tableSize);
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
void HandlePool<_HandleType, _DataType, _Reserve>::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(_handles == nullptr, "pool should be freed/uninitialized before setting allocator");
    _alloc = alloc;
    _items.SetAllocator(_alloc);
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline _HandleType HandlePool<_HandleType, _DataType, _Reserve>::Add(const _DataType& item, _DataType* prevItem)
{
    if (_handles == nullptr) {
        ASSERT(_alloc);
        _handles = _private::handleCreatePoolTable(_Reserve, _alloc);
    } 
    else if (_handles->count == _handles->capacity) {
        if (_alloc) {
           Grow();
        }
        else {
            ASSERT_MSG(0, "HandlePool overflow, capacity=%u", _handles->capacity);
        }
    }

    _HandleType handle(_private::handleNew(_handles));
    uint32 index = handle.GetSparseIndex();
    if (index >= _items.Count()) {
        _items.Push(item);
        if (prevItem)
            *prevItem = _DataType {};
    }
    else {
        if (prevItem) 
            *prevItem = _items[index];
        _items[index] = item;
    }

    return handle;
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline void HandlePool<_HandleType, _DataType, _Reserve>::Remove(_HandleType handle)
{
    ASSERT(_handles);
    _private::handleDel(_handles, static_cast<uint32>(handle));
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline uint32 HandlePool<_HandleType, _DataType, _Reserve>::Count() const
{
    return _handles ? _handles->count : 0;
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline void HandlePool<_HandleType, _DataType, _Reserve>::Clear()
{
    if (_handles)
        _private::handleResetPoolTable(_handles);
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline bool HandlePool<_HandleType, _DataType, _Reserve>::IsValid(_HandleType handle)
{
    ASSERT(_handles);
    return _private::handleIsValid(_handles, static_cast<uint32>(handle));
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline _HandleType HandlePool<_HandleType, _DataType, _Reserve>::HandleAt(uint32 index)
{
    ASSERT(_handles);
    return _HandleType(_private::handleAt(_handles, index));
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline _DataType& HandlePool<_HandleType, _DataType, _Reserve>::Data(uint32 index)
{
    _HandleType handle = HandleAt(index);
    return _items[handle.GetSparseIndex()];
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline _DataType& HandlePool<_HandleType, _DataType, _Reserve>::Data(_HandleType handle)
{
    ASSERT(_handles);
    ASSERT_MSG(IsValid(handle), "Invalid handle (%u): Generation=%u, SparseIndex=%u", 
               handle.id, handle.GetGen(), handle.GetSparseIndex());
    return _items[handle.GetSparseIndex()];
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline void HandlePool<_HandleType, _DataType, _Reserve>::Free()
{
    if (_alloc) {
        if (_handles) 
            _private::handleDestroyPoolTable(_handles, _alloc);
        _items.Free();
        _handles = nullptr;
    }
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
template<typename _Func> inline _HandleType HandlePool<_HandleType, _DataType, _Reserve>::FindIf(_Func findFunc)
{
    if (_handles) {
        for (uint32 i = 0, c = _handles->count; i < c; i++) {
            _HandleType h = _HandleType(_private::handleAt(_handles, i));
            if (findFunc(_items[h.GetSparseIndex()])) {
                return h;
            }
        }
    }
    
    return _HandleType();
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline bool HandlePool<_HandleType, _DataType, _Reserve>::IsFull() const
{
    return !_handles && _handles->count == _handles->capacity;
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline uint32 HandlePool<_HandleType, _DataType, _Reserve>::Capacity() const
{
    return _handles ? _handles->capacity : _Reserve;
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline size_t HandlePool<_HandleType, _DataType, _Reserve>::GetMemoryRequirement(uint32 capacity)
{
    return _private::handleGetMemoryRequirement(capacity) + Array<_DataType>::GetMemoryRequirement(capacity);
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline bool HandlePool<_HandleType, _DataType, _Reserve>::Grow()
{
    ASSERT(_alloc);
    ASSERT(_handles);
       
    _items.Reserve(_handles->capacity << 1);
    return _private::handleGrowPoolTable(&_handles, _alloc);
}

template<typename _HandleType, typename _DataType, uint32 _Reserve>
inline bool HandlePool<_HandleType, _DataType, _Reserve>::Grow(void* data, size_t size)
{
    ASSERT(!_alloc);
    ASSERT(_handles);

    uint32 newCapacity = _handles->capacity << 1;
    size_t handleTableSize = GetMemoryRequirement(newCapacity);
    ASSERT(handleTableSize < size);

    _items.Reserve(_handles->capacity << 1, (uint8*)data + handleTableSize, size - handleTableSize);
    return _private::handleGrowPoolTableWithBuffer(&_handles, data, handleTableSize);
}

//------------------------------------------------------------------------
// @impl BuffersAllocPOD
template <typename _T, uint32 _MaxFields>
inline BuffersAllocPOD<_T, _MaxFields>::BuffersAllocPOD(uint32 align)
{
    align = Max(CONFIG_MACHINE_ALIGNMENT, align);
    _size = AlignValue<size_t>(sizeof(_T), align);
    _structAlign = align;

    _fields[0].pPtr = nullptr;
    _fields[0].offset = 0;
    _fields[0].offsetInStruct = UINT32_MAX;
    _numFields = 1;
}

template <typename _T, uint32 _MaxFields>
template <typename _FieldType> inline BuffersAllocPOD<_T, _MaxFields>& 
    BuffersAllocPOD<_T, _MaxFields>::AddMemberField(uint32 offsetInStruct, size_t arrayCount, bool relativePtr, uint32 align)
{
    uint32 index = _numFields;
    ASSERT_MSG(index < _MaxFields, "Cannot add more fields, increase the _MaxFields");
    
    align = Max(CONFIG_MACHINE_ALIGNMENT, align);
    size_t size = sizeof(_FieldType) * arrayCount;
    size = AlignValue<size_t>(size, align);

    size_t offset = _size;
    if (offset % align != 0) {
        offset = AlignValue<size_t>(offset, align);
    }

    Field& buff = _fields[index];
    buff.pPtr = nullptr;
    buff.offset = offset;
    buff.offsetInStruct = offsetInStruct;
    buff.relativePtr = relativePtr;

    _size += size;
    ++_numFields;

    return *this;
}

template <typename _T, uint32 _MaxFields>
template <typename _PodAllocType> inline BuffersAllocPOD<_T, _MaxFields>& 
BuffersAllocPOD<_T, _MaxFields>::AddMemberChildPODField(const _PodAllocType& podAlloc, uint32 offsetInStruct, 
                                                        size_t arrayCount, bool relativePtr, uint32 align)
{
    uint32 index = _numFields;
    ASSERT_MSG(index < _MaxFields, "Cannot add more fields, increase the _MaxFields");
    
    align = Max(CONFIG_MACHINE_ALIGNMENT, align);
    size_t size = podAlloc.GetMemoryRequirement() * arrayCount;
    size = AlignValue<size_t>(size, align);

    size_t offset = _size;
    if (offset % align != 0) {
        offset = AlignValue<size_t>(offset, align);
    }

    Field& buff = _fields[index];
    buff.pPtr = nullptr;
    buff.offset = offset;
    buff.offsetInStruct = offsetInStruct;
    buff.relativePtr = relativePtr;

    _size += size;
    ++_numFields;

    return *this;
}


template <typename _T, uint32 _MaxFields>
template <typename _FieldType> inline BuffersAllocPOD<_T, _MaxFields>& 
    BuffersAllocPOD<_T, _MaxFields>::AddExternalPointerField(_FieldType** pPtr, size_t arrayCount, uint32 align)
{
    ASSERT(pPtr);

    uint32 index = _numFields;
    ASSERT_MSG(index < _MaxFields, "Cannot add more fields, increase the _MaxFields");
    
    align = Max(CONFIG_MACHINE_ALIGNMENT, align);
    size_t size = sizeof(_FieldType) * arrayCount;
    size = AlignValue<size_t>(size, align);
    
    size_t offset = _size;
    if (offset % align != 0) {
        offset = AlignValue<size_t>(offset, align);
    }
    
    Field& buff = _fields[index];
    buff.pPtr = (void**)pPtr;
    buff.offset = offset;
    buff.offsetInStruct = UINT32_MAX;
    buff.relativePtr = false;
    
    _size += size;
    ++_numFields;

    return *this;
}

template <typename _T, uint32 _MaxFields>
inline _T* BuffersAllocPOD<_T, _MaxFields>::Calloc(Allocator* alloc)
{
    void* mem = memAllocAligned(_size, _structAlign, alloc);
    return Calloc(mem, _size);
}

template <typename _T, uint32 _MaxFields>
inline size_t  BuffersAllocPOD<_T, _MaxFields>::GetMemoryRequirement() const
{
    return AlignValue<size_t>(_size, _structAlign);
}

template <typename _T, uint32 _MaxFields>
inline size_t BuffersAllocPOD<_T, _MaxFields>::GetSize() const
{
    return _size;
}

template <typename _T, uint32 _MaxFields>
inline _T*  BuffersAllocPOD<_T, _MaxFields>::Calloc(void* buff, [[maybe_unused]] size_t size)
{
    ASSERT(buff);
    ASSERT(size == 0 || size >= GetMemoryRequirement());

    memset(buff, 0x0, _size);
    
    uint8* tmp = (uint8*)buff;
    
    // Assign buffer pointers
    for (int i = 1, c = _numFields; i < c; i++) {
        if (_fields[i].offsetInStruct != UINT32_MAX) {
            ASSERT(_fields[i].pPtr == NULL);
            if (!_fields[i].relativePtr) 
                *((void**)(tmp + _fields[i].offsetInStruct)) = tmp + _fields[i].offset;
            else
                *((uint32*)(tmp + _fields[i].offsetInStruct)) = (uint32)_fields[i].offset - _fields[i].offsetInStruct;
        } else {
            ASSERT(_fields[i].offsetInStruct == -1);
            *_fields[i].pPtr = tmp + _fields[i].offset;
        }
    }

    return (_T*)buff;
}

//------------------------------------------------------------------------
// @impl RingBuffer
inline RingBuffer::RingBuffer(void* buffer, size_t size)
{
    ASSERT(buffer);
    ASSERT(size);

    _capacity = size;
    _buffer = reinterpret_cast<uint8*>(buffer);
}

inline void RingBuffer::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(_buffer == nullptr, "buffer should be freed/uninitialized before setting allocator");
    _alloc = alloc;
}

inline void RingBuffer::Reserve(size_t capacity)
{
    ASSERT(_alloc);
    _capacity = Max(capacity, _capacity);
    _buffer = reinterpret_cast<uint8*>(memRealloc(_buffer, _capacity, _alloc));
    ASSERT(_buffer);
}

inline void RingBuffer::Reserve(void* buffer, size_t size)
{
    ASSERT_MSG(_buffer == nullptr, "RingBuffer must not get used before setting buffer pointer");
    ASSERT(buffer);
    
    _capacity = size;
    _buffer = reinterpret_cast<uint8*>(buffer);
    _alloc = nullptr;
}

inline void RingBuffer::Free()
{
    if (_alloc) {
        memFree(_buffer, _alloc);
        _capacity = _size = _start = _end = 0;
        _buffer = nullptr;
    }
}

inline size_t RingBuffer::GetMemoryRequirement(size_t capacity)
{
    return capacity;
}

inline size_t RingBuffer::ExpectWrite() const
{
    return _capacity - _size;
}

inline void RingBuffer::Write(const void* src, size_t size)
{
    ASSERT(size <= ExpectWrite());
    
    uint8* buff = _buffer;
    const uint8* udata = reinterpret_cast<const uint8*>(src);
    size_t remain = _capacity - _end;
    if (remain >= size) {
        memcpy(&buff[_end], udata, size);
    } else {
        memcpy(&buff[_end], udata, remain);
        memcpy(buff, &udata[remain], size - remain);
    }
    
    _end = (_end + size) % _capacity;
    _size += size;
}

inline size_t RingBuffer::Read(void* dst, size_t size)
{
    ASSERT(size > 0);
    
    size = Min(size, _size);
    if (size == 0)
        return 0;
    
    if (dst) {
        uint8* buff = _buffer;
        uint8* udata = reinterpret_cast<uint8*>(dst);
        size_t remain = _capacity - _start;
        if (remain >= size) {
            memcpy(udata, &buff[_start], size);
        } else {
            memcpy(udata, &buff[_start], remain);
            memcpy(&udata[remain], buff, size - remain);
        }
    }
    
    _start = (_start + size) % _capacity;
    _size -= size;
    return size;
}

inline size_t RingBuffer::Peek(void* dst, size_t size, size_t* pOffset)
{
    ASSERT(size > 0);
    
    size = Min(size, _size);
    if (size == 0)
        return 0;
    
    ASSERT(dst);
    uint8* buff = _buffer;
    uint8* udata = reinterpret_cast<uint8*>(dst);
    size_t _offset = pOffset ? *pOffset : _start;
    size_t remain = _capacity - _offset;
    if (remain >= size) {
        memcpy(udata, &buff[_offset], size);
    } else {
        memcpy(udata, &buff[_offset], remain);
        memcpy(&udata[remain], buff, (size_t)size - (size_t)remain);
    }
    
    if (pOffset)
        *pOffset = (*pOffset + size) % _capacity;

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
    return _capacity;
}

//------------------------------------------------------------------------
// @impl Blob
inline Blob::Blob(void* buffer, size_t size)
{
    ASSERT(buffer && size);
    _buffer = buffer;
    _capacity = size;
}

inline void Blob::Attach(void* data, size_t size, Allocator* alloc)
{
    ASSERT(data);
    ASSERT_MSG(!_buffer, "buffer should be freed before attach");
    _alloc = alloc;
    _growPolicy = GrowPolicy::None;
    _buffer = data;
    _offset = 0;
    _size = size;
    _capacity = size;
}

inline void Blob::Detach(void** outData, size_t* outSize)
{
    ASSERT(outData);
    ASSERT(outSize);

    *outData = _buffer;
    *outSize = _size;

    _buffer = nullptr;
    _size = 0;
    _offset = 0;
    _capacity = 0;
}

inline void Blob::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(!_buffer, "SetAllocator must be called before using/initializing the Blob");
    _alloc = alloc;
}

inline void Blob::SetSize(size_t size)
{
    ASSERT_MSG(size <= _capacity, "Size cannot be larger than capacity");
    _size = size;
}

inline void Blob::Reserve(size_t capacity)
{
    ASSERT_MSG(_alloc, "Allocator must be set for dynamic Reserve");
    ASSERT(capacity > _size);

    _buffer = memReallocAligned(_buffer, capacity, _align, _alloc);
    _capacity = capacity;
}

inline void Blob::Reserve(void* buffer, size_t size)
{
    ASSERT(size > _size);
    ASSERT(PtrToInt<uint64>(buffer) % _align == 0);
    ASSERT(_buffer == nullptr);

    _buffer = buffer;
    _capacity = size;
    _alloc = nullptr;
}

inline void Blob::Free()
{
    if (_alloc)
        memFreeAligned(_buffer, _align, _alloc);
    _buffer = nullptr;
    _size = 0;
    _capacity = 0;
    _alloc = nullptr;
}

inline void Blob::ResetOffset() 
{
    _offset = 0;
}

inline void Blob::SetOffset(size_t offset) 
{
    ASSERT(_offset < _size);
    _offset = offset;
}

inline size_t Blob::Write(const void* src, size_t size)
{
    ASSERT(src);
    ASSERT(size);

    size_t writeBytes = Min(_capacity - _size, size);
    if (writeBytes < size && _growPolicy != GrowPolicy::None) {
        ASSERT_MSG(_alloc, "Growable blobs should have allocator");
        ASSERT(_growAmount);

        if (_growPolicy == GrowPolicy::Linear) {
            _capacity += _growAmount;
            _buffer = memReallocAligned(_buffer, _capacity, _align, _alloc);
        }
        else if (_growPolicy == GrowPolicy::Multiply) {
            if (!_capacity)
                _capacity = _growAmount;
            else
                _capacity <<= 1;
            _buffer = memReallocAligned(_buffer, _capacity, _align, _alloc);
        }

        return Write(src, size);
    }

    if (writeBytes) {
        uint8* buff = reinterpret_cast<uint8*>(_buffer);
        memcpy(buff + _size, src, writeBytes);
        _size += writeBytes;
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

    size_t readBytes = Min(_size - _offset, size);
    if (readBytes) {
        uint8* buff = reinterpret_cast<uint8*>(_buffer);
        memcpy(dst, buff + _offset, readBytes);
        const_cast<Blob*>(this)->_offset += readBytes;
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

inline size_t Blob::WriteStringBinary(const char* str, uint32 len)
{
    size_t writtenBytes = Write<uint32>(len);
    if (len) 
        writtenBytes += Write(str, len);
    return writtenBytes;
}

inline size_t Blob::ReadStringBinary(char* outStr, [[maybe_unused]] uint32 outStrSize) const
{
    uint32 len = 0;
    size_t readStrBytes = 0;
    size_t readBytes = Read<uint32>(&len);
    ASSERT(readBytes == sizeof(len));
    ASSERT(len < outStrSize);
    if (len) {
        readStrBytes = Read(outStr, len);
        ASSERT(readStrBytes == len);
    }
    outStr[len] = '\0';
    return readStrBytes + readBytes;
}

inline size_t Blob::Size() const
{
    return _size;
}

inline size_t Blob::ReadOffset() const
{
    return _offset;
}

inline size_t Blob::Capacity() const
{
    return _capacity;
}

inline const void* Blob::Data() const
{
    return _buffer;
}

inline bool Blob::IsValid() const
{
    return _buffer && _size;
}

inline void Blob::CopyTo(Blob* otherBlob) const
{
    ASSERT(_size);
    otherBlob->Reserve(_size);
    otherBlob->SetSize(_size);
    memcpy(otherBlob->_buffer, _buffer, _size); 
}

inline void Blob::SetAlignment(uint8 align)
{
    if (align < CONFIG_MACHINE_ALIGNMENT)
        align = CONFIG_MACHINE_ALIGNMENT;
    _align = align;
}

inline void Blob::SetGrowPolicy(GrowPolicy policy, uint32 amount)
{
    _growPolicy = policy;
    _growAmount = amount == 0 ? 4096u : AlignValue(amount, CACHE_LINE_SIZE);
}

//------------------------------------------------------------------------
// @impl PoolBuffer
template <typename _T, uint32 _Align>
inline PoolBuffer<_T, _Align>::PoolBuffer(void* buffer, size_t size)
{
    ASSERT(buffer);
    ASSERT(size > sizeof(Page));

    _pageSize = (size - sizeof(Page))/sizeof(_T);
    ASSERT_MSG(_pageSize, "Buffer size is too small");
    _pages = CreatePage(buffer, size);
}

template <typename _T, uint32 _Align>
inline void PoolBuffer<_T, _Align>::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(!_pages, "SetAllocator must be called before using/initializing the Blob");
    _alloc = alloc;
}

template <typename _T, uint32 _Align>
inline void PoolBuffer<_T, _Align>::Reserve(uint32 pageSize)
{
    ASSERT(_alloc);
    ASSERT(pageSize);

    _pageSize = pageSize;
    _pages = CreatePage(nullptr, 0);
}

template <typename _T, uint32 _Align>
inline void PoolBuffer<_T, _Align>::Reserve(void* buffer, size_t size, uint32 pageSize)
{
    ASSERT(buffer);
    ASSERT(size > sizeof(Page));
    ASSERT(_pages == nullptr);
    ASSERT(pageSize);
    
    _pageSize = pageSize;
    _alloc = nullptr;
    _pages = CreatePage(buffer, size);
}

template <typename _T, uint32 _Align>
inline void PoolBuffer<_T, _Align>::Free()
{
    if (_alloc) {
        Page* page = _pages;
        while (page) {
            Page* next = page->next;
            memFree(page, _alloc);
            page = next;
        }
    }

    _pageSize = 0;
    _pages = nullptr;
}

template <typename _T, uint32 _Align>
inline size_t PoolBuffer<_T, _Align>::GetMemoryRequirement(uint32 pageSize)
{
    BuffersAllocPOD<Page> pageBuffer;
    pageBuffer.template AddMemberField<_T*>(offsetof(Page, ptrs), pageSize);
    pageBuffer.template AddMemberField<_T>(offsetof(Page, data), pageSize, false, _Align);
    return pageBuffer.GetMemoryRequirement();
}

template <typename _T, uint32 _Align>
inline _T* PoolBuffer<_T, _Align>::New()
{
    Page* page = _pages;
    while (page && page->index == 0 && page->next)
        page = page->next;
    
    // Grow if necassory 
    if (!page || page->index == 0) {
        if (!_alloc) {
            ASSERT_MSG(0, "Cannot allocate anymore new objects. Pool is full");
            return nullptr;
        }

        page = CreatePage(nullptr, 0);
        if (_pages) {
            Page* lastPage = _pages;
            while (lastPage->next)
                lastPage = lastPage->next;
            lastPage->next = page;
        }
        else {
            _pages = page;
        }
    }

    ASSERT(page->index);
    return page->ptrs[--page->index];
}

template <typename _T, uint32 _Align>
inline void PoolBuffer<_T, _Align>::Delete(_T* item)
{
    uint64 uptr = PtrToInt<uint64>(item);
    Page* page = _pages;
    uint32 pageSize = _pageSize;

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
    Page* page = _pages;
    while (page && page->index == 0 && page->next)
        page = page->next;
    
    return !page || page->index == 0;
}

template <typename _T, uint32 _Align>
inline typename PoolBuffer<_T, _Align>::Page* PoolBuffer<_T, _Align>::CreatePage(void* buffer, size_t size)
{
    ASSERT(_pageSize);

    BuffersAllocPOD<Page> pageBuffer;
    pageBuffer.template AddMemberField<_T*>(offsetof(Page, ptrs), _pageSize);
    pageBuffer.template AddMemberField<_T>(offsetof(Page, data), _pageSize, false, _Align); // Only align data buffer

    Page* page = (buffer && size) ? pageBuffer.Calloc(buffer, size) : page = pageBuffer.Calloc(_alloc);
    page->index = _pageSize;
    for (uint32 i = 0, c = _pageSize; i < c; i++)
        page->ptrs[c - i - 1] = page->data + i;
    return page;
}
