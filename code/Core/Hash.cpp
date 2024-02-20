#include "Hash.h"

#include "StringUtil.h" // strLen
#include "Allocators.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
/* This file is derived from crc32.c from the zlib-1.1.3 distribution
 * by Jean-loup Gailly and Mark Adler.
 */

/* crc32.c -- compute the CRC-32 of a data stream
 * Copyright (C) 1995-1998 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 */


/* ========================================================================
*  Table of CRC-32's of all single-byte values (made by make_crc_table)
*/
static const uint32 kCrcTable[256] = {
    0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L, 0x706af48fL, 0xe963a535L,
    0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L, 0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL,
    0xe7b82d07L, 0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL, 0x1adad47dL,
    0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L, 0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL,
    0x14015c4fL, 0x63066cd9L, 0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
    0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL, 0x35b5a8faL, 0x42b2986cL,
    0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L, 0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL,
    0x51de003aL, 0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L, 0xb8bda50fL,
    0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L, 0x2f6f7c87L, 0x58684c11L, 0xc1611dabL,
    0xb6662d3dL, 0x76dc4190L, 0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
    0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL, 0xe10e9818L, 0x7f6a0dbbL,
    0x086d3d2dL, 0x91646c97L, 0xe6635c01L, 0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL,
    0x6c0695edL, 0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L, 0x8bbeb8eaL,
    0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L, 0xfbd44c65L, 0x4db26158L, 0x3ab551ceL,
    0xa3bc0074L, 0xd4bb30e2L, 0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
    0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L, 0xaa0a4c5fL, 0xdd0d7cc9L,
    0x5005713cL, 0x270241aaL, 0xbe0b1010L, 0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L,
    0xce61e49fL, 0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L, 0x2eb40d81L,
    0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L, 0x03b6e20cL, 0x74b1d29aL, 0xead54739L,
    0x9dd277afL, 0x04db2615L, 0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
    0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L, 0x8708a3d2L, 0x1e01f268L,
    0x6906c2feL, 0xf762575dL, 0x806567cbL, 0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L,
    0x10da7a5aL, 0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L, 0xd6d6a3e8L,
    0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L, 0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL,
    0xd80d2bdaL, 0xaf0a1b4cL, 0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
    0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L, 0xcc0c7795L, 0xbb0b4703L,
    0x220216b9L, 0x5505262fL, 0xc5ba3bbeL, 0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L,
    0xb5d0cf31L, 0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL, 0x026d930aL,
    0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L, 0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL,
    0x0cb61b38L, 0x92d28e9bL, 0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
    0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L, 0x18b74777L, 0x88085ae6L,
    0xff0f6a70L, 0x66063bcaL, 0x11010b5cL, 0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L,
    0xa00ae278L, 0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L, 0x4969474dL,
    0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L, 0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L,
    0x47b2cf7fL, 0x30b5ffe9L, 0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
    0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L, 0x5d681b02L, 0x2a6f2b94L,
    0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL, 0x2d02ef8dL
};

#define DO1(buf) crc = kCrcTable[((int)crc ^ (*buf++)) & 0xff] ^ (crc >> 8);
#define DO2(buf) \
    DO1(buf);    \
    DO1(buf);
#define DO4(buf) \
    DO2(buf);    \
    DO2(buf);
#define DO8(buf) \
    DO4(buf);    \
    DO4(buf);

uint32 hashCRC32(const void* data, size_t len, uint32 seed) 
{
    const uint8* buf = (const uint8*)data;
    uint32 crc = seed ^ 0xffffffffL;
    
    while (len >= 8) {
        DO8(buf);
        len -= 8;
    }
    
    while (len--) {
        DO1(buf);
    }
    
    crc ^= 0xffffffffL;
    
    return crc;
}

////////////////////////////////////////////////////////////////////////////////////////////
// Murmur3
// https://github.com/PeterScott/murmur3/blob/master/murmur3.c
FORCE_INLINE uint32 rotl32(uint32 x, int8 r)
{
    return (x << r) | (x >> (32 - r));
}

FORCE_INLINE uint64 rotl64(uint64 x, int8 r)
{
    return (x << r) | (x >> (64 - r));
}

#define	ROTL32(x,y)	rotl32(x,y)
#define ROTL64(x,y)	rotl64(x,y)
#define BIG_CONSTANT(x) (x##LLU)
#define HASH_M 0x5bd1e995
#define HASH_R 24
#define MMIX(h, k) { k *= HASH_M; k ^= k >> HASH_R; k *= HASH_M; h *= HASH_M; h ^= k; }

//-----------------------------------------------------------------------------
// Block read - if your platform needs to do endian-swapping or can only
// handle aligned reads, do the conversion here
#define getblock(p, i) (p[i])

//-----------------------------------------------------------------------------
// Finalization mix - force all bits of a hash block to avalanche

FORCE_INLINE uint32 hashMurmurFmix32(uint32 h)
{
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    
    return h;
}

//----------

FORCE_INLINE uint64 hashMurmurFmix64(uint64 k)
{
    k ^= k >> 33;
    k *= BIG_CONSTANT(0xff51afd7ed558ccd);
    k ^= k >> 33;
    k *= BIG_CONSTANT(0xc4ceb9fe1a85ec53);
    k ^= k >> 33;
    
    return k;
}

//-----------------------------------------------------------------------------
uint32 hashMurmur32(const void * key, uint32 len, uint32 seed)
{
    const uint8 * data = (const uint8*)key;
    const int nblocks = static_cast<int>(len / 4);
    int i;
    
    uint32 h1 = seed;
    constexpr uint32 c1 = 0xcc9e2d51;
    constexpr uint32 c2 = 0x1b873593;
    
    // body
    auto blocks = reinterpret_cast<const uint32*>(data + nblocks*4);
    
    for(i = -nblocks; i; i++) {
        uint32 k1 = getblock(blocks,i);
        
        k1 *= c1;
        k1 = ROTL32(k1,15);
        k1 *= c2;
        
        h1 ^= k1;
        h1 = ROTL32(h1,13); 
        h1 = h1*5+0xe6546b64;
    }
    
    // tail
    auto tail = reinterpret_cast<const uint8*>(data + nblocks*4);
    uint32 k1 = 0;
    
    switch(len & 3) {
    case 3: k1 ^= tail[2] << 16;
    case 2: k1 ^= tail[1] << 8;
    case 1: k1 ^= tail[0];
        k1 *= c1; k1 = ROTL32(k1,15); k1 *= c2; h1 ^= k1;
    }
    
    // finalization
    h1 ^= len;    
    return hashMurmurFmix32(h1);
} 

HashResult128 hashMurmur128(const void * key, size_t len, const uint32 seed)
{
    const uint8 * data = (const uint8*)key;
    const size_t nblocks = len / 16;
    size_t i;
    
    uint64 h1 = seed;
    uint64 h2 = seed;
    
    uint64 c1 = BIG_CONSTANT(0x87c37b91114253d5);
    uint64 c2 = BIG_CONSTANT(0x4cf5ad432745937f);
    
    // body
    const uint64 * blocks = (const uint64 *)(data);
    
    for(i = 0; i < nblocks; i++)
    {
        uint64 k1 = getblock(blocks,i*2+0);
        uint64 k2 = getblock(blocks,i*2+1);
        
        k1 *= c1; k1  = ROTL64(k1,31); k1 *= c2; h1 ^= k1;
        h1 = ROTL64(h1,27); h1 += h2; h1 = h1*5+0x52dce729;
        k2 *= c2; k2  = ROTL64(k2,33); k2 *= c1; h2 ^= k2;
        h2 = ROTL64(h2,31); h2 += h1; h2 = h2*5+0x38495ab5;
    }
    
    auto tail = reinterpret_cast<const uint8*>(data + nblocks*16);
    
    uint64 k1 = 0;
    uint64 k2 = 0;
    
    switch(len & 15)
    {
    case 15: k2 ^= (uint64)(tail[14]) << 48;
    case 14: k2 ^= (uint64)(tail[13]) << 40;
    case 13: k2 ^= (uint64)(tail[12]) << 32;
    case 12: k2 ^= (uint64)(tail[11]) << 24;
    case 11: k2 ^= (uint64)(tail[10]) << 16;
    case 10: k2 ^= (uint64)(tail[ 9]) << 8;
    case  9: k2 ^= (uint64)(tail[ 8]) << 0;
        k2 *= c2; k2  = ROTL64(k2,33); k2 *= c1; h2 ^= k2;
        
    case  8: k1 ^= (uint64)(tail[ 7]) << 56;
    case  7: k1 ^= (uint64)(tail[ 6]) << 48;
    case  6: k1 ^= (uint64)(tail[ 5]) << 40;
    case  5: k1 ^= (uint64)(tail[ 4]) << 32;
    case  4: k1 ^= (uint64)(tail[ 3]) << 24;
    case  3: k1 ^= (uint64)(tail[ 2]) << 16;
    case  2: k1 ^= (uint64)(tail[ 1]) << 8;
    case  1: k1 ^= (uint64)(tail[ 0]) << 0;
        k1 *= c1; k1  = ROTL64(k1,31); k1 *= c2; h1 ^= k1;
    }
    
    // finalization
    h1 ^= len; h2 ^= len;
    
    h1 += h2;
    h2 += h1;
    
    h1 = hashMurmurFmix64(h1);
    h2 = hashMurmurFmix64(h2);
    
    h1 += h2;
    h2 += h1;
    
    return {
        .h1 = h1,
        .h2 = h2
    };
}

static void hashMurmur32MixTail(HashMurmur32Incremental* ctx, const uint8** pData, uint32* pSize)
{
    uint32 size = *pSize;
    const uint8* data = *pData;
    
    while (size && ((size<4) || ctx->mCount)) {
        ctx->mTail |= (*data++) << (ctx->mCount * 8);
        
        ctx->mCount++;
        size--;
        
        if (ctx->mCount == 4)	{
            MMIX(ctx->mHash, ctx->mTail);
            ctx->mTail = 0;
            ctx->mCount = 0;
        }
    }
    
    *pData = data;
    *pSize = size;
}

HashMurmur32Incremental::HashMurmur32Incremental(uint32 seed) : 
    mHash(seed),
    mTail(0),
    mCount(0),
    mSize(0)
{
}

HashMurmur32Incremental& HashMurmur32Incremental::AddAny(const void* _data, uint32 _size)
{
    if (!_data || !_size)
        return *this;

    const uint8* key = (const uint8*)_data;
    mSize += _size;
    
    hashMurmur32MixTail(this, &key, &_size);
    
    while (_size >= 4)	{
        uint32 k = *((const uint32*)key);
        
        MMIX(mHash, k);
        
        key += 4;
        _size -= 4;
    }
    
    hashMurmur32MixTail(this, &key, &_size);
    return *this;
}

HashMurmur32Incremental& HashMurmur32Incremental::AddCStringArray(const char** _strs, uint32 _numStrings)
{
    if (!_strs || !_numStrings) 
        return *this;

    for (uint32 i = 0; i < _numStrings; i++) 
        AddAny(_strs[i], strLen(_strs[i]));

    return *this;
}

uint32 HashMurmur32Incremental::Hash()
{
    MMIX(mHash, mTail);
    MMIX(mHash, mSize);
    
    mHash ^= mHash >> 13;
    mHash *= HASH_M;
    mHash ^= mHash >> 15;
    
    return mHash;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation reference:
// https://probablydance.com/2018/06/16/fibonacci-hashing-the-optimization-that-the-world-forgot-or-a-better-alternative-to-integer-modulo/
FORCE_INLINE uint32 hashTableFibHash(uint32 h, int bits)
{
    uint64 h64 = static_cast<uint64>(h);
    h64 ^= (h64 >> bits);
    return static_cast<uint32>((h64 * 11400714819323198485llu) >> bits);
}

// https://www.exploringbinary.com/number-of-bits-in-a-decimal-integer/
FORCE_INLINE uint32 hashTableCalcBitShift(uint32 n)
{
    uint32 c = 0;
    uint32 un = n;
    while (un > 1) {
        c++;
        un >>= 1;
    }
    return 64 - c;
}

FORCE_INLINE constexpr int hashTableNearestPow2(int n)
{
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;
    return n;
}

_private::HashTableData* _private::hashtableCreate(uint32 capacity, uint32 valueStride, Allocator* alloc)
{
    ASSERT(capacity > 0);
    
    capacity = hashTableNearestPow2(capacity);   
    
    MemSingleShotMalloc<HashTableData> mallocator;
    mallocator.AddMemberField<uint32>(offsetof(HashTableData, keys), capacity)
              .AddMemberField<uint8>(offsetof(HashTableData, values), valueStride*capacity);
    HashTableData* tbl = mallocator.Calloc(alloc);
    
    tbl->bitshift = hashTableCalcBitShift(capacity);
    tbl->valueStride = valueStride;
    tbl->count = 0;
    tbl->capacity = capacity;
    
    return tbl;
}

size_t _private::hashtableGetMemoryRequirement(uint32 capacity, uint32 valueStride)
{
    ASSERT(capacity > 0);
    
    capacity = hashTableNearestPow2(capacity);
    MemSingleShotMalloc<HashTableData> mallocator;
    return mallocator.AddMemberField<uint32>(offsetof(HashTableData, keys), capacity)
                     .AddMemberField<uint8>(offsetof(HashTableData, values), valueStride*capacity)
                     .GetMemoryRequirement();        
}

void _private::hashtableDestroy(HashTableData* tbl, Allocator* alloc)
{
    ASSERT(tbl);
    tbl->count = tbl->capacity = 0;

    MemSingleShotMalloc<HashTableData>::Free(tbl, alloc);
}

bool _private::hashtableGrow(HashTableData** pTbl, Allocator* alloc)
{
    HashTableData* tbl = *pTbl;
    // Create a new table (fl64 the size), repopulate it and replace previous one
    HashTableData* newTable = hashtableCreate(tbl->capacity << 1, tbl->valueStride, alloc);
    if (!newTable)
        return false;
    
    for (int i = 0, c = tbl->capacity; i < c; i++) {
        if (tbl->keys[i] > 0) {
            hashtableAdd(newTable, tbl->keys[i], tbl->values + i * tbl->valueStride);
        }
    }
    
    hashtableDestroy(tbl, alloc);
    *pTbl = newTable;
    return true;
}

uint32 _private::hashtableAdd(HashTableData* tbl, uint32 key, const void* value)
{
    ASSERT(tbl->count < tbl->capacity);
    
    uint32 h = hashTableFibHash(key, tbl->bitshift);
    uint32 cnt = (uint32)tbl->capacity;
    while (tbl->keys[h] != 0) {
        h = (h + 1) % cnt;
    }
    
    ASSERT(tbl->keys[h] == 0);    // something went wrong!
    tbl->keys[h] = key;
    memcpy(tbl->values + tbl->valueStride * h, value, tbl->valueStride);
    ++tbl->count;
    return h;
}

uint32 _private::hashtableAddKey(HashTableData* tbl, uint32 key)
{
    ASSERT(tbl->count < tbl->capacity);
    
    uint32 h = hashTableFibHash(key, tbl->bitshift);
    uint32 cnt = (uint32)tbl->capacity;
    while (tbl->keys[h] != 0) {
        h = (h + 1) % cnt;
    }
    
    ASSERT_MSG(tbl->keys[h] == 0, "No free slot found in the hash-table");
    tbl->keys[h] = key;
    ++tbl->count;
    return h;
}

uint32 _private::hashtableFind(const HashTableData* tbl, uint32 key)
{
    uint32 h = hashTableFibHash(key, tbl->bitshift);
    uint32 cnt = (uint32)tbl->capacity;
    if (tbl->keys[h] == key) {
        return h;
    } else {
        // probe lineary in the keys array
        for (uint32 i = 1; i < cnt; i++) {
            int idx = (h + i) % cnt;
            if (tbl->keys[idx] == key)
                return idx;
            else if (tbl->keys[idx] == 0) 
                break;
        }
        
        return INVALID_INDEX;    // Worst case: Not found!
    }
}

void _private::hashtableClear(HashTableData* tbl)
{
    memset(tbl->keys, 0x0, sizeof(uint32) * tbl->capacity);
    tbl->count = 0;
}

_private::HashTableData* _private::hashtableCreateWithBuffer(uint32 capacity, uint32 valueStride, void* buff, size_t size)
{
    ASSERT(capacity > 0);
    
    capacity = hashTableNearestPow2(capacity);   
    
    MemSingleShotMalloc<HashTableData> hashTableBuff;
    hashTableBuff.AddMemberField<uint32>(offsetof(HashTableData, keys), capacity)
                 .AddMemberField<uint8>(offsetof(HashTableData, values), valueStride*capacity);
    HashTableData* tbl = hashTableBuff.Calloc(buff, size);
    
    tbl->bitshift = hashTableCalcBitShift(capacity);
    tbl->valueStride = valueStride;
    tbl->count = 0;
    tbl->capacity = capacity;
    
    return tbl;
}

bool _private::hashtableGrowWithBuffer(HashTableData** pTbl, void* buff, size_t size)
{
    HashTableData* tbl = *pTbl;
    // Create a new table (fl64 the size), repopulate it and replace previous one
    HashTableData* newTable = hashtableCreateWithBuffer(tbl->capacity << 1, tbl->valueStride, buff, size);
    if (!newTable)
        return false;
    
    for (int i = 0, c = tbl->capacity; i < c; i++) {
        if (tbl->keys[i] > 0) {
            hashtableAdd(newTable, tbl->keys[i], tbl->values + i * tbl->valueStride);
        }
    }
    
    *pTbl = newTable;
    return true;
}
