#pragma once

#include "../Core/Base.h"

struct MemBumpAllocatorVM;

struct MemThreadAllocatorArena
{
    MemBumpAllocatorVM* GetOrCreateAllocatorForCurrentThread();
    void Reset();
};

namespace Mem
{
    API MemThreadAllocatorArena* CreateThreadAllocatorArena(uint32 maxAllocators, size_t capacity, size_t pageSize, 
                                                            const char* trackingName,
                                                            bool debugMode = false, 
                                                            MemAllocator* alloc = Mem::GetDefaultAlloc());
    API void DestroyThreadAllocatorArena(MemThreadAllocatorArena* arena);
}