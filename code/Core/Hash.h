#pragma once
//
// Hash: Useful Hashing functions + HashTable
// Hashing functions:
//     Fnv32/Fnv32Str: Fast for small hashes and especially strings.
//                     Str version is more optimized towards null-terminated strings, so you don't have to provide length
//                      
//     Uint32/Uint64:  Hashes a 32bit/64bit value into same size variable. This will produce better results for hash-table lookups
//     Uint64To32: Hashes a 64bit integer value into 32bit integer
//     CRC32: Old hashing technique, useful for files and data that needs more portability
//     Murmur3: Suitable for hashing larger data blobs. It has two versions, 32bit and 128bit. 
// 
// HashTable: Fibonacci based hash table. 
//            No allocation or deallocation happen in ctor/dtor. Memory should be allocated explicity with `Reserve` and `Free` calls.
//            It can grow if you provide allocator, otherwise, it cannot if you provide pre-allocated buffer/size pairs instead
//            Becareful not to add duplicates. `Add` method can add multiple hashes if any free slot is found. 
//            In that case, you should use `AddIfNotFound` method.
//      

#include "Base.h"

struct HashResult128
{
    uint64 h1;
    uint64 h2;

    bool operator==(const HashResult128& h) const { return h1 == h.h1 && h2 == h.h2; }
    bool operator!=(const HashResult128& h) const { return h1 != h.h1 || h2 != h.h2; }
};

// FNV1a: suitable for small data (usually less than 32 bytes), mainly small strings
INLINE constexpr uint32 hashFnv32(const void* data, size_t len);
INLINE constexpr uint32 hashFnv32Str(const char* str);
template <typename _T> constexpr uint32 hashFnv32(const _T& data);

// Integer hash functions: useful for pointer/index hashing
// Reference: https://gist.github.com/badboy/6267743
INLINE constexpr uint32 hashUint32(uint32 key);
INLINE constexpr uint64 hashUint64(uint64 key);
INLINE constexpr uint32 hashUint64To32(uint64 key);

// CRC32: Pretty standard hash, mainly used for files
API uint32 hashCRC32(const void* data, size_t len, uint32 seed = randomGenSeed());

// Murmur3
API uint32 hashMurmur32(const void* key, uint32 len, uint32 seed = randomGenSeed());
API HashResult128 hashMurmur128(const void* key, uint32 len, uint32 seed = randomGenSeed());

struct HashMurmur32Incremental
{
    HashMurmur32Incremental(uint32 seed = randomGenSeed());
    
    template <typename _T> 
    HashMurmur32Incremental& Add(const _T* data, uint32 count = 1) { return AddAny(data, sizeof(_T)*count); }

    uint32 Hash();
    HashMurmur32Incremental& AddAny(const void* data, uint32 size);
    HashMurmur32Incremental& AddCStringArray(const char** strs, uint32 numStrings);

    uint32 mHash;
    uint32 mTail;
    uint32 mCount;
    uint32 mSize;
};

//----------------------------------------------------------------------------------------------------------------------
// HashTable
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
    HashTable() : mHashTable(nullptr), mAlloc(memDefaultAlloc()) {}
    explicit HashTable(Allocator* alloc) :  mHashTable(nullptr), mAlloc(alloc) {}
    explicit HashTable(uint32 capacity, void* buff, size_t size);

    void SetAllocator(Allocator* alloc);
    bool Reserve(uint32 capacity);
    bool Reserve(uint32 capacity, void* buff, size_t size);
    void Free();

    bool Grow(uint32 newCapacity);
    bool Grow(uint32 newCapacity, void* buff, size_t size);

    static size_t GetMemoryRequirement(uint32 capacity);
    
    _T* Add(uint32 key);
    uint32 Add(uint32 key, const _T& value);
    uint32 AddIfNotFound(uint32 key, const _T& value);
    uint32 Find(uint32 key) const;
    void Clear();

    const _T& Get(uint32 index) const;
    void Set(uint32 index, const _T& value);
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
    _private::HashTableData* mHashTable;
    Allocator*               mAlloc;
};

using HashTableUint = HashTable<uint32>;

//------------------------------------------------------------------------
// FNV1a
// http://www.isthe.com/chongo/src/fnv/hash_32a.c
inline constexpr uint32 kFnv1Init = 0x811c9dc5;
inline constexpr uint32 kFnv1Prime = 0x01000193;

INLINE constexpr 
uint32 hashFnv32(const void* data, size_t len)
{
    const char* bp = (const char*)data;
    const char* be = bp + len;
    
    uint32 hval = kFnv1Init;
    while (bp < be) {
        hval ^= (uint32)*bp++;
        hval *= kFnv1Prime;
    }
    
    return hval;
}

INLINE constexpr 
uint32 hashFnv32Str(const char* str)
{
    const char* s = str;
    
    uint32 hval = kFnv1Init;
    while (*s) {
        hval ^= (uint32)*s++;
        hval *= kFnv1Prime;
    }
    
    return hval;
}

#ifdef __cplusplus
template <typename _T> constexpr 
uint32 hashFnv32(const _T& data)
{
    return hashFnv32(&data, sizeof(data));
}
#endif

INLINE constexpr 
uint32 hashUint32(uint32 key)
{
    const uint32 c2 = 0x27d4eb2d;    // a prime or an odd constant
    key = (key ^ 61) ^ (key >> 16);
    key = key + (key << 3);
    key = key ^ (key >> 4);
    key = key * c2;
    key = key ^ (key >> 15);
    return key;
}

INLINE constexpr 
uint64 hashUint64(uint64 key)
{
    key = (~key) + (key << 21);    // key = (key << 21) - key - 1;
    key = key ^ (key >> 24);
    key = (key + (key << 3)) + (key << 8);    // key * 265
    key = key ^ (key >> 14);
    key = (key + (key << 2)) + (key << 4);    // key * 21
    key = key ^ (key >> 28);
    key = key + (key << 31);
    return key;
}

INLINE constexpr 
uint32 hashUint64To32(uint64 key)
{
    key = (~key) + (key << 18);
    key = key ^ (key >> 31);
    key = key * 21;
    key = key ^ (key >> 11);
    key = key + (key << 6);
    key = key ^ (key >> 22);
    return (uint32)key;
}

//------------------------------------------------------------------------
// @impl HashTable
template <typename _T>
inline HashTable<_T>::HashTable(uint32 capacity, void* buff, size_t size)
{
    mHashTable = _private::hashtableCreateWithBuffer(capacity, sizeof(_T), buff, size);
    mAlloc = nullptr;
    ASSERT(mHashTable);
}    

template <typename _T>
inline void HashTable<_T>::SetAllocator(Allocator* alloc)
{
    ASSERT_MSG(!mHashTable, "hash-table already initialized with another allocator");
    mAlloc = alloc;
}

template <typename _T>
inline bool HashTable<_T>::Reserve(uint32 capacity)
{
    ASSERT_MSG(!mHashTable, "hash-table already initialized");
    ASSERT(mAlloc);
    mHashTable = _private::hashtableCreate(capacity, sizeof(_T), mAlloc);
    return mHashTable ? true : false;
}

template <typename _T>
inline bool HashTable<_T>::Reserve(uint32 capacity, void* buff, size_t size)
{
    ASSERT_MSG(!mHashTable, "hash-table already initialized");
    mHashTable = _private::hashtableCreateWithBuffer(capacity, sizeof(_T), buff, size);
    mAlloc = nullptr;
    return mHashTable ? true : false;
}    

template <typename _T>
inline void HashTable<_T>::Free()
{
    if (mHashTable && mAlloc)
        _private::hashtableDestroy(mHashTable, mAlloc);
    mHashTable = nullptr;
}

template <typename _T>
inline const _T* HashTable<_T>::Values() const
{
    return reinterpret_cast<const _T*> (mHashTable->values);
}

template <typename _T>
inline const uint32* HashTable<_T>::Keys() const
{
    return mHashTable->keys;
}

template <typename _T>
inline uint32 HashTable<_T>::Count() const
{
    return mHashTable->count;
}

template <typename _T>
inline uint32 HashTable<_T>::Capacity() const
{
    return mHashTable->capacity;
}

template <typename _T>
inline const _T& HashTable<_T>::Get(uint32 index) const
{
    return reinterpret_cast<_T*>(mHashTable->values)[index];
}

template <typename _T>
inline void HashTable<_T>::Set(uint32 index, const _T& value)
{
    reinterpret_cast<_T*>(mHashTable->values)[index] = value;
}

template <typename _T>
inline _T& HashTable<_T>::GetMutable(uint32 index)
{
    return reinterpret_cast<_T*>(mHashTable->values)[index];
}

template <typename _T>
inline void HashTable<_T>::Remove(uint32 index)
{
    ASSERT_MSG(index < mHashTable->capacity, "index out of range");

    mHashTable->keys[index] = 0;
    --mHashTable->count;
}

template <typename _T>
inline void HashTable<_T>::Clear()
{
    _private::hashtableClear(mHashTable);
}

template <typename _T>
inline uint32 HashTable<_T>::Find(uint32 key) const
{
    return _private::hashtableFind(mHashTable, key);
}

template <typename _T>
inline uint32 HashTable<_T>::AddIfNotFound(uint32 key, const _T& value)
{
    uint32 index = Find(key);

    if (index == INVALID_INDEX)
        return Add(key, value);
    else
        return index;
}

template <typename _T>
inline uint32 HashTable<_T>::Add(uint32 key, const _T& value)
{
    ASSERT(mHashTable);
    if (mHashTable->count == mHashTable->capacity) {
        [[maybe_unused]] bool r = false;
        ASSERT_MSG(mAlloc, "Only hashtables with allocators can be self-grown automatically");
        if (mAlloc) 
            r = _private::hashtableGrow(&mHashTable, mAlloc);
        ASSERT_ALWAYS(r, "could not grow hash-table");
    }
    uint32 h = _private::hashtableAddKey(mHashTable, key);

    // do memcpy with template types, so it leaves some hints for the compiler to optimize
    memcpy((_T*)mHashTable->values + h, &value, sizeof(_T));
    return h;
}

template <typename _T>
inline _T* HashTable<_T>::Add(uint32 key)
{
    ASSERT(mHashTable);
    if (mHashTable->count == mHashTable->capacity) {
        bool r = false;
        ASSERT_MSG(mAlloc, "HashTable full. Only hashtables with allocators can be self-grown automatically");
        if (mAlloc) 
            r = _private::hashtableGrow(&mHashTable, mAlloc);
        UNUSED(r);
        ASSERT_ALWAYS(r, "could not grow hash-table");
    }
    uint32 h = _private::hashtableAddKey(mHashTable, key);

    return &((_T*)mHashTable->values)[h];
}

template <typename _T>
inline const _T& HashTable<_T>::FindAndFetch(uint32 key, const _T& not_found_value /*= {0}*/) const
{
    ASSERT(mHashTable);
    uint32 index = _private::hashtableFind(mHashTable, key);
    return index != UINT32_MAX ? Get(index) : not_found_value;
}

template <typename _T>
inline void HashTable<_T>::FindAndRemove(uint32 key)
{
    ASSERT(mHashTable);
    uint32 index = _private::hashtableFind(mHashTable, key);
    if (index != UINT32_MAX)
        Remove(index);
}

template <typename _T>
inline bool HashTable<_T>::IsFull() const
{
    ASSERT(mHashTable);
    return mHashTable->count == mHashTable->capacity;
}

template <typename _T>
inline size_t HashTable<_T>::GetMemoryRequirement(uint32 capacity)
{
    return _private::hashtableGetMemoryRequirement(capacity, sizeof(_T));
}

template <typename _T>
inline bool HashTable<_T>::Grow(uint32 newCapacity)
{
    ASSERT(mAlloc);
    ASSERT(mHashTable);
    ASSERT(newCapacity > mHashTable->capacity);

    return _private::hashtableGrow(&mHashTable, mAlloc);
}

template <typename _T>
inline bool HashTable<_T>::Grow(uint32 newCapacity, void* buff, size_t size)
{
    ASSERT(!mAlloc);
    ASSERT(mHashTable);
    ASSERT(newCapacity > mHashTable->capacity);

    return _private::hashtableGrowWithBuffer(&mHashTable, buff, size);
}

