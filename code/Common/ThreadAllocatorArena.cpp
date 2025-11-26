#include "ThreadAllocatorArena.h"

#include "../Core/Allocators.h"
#include "../Core/Hash.h"
#include "../Core/System.h"

#include "../Engine.h"

struct MemThreadAllocatorArenaInternal : MemThreadAllocatorArena
{
    SpinLockMutex threadToAllocatorTableMtx;
    HashTableUint threadToAllocatorTable;
    const char* rootTrackingName;
    String32* trackingNames;
    MemAllocator* parentAlloc;
    MemBumpAllocatorVM* allocators;
    size_t capacity;
    size_t pageSize;
    uint32 maxAllocators;
    uint32 numAllocators;
    bool debugMode;
};

MemThreadAllocatorArena* Mem::CreateThreadAllocatorArena(uint32 maxAllocators, size_t capacity, size_t pageSize, 
                                                         const char* trackingName,
                                                         bool debugMode, 
                                                         MemAllocator* alloc)
{
    ASSERT(maxAllocators && maxAllocators < UINT16_MAX) ;
    ASSERT(capacity >= CONFIG_MACHINE_ALIGNMENT);
    ASSERT(pageSize >= CONFIG_MACHINE_ALIGNMENT);
    ASSERT(alloc);

    MemSingleShotMalloc<MemThreadAllocatorArenaInternal> mallocator;
    uint32 hashTableSize = (uint32)HashTableUint::GetMemoryRequirement(maxAllocators);

    MemBumpAllocatorVM* vmAllocsMem;
    uint8* hashTableMem;
    mallocator.AddMemberArray<String32>(offsetof(MemThreadAllocatorArenaInternal, trackingNames), maxAllocators);
    mallocator.AddExternalPointerField<uint8>(&hashTableMem, hashTableSize);
    mallocator.AddExternalPointerField<MemBumpAllocatorVM>(&vmAllocsMem, maxAllocators);
    MemThreadAllocatorArenaInternal* arena = mallocator.Calloc(alloc);

    arena->threadToAllocatorTable.Reserve(maxAllocators, hashTableMem, hashTableSize);
    arena->rootTrackingName = trackingName;
    arena->parentAlloc = alloc;
    arena->allocators = PLACEMENT_NEW_ARRAY(vmAllocsMem, MemBumpAllocatorVM, maxAllocators);
    arena->capacity = capacity;
    arena->pageSize = pageSize;
    arena->maxAllocators = maxAllocators;
    arena->debugMode = debugMode;

    return arena;
}

void Mem::DestroyThreadAllocatorArena(MemThreadAllocatorArena* arena)
{
    if (!arena)
        return;

    MemThreadAllocatorArenaInternal* a = (MemThreadAllocatorArenaInternal*)arena;

    for (uint32 i = 0; i < a->numAllocators; i++) {
        if (a->allocators[i].IsInitialized()) {
            Engine::UnregisterVMAllocator(&a->allocators[i]);
            a->allocators[i].Release();
        }
    }

    MemSingleShotMalloc<MemThreadAllocatorArena>::Free(a, a->parentAlloc);
}

MemBumpAllocatorVM* MemThreadAllocatorArena::GetOrCreateAllocatorForCurrentThread()
{
    MemThreadAllocatorArenaInternal* arena = (MemThreadAllocatorArenaInternal*)this;
    MemBumpAllocatorVM* alloc = nullptr;
    String32* trackingName = nullptr;
    {
        uint32 tId = Thread::GetCurrentId();

        SpinLockMutexScope mtx(arena->threadToAllocatorTableMtx);
        uint32 allocIndex = arena->threadToAllocatorTable.FindAndFetch(tId, uint32(-1));

        if (allocIndex != -1) {
            alloc = &arena->allocators[allocIndex];
        }
        else {
            ASSERT_MSG(arena->numAllocators < arena->maxAllocators, 
                       "Too many threads are accessing allocators. Increase the maximum allocators");
            allocIndex = arena->numAllocators++;
            alloc = &arena->allocators[allocIndex];
            arena->threadToAllocatorTable.Add(tId, allocIndex);
            trackingName = &arena->trackingNames[allocIndex];
        }
    }

    if (!alloc->IsInitialized()) {
        alloc->Initialize(arena->capacity, arena->pageSize, arena->debugMode);

        if (arena->rootTrackingName) {
            ASSERT(trackingName);
            char threadName[32];
            Thread::GetCurrentThreadName(threadName, sizeof(threadName));
            trackingName->FormatSelf("%s (%s)", arena->rootTrackingName, threadName);
            Engine::RegisterVMAllocator(alloc, trackingName->CStr());
        }
    }

    return alloc;

}

void MemThreadAllocatorArena::Reset()
{
    MemThreadAllocatorArenaInternal* arena = (MemThreadAllocatorArenaInternal*)this;

    for (uint32 i = 0; i < arena->numAllocators; i++) {
        if (arena->allocators[i].IsInitialized())
            arena->allocators[i].Reset();
    }
}