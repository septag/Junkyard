#include "Memory.h"

#include <stdlib.h>

// MemPro
#define OVERRIDE_NEW_DELETE
#define WAIT_FOR_CONNECT true
#define MEMPRO_BACKTRACE(_stackframes, _maxStackframes, _hashPtr) debugCaptureStacktrace(_stackframes, _maxStackframes, 3, _hashPtr)
#include "../External/mempro/MemPro.cpp"

#if MEMPRO_ENABLED
    #define MEMPRO_TRACK_REALLOC(oldPtr, ptr, size) \
        do { if (oldPtr)  { \
        MEMPRO_TRACK_FREE(oldPtr);   \
        } \
        MEMPRO_TRACK_ALLOC(ptr, size);} while(0)
#else
    #define MEMPRO_TRACK_REALLOC(oldPtr, ptr, size)
#endif
//

#include "System.h"
#include "Buffers.h"
#include "Array.h"
#include "Atomic.h"
#include "Settings.h"
#include "Log.h"

// TODO: Remove this dependency
#include "TracyHelper.h"

#if PLATFORM_POSIX
    #include <stdlib.h>
#else
    #include <malloc.h>
#endif

#include "../External/tlsf/tlsf.h"

#if PLATFORM_WINDOWS
    #define aligned_malloc(_align, _size) _aligned_malloc(_size, _align)
    #define aligned_realloc(_ptr, _align, _size) _aligned_realloc(_ptr, _size, _align)
    #define aligned_free(_ptr) _aligned_free(_ptr)
#else
    INLINE void* aligned_malloc(uint32 align, size_t size);
    INLINE void* aligned_realloc(void*, uint32, size_t);
    INLINE void  aligned_free(void* ptr);
#endif

static constexpr size_t kTempMaxBufferSize = 4*kGB;
static constexpr uint32 kTempFramePeaksCount = 4;
static constexpr uint32 kTempPageSize = 256*kKB;
static constexpr float  kTempValidateResetTime = 5.0f;
static constexpr uint32 kTempMaxStackframes = 8;

struct MemHeapAllocator : Allocator 
{
    void* Malloc(size_t size, uint32 align) override;
    void* Realloc(void* ptr, size_t size, uint32 align) override;
    void  Free(void* ptr, uint32 align) override;
};

struct MemTempStack
{
    size_t baseOffset;
    size_t offset;
    void* lastAllocatedPtr;
    void* stacktrace[kTempMaxStackframes];
    Array<MemDebugPointer> debugPointers;    
    uint16 numStackframes;
};

struct MemTempContext
{
    Array<MemTempStack> allocStack;
    uint32 generationIdx;   // Just a counter to make temp IDs unique
    uint32 resetCount;
    size_t framePeaks[kTempFramePeaksCount];
    size_t curFramePeak;
    size_t peakBytes;
    uint8* buffer;
    size_t bufferSize;
    alignas(CACHE_LINE_SIZE) atomicUint32 isInUse;  // This is atomic because we are accessing it in the memTempReset thread
    float noresetTime;

    bool init;      // is Buffer initialized ?
    bool used;      // Temp allocator is used since last reset ?
    bool debugMode; // Allocate from heap

    ~MemTempContext();
};

struct MemState
{
    MemFailCallback  memFailFn			= nullptr;
    void* 			 memFailUserdata	= nullptr;
    Allocator*		 defaultAlloc       = &heapAlloc;
    MemHeapAllocator heapAlloc;
    size_t           pageSize           = sysGetPageSize();
    Mutex            tempMtx;
    Array<MemTempContext*> tempCtxs; 

    MemState()  { tempMtx.Initialize(); ASSERT(kTempPageSize % pageSize == 0); }
    ~MemState() { tempMtx.Release(); tempCtxs.Free(); }
};

static MemState gMem;
static thread_local MemTempContext gMemTempCtx;

void memSetFailCallback(MemFailCallback callback, void* userdata)
{
    gMem.memFailFn = callback;
    gMem.memFailUserdata = userdata;
}

void memRunFailCallback()
{
    if (gMem.memFailFn) {
        gMem.memFailFn(gMem.memFailUserdata);
    }
}

void* memAlignPointer(void* ptr, size_t extra, uint32 align)
{
    union {
        void* ptr;
        uintptr_t addr;
    } un;
    un.ptr = ptr;
    uintptr_t unaligned = un.addr + extra;    // space for header
    uintptr_t aligned = AlignValue<uintptr_t>(unaligned, align);
    un.addr = aligned;
    return un.ptr;
}

Allocator* memDefaultAlloc()
{
    return static_cast<Allocator*>(&gMem.heapAlloc);
}

void memSetDefaultAlloc(Allocator* alloc)
{
    gMem.defaultAlloc = alloc != nullptr ? alloc : &gMem.heapAlloc;
}

void memTempSetDebugMode(bool enable)
{
    ASSERT_MSG(gMemTempCtx.allocStack.Count() == 0, "MemTemp must be at it's initial state");
    gMemTempCtx.debugMode = enable;
}

MemTempId memPushTempId()
{
    // Note that we use an atomic var for syncing between threads and memTempReset caller thread
    // The reason is because while someone Pushed the mem temp stack. Reset might be called and mess things up
    atomicExchange32Explicit(&gMemTempCtx.isInUse, 1, AtomicMemoryOrder::Release);

    ++gMemTempCtx.generationIdx;
    ASSERT_MSG(gMemTempCtx.generationIdx <= UINT16_MAX, "Too many push temp allocator, generation overflowed");

    if (!gMemTempCtx.init) {
        if (gMemTempCtx.buffer == nullptr && !gMemTempCtx.debugMode) {
            gMemTempCtx.buffer = (uint8*)memVirtualReserve(kTempMaxBufferSize);
            gMemTempCtx.bufferSize = kTempPageSize;
            memVirtualCommit(gMemTempCtx.buffer, gMemTempCtx.bufferSize); 
        }
        gMemTempCtx.init = true;
    }

    if (!gMemTempCtx.used) {
        MutexScope mtx(gMem.tempMtx);
        gMem.tempCtxs.Push(&gMemTempCtx);
        gMemTempCtx.used = true;
    }

    uint32 index = gMemTempCtx.allocStack.Count();
    ASSERT_MSG(index <= UINT16_MAX, "Temp stack depth is too high! Perhaps a mistake in Push/Pop order");

    // Id: High bits is the index to the allocStack
    //     Low bits is the call generation
    MemTempId id = (index << 16) | (gMemTempCtx.generationIdx & 0xffff);
    
    MemTempStack memStack { 
        .baseOffset = index > 0 ? (gMemTempCtx.allocStack.Last().baseOffset + gMemTempCtx.allocStack.Last().offset) : 0
    };

    if constexpr(!CONFIG_FINAL_BUILD) {
        if (settingsGetDebug().captureStacktraceForTempAllocator)
            memStack.numStackframes = debugCaptureStacktrace(memStack.stacktrace, kTempMaxStackframes, 2);
    }

    gMemTempCtx.allocStack.Push(memStack);
    return id;
}

void memPopTempId(MemTempId id)
{
    ASSERT(id);
    ASSERT(gMemTempCtx.used);
    ASSERT(gMemTempCtx.generationIdx);

    [[maybe_unused]] uint32 index = id >> 16;
    ASSERT_MSG(index == gMemTempCtx.allocStack.Count() - 1, "Invalid temp Push/Pop order");

    MemTempStack memStack = gMemTempCtx.allocStack.PopLast();
    if (memStack.debugPointers.Count()) {
        for (MemDebugPointer p : memStack.debugPointers)
            gMem.defaultAlloc->Free(p.ptr, p.align);
        memStack.debugPointers.Free();
    }
    atomicExchange32Explicit(&gMemTempCtx.isInUse, 0, AtomicMemoryOrder::Release);
}

void* memReallocTemp(MemTempId id, void* ptr, size_t size, uint32 align)
{
    ASSERT(id);
    ASSERT(gMemTempCtx.used);
    ASSERT(size);

    uint32 index = id >> 16;
    ASSERT_MSG(index == gMemTempCtx.allocStack.Count() - 1, "Invalid temp id, likely doesn't belong to current temp stack scope");

    MemTempStack& memStack = gMemTempCtx.allocStack[index];

    if (!gMemTempCtx.debugMode) {
        align = Max(align, CONFIG_MACHINE_ALIGNMENT);
        size = AlignValue<size_t>(size, align);

        // For a common case that we call realloc several times (dynamic Arrays), we can reuse the last allocated pointer
        void* newPtr = nullptr;
        size_t lastSize = 0;
        if (ptr && memStack.lastAllocatedPtr == ptr) {
            lastSize = *((size_t*)ptr - 1);
            ASSERT(size > lastSize);
            newPtr = ptr;
        }

        // align to requested alignment
        size_t offset = memStack.baseOffset + memStack.offset;
        if (newPtr == nullptr) {
            offset += sizeof(size_t);
            if (offset % align != 0) 
                offset = AlignValue<size_t>(offset, align);
        }
        else {
            ASSERT(offset % align == 0);
        }
    
        size_t endOffset = offset + (size - lastSize);

        if (endOffset > kTempMaxBufferSize) {
            MEMORY_FAIL();
            return nullptr;
        }

        // Grow the buffer if necessary (double size policy)
        if (endOffset > gMemTempCtx.bufferSize) {
            size_t newSize = Clamp(gMemTempCtx.bufferSize << 1, endOffset, kTempMaxBufferSize);

            // Align grow size to page size for virtual memory commit
            size_t growSize = AlignValue(newSize - gMemTempCtx.bufferSize, gMem.pageSize);
            memVirtualCommit(gMemTempCtx.buffer + gMemTempCtx.bufferSize, growSize);
            gMemTempCtx.bufferSize += growSize;
        }

        gMemTempCtx.curFramePeak = Max<size_t>(gMemTempCtx.curFramePeak, endOffset);
        gMemTempCtx.peakBytes = Max<size_t>(gMemTempCtx.peakBytes, endOffset);

        // Create the pointer if we are not re-using the previous one
        if (!newPtr) {
            newPtr = gMemTempCtx.buffer + offset;

            // we are not re-using the previous allocation, memcpy the previous block in case of realloc
            if (ptr) {
                memcpy(newPtr, ptr, *((size_t*)ptr - 1));
            }
        }

        *((size_t*)newPtr - 1) = size;
        memStack.offset = endOffset - memStack.baseOffset;
        memStack.lastAllocatedPtr = newPtr;
        return newPtr;
    }
    else {
        if (ptr == nullptr)
            ptr = gMem.defaultAlloc->Malloc(size, align);
        else
            ptr = gMem.defaultAlloc->Realloc(ptr, size, align);

        if (ptr) {
            memStack.offset += size;
            size_t endOffset = memStack.baseOffset + memStack.offset;

            gMemTempCtx.peakBytes = Max<size_t>(gMemTempCtx.peakBytes, endOffset);
            memStack.debugPointers.Push(MemDebugPointer {ptr, align});
        }
        return ptr;
    }
}

MemTempContext::~MemTempContext()
{
    if (buffer) {
        if (bufferSize)
            memVirtualDecommit(buffer, bufferSize);
        memVirtualRelease(buffer, bufferSize);
    }

    if (debugMode) {
        for (MemTempStack& memStack : allocStack) {
            for (MemDebugPointer p : memStack.debugPointers)
                gMem.defaultAlloc->Free(p.ptr, p.align);
            memStack.debugPointers.Free();
        }
    }
    allocStack.Free();

    used = false;
    init = false;
}

void* memAllocTemp(MemTempId id, size_t size, uint32 align)
{
    return memReallocTemp(id, nullptr, size, align);
}

void* memAllocTempZero(MemTempId id, size_t size, uint32 align)
{
    void* ptr = memAllocTemp(id, size, align);
    if (ptr)
        memset(ptr, 0x0, size);
    return ptr;
}

void _private::memTempReset(float dt)
{
    MutexScope mtx(gMem.tempMtx);
    for (uint32 i = 0; i < gMem.tempCtxs.Count(); i++) {
        MemTempContext* ctx = gMem.tempCtxs[i];

        if (atomicLoad32Explicit(&ctx->isInUse, AtomicMemoryOrder::Acquire)) 
            continue;

        if (ctx->used) {
            // TODO: do some kind of heuristics to detect leaks if allocStack is not empty
            if (ctx->allocStack.Count() == 0) {
                ctx->generationIdx = 0;
                ctx->framePeaks[ctx->resetCount] = ctx->curFramePeak;
                ctx->resetCount = (ctx->resetCount + 1) % kTempFramePeaksCount;
                ctx->curFramePeak = 0;
                ctx->noresetTime = 0;

                if (!ctx->debugMode) {
                    // resize buffer to the maximum of the last 4 frames peak allocations
                    // So based on the last frames activity, we might grow or shrink the temp buffer
                    size_t maxPeakSize = 0;
                    for (uint32 k = 0; k < kTempFramePeaksCount; k++) {
                        if (ctx->framePeaks[k] > maxPeakSize) 
                            maxPeakSize = ctx->framePeaks[k];
                    }

                    maxPeakSize = Max<size_t>(kTempPageSize, maxPeakSize);
                    maxPeakSize = AlignValue(maxPeakSize, gMem.pageSize);
                    if (maxPeakSize > gMemTempCtx.bufferSize) {
                        size_t growSize = maxPeakSize - gMemTempCtx.bufferSize;
                        memVirtualCommit(gMemTempCtx.buffer + gMemTempCtx.bufferSize, growSize);
                    }
                    else if (maxPeakSize < gMemTempCtx.bufferSize) {
                        size_t shrinkSize = gMemTempCtx.bufferSize - maxPeakSize;
                        memVirtualDecommit(gMemTempCtx.buffer + maxPeakSize, shrinkSize);
                    }
                    gMemTempCtx.bufferSize = maxPeakSize;
                }

                ctx->used = false;
                gMem.tempCtxs.RemoveAndSwap(i);
                i--;
            }   // MemTempContext can reset (allocStack is empty)
            else {
                ctx->noresetTime += dt;
                if (ctx->noresetTime >= kTempValidateResetTime) {
                    logWarning("Temp stack failed to pop during the frame after %.0f seconds", kTempValidateResetTime);
                    ctx->noresetTime = 0;

                    if constexpr(!CONFIG_FINAL_BUILD) {
                        if (settingsGetDebug().captureStacktraceForTempAllocator) {
                            DebugStacktraceEntry entries[kTempMaxStackframes];
                            uint32 index = 0;
                            logDebug("Callstacks for each remaining MemTempPush:");
                            for (const MemTempStack& memStack : ctx->allocStack) {
                                debugResolveStacktrace(memStack.numStackframes, memStack.stacktrace, entries);
                                logDebug("\t%u)", ++index);
                                for (uint16 s = 0; s < memStack.numStackframes; s++) {
                                    logDebug("\t\t%s(%u): %s", entries[s].filename, entries[s].line, entries[s].name);
                                }
                            }
                        }
                    } // CONFIG_FINAL_BUILD
                }
            }
        } // MemTempContext->used
    }
}

void* MemHeapAllocator::Malloc(size_t size, uint32 align)
{
    void* ptr;
    if (align <= CONFIG_MACHINE_ALIGNMENT) {
        ptr = malloc(size);
    }
    else {
        align = Max(align, CONFIG_MACHINE_ALIGNMENT);
        ptr = aligned_malloc(align, size);
    }
    if (!ptr) {
        MEMORY_FAIL();
        return nullptr;
    }

    TracyCAlloc(ptr, size);        

    if constexpr (MEMPRO_ENABLED) {
        if (settingsGetEngine().enableMemPro)
            MEMPRO_TRACK_ALLOC(ptr, size);
    }
    return ptr;
}
    
void* MemHeapAllocator::Realloc(void* ptr, size_t size, uint32 align)
{
    [[maybe_unused]] void* freePtr = ptr;

    if (align <= CONFIG_MACHINE_ALIGNMENT) {
        ptr = realloc(ptr, size);
    }
    else {
        align = Max(align, CONFIG_MACHINE_ALIGNMENT);
        ptr = aligned_realloc(ptr, align, size);
    }
    
    if (!ptr) {
        MEMORY_FAIL();
        return nullptr;
    }
    
    TracyCRealloc(freePtr, ptr, size);

    if constexpr (MEMPRO_ENABLED) {
        if (settingsGetEngine().enableMemPro) 
            MEMPRO_TRACK_REALLOC(freePtr, ptr, size);
    }
   
    return ptr;
}
    
void MemHeapAllocator::Free(void* ptr, uint32 align)
{
    if (ptr != nullptr) {
        if (align <= CONFIG_MACHINE_ALIGNMENT) {
            free(ptr);
        }
        else {
            aligned_free(ptr);
        }
    
        TracyCFree(ptr);

        if constexpr (MEMPRO_ENABLED) {
            if (settingsGetEngine().enableMemPro) 
                MEMPRO_TRACK_FREE(ptr);
        }
    }
}

//------------------------------------------------------------------------
// Temp Allocator Scope
MemTempAllocator::MemTempAllocator() : 
    _id(memPushTempId()), 
    _fiberProtectorId(debugFiberScopeProtector_Push("TempAllocator")),
    _ownsId(true) 
{ 
}

MemTempAllocator::MemTempAllocator(MemTempId id) : 
    _id(id), 
    _fiberProtectorId(debugFiberScopeProtector_Push("TempAllocator")),
    _ownsId(false)
{
}

MemTempAllocator::~MemTempAllocator() 
{ 
    debugFiberScopeProtector_Pop(_fiberProtectorId); 
    if (_ownsId) 
        memPopTempId(_id); 
}

void* MemTempAllocator::Malloc(size_t size, uint32 align) 
{
    return memAllocTemp(_id, size, align);
}

void* MemTempAllocator::Realloc(void* ptr, size_t size, uint32 align) 
{
    return memReallocTemp(_id, ptr, size, align);
}

void MemTempAllocator::Free(void*, uint32) 
{
}

size_t MemTempAllocator::GetOffset() const
{
    uint32 index = _id >> 16;
    ASSERT_MSG(index == gMemTempCtx.allocStack.Count() - 1, "Invalid temp id, likely doesn't belong to current temp stack scope");

    const MemTempStack& memStack = gMemTempCtx.allocStack[index];
    return memStack.baseOffset + memStack.offset;
}

size_t MemTempAllocator::GetPointerOffset(void* ptr) const
{
    return size_t((uint8*)ptr - gMemTempCtx.buffer);
}

//------------------------------------------------------------------------
// BudgetAllocator
MemBudgetAllocator::MemBudgetAllocator(const char* name)
{
    strCopy(_name, sizeof(_name), name);
}

void MemBudgetAllocator::Initialize(size_t sizeBudget, size_t pageSize, bool commitAll, bool debugMode)
{
    _debugMode = debugMode;

    if (!debugMode) {
        ASSERT(sizeBudget >= 4*kKB);
        if (pageSize == 0)
            pageSize = 256*kKB;
        ASSERT(pageSize % sysGetPageSize() == 0);
        _pageSize = !commitAll ? pageSize : 0llu;

        _buffer = reinterpret_cast<uint8*>(memVirtualReserve(sizeBudget, MemVirtualFlags::None));

        if (commitAll) {
            memVirtualCommit(_buffer, sizeBudget);
            _commitSize = sizeBudget;
        }
        else {
            memVirtualCommit(_buffer, pageSize);
            _commitSize = pageSize;
        }
        _maxSize = sizeBudget;
    }
    else {
        _debugPointers = NEW(gMem.defaultAlloc, Array<MemDebugPointer>);
    }    
}

void MemBudgetAllocator::Release()
{
    if (_buffer) {
        memVirtualDecommit(_buffer, _commitSize);
        memVirtualRelease(_buffer, _maxSize);
    }

    if (_debugPointers) {
        for (MemDebugPointer p: *_debugPointers)
            gMem.defaultAlloc->Free(p.ptr, p.align);
        _debugPointers->Free();
        memFree(_debugPointers, gMem.defaultAlloc);
    }
}

void* MemBudgetAllocator::Malloc(size_t size, uint32 align)
{
    if (!_debugMode) {
        align = Max(align, CONFIG_MACHINE_ALIGNMENT);
        size = AlignValue<size_t>(size, align);
    
        // align to requested alignment
        size_t offset = _offset;
        if (offset % align != 0) 
            offset = AlignValue<size_t>(offset, align);
        size_t endOffset = offset + size;
    
        if (endOffset > _maxSize) {
            MEMORY_FAIL();
            return nullptr;
        }
    
        // Grow the buffer if necessary (double size policy)
        if (endOffset > _commitSize) {
            size_t newSize = endOffset;
            // Align grow size to page size for virtual memory commit
            size_t growSize = AlignValue(newSize - _commitSize, _pageSize);
            memVirtualCommit(_buffer + _commitSize, growSize);
            _commitSize += growSize;
        }
    
        void* ptr = _buffer + _offset;
        _offset = endOffset;
        return ptr;
    }
    else {
        void* ptr = gMem.defaultAlloc->Malloc(size, align);
        if (ptr) {
            _debugPointers->Push(MemDebugPointer {ptr, align});
            _commitSize += size;
            _offset += size;
        }
        return ptr;
    }
}

void* MemBudgetAllocator::Realloc(void* ptr, size_t size, uint32 align)
{
    if (ptr == nullptr) {
        return Malloc(size, align);
    }
    else {
        ASSERT_MSG(0, "Normally, you should not realloc with BudgetAllocator. Check the code");
        return nullptr;
    }    
}

void MemBudgetAllocator::Free(void*, uint32)
{
    ASSERT(0);
}

//------------------------------------------------------------------------
// MemTlsfAllocator_ThreadSafe
void* MemTlsfAllocator_ThreadSafe::Malloc(size_t size, uint32 align)
{
    AtomicLockScope lock(_lock);
    return MemTlsfAllocator::Malloc(size, align);
}

void* MemTlsfAllocator_ThreadSafe::Realloc(void* ptr, size_t size, uint32 align)
{
    AtomicLockScope lock(_lock);
    return MemTlsfAllocator::Realloc(ptr, size, align);
}

void  MemTlsfAllocator_ThreadSafe::Free(void* ptr, uint32 align)
{
    AtomicLockScope lock(_lock);
    MemTlsfAllocator::Free(ptr, align);
}

//------------------------------------------------------------------------
// TLSF allocator
size_t MemTlsfAllocator::GetMemoryRequirement(size_t poolSize)
{
    return tlsf_size() + tlsf_align_size() + tlsf_pool_overhead() + poolSize;
}

void MemTlsfAllocator::Initialize([[maybe_unused]] size_t poolSize, void* buffer, size_t size, bool debugMode)
{
    _debugMode = debugMode;

    if (!debugMode) {
        ASSERT(GetMemoryRequirement(poolSize) <= size);

        _tlsf = tlsf_create_with_pool(buffer, size);
        if (_tlsf == nullptr) {
            MEMORY_FAIL();
        }
        _tlsfSize = size;
    }
}

void MemTlsfAllocator::Release()
{
    if (_tlsf)
        tlsf_destroy(_tlsf);
}

void* MemTlsfAllocator::Malloc(size_t size, uint32 align)
{
    if (!_debugMode) {
        ASSERT(_tlsf);

        void* ptr = nullptr;
        align = Max(align, CONFIG_MACHINE_ALIGNMENT);
        if (align <= CONFIG_MACHINE_ALIGNMENT) {
            ptr = tlsf_malloc(_tlsf, size);
        }
        else {
            ptr = tlsf_memalign(_tlsf, align, size);
        }

        if (ptr) {
            size_t blockSize = tlsf_block_size(ptr);
            _allocatedSize += blockSize;

            TracyCAlloc(ptr, size);

            if constexpr (MEMPRO_ENABLED) {
                if (settingsGetEngine().enableMemPro)
                    MEMPRO_TRACK_ALLOC(ptr, size);
            }
            return ptr;
        }
        else {
            MEMORY_FAIL();
            return nullptr;
        }
    }
    else {
        return gMem.defaultAlloc->Malloc(size, align);
    }
}

void* MemTlsfAllocator::Realloc(void* ptr, size_t size, uint32 align)
{
    if (!_debugMode) {
        ASSERT(_tlsf);
        [[maybe_unused]] void* freePtr = ptr;

        if (ptr) 
            _allocatedSize -= tlsf_block_size(ptr);

        ptr = tlsf_realloc(_tlsf, ptr, size);
        if (ptr) {
            _allocatedSize += tlsf_block_size(ptr);
            TracyCRealloc(freePtr, ptr, size);

            if constexpr (MEMPRO_ENABLED) {
                if (settingsGetEngine().enableMemPro)
                    MEMPRO_TRACK_REALLOC(freePtr, ptr, size);
            }
            return ptr;
        }
        else {
            MEMORY_FAIL();
            return nullptr;
        }
    }
    else {
        return gMem.defaultAlloc->Realloc(ptr, size, align);
    }
}

void MemTlsfAllocator::Free(void* ptr, uint32 align)
{
    if (!_debugMode) {
        ASSERT(_tlsf);
        if (ptr) {
            size_t blockSize = tlsf_block_size(ptr);
            _allocatedSize -= blockSize;
            tlsf_free(_tlsf, ptr);
            TracyCFree(ptr);

            if constexpr (MEMPRO_ENABLED) {
                if (settingsGetEngine().enableMemPro)
                    MEMPRO_TRACK_FREE(ptr);
            }
        }
    }
    else {
        return gMem.defaultAlloc->Free(ptr, align);
    }
}

bool MemTlsfAllocator::Validate()
{
    if (!_debugMode) {
        ASSERT(_tlsf);
        return tlsf_check(_tlsf) == 0;
    }
    else {
        return true;
    }
}

float MemTlsfAllocator::CalculateFragmentation()
{
    struct AllocData
    {
        uint64 offset;
        uint64 size;
    };

    struct GetAllocsData
    {
        Array<AllocData> allocs;
        uint64 baseOffset;
    };

    auto GetAllocs = [](void* ptr, size_t size, int used, void* user)
    {
        GetAllocsData* data = reinterpret_cast<GetAllocsData*>(user);
        if (used) {
            data->allocs.Push(AllocData { 
                .offset = PtrToInt<uint64>(ptr) - data->baseOffset,
                .size = size
            });
        }
    };

    if (!_debugMode) {
        MemTempAllocator tmpAlloc;
        GetAllocsData data;
        data.allocs.SetAllocator(&tmpAlloc);
        data.baseOffset = PtrToInt<uint64>(_tlsf);

        tlsf_walk_pool(tlsf_get_pool(_tlsf), GetAllocs, &data);

        if (data.allocs.Count()) {
            data.allocs.Sort([](const AllocData& a, const AllocData& b) { return a.offset < b.offset ? -1 : 1; });

            // Start from the first offset
            uint32 lastItemIdx = data.allocs.Count() - 1;
            uint64 firstOffset = data.allocs[0].offset;
            uint64 totalSize = (data.allocs[lastItemIdx].offset + data.allocs[lastItemIdx].size) - firstOffset;
            uint64 fragSize = 0;

            for (uint32 i = 1; i < data.allocs.Count(); i++) {
                uint64 prevEndOffset = data.allocs[i - 1].offset + data.allocs[i - 1].size;
                fragSize += data.allocs[i].offset - prevEndOffset;
            }

            return static_cast<float>(double(fragSize) / double(totalSize));
        }
    }

    return 0;
}

//------------------------------------------------------------------------
// Custom implementation for aligned allocations
#if !PLATFORM_WINDOWS
INLINE void* aligned_malloc(uint32 align, size_t size)
{
    ASSERT(align >= CONFIG_MACHINE_ALIGNMENT);
    
    size_t total = size + align + sizeof(uint32);
    uint8* ptr = (uint8*)malloc(total);
    if (!ptr)
        return nullptr;
    uint8* aligned = (uint8*)memAlignPointer(ptr, sizeof(uint32), align);
    uint32* header = (uint32*)aligned - 1;
    *header = PtrToInt<uint32>((void*)(aligned - ptr));  // Save the offset needed to move back from aligned pointer
    return aligned;
}

INLINE void* aligned_realloc(void* ptr, uint32 align, size_t size)
{
    ASSERT(align >= CONFIG_MACHINE_ALIGNMENT);

    if (ptr) {
        uint8* aligned = (uint8*)ptr;
        uint32 offset = *((uint32*)aligned - 1);
        ptr = aligned - offset;

        size_t total = size + align + sizeof(uint32);
        ptr = realloc(ptr, total);
        if (!ptr)
            return nullptr;
        uint8* newAligned = (uint8*)memAlignPointer(ptr, sizeof(uint32), align);
        if (newAligned == aligned)
            return aligned;

        aligned = (uint8*)ptr + offset;
        memmove(newAligned, aligned, size);
        uint32* header = (uint32*)newAligned - 1;
        *header = PtrToInt<uint32>((void*)(newAligned - (uint8*)ptr));
        return newAligned;
    }
    else {
        return aligned_malloc(align, size);
    }
}

INLINE void aligned_free(void* ptr)
{
    if (ptr) {
        uint8* aligned = (uint8*)ptr;
        uint32* header = (uint32*)aligned - 1;
        ptr = aligned - *header;
        free(ptr);
    }
}
#endif  // !PLATFORM_WINDOWS