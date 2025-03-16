#include "Engine.h"

// TEMP
#if PLATFORM_LINUX

#include "Core/Allocators.h"
#include "Core/Settings.h"
#include "Core/Log.h"
#include "Core/System.h"
#include "Core/Arrays.h"

#include "Common/JunkyardSettings.h"

static constexpr size_t ENGINE_MAX_MEMORY_SIZE = 2*SIZE_GB;

struct EngineContext
{
    MemProxyAllocator alloc;
    MemBumpAllocatorVM mainAlloc;   // Virtual memory bump allocator that is used for initializing all sub-systems

    SysInfo sysInfo = {};

    Array<MemProxyAllocator*> proxyAllocs;
};

static EngineContext gEng;

void Engine::RegisterProxyAllocator(MemProxyAllocator* alloc)
{
    [[maybe_unused]] uint32 index = gEng.proxyAllocs.FindIf([alloc](const MemProxyAllocator* a) { return alloc == a; });
    ASSERT(index == -1);
    gEng.proxyAllocs.Push(alloc);
}

void Engine::HelperInitializeProxyAllocator(MemProxyAllocator* alloc, const char* name, MemAllocator* baseAlloc)
{
    MemProxyAllocatorFlags proxyAllocFlags = SettingsJunkyard::Get().engine.trackAllocations ? 
        MemProxyAllocatorFlags::EnableTracking : MemProxyAllocatorFlags::None;

    if (!baseAlloc) {
        ASSERT(gEng.mainAlloc.IsInitialized());
        alloc->Initialize(name, &gEng.mainAlloc, proxyAllocFlags);
    }
    else {
        alloc->Initialize(name, baseAlloc, proxyAllocFlags);
    }
}

#endif