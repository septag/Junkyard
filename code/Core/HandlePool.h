#pragma once

// Handles are widely used in public APIs and the places where we need to export some sort of data to other systems
// They basically replace pointers in APIs. The data is not tightly packed in the arrays
// But the upside is that Add/Deletes are very fast, since we keep sparse indexes to data. Just make sure that you keep capacity as small as possible 
//
// To declare a handle type, use DEFINE_HANDLE(name) macro. See CommonTypes.h for examples
// Each handle type contains a 32bit Id, with some bits reserved for Generation and the rest is for index in the buffer
// 
// HandlePool is the main container template type. Recommended to be used only on POD (plain-old-data) types
// Stores both handle tables and a large buffer to hold the data that handles reference to
//      Memory Management:
//          Ctor and Dtors don't allocate/deallocate anything as usual.
//          Allocation happens on the first Add()
//          Fixed-size custom buffers can be passed to container ctor instead of dynamic allocators. 
//          In case of Fixed-size custom buffers, call `GetMemoryRequirement` function to calculate needed memory
//          
//      How it works:
//          There are three buffers involved:
//              - Data: Contains the actual data (only in HandlePool) that handles point to     
//              - Dense: Stores actual handles [0..count]. You can enumerate all active handles by iterating this array
//              - Sparse: Stores indexes to dense array [0..capacity]. It's used to maintain the two way relationship between Data index and handles
//                        For example, when we remove a handle from the container, it looks up the dense index in sparse array 
//                        and swaps the handle with the last one in dense array
//
//          Handles:
//              - Generation (high-bits): After each new handle is created. This generation counter is increased
//                                        This way, we can validate that we are not using stale handles in the program
//              - Index (low-bits): Keeps an index to sparse array and data buffers. 
//
//      Usage examples:
//          Adding and Removing data:
//              struct Data {
//                  uint32 a;
//                  uint32 b;
//              };
//              DEFINE_HANDLE(DataHandle);
//              HandlePool<DataHandle, Data> handlePool;
//              DataHandle handle = handlePool.Add(Data {})
//              ...
//              handlePool.Remove(handle)
//
//          Iterating through the active data:
//              for (Data& data : handlePool)
//                  ...
//              OR 
//              for (uint32 i = 0; i < handlePool.Count(); i++) {
//                  Data& data = handlePool.Data(handlePool.HandleAt(i))
//                  ...
//              }
//

#include "Memory.h"
#include "Array.h"

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

///////////////////////////////////////////////////////////////////////////////////////////////////////
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
