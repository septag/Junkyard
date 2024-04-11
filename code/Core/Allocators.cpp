#include "Allocators.h"

#include <stdlib.h>

#include "System.h"
#include "Atomic.h"
#include "Log.h"
#include "BlitSort.h"
#include "TracyHelper.h"
#include "Debug.h"
#include "Arrays.h"

#include "External/tlsf/tlsf.h"
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(5054)
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4245)
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4127)
#define tlsf_assert ASSERT
#include "External/tlsf/tlsf.c"
PRAGMA_DIAGNOSTIC_POP()

//    ████████╗███████╗███╗   ███╗██████╗      █████╗ ██╗     ██╗      ██████╗  ██████╗
//    ╚══██╔══╝██╔════╝████╗ ████║██╔══██╗    ██╔══██╗██║     ██║     ██╔═══██╗██╔════╝
//       ██║   █████╗  ██╔████╔██║██████╔╝    ███████║██║     ██║     ██║   ██║██║     
//       ██║   ██╔══╝  ██║╚██╔╝██║██╔═══╝     ██╔══██║██║     ██║     ██║   ██║██║     
//       ██║   ███████╗██║ ╚═╝ ██║██║         ██║  ██║███████╗███████╗╚██████╔╝╚██████╗
//       ╚═╝   ╚══════╝╚═╝     ╚═╝╚═╝         ╚═╝  ╚═╝╚══════╝╚══════╝ ╚═════╝  ╚═════╝

static constexpr size_t kTempMaxBufferSize = kGB;
static constexpr uint32 kTempFramePeaksCount = 4;
static constexpr uint32 kTempPageSize = 256*kKB;
static constexpr float  kTempValidateResetTime = 5.0f;
static constexpr uint32 kTempMaxStackframes = 8;

struct MemTempStack
{
    size_t baseOffset;
    size_t offset;
    void* lastAllocatedPtr;
    void* stacktrace[kTempMaxStackframes];
    Array<_private::MemDebugPointer> debugPointers;
    MemTempId id;
    uint16 numStackframes;
};

struct alignas(CACHE_LINE_SIZE) MemTempContext
{
    atomicUint32 isInUse;  // This is atomic because we are accessing it in the memTempReset thread
    uint8 _padding1[CACHE_LINE_SIZE - sizeof(atomicUint32)];

    Array<MemTempStack> allocStack;
    uint32 generationIdx;   // Just a counter to make temp IDs unique
    uint32 resetCount;
    size_t framePeaks[kTempFramePeaksCount];
    size_t curFramePeak;
    size_t peakBytes;
    uint8* buffer;
    size_t bufferSize;
    
    float noresetTime;
    uint32 threadId;
    char threadName[32];

    bool init;      // is Buffer initialized ?
    bool used;      // Temp allocator is used since last reset ?
    bool debugMode; // Allocate from heap

    uint8 _paddingBottom[53];

    ~MemTempContext();
};

struct MemTempData
{
    Mutex            tempMtx;
    size_t           pageSize = sysGetPageSize();
    Array<MemTempContext*> tempCtxs; 
    bool             captureTempStackTrace;

    MemTempData()  { tempMtx.Initialize(); ASSERT(kTempPageSize % pageSize == 0); }
    ~MemTempData() { tempMtx.Release(); tempCtxs.Free(); }
};

static MemTempData gMemTemp;
NO_INLINE static MemTempContext& MemGetTempContext() 
{ 
    static thread_local MemTempContext tempCtx;
    return tempCtx; 
}

void memTempSetDebugMode(bool enable)
{
    ASSERT_MSG(MemGetTempContext().allocStack.Count() == 0, "MemTemp must be at it's initial state");
    MemGetTempContext().debugMode = enable;
}

void memTempSetCaptureStackTrace(bool capture)
{
    gMemTemp.captureTempStackTrace = capture;
}

void memTempGetStats(Allocator* alloc, MemTransientAllocatorStats** outStats, uint32* outCount)
{
    ASSERT(alloc);
    ASSERT(outStats);
    ASSERT(outCount);

    MutexScope mtx(gMemTemp.tempMtx);
    if (gMemTemp.tempCtxs.Count())
        *outStats = memAllocTyped<MemTransientAllocatorStats>(gMemTemp.tempCtxs.Count(), alloc);
    *outCount = gMemTemp.tempCtxs.Count();

    for (uint32 i = 0; i < *outCount; i++) {
        (*outStats)[i].curPeak = gMemTemp.tempCtxs[i]->curFramePeak;
        (*outStats)[i].maxPeak = gMemTemp.tempCtxs[i]->peakBytes;
        (*outStats)[i].threadId = gMemTemp.tempCtxs[i]->threadId;
        (*outStats)[i].threadName = gMemTemp.tempCtxs[i]->threadName;
    }
}

MemTempId memTempPushId()
{
    MemTempContext& ctx = MemGetTempContext();

    // Note that we use an atomic var for syncing between threads and memTempReset caller thread
    // The reason is because while someone Pushed the mem temp stack. Reset might be called and mess things up
    atomicExchange32Explicit(&ctx.isInUse, 1, AtomicMemoryOrder::Release);

    ++ctx.generationIdx;
    ASSERT_MSG(ctx.generationIdx <= UINT16_MAX, "Too many push temp allocator, generation overflowed");

    if (!ctx.init) {
        if (ctx.buffer == nullptr && !ctx.debugMode) {
            ctx.buffer = (uint8*)memVirtualReserve(kTempMaxBufferSize);
            ctx.bufferSize = kTempPageSize;
            memVirtualCommit(ctx.buffer, ctx.bufferSize); 
        }
        ctx.init = true;
    }

    if (!ctx.used) {
        MutexScope mtx(gMemTemp.tempMtx);
        if (gMemTemp.tempCtxs.FindIf([ctx = &ctx](const MemTempContext* tmpCtx)->bool { return ctx == tmpCtx; }) == UINT32_MAX) {
            gMemTemp.tempCtxs.Push(&ctx);
            ctx.threadId = threadGetCurrentId();
            threadGetCurrentThreadName(ctx.threadName, sizeof(ctx.threadName));
        }

        ctx.used = true;
    }

    uint32 index = ctx.allocStack.Count();
    ASSERT_MSG(index <= UINT16_MAX, "Temp stack depth is too high! Perhaps a mistake in Push/Pop order");

    // Id: High bits is the index to the allocStack
    //     Low bits is the call generation
    MemTempId id = (index << 16) | (ctx.generationIdx & 0xffff);
    
    MemTempStack memStack { 
        .baseOffset = index > 0 ? (ctx.allocStack.Last().baseOffset + ctx.allocStack.Last().offset) : 0,
        .id = id
    };

    if constexpr(!CONFIG_FINAL_BUILD) {
        if (gMemTemp.captureTempStackTrace)
            memStack.numStackframes = debugCaptureStacktrace(memStack.stacktrace, kTempMaxStackframes, 2);
    }

    ctx.allocStack.Push(memStack);
    return id;
}

void memTempPopId(MemTempId id)
{
    MemTempContext& ctx = MemGetTempContext();

    ASSERT(id);
    ASSERT(ctx.used);
    ASSERT(ctx.generationIdx);

    [[maybe_unused]] uint32 index = id >> 16;
    ASSERT_MSG(index == ctx.allocStack.Count() - 1, "Invalid temp Push/Pop order");

    MemTempStack memStack = ctx.allocStack.PopLast();
    if (memStack.debugPointers.Count()) {
        for (_private::MemDebugPointer p : memStack.debugPointers)
            memDefaultAlloc()->Free(p.ptr, p.align);
        memStack.debugPointers.Free();
    }
    atomicExchange32Explicit(&ctx.isInUse, 0, AtomicMemoryOrder::Release);
}

void* memReallocTemp(MemTempId id, void* ptr, size_t size, uint32 align)
{
    MemTempContext& ctx = MemGetTempContext();

    ASSERT(id);
    ASSERT(ctx.used);
    ASSERT(size);

    uint32 index = id >> 16;
    ASSERT_MSG(index == ctx.allocStack.Count() - 1, "Invalid temp id, likely doesn't belong to current temp stack scope");

    MemTempStack& memStack = ctx.allocStack[index];

    if (!ctx.debugMode) {
        align = Max(align, CONFIG_MACHINE_ALIGNMENT);
        size = AlignValue<size_t>(size, align);

        // For a common case that we call realloc several times (dynamic Arrays), we can reuse the last allocated pointer
        void* newPtr = nullptr;
        size_t lastSize = 0;
        if (ptr) {
            lastSize = *((size_t*)ptr - 1);
            ASSERT(size > lastSize);

            if (memStack.lastAllocatedPtr == ptr)
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
        if (endOffset > ctx.bufferSize) {
            size_t newSize = Clamp(ctx.bufferSize << 1, endOffset, kTempMaxBufferSize);

            // Align grow size to page size for virtual memory commit
            size_t growSize = AlignValue(newSize - ctx.bufferSize, gMemTemp.pageSize);
            memVirtualCommit(ctx.buffer + ctx.bufferSize, growSize);
            ctx.bufferSize += growSize;
        }

        ctx.curFramePeak = Max<size_t>(ctx.curFramePeak, endOffset);
        ctx.peakBytes = Max<size_t>(ctx.peakBytes, endOffset);

        // Create the pointer if we are not re-using the previous one
        if (newPtr == nullptr) {
            newPtr = ctx.buffer + offset;

            // Fill the alighnment gap with zeros
            memset(ctx.buffer + memStack.offset + memStack.baseOffset, 0x0, offset - memStack.offset - memStack.baseOffset);

            // we are not re-using the previous allocation, memcpy the previous block in case of realloc
            if (ptr)
                memcpy(newPtr, ptr, lastSize);
        }

        *((size_t*)newPtr - 1) = size;
        memStack.offset = endOffset - memStack.baseOffset;
        memStack.lastAllocatedPtr = newPtr;
        return newPtr;
    }
    else {
        if (ptr == nullptr)
            ptr = memDefaultAlloc()->Malloc(size, align);
        else
            ptr = memDefaultAlloc()->Realloc(ptr, size, align);

        if (ptr) {
            memStack.offset += size;
            size_t endOffset = memStack.baseOffset + memStack.offset;

            ctx.peakBytes = Max<size_t>(ctx.peakBytes, endOffset);
            memStack.debugPointers.Push(_private::MemDebugPointer {ptr, align});
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
            for (_private::MemDebugPointer p : memStack.debugPointers)
                memDefaultAlloc()->Free(p.ptr, p.align);
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

void memTempReset(float dt, bool resetValidation)
{
    MutexScope mtx(gMemTemp.tempMtx);
    for (uint32 i = 0; i < gMemTemp.tempCtxs.Count(); i++) {
        MemTempContext* ctx = gMemTemp.tempCtxs[i];

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
                    maxPeakSize = AlignValue(maxPeakSize, gMemTemp.pageSize);
                    if (maxPeakSize > MemGetTempContext().bufferSize) {
                        size_t growSize = maxPeakSize - MemGetTempContext().bufferSize;
                        memVirtualCommit(MemGetTempContext().buffer + MemGetTempContext().bufferSize, growSize);
                    }
                    else if (maxPeakSize < MemGetTempContext().bufferSize) {
                        size_t shrinkSize = MemGetTempContext().bufferSize - maxPeakSize;
                        memVirtualDecommit(MemGetTempContext().buffer + maxPeakSize, shrinkSize);
                    }
                    MemGetTempContext().bufferSize = maxPeakSize;
                }

                ctx->used = false;
            }   // MemTempContext can reset (allocStack is empty)
            else if (resetValidation) {
                ctx->noresetTime += dt;
                if (ctx->noresetTime >= kTempValidateResetTime) {
                    logWarning("Temp stack failed to pop during the frame after %.0f seconds", kTempValidateResetTime);
                    ctx->noresetTime = 0;

                    if constexpr(!CONFIG_FINAL_BUILD) {
                        if (gMemTemp.captureTempStackTrace) {
                            DebugStacktraceEntry entries[kTempMaxStackframes];
                            uint32 index = 0;
                            logDebug("Callstacks for each remaining MemTempPush:");
                            for (const MemTempStack& memStack : ctx->allocStack) {
                                debugResolveStacktrace(memStack.numStackframes, memStack.stacktrace, entries);
                                logDebug("\t%u) Id=%u", ++index, memStack.id);
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

//    ██████╗ ██╗   ██╗███╗   ███╗██████╗      █████╗ ██╗     ██╗      ██████╗  ██████╗
//    ██╔══██╗██║   ██║████╗ ████║██╔══██╗    ██╔══██╗██║     ██║     ██╔═══██╗██╔════╝
//    ██████╔╝██║   ██║██╔████╔██║██████╔╝    ███████║██║     ██║     ██║   ██║██║     
//    ██╔══██╗██║   ██║██║╚██╔╝██║██╔═══╝     ██╔══██║██║     ██║     ██║   ██║██║     
//    ██████╔╝╚██████╔╝██║ ╚═╝ ██║██║         ██║  ██║███████╗███████╗╚██████╔╝╚██████╗
//    ╚═════╝  ╚═════╝ ╚═╝     ╚═╝╚═╝         ╚═╝  ╚═╝╚══════╝╚══════╝ ╚═════╝  ╚═════╝

void MemBumpAllocatorBase::Initialize(size_t reserveSize, size_t pageSize, bool debugMode)
{
    mDebugMode = debugMode;

    // We use RAII reserve/release here because FrameAllocator remains during the lifetime of the program
    if (!debugMode) {
        ASSERT(reserveSize);
        ASSERT(pageSize);

        mBuffer = (uint8*)BackendReserve(reserveSize);
        if (!mBuffer) 
            MEMORY_FAIL();

        mPageSize = pageSize;
        mReserveSize = reserveSize;
    }
    else {
        mDebugPointers = NEW(memDefaultAlloc(), Array<_private::MemDebugPointer>);
    }
}

void MemBumpAllocatorBase::Release()
{
    if (mBuffer) {
        if (mCommitSize)
            BackendDecommit(mBuffer, mCommitSize);
        BackendRelease(mBuffer, mReserveSize);
    }
    
    if (mDebugMode) {
        for (_private::MemDebugPointer p : *mDebugPointers)
            memDefaultAlloc()->Free(p.ptr, p.align);
        mDebugPointers->Free();
        memFree(mDebugPointers, memDefaultAlloc());
    }
}

void MemBumpAllocatorBase::CommitAll()
{
    BackendCommit(mBuffer + mCommitSize, mReserveSize - mCommitSize);
    mCommitSize = mReserveSize;
}

void* MemBumpAllocatorBase::Malloc(size_t size, uint32 align)
{
    return MemBumpAllocatorBase::Realloc(nullptr, size, align);
}

void* MemBumpAllocatorBase::Realloc(void* ptr, size_t size, uint32 align)
{
    ASSERT(size);

    if (!mDebugMode) {
        align = Max(align, CONFIG_MACHINE_ALIGNMENT);
        size = AlignValue<size_t>(size, align);

        // For a common case that we call realloc several times (dynamic Arrays), we can reuse the last allocated pointer
        void* newPtr = nullptr;
        size_t lastSize = 0;
        if (ptr && mLastAllocatedPtr == ptr) {
            lastSize = *((size_t*)ptr - 1);
            ASSERT(size > lastSize);
            newPtr = ptr;
        }

        // align to requested alignment
        size_t offset_ = mOffset;
        if (newPtr == nullptr) {
            offset_ += sizeof(size_t);
            if (offset_ % align != 0) 
                offset_ = AlignValue<size_t>(offset_, align);
        }
        else {
            ASSERT(offset_ % align == 0);
        }
    
        size_t endOffset = offset_ + (size - lastSize);

        if (endOffset > mReserveSize) {
            MEMORY_FAIL();
            return nullptr;
        }

        // Grow the buffer if necessary (double size policy)
        if (endOffset > mCommitSize) {
            size_t newSize = endOffset;

            // Align grow size to page size for virtual memory commit
            size_t growSize = AlignValue(newSize - mCommitSize, mPageSize);
            BackendCommit(mBuffer + mCommitSize, growSize);
            mCommitSize += growSize;
        }

        // Create the pointer if we are not re-using the previous one
        if (!newPtr) {
            newPtr = mBuffer + offset_;

            // we are not re-using the previous allocation, memcpy the previous block in case of realloc
            if (ptr)
                memcpy(newPtr, ptr, *((size_t*)ptr - 1));
        }

        *((size_t*)newPtr - 1) = size;
        mOffset = endOffset;
        mLastAllocatedPtr = newPtr;
        return newPtr;
    }
    else {
        if (ptr == nullptr)
            ptr = memDefaultAlloc()->Malloc(size, align);
        else
            ptr = memDefaultAlloc()->Realloc(ptr, size, align);

        if (ptr)
            mDebugPointers->Push(_private::MemDebugPointer {ptr, align});
        return ptr;
    }
}

void MemBumpAllocatorBase::Free(void*, uint32)
{
}

void MemBumpAllocatorBase::Reset()
{
    if (!mDebugMode) {
        // Invalidate already allocated memory, so we can have better debugging if something is still lingering 
        if (mOffset)
            memset(mBuffer, 0xfe, mOffset);

        mLastAllocatedPtr = nullptr;
        mOffset = 0;
        mCommitSize = 0;
    }
    else {
        mOffset = 0;

        for (_private::MemDebugPointer& dbgPtr : *mDebugPointers) 
            memDefaultAlloc()->Free(dbgPtr.ptr, dbgPtr.align);
        mDebugPointers->Clear();
    }
}

//----------------------------------------------------------------------------------------------------------------------
void* MemBumpAllocatorVM::BackendReserve(size_t size)
{
    return memVirtualReserve(size);
}

void* MemBumpAllocatorVM::BackendCommit(void* ptr, size_t size)
{
    return memVirtualCommit(ptr, size);
}

void  MemBumpAllocatorVM::BackendDecommit(void* ptr, size_t size)
{
    return memVirtualDecommit(ptr, size);
}

void  MemBumpAllocatorVM::BackendRelease(void* ptr, size_t size)
{
    return memVirtualRelease(ptr, size);
}

//----------------------------------------------------------------------------------------------------------------------
// Temp Allocator Scoped
MemTempAllocator::MemTempAllocator() : 
    mId(memTempPushId()), 
    mFiberProtectorId(debugFiberScopeProtector_Push("TempAllocator")),
    mOwnsId(true) 
{ 
}

MemTempAllocator::MemTempAllocator(MemTempId id) : 
    mId(id), 
    mFiberProtectorId(debugFiberScopeProtector_Push("TempAllocator")),
    mOwnsId(false)
{
}

MemTempAllocator::~MemTempAllocator() 
{ 
    debugFiberScopeProtector_Pop(mFiberProtectorId); 
    if (mOwnsId) 
        memTempPopId(mId); 
}

void* MemTempAllocator::Malloc(size_t size, uint32 align) 
{
    return memAllocTemp(mId, size, align);
}

void* MemTempAllocator::Realloc(void* ptr, size_t size, uint32 align) 
{
    return memReallocTemp(mId, ptr, size, align);
}

void MemTempAllocator::Free(void*, uint32) 
{
    // No Free!
}

size_t MemTempAllocator::GetOffset() const
{
    uint32 index = mId >> 16;
    ASSERT_MSG(index == MemGetTempContext().allocStack.Count() - 1, "Invalid temp id, likely doesn't belong to current temp stack scope");

    const MemTempStack& memStack = MemGetTempContext().allocStack[index];
    return memStack.baseOffset + memStack.offset;
}

size_t MemTempAllocator::GetPointerOffset(void* ptr) const
{
    return size_t((uint8*)ptr - MemGetTempContext().buffer);
}

//    ████████╗██╗     ███████╗███████╗     █████╗ ██╗     ██╗      ██████╗  ██████╗
//    ╚══██╔══╝██║     ██╔════╝██╔════╝    ██╔══██╗██║     ██║     ██╔═══██╗██╔════╝
//       ██║   ██║     ███████╗█████╗      ███████║██║     ██║     ██║   ██║██║     
//       ██║   ██║     ╚════██║██╔══╝      ██╔══██║██║     ██║     ██║   ██║██║     
//       ██║   ███████╗███████║██║         ██║  ██║███████╗███████╗╚██████╔╝╚██████╗
//       ╚═╝   ╚══════╝╚══════╝╚═╝         ╚═╝  ╚═╝╚══════╝╚══════╝ ╚═════╝  ╚═════╝
size_t MemTlsfAllocator::GetMemoryRequirement(size_t poolSize)
{
    return tlsf_size() + tlsf_align_size() + tlsf_pool_overhead() + poolSize;
}

void MemTlsfAllocator::Initialize([[maybe_unused]] size_t poolSize, void* buffer, size_t size, bool debugMode)
{
    mDebugMode = debugMode;

    if (!debugMode) {
        ASSERT(GetMemoryRequirement(poolSize) <= size);

        mTlsf = tlsf_create_with_pool(buffer, size);
        if (mTlsf == nullptr) {
            MEMORY_FAIL();
        }
        mTlsfSize = size;
    }
}

void MemTlsfAllocator::Release()
{
    if (mTlsf)
        tlsf_destroy(mTlsf);
}

void* MemTlsfAllocator::Malloc(size_t size, uint32 align)
{
    if (!mDebugMode) {
        ASSERT(mTlsf);

        void* ptr = nullptr;
        align = Max(align, CONFIG_MACHINE_ALIGNMENT);
        if (align <= CONFIG_MACHINE_ALIGNMENT) {
            ptr = tlsf_malloc(mTlsf, size);
        }
        else {
            ptr = tlsf_memalign(mTlsf, align, size);
        }

        if (ptr) {
            size_t blockSize = tlsf_block_size(ptr);
            mAllocatedSize += blockSize;

            TracyCAlloc(ptr, size);

            memTrackMalloc(ptr, size);
            return ptr;
        }
        else {
            MEMORY_FAIL();
            return nullptr;
        }
    }
    else {
        return memDefaultAlloc()->Malloc(size, align);
    }
}

void* MemTlsfAllocator::Realloc(void* ptr, size_t size, uint32 align)
{
    if (!mDebugMode) {
        ASSERT(mTlsf);
        [[maybe_unused]] void* freePtr = ptr;

        if (ptr) 
            mAllocatedSize -= tlsf_block_size(ptr);

        ptr = tlsf_realloc(mTlsf, ptr, size);
        if (ptr) {
            mAllocatedSize += tlsf_block_size(ptr);
            TracyCRealloc(freePtr, ptr, size);

            memTrackRealloc(freePtr, ptr, size);
            return ptr;
        }
        else {
            MEMORY_FAIL();
            return nullptr;
        }
    }
    else {
        return memDefaultAlloc()->Realloc(ptr, size, align);
    }
}

void MemTlsfAllocator::Free(void* ptr, uint32 align)
{
    if (!mDebugMode) {
        ASSERT(mTlsf);
        if (ptr) {
            size_t blockSize = tlsf_block_size(ptr);
            mAllocatedSize -= blockSize;
            tlsf_free(mTlsf, ptr);
            TracyCFree(ptr);
            memTrackFree(ptr);
        }
    }
    else {
        return memDefaultAlloc()->Free(ptr, align);
    }
}

bool MemTlsfAllocator::Validate()
{
    if (!mDebugMode) {
        ASSERT(mTlsf);
        return tlsf_check(mTlsf) == 0;
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

    if (!mDebugMode) {
        MemTempAllocator tmpAlloc;
        GetAllocsData data;
        data.allocs.SetAllocator(&tmpAlloc);
        data.baseOffset = PtrToInt<uint64>(mTlsf);

        tlsf_walk_pool(tlsf_get_pool(mTlsf), GetAllocs, &data);

        if (data.allocs.Count()) {
            BlitSort<AllocData>(data.allocs.Ptr(), data.allocs.Count(), 
                [](const AllocData& a, const AllocData& b) { return a.offset < b.offset ? -1 : 1; });

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

//----------------------------------------------------------------------------------------------------------------------
// MemThreadSafeAllocator
MemThreadSafeAllocator::MemThreadSafeAllocator(Allocator* alloc) : mAlloc(alloc)
{
    SpinLockMutex* lock = (SpinLockMutex*)mLock;
    memset(lock, 0x0, sizeof(SpinLockMutex));
}

void MemThreadSafeAllocator::SetAllocator(Allocator* alloc)
{
    mAlloc = alloc;
}

void* MemThreadSafeAllocator::Malloc(size_t size, uint32 align)
{
    ASSERT(mAlloc);
    SpinLockMutex* lock_ = (SpinLockMutex*)mLock;
    SpinLockMutexScope lock(*lock_);
    return mAlloc->Malloc(size, align);
}

void* MemThreadSafeAllocator::Realloc(void* ptr, size_t size, uint32 align)
{
    ASSERT(mAlloc);
    SpinLockMutex* lock_ = (SpinLockMutex*)mLock;
    SpinLockMutexScope lock(*lock_);
    return mAlloc->Realloc(ptr, size, align);
}

void MemThreadSafeAllocator::Free(void* ptr, uint32 align)
{
    ASSERT(mAlloc);
    SpinLockMutex* lock_ = (SpinLockMutex*)mLock;
    SpinLockMutexScope lock(*lock_);
    mAlloc->Free(ptr, align);
}

AllocatorType MemThreadSafeAllocator::GetType() const
{
    ASSERT(mAlloc);
    return mAlloc->GetType();
}

