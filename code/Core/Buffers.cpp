#include "Buffers.h"

#include "StringUtil.h"

DEFINE_HANDLE(HandleDummy);

_private::HandlePoolTable* _private::handleCreatePoolTable(uint32 capacity, Allocator* alloc)
{
    // Align count to 16, for a better aligned internal memory
    uint32 maxSize = AlignValue(capacity, 16u);

    MemSingleShotMalloc<HandlePoolTable> buff;
    HandlePoolTable* tbl = buff.AddMemberField<uint32>(offsetof(HandlePoolTable, dense), maxSize)
                               .AddMemberField<uint32>(offsetof(HandlePoolTable, sparse), maxSize)
                               .Calloc(alloc);
    tbl->capacity = capacity;
    handleResetPoolTable(tbl);

    return tbl;
}

void _private::handleDestroyPoolTable(HandlePoolTable* tbl, Allocator* alloc)
{
    MemSingleShotMalloc<HandlePoolTable>::Free(tbl, alloc);
}

bool _private::handleGrowPoolTable(HandlePoolTable** pTbl, Allocator* alloc)
{
    HandlePoolTable* tbl = *pTbl;
    uint32 newCapacity = tbl->capacity << 1;

    HandlePoolTable* newTable = handleCreatePoolTable(newCapacity, alloc);
    if (!newTable)
        return false;
    newTable->count = tbl->count;
    memcpy(newTable->dense, tbl->dense, sizeof(uint32) * tbl->capacity);
    memcpy(newTable->sparse, tbl->sparse, sizeof(uint32) * tbl->capacity);

    handleDestroyPoolTable(tbl, alloc);
    *pTbl = newTable;
    return true;
}

_private::HandlePoolTable* _private::handleClone(HandlePoolTable* tbl, Allocator* alloc)
{
    ASSERT(tbl->capacity);
    HandlePoolTable* newTable = handleCreatePoolTable(tbl->capacity, alloc);
    if (!newTable)
        return nullptr;

    newTable->count = tbl->count;
    memcpy(newTable->dense, tbl->dense, sizeof(uint32) * tbl->capacity);
    memcpy(newTable->sparse, tbl->sparse, sizeof(uint32) * tbl->capacity);

    return newTable;
}

uint32 _private::handleNew(HandlePoolTable* tbl)
{
    if (tbl->count < tbl->capacity) {
        uint32 index = tbl->count++;
        HandleDummy handle(tbl->dense[index]);

        // increase generation
        uint32 gen = handle.GetGen();
        uint32 sparseIndex = handle.GetSparseIndex();
        HandleDummy newHandle;
        newHandle.Set(++gen, sparseIndex);

        tbl->dense[index] = static_cast<uint32>(newHandle);
        tbl->sparse[sparseIndex] = index;
        return static_cast<uint32>(newHandle);
    } else {
        ASSERT_MSG(0, "handle pool table is full");
    }

    return UINT32_MAX;
}

void _private::handleDel(HandlePoolTable* tbl, uint32 handle)
{
    ASSERT(tbl->count > 0);
    ASSERT(handleIsValid(tbl, handle));

    HandleDummy h(handle);

    uint32 index = tbl->sparse[h.GetSparseIndex()];
    HandleDummy lastHandle = HandleDummy(tbl->dense[--tbl->count]);

    tbl->dense[tbl->count] = handle;
    tbl->sparse[lastHandle.GetSparseIndex()] = index;
    tbl->dense[index] = static_cast<uint32>(lastHandle);
}

void _private::handleResetPoolTable(HandlePoolTable* tbl)
{
    tbl->count = 0;
    uint32* dense = tbl->dense;
    for (uint32 i = 0, c = tbl->capacity; i < c; i++) {
        HandleDummy h;
        h.Set(0, i);
        dense[i] = static_cast<uint32>(h);
    }
}

bool _private::handleIsValid(const HandlePoolTable* tbl, uint32 handle)
{
    ASSERT(handle);
    HandleDummy h(handle);

    uint32 index = tbl->sparse[h.GetSparseIndex()];
    return index < tbl->count && tbl->dense[index] == handle;
}

uint32 _private::handleAt(const HandlePoolTable* tbl, uint32 index)
{
    ASSERT(index < tbl->count);
    return tbl->dense[index];
}

bool _private::handleFull(const HandlePoolTable* tbl)
{
    return tbl->count == tbl->capacity;
}

size_t _private::handleGetMemoryRequirement(uint32 capacity)
{
    uint32 maxSize = AlignValue(capacity, 16u);
    
    MemSingleShotMalloc<HandlePoolTable> mallocator;
    return mallocator.AddMemberField<uint32>(offsetof(HandlePoolTable, dense), maxSize)
                     .AddMemberField<uint32>(offsetof(HandlePoolTable, sparse), maxSize)
                     .GetMemoryRequirement();
}

_private::HandlePoolTable* _private::handleCreatePoolTableWithBuffer(uint32 capacity, void* data, size_t size)
{
    // Align count to 16, for a better aligned internal memory
    uint32 maxSize = AlignValue(capacity, 16u);
    
    MemSingleShotMalloc<HandlePoolTable> mallocator;
    HandlePoolTable* tbl = mallocator.AddMemberField<uint32>(offsetof(HandlePoolTable, dense), maxSize)
                                     .AddMemberField<uint32>(offsetof(HandlePoolTable, sparse), maxSize)
                                     .Calloc(data, size);
    tbl->capacity = capacity;
    handleResetPoolTable(tbl);
    
    return tbl;
}

bool _private::handleGrowPoolTableWithBuffer(HandlePoolTable** pTbl, void* buff, size_t size)
{
    HandlePoolTable* tbl = *pTbl;
    uint32 newCapacity = tbl->capacity << 1;
    
    HandlePoolTable* newTable = handleCreatePoolTableWithBuffer(newCapacity, buff, size);
    if (!newTable)
        return false;
    newTable->count = tbl->count;
    memcpy(newTable->dense, tbl->dense, sizeof(uint32) * tbl->capacity);
    memcpy(newTable->sparse, tbl->sparse, sizeof(uint32) * tbl->capacity);
    
    *pTbl = newTable;
    return true;
}

size_t Blob::ReadStringBinary(char* outStr, [[maybe_unused]] uint32 outStrSize) const
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


size_t Blob::ReadStringBinary16(char* outStr, [[maybe_unused]] uint32 outStrSize) const
{
    uint16 len = 0;
    size_t readStrBytes = 0;
    size_t readBytes = Read<uint16>(&len);
    ASSERT(readBytes == sizeof(len));
    ASSERT(len < outStrSize);
    if (len) {
        readStrBytes = Read(outStr, len);
        ASSERT(readStrBytes == len);
    }
    outStr[len] = '\0';
    return readStrBytes + readBytes;
}

size_t Blob::WriteStringBinary(const char* str, uint32 len)
{
    ASSERT(str);
    if (len == 0)
        len = strLen(str);
    size_t writtenBytes = Write<uint32>(len);
    if (len) 
        writtenBytes += Write(str, len);
    return writtenBytes;
}

size_t Blob::WriteStringBinary16(const char* str, uint32 len)
{
    ASSERT(str);
    if (len == 0)
        len = strLen(str);
    ASSERT(len < UINT16_MAX);
    size_t writtenBytes = Write<uint16>(uint16(len));
    if (len) 
        writtenBytes += Write(str, len);
    return writtenBytes;
}


