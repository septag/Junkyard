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

static constexpr size_t MEM_TEMP_MAX_BUFFER_SIZE = CONFIG_TEMP_ALLOC_MAX;
static constexpr uint32 MEM_TEMP_FRAME_PEAKS_COUNT = 4;
static constexpr uint32 MEM_TEMP_PAGE_SIZE = CONFIG_TEMP_ALLOC_PAGE_SIZE;
static constexpr float  MEM_TEMP_VALIDATE_RESET_TIME = 5.0f;
static constexpr uint32 MEM_TEMP_MAX_STACK_FRAMES = 8;

struct MemTempStack
{
    size_t baseOffset;
    size_t offset;
    void* lastAllocatedPtr;
    void* stacktrace[MEM_TEMP_MAX_STACK_FRAMES];
    Array<MemDebugPointer> debugPointers;
    MemTempAllocator::ID id;
    uint16 numStackframes;
};

struct alignas(CACHE_LINE_SIZE) MemTempContext
{
    AtomicUint32 isInUse;  // This is atomic because we are accessing it in the memTempReset thread
    uint8 _padding1[CACHE_LINE_SIZE - sizeof(AtomicUint32)];

    Array<MemTempStack> allocStack;
    uint32 generationIdx;   // Just a counter to make temp IDs unique
    uint32 resetCount;
    size_t framePeaks[MEM_TEMP_FRAME_PEAKS_COUNT];
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
    size_t           pageSize = OS::GetPageSize();
    Array<MemTempContext*> tempCtxs; 
    bool             captureTempStackTrace;

    MemTempData()  { tempMtx.Initialize(); ASSERT(MEM_TEMP_PAGE_SIZE % pageSize == 0); }
    ~MemTempData() { tempMtx.Release(); tempCtxs.Free(); }
};

static MemTempData gMemTemp;

NO_INLINE static MemTempContext& MemGetTempContext() 
{ 
    static thread_local MemTempContext tempCtx;
    return tempCtx; 
}

void MemTempAllocator::EnableDebugMode(bool enable)
{
    ASSERT_MSG(MemGetTempContext().allocStack.Count() == 0, "MemTemp must be at it's initial state");
    MemGetTempContext().debugMode = enable;
}

void MemTempAllocator::EnableCallstackCapture(bool capture)
{
    gMemTemp.captureTempStackTrace = capture;
}

void MemTempAllocator::GetStats(MemAllocator* alloc, Stats** outStats, uint32* outCount)
{
    ASSERT(alloc);
    ASSERT(outStats);
    ASSERT(outCount);

    MutexScope mtx(gMemTemp.tempMtx);
    if (gMemTemp.tempCtxs.Count())
        *outStats = Mem::AllocTyped<MemTempAllocator::Stats>(gMemTemp.tempCtxs.Count(), alloc);
    *outCount = gMemTemp.tempCtxs.Count();

    for (uint32 i = 0; i < *outCount; i++) {
        (*outStats)[i].curPeak = gMemTemp.tempCtxs[i]->curFramePeak;
        (*outStats)[i].maxPeak = gMemTemp.tempCtxs[i]->peakBytes;
        (*outStats)[i].threadId = gMemTemp.tempCtxs[i]->threadId;
        (*outStats)[i].threadName = gMemTemp.tempCtxs[i]->threadName;
    }
}

MemTempAllocator::ID MemTempAllocator::PushId()
{
    MemTempContext& ctx = MemGetTempContext();

    // Note that we use an atomic var for syncing between threads and memTempReset caller thread
    // The reason is because while someone Pushed the mem temp stack. Reset might be called and mess things up
    Atomic::ExchangeExplicit(&ctx.isInUse, 1, AtomicMemoryOrder::Release);

    ++ctx.generationIdx;
    ASSERT_MSG(ctx.generationIdx <= UINT16_MAX, "Too many push temp allocator, generation overflowed");

    if (!ctx.init) {
        if (ctx.buffer == nullptr && !ctx.debugMode) {
            ctx.buffer = (uint8*)Mem::VirtualReserve(MEM_TEMP_MAX_BUFFER_SIZE);
            ctx.bufferSize = MEM_TEMP_PAGE_SIZE;
            Mem::VirtualCommit(ctx.buffer, ctx.bufferSize); 
        }
        ctx.init = true;
    }

    if (!ctx.used) {
        MutexScope mtx(gMemTemp.tempMtx);
        if (gMemTemp.tempCtxs.FindIf([ctx = &ctx](const MemTempContext* tmpCtx)->bool { return ctx == tmpCtx; }) == UINT32_MAX) {
            gMemTemp.tempCtxs.Push(&ctx);
            ctx.threadId = Thread::GetCurrentId();
            Thread::GetCurrentThreadName(ctx.threadName, sizeof(ctx.threadName));
        }

        ctx.used = true;
    }

    uint32 index = ctx.allocStack.Count();
    ASSERT_MSG(index <= UINT16_MAX, "Temp stack depth is too high! Perhaps a mistake in Push/Pop order");

    // Id: High bits is the index to the allocStack
    //     Low bits is the call generation
    ID id = (index << 16) | (ctx.generationIdx & 0xffff);
    
    MemTempStack memStack { 
        .baseOffset = index > 0 ? (ctx.allocStack.Last().baseOffset + ctx.allocStack.Last().offset) : 0,
        .id = id
    };

    if constexpr(!CONFIG_FINAL_BUILD) {
        if (gMemTemp.captureTempStackTrace)
            memStack.numStackframes = Debug::CaptureStacktrace(memStack.stacktrace, MEM_TEMP_MAX_STACK_FRAMES, 2);
    }

    ctx.allocStack.Push(memStack);
    return id;
}

void MemTempAllocator::PopId(ID id)
{
    MemTempContext& ctx = MemGetTempContext();

    ASSERT(id);
    ASSERT(ctx.used);
    ASSERT(ctx.generationIdx);

    [[maybe_unused]] uint32 index = id >> 16;
    ASSERT_MSG(index == ctx.allocStack.Count() - 1, "Invalid temp Push/Pop order");

    MemTempStack memStack = ctx.allocStack.PopLast();
    if (memStack.debugPointers.Count()) {
        for (MemDebugPointer p : memStack.debugPointers)
            Mem::GetDefaultAlloc()->Free(p.ptr, p.align);
        memStack.debugPointers.Free();
    }
    Atomic::ExchangeExplicit(&ctx.isInUse, 0, AtomicMemoryOrder::Release);
}

MemTempContext::~MemTempContext()
{
    if (buffer) {
        if (bufferSize)
            Mem::VirtualDecommit(buffer, bufferSize);
        Mem::VirtualRelease(buffer, bufferSize);
    }

    if (debugMode) {
        for (MemTempStack& memStack : allocStack) {
            for (MemDebugPointer p : memStack.debugPointers)
                Mem::GetDefaultAlloc()->Free(p.ptr, p.align);
            memStack.debugPointers.Free();
        }
    }
    allocStack.Free();

    used = false;
    init = false;
}

void MemTempAllocator::Reset(float dt, bool resetValidation)
{
    MutexScope mtx(gMemTemp.tempMtx);
    for (uint32 i = 0; i < gMemTemp.tempCtxs.Count(); i++) {
        MemTempContext* ctx = gMemTemp.tempCtxs[i];

        if (Atomic::LoadExplicit(&ctx->isInUse, AtomicMemoryOrder::Acquire)) 
            continue;

        if (ctx->used) {
            // TODO: do some kind of heuristics to detect leaks if allocStack is not empty
            if (ctx->allocStack.Count() == 0) {
                ctx->generationIdx = 0;
                ctx->framePeaks[ctx->resetCount] = ctx->curFramePeak;
                ctx->resetCount = (ctx->resetCount + 1) % MEM_TEMP_FRAME_PEAKS_COUNT;
                ctx->curFramePeak = 0;
                ctx->noresetTime = 0;

                if (!ctx->debugMode) {
                    // resize buffer to the maximum of the last 4 frames peak allocations
                    // So based on the last frames activity, we might grow or shrink the temp buffer
                    size_t maxPeakSize = 0;
                    for (uint32 k = 0; k < MEM_TEMP_FRAME_PEAKS_COUNT; k++) {
                        if (ctx->framePeaks[k] > maxPeakSize) 
                            maxPeakSize = ctx->framePeaks[k];
                    }

                    maxPeakSize = Max<size_t>(MEM_TEMP_PAGE_SIZE, maxPeakSize);
                    maxPeakSize = AlignValue(maxPeakSize, gMemTemp.pageSize);
                    if (maxPeakSize > MemGetTempContext().bufferSize) {
                        size_t growSize = maxPeakSize - MemGetTempContext().bufferSize;
                        Mem::VirtualCommit(MemGetTempContext().buffer + MemGetTempContext().bufferSize, growSize);
                    }
                    else if (maxPeakSize < MemGetTempContext().bufferSize) {
                        size_t shrinkSize = MemGetTempContext().bufferSize - maxPeakSize;
                        Mem::VirtualDecommit(MemGetTempContext().buffer + maxPeakSize, shrinkSize);
                    }
                    MemGetTempContext().bufferSize = maxPeakSize;
                }

                ctx->used = false;
            }   // MemTempContext can reset (allocStack is empty)
            else if (resetValidation) {
                ctx->noresetTime += dt;
                if (ctx->noresetTime >= MEM_TEMP_VALIDATE_RESET_TIME) {
                    LOG_WARNING("Temp stack failed to pop during the frame after %.0f seconds", MEM_TEMP_VALIDATE_RESET_TIME);
                    ctx->noresetTime = 0;

                    if constexpr(!CONFIG_FINAL_BUILD) {
                        if (gMemTemp.captureTempStackTrace) {
                            DebugStacktraceEntry entries[MEM_TEMP_MAX_STACK_FRAMES];
                            uint32 index = 0;
                            LOG_DEBUG("Callstacks for each remaining MemTempPush:");
                            for (const MemTempStack& memStack : ctx->allocStack) {
                                Debug::ResolveStacktrace(memStack.numStackframes, memStack.stacktrace, entries);
                                LOG_DEBUG("\t%u) Id=%u", ++index, memStack.id);
                                for (uint16 s = 0; s < memStack.numStackframes; s++) {
                                    LOG_DEBUG("\t\t%s(%u): %s", entries[s].filename, entries[s].line, entries[s].name);
                                }
                            }
                        }
                    } // CONFIG_FINAL_BUILD
                }
            }
        } // MemTempContext->used
    }
}

MemTempAllocator::MemTempAllocator() : 
    mId(MemTempAllocator::PushId()), 
    mFiberProtectorId(Debug::FiberScopeProtector_Push("TempAllocator")),
    mOwnsId(true) 
{ 
}

MemTempAllocator::MemTempAllocator(ID id) : 
    mId(id), 
    mFiberProtectorId(Debug::FiberScopeProtector_Push("TempAllocator")),
    mOwnsId(false)
{
}

MemTempAllocator::~MemTempAllocator() 
{ 
    Debug::FiberScopeProtector_Pop(mFiberProtectorId); 
    if (mOwnsId) 
        MemTempAllocator::PopId(mId); 
}

void* MemTempAllocator::Malloc(size_t size, uint32 align) 
{
    return Realloc(nullptr, size, align);
}

void* MemTempAllocator::MallocZero(size_t size, uint32 align) 
{
    void* ptr = Realloc(nullptr, size, align);
    if (ptr)
        memset(ptr, 0x0, size);
    return ptr;
}

void* MemTempAllocator::Realloc(void* ptr, size_t size, uint32 align) 
{
    ID id = mId;
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

        void* newPtr = nullptr;
        size_t lastSize = 0;
        size_t addOffset = size;
        if (ptr) {
            lastSize = *((size_t*)ptr - 1);
            ASSERT(size > lastSize);

            if (memStack.lastAllocatedPtr == ptr) {
                newPtr = ptr;
                addOffset -= lastSize;
            }
        }

        size_t startOffset = memStack.baseOffset + memStack.offset;
        size_t offset = startOffset;
        if (newPtr == nullptr) {
            offset += sizeof(size_t);
            if (offset % align != 0) 
                offset = AlignValue<size_t>(offset, align);
        }
        else {
            ASSERT(offset % align == 0);
        }
    
        size_t endOffset = offset + addOffset;

        if (endOffset > MEM_TEMP_MAX_BUFFER_SIZE) {
            MEM_FAIL();
            return nullptr;
        }

        if (endOffset > ctx.bufferSize) {
            size_t newSize = Clamp(ctx.bufferSize << 1, endOffset, MEM_TEMP_MAX_BUFFER_SIZE);

            size_t growSize = AlignValue(newSize - ctx.bufferSize, gMemTemp.pageSize);
            Mem::VirtualCommit(ctx.buffer + ctx.bufferSize, growSize);
            ctx.bufferSize += growSize;
        }

        ctx.curFramePeak = Max<size_t>(ctx.curFramePeak, endOffset);
        ctx.peakBytes = Max<size_t>(ctx.peakBytes, endOffset);

        if (newPtr == nullptr) {
            newPtr = ctx.buffer + offset;

            memset(ctx.buffer + startOffset, 0x0, offset - startOffset);

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
            ptr = Mem::GetDefaultAlloc()->Malloc(size, align);
        else
            ptr = Mem::GetDefaultAlloc()->Realloc(ptr, size, align);

        if (ptr) {
            memStack.offset += size;
            size_t endOffset = memStack.baseOffset + memStack.offset;

            ctx.peakBytes = Max<size_t>(ctx.peakBytes, endOffset);
            memStack.debugPointers.Push({ptr, align});
        }
        return ptr;
    }
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
            MEM_FAIL();

        mPageSize = pageSize;
        mReserveSize = reserveSize;
    }
    else {
        mDebugPointers = NEW(Mem::GetDefaultAlloc(), Array<MemDebugPointer>);
    }
}

void MemBumpAllocatorBase::Release()
{
    if (mBuffer) {
        if (mCommitSize)
            BackendDecommit(mBuffer, mCommitSize);
        BackendRelease(mBuffer, mReserveSize);
        mBuffer = nullptr;
    }
    
    if (mDebugMode) {
        for (MemDebugPointer p : *mDebugPointers)
            Mem::GetDefaultAlloc()->Free(p.ptr, p.align);
        mDebugPointers->Free();
        Mem::Free(mDebugPointers, Mem::GetDefaultAlloc());
        mDebugPointers = nullptr;
    }
}

bool MemBumpAllocatorBase::IsInitialized() const
{
    return !mDebugMode ? (mBuffer != nullptr) : (mDebugPointers != nullptr);
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
        size_t addOffset = size;
        if (ptr) {
            lastSize = *((size_t*)ptr - 1);
            ASSERT(size > lastSize);

            if (mLastAllocatedPtr == ptr) {
                newPtr = ptr;
                addOffset -= lastSize;
            }
        }

        // align to requested alignment
        size_t startOffset = mOffset;
        size_t offset = startOffset;
        if (newPtr == nullptr) {
            offset += sizeof(size_t);
            if (offset % align != 0) 
                offset = AlignValue<size_t>(offset, align);
        }
        else {
            ASSERT(offset % align == 0);
        }
    
        size_t endOffset = offset + addOffset;

        if (endOffset > mReserveSize) {
            MEM_FAIL();
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
            newPtr = mBuffer + offset;

            memset(mBuffer + startOffset, 0x0, offset - startOffset);

            // we are not re-using the previous allocation, memcpy the previous block in case of realloc
            if (ptr)
                memcpy(newPtr, ptr, lastSize);
        }

        *((size_t*)newPtr - 1) = size;
        mOffset = endOffset;
        mLastAllocatedPtr = newPtr;
        return newPtr;
    }
    else {
        if (ptr == nullptr)
            ptr = Mem::GetDefaultAlloc()->Malloc(size, align);
        else
            ptr = Mem::GetDefaultAlloc()->Realloc(ptr, size, align);

        if (ptr)
            mDebugPointers->Push({ptr, align});
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

        for (MemDebugPointer& dbgPtr : *mDebugPointers) 
            Mem::GetDefaultAlloc()->Free(dbgPtr.ptr, dbgPtr.align);
        mDebugPointers->Clear();
    }
}

//----------------------------------------------------------------------------------------------------------------------
void* MemBumpAllocatorVM::BackendReserve(size_t size)
{
    return Mem::VirtualReserve(size);
}

void* MemBumpAllocatorVM::BackendCommit(void* ptr, size_t size)
{
    return Mem::VirtualCommit(ptr, size);
}

void  MemBumpAllocatorVM::BackendDecommit(void* ptr, size_t size)
{
    return Mem::VirtualDecommit(ptr, size);
}

void  MemBumpAllocatorVM::BackendRelease(void* ptr, size_t size)
{
    return Mem::VirtualRelease(ptr, size);
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
            MEM_FAIL();
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

            Mem::TrackMalloc(ptr, size);
            return ptr;
        }
        else {
            MEM_FAIL();
            return nullptr;
        }
    }
    else {
        return Mem::GetDefaultAlloc()->Malloc(size, align);
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

            Mem::TrackRealloc(freePtr, ptr, size);
            return ptr;
        }
        else {
            MEM_FAIL();
            return nullptr;
        }
    }
    else {
        return Mem::GetDefaultAlloc()->Realloc(ptr, size, align);
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
            Mem::TrackFree(ptr);
        }
    }
    else {
        return Mem::GetDefaultAlloc()->Free(ptr, align);
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
MemThreadSafeAllocator::MemThreadSafeAllocator(MemAllocator* alloc) : mAlloc(alloc)
{
    SpinLockMutex* lock = (SpinLockMutex*)mLock;
    memset(lock, 0x0, sizeof(SpinLockMutex));
}

void MemThreadSafeAllocator::SetAllocator(MemAllocator* alloc)
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

MemAllocatorType MemThreadSafeAllocator::GetType() const
{
    ASSERT(mAlloc);
    return mAlloc->GetType();
}

