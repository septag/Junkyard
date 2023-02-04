#pragma once

#include "Memory.h"

namespace _private
{
    struct HashTableData
    {
        uint32* keys;
        uint8* 	values;      // Can be any POD data or opaque type
        uint32	bitshift;
        uint32	valueStride;
        uint32	count;
        uint32	capacity;
    };

    API [[nodiscard]] HashTableData* hashtableCreate(uint32 capacity, 
                                                     uint32 valueStride,
                                                     Allocator* alloc = memDefaultAlloc());
    API void hashtableDestroy(HashTableData* tbl, Allocator* alloc = memDefaultAlloc());
    API bool hashtableGrow(HashTableData** pTbl, Allocator* alloc = memDefaultAlloc());
    
    API uint32 hashtableAdd(HashTableData* tbl, uint32 key, const void* value);
    API uint32 hashtableAddKey(HashTableData* tbl, uint32 key);
    API uint32 hashtableFind(const HashTableData* tbl, uint32 key);
    API void hashtableClear(HashTableData* tbl);

    // TODO: memory control (without allocator)
    API size_t hashtableGetMemoryRequirement(uint32 capacity, uint32 valueStride);
    API HashTableData* hashtableCreateWithBuffer(uint32 capacity, uint32 valueStride, void* buff, size_t size);
    API bool hashtableGrowWithBuffer(HashTableData** pTbl, void* buff, size_t size);
} // _private

template <typename _T>
struct HashTable
{
    HashTable();
    ~HashTable();
    explicit HashTable(uint32 capacity, Allocator* alloc = memDefaultAlloc());
    explicit HashTable(uint32 capacity, void* buff, size_t size);

    bool Initialize(uint32 capacity, Allocator* alloc = memDefaultAlloc());
    bool Initialize(uint32 capacity, void* buff, size_t size);
    void Release();

    bool Grow(uint32 newCapacity);
    bool Grow(uint32 newCapacity, void* buff, size_t size);

    static size_t GetMemoryRequirement(uint32 capacity);
    
    _T* Add(uint32 key);
    uint32 Add(uint32 key, const _T& value);
    uint32 AddIfNotFound(uint32 key, const _T& value);
    uint32 Find(uint32 key) const;
    void Clear();

    const _T& Get(uint32 index) const;
    _T& GetMutable(uint32 index);
    void Remove(uint32 index);

    const _T& FindAndFetch(uint32 key, const _T& notFoundValue) const;
    void FindAndRemove(uint32 key);

    uint32 Capacity() const;
    uint32 Count() const;
    const uint32* Keys() const;
    const _T* Values() const;
    bool IsFull() const;

private:
    _private::HashTableData* _ht;
    Allocator*               _alloc;
};

using HashTableUint = HashTable<uint32>;

//------------------------------------------------------------------------
// Implementation
template <typename _T>
inline HashTable<_T>::HashTable() : _ht(nullptr), _alloc(nullptr)
{
}

template <typename _T>
inline HashTable<_T>::HashTable(uint32 capacity, Allocator* alloc) : _ht(nullptr)
{
    [[maybe_unused]] bool r = Initialize(capacity, alloc);
    ASSERT_ALWAYS(r, "failed to initialize hash-table (capacity=%d)", capacity);
}

template <typename _T>
inline HashTable<_T>::HashTable(uint32 capacity, void* buff, size_t size)
{
    [[maybe_unused]] bool r = Initialize(capacity, buff, size);
    ASSERT_ALWAYS(r, "failed to initialize hash-table (capacity=%d)", capacity);
}    

template <typename _T>
inline HashTable<_T>::~HashTable()
{
    Release();
}

template <typename _T>
inline bool HashTable<_T>::Initialize(uint32 capacity, Allocator* alloc)
{
    ASSERT_MSG(!_ht, "hash-table already initialized");
    _ht = _private::hashtableCreate(capacity, sizeof(_T), alloc);
    _alloc = alloc;
    return _ht ? true : false;
}

template <typename _T>
inline bool HashTable<_T>::Initialize(uint32 capacity, void* buff, size_t size)
{
    ASSERT_MSG(!_ht, "hash-table already initialized");
    _ht = _private::hashtableCreateWithBuffer(capacity, sizeof(_T), buff, size);
    _alloc = nullptr;
    return _ht ? true : false;
}    

template <typename _T>
inline void HashTable<_T>::Release()
{
    if (_ht && _alloc)
        _private::hashtableDestroy(_ht, _alloc);
    _ht = nullptr;
    _alloc = nullptr;
}

template <typename _T>
inline const _T* HashTable<_T>::Values() const
{
    return reinterpret_cast<const _T*> (_ht->values);
}

template <typename _T>
inline const uint32* HashTable<_T>::Keys() const
{
    return _ht->keys;
}

template <typename _T>
inline uint32 HashTable<_T>::Count() const
{
    return _ht->count;
}

template <typename _T>
inline uint32 HashTable<_T>::Capacity() const
{
    return _ht->capacity;
}

template <typename _T>
inline const _T& HashTable<_T>::Get(uint32 index) const
{
    return reinterpret_cast<_T*>(_ht->values)[index];
}

template <typename _T>
inline _T& HashTable<_T>::GetMutable(uint32 index)
{
    return reinterpret_cast<_T*>(_ht->values)[index];
}

template <typename _T>
inline void HashTable<_T>::Remove(uint32 index)
{
    ASSERT_MSG(index < _ht->capacity, "index out of range");

    _ht->keys[index] = 0;
    --_ht->count;
}

template <typename _T>
inline void HashTable<_T>::Clear()
{
    _private::hashtableClear(_ht);
}

template <typename _T>
inline uint32 HashTable<_T>::Find(uint32 key) const
{
    return _private::hashtableFind(_ht, key);
}

template <typename _T>
inline uint32 HashTable<_T>::AddIfNotFound(uint32 key, const _T& value)
{
    if (Find(key) == INVALID_INDEX)
        return Add(key, value);
    else
        return INVALID_INDEX;
}

template <typename _T>
inline uint32 HashTable<_T>::Add(uint32 key, const _T& value)
{
    if (_ht->count == _ht->capacity) {
        [[maybe_unused]] bool r = false;
        ASSERT_MSG(_alloc, "Only hashtables with allocators can be self-grown automatically");
        if (_alloc) 
            r = _private::hashtableGrow(&_ht, _alloc);
        ASSERT_ALWAYS(r, "could not grow hash-table");
    }
    uint32 h = _private::hashtableAddKey(_ht, key);

    // do memcpy with template types, so it leaves some hints for the compiler to optimize
    memcpy((_T*)_ht->values + h, &value, sizeof(_T));
    return h;
}

template <typename _T>
inline _T* HashTable<_T>::Add(uint32 key)
{
    if (_ht->count == _ht->capacity) {
        bool r = false;
        ASSERT_MSG(_alloc, "HashTable full. Only hashtables with allocators can be self-grown automatically");
        if (_alloc) 
            r = _private::hashtableGrow(&_ht, _alloc);
        UNUSED(r);
        ASSERT_ALWAYS(r, "could not grow hash-table");
    }
    uint32 h = _private::hashtableAddKey(_ht, key);

    return &((_T*)_ht->values)[h];
}

template <typename _T>
inline const _T& HashTable<_T>::FindAndFetch(uint32 key, const _T& not_found_value /*= {0}*/) const
{
    int index = _private::hashtableFind(_ht, key);
    return index != -1 ? Get(index) : not_found_value;
}

template <typename _T>
inline void HashTable<_T>::FindAndRemove(uint32 key)
{
    int index = _private::hashtableFind(_ht, key);
    if (index != -1) {
        Remove(index);
    }
}

template <typename _T>
inline bool HashTable<_T>::IsFull() const
{
    return _ht->count == _ht->capacity;
}

template <typename _T>
inline size_t HashTable<_T>::GetMemoryRequirement(uint32 capacity)
{
    return _private::hashtableGetMemoryRequirement(capacity, sizeof(_T));
}

template <typename _T>
inline bool HashTable<_T>::Grow(uint32 newCapacity)
{
    ASSERT(_alloc);
    ASSERT(_ht);
    ASSERT(newCapacity > _ht->capacity);

    return _private::hashtableGrow(&_ht, _alloc);
}

template <typename _T>
inline bool HashTable<_T>::Grow(uint32 newCapacity, void* buff, size_t size)
{
    ASSERT(!_alloc);
    ASSERT(_ht);
    ASSERT(newCapacity > _ht->capacity);

    return _private::hashtableGrowWithBuffer(&_ht, buff, size);
}

