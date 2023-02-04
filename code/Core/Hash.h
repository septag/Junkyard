#pragma once

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

    uint32 hash;
    uint32 tail;
    uint32 count;
    uint32 size;
};

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
