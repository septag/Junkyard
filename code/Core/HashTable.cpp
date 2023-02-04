#include "HashTable.h"

#include "Buffers.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Reference:
// https://probablydance.com/2018/06/16/fibonacci-hashing-the-optimization-that-the-world-forgot-or-a-better-alternative-to-integer-modulo/

INLINE uint32 hashTableFibHash(uint32 h, int bits)
{
    uint64 h64 = static_cast<uint64>(h);
    h64 ^= (h64 >> bits);
    return static_cast<uint32>((h64 * 11400714819323198485llu) >> bits);
}

// https://www.exploringbinary.com/number-of-bits-in-a-decimal-integer/
INLINE uint32 hashTableCalcBitShift(uint32 n)
{
    uint32 c = 0;
    uint32 un = n;
    while (un > 1) {
        c++;
        un >>= 1;
    }
    return 64 - c;
}

INLINE constexpr int hashTableNearestPow2(int n)
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
    
    BuffersAllocPOD<HashTableData> hashTableBuff;
    hashTableBuff.AddMemberField<uint32>(offsetof(HashTableData, keys), capacity)
                 .AddMemberField<uint8>(offsetof(HashTableData, values), valueStride*capacity);
    HashTableData* tbl = hashTableBuff.Calloc(alloc);
    
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
    BuffersAllocPOD<HashTableData> hashTableBuff;
    return hashTableBuff.AddMemberField<uint32>(offsetof(HashTableData, keys), capacity)
                        .AddMemberField<uint8>(offsetof(HashTableData, values), valueStride*capacity)
                        .GetMemoryRequirement();        
}

void _private::hashtableDestroy(HashTableData* tbl, Allocator* alloc)
{
    ASSERT(tbl);
    tbl->count = tbl->capacity = 0;
    memFree(tbl, alloc);
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
    
    BuffersAllocPOD<HashTableData> hashTableBuff;
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
