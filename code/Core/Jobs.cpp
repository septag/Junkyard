// TODO: I think I can get rid of getThreadData() calls that only happens within the scope of the thread
#include "Jobs.h"
#include "Log.h"

#define MINICORO_IMPL
#define MCO_ASSERT(c) ASSERT(c)
#define MCO_LOG(e) logError(e)
#include "External/minicoro/minicoro.h"

#include "System.h"
#include "Atomic.h"
#include "StringUtil.h"
#include "TracyHelper.h"
#include "Hash.h"
#include "Debug.h"
#include "Arrays.h"
#include "Allocators.h"

// set this to 1 to spam output with tracy zones debugging
#define JOBS_DEBUG_TRACY_ZONES 0

namespace _limits
{
    static constexpr uint32 kJobsMaxFibers = 128;
    static constexpr uint32 kJobsMaxInstances = 512;
    static constexpr uint32 kJobsMaxPending = 4096;

#ifdef TRACY_ENABLE
    static constexpr uint32 kJobsMaxTracyStackDepth = 8;
#endif
}

#define JOBS_USE_ANDERSON_LOCK  // Experimental
#ifdef JOBS_USE_ANDERSON_LOCK
    using JobsLock = AtomicALock;
    using JobsLockScope = AtomicALockScope;
#else
    using JobsLock = AtomicLock;
    using JobsLockScope = AtomicLockScope;
#endif

struct JobsFiber;

struct JobsFiberProperties
{
    JobsCallback callback;
    void* userData;
    JobsInstance* instance;
    JobsPriority prio;
    JobsFiber* fiber;
    JobsFiberProperties* next;
    JobsFiberProperties* prev;
    uint32 index;
    uint32 stackSize;
};

struct JobsSignalInternal
{
    atomicUint32 signaled;
    uint8 reserved[CACHE_LINE_SIZE-4];
    atomicUint32 value;
};
static_assert(sizeof(JobsSignalInternal) <= sizeof(JobsSignal), "Mismatch sizes between JobsSignal and JobsSignalInternal");

#ifdef TRACY_ENABLE
struct JobsTracyZone
{
    TracyCZoneCtx ctx;
    const ___tracy_source_location_data* sourceLoc;
};
#endif

#if defined(TRACY_ENABLE) && JOBS_DEBUG_TRACY_ZONES
#define logDebugTracy(_text, ...) _private::logPrintDebug(0, __FILE__, __LINE__, _text, ##__VA_ARGS__)
#else
#define logDebugTracy(_text, ...)
#endif

struct JobsFiber
{
    uint32 ownerTid;
    mco_coro* co;
    mco_desc coDesc;
    atomicUint32* childCounter;
    JobsFiberProperties* props;
    JobsSignalInternal* signal;
    #ifdef TRACY_ENABLE
    StaticArray<JobsTracyZone, _limits::kJobsMaxTracyStackDepth> tracyZonesStack;
    #endif
};

struct JobsThreadData
{
    JobsFiber* curFiber;
    JobsInstance* waitInstance;
    JobsType type;
    uint32 threadIndex;
    uint32 threadId;
};

struct alignas(CACHE_LINE_SIZE) JobsInstance
{
    atomicUint32 counter;                       // Atomic counter of sub items within a job 
    uint8 _padding1[CACHE_LINE_SIZE - 4];        // padding for the atomic var to fit inside a cache line
    JobsType type;
    bool isAutoDelete;
    uint8 _padding2[CACHE_LINE_SIZE - sizeof(JobsType) - sizeof(bool)];
};

struct JobsWaitingList
{
    JobsFiberProperties* waitingList[static_cast<uint32>(JobsPriority::_Count)];
    JobsFiberProperties* waitingListLast[static_cast<uint32>(JobsPriority::_Count)];
};

template <typename _T, uint32 _MaxCount> 
struct alignas(CACHE_LINE_SIZE) JobsAtomicPool
{
    static JobsAtomicPool<_T, _MaxCount>* Create(Allocator* alloc);
    static void Destroy(JobsAtomicPool<_T, _MaxCount>* p, Allocator* alloc);

    _T* New();
    void Delete(_T* props);

    atomicUint32 mIndex;
    uint8 _reserved1[CACHE_LINE_SIZE - sizeof(atomicUint32)];
    _T** mPtrs;
    _T* mProps;
    uint8 _reserved2[CACHE_LINE_SIZE - sizeof(void*) * 2];
};

struct JobsContext
{
    JobsInitParams initParams;
    Thread* threads[uint32(JobsType::_Count)];
    uint32 numThreads[uint32(JobsType::_Count)];
    uint8 _padding1[8];

    MemTlsfAllocator tlsfAlloc;
    uint8 _padding2[alignof(MemThreadSafeAllocator) - sizeof(MemTlsfAllocator)];
    MemThreadSafeAllocator runtimeAlloc;
    JobsWaitingList waitingLists[uint32(JobsType::_Count)];
    uint8 _padding3[sizeof(JobsWaitingList) * 2 - alignof(JobsLock)];
    JobsLock waitingListLock;
    Semaphore semaphores[uint32(JobsType::_Count)];
    JobsAtomicPool<JobsInstance, _limits::kJobsMaxInstances>* instancePool;
    JobsAtomicPool<JobsFiberProperties, _limits::kJobsMaxPending>* fiberPropsPool;
    StaticArray<Pair<void*, uint32>, 7> pointers;    // Storing memory pointers, For garbage collection at release

    // Stats
    size_t runtimeHeapTotal;
    size_t initHeapStart;
    size_t initHeapSize;

    atomicUint32 numBusyShortThreads;
    atomicUint32 numBusyLongThreads;
    atomicUint32 numFibers;
    atomicUint32 numInstances;

    struct MaxValues
    {
        uint32 numBusyShortThreadsMax;
        uint32 numBusyLongThreadsMax;
        uint32 numFibersMax;
        uint32 numInstancesMax;
        uint32 maxFiberHeap;
    };

    uint32 fiberHeapAllocSize;
    MaxValues maxValues[2];     // Index: 0 = Write, 1 = Present

    bool quit;
};

static JobsContext gJobs;

static thread_local JobsThreadData* gJobsThreadData = nullptr;      // Only worker threads initialize this

// Use no_inline function to return our TL var. To avoid compiler confusion with thread-locals when switching fibers
NO_INLINE static JobsThreadData* jobsGetThreadData() { return gJobsThreadData; }

static void* jobsMcoMallocFn(size_t size, void* allocData)
{
    Allocator* alloc = reinterpret_cast<Allocator*>(allocData);
    return memAllocAligned(size, 16, alloc);
}
    
static void jobsMcoFreeFn(void* ptr, void* allocData)
{
    Allocator* alloc = reinterpret_cast<Allocator*>(allocData);
    memFreeAligned(ptr, 16, alloc);
}

static void jobsJumpIn(mco_coro* co)
{
    ASSERT(co->state != MCO_RUNNING);
    ASSERT(co->state != MCO_DEAD);
    co->state = MCO_RUNNING;

    #ifdef _MCO_USE_ASAN
        __sanitizer_start_switch_fiber(&co->asan_prev_stack, co->stack_base, co->stack_size);
    #endif

    #ifdef _MCO_USE_TSAN
        co->tsan_prev_fiber = __tsan_get_current_fiber();
        __tsan_switch_to_fiber(co->tsan_fiber, 0);
    #endif
    
    _mco_context* context = (_mco_context*)co->context;
    _mco_switch(&context->back_ctx, &context->ctx);
}

static void jobsJumpOut(mco_coro* co)
{
    ASSERT(co);
    ASSERT(co->state != MCO_SUSPENDED);
    ASSERT(co->state != MCO_DEAD);
    co->state = MCO_SUSPENDED;

    #ifdef _MCO_USE_ASAN
        void* bottom_old = nullptr;
        size_t size_old = 0;
        __sanitizer_finish_switch_fiber(co->asan_prev_stack, (const void**)&bottom_old, &size_old);
    #endif

    #ifdef _MCO_USE_TSAN
        void* tsan_prev_fiber = co->tsan_prev_fiber;
        co->tsan_prev_fiber = nullptr;
        __tsan_switch_to_fiber(tsan_prev_fiber, 0);
    #endif

    debugFiberScopeProtector_Check();

    _mco_context* context = (_mco_context*)co->context;
    _mco_switch(&context->ctx, &context->back_ctx);
}

INLINE void jobsAddToList(JobsWaitingList* list, JobsFiberProperties* props)
{
    uint32 index = static_cast<uint32>(props->prio);
    JobsFiberProperties** pfirst = &list->waitingList[index];
    JobsFiberProperties** plast = &list->waitingListLast[index];

    // Add to the end of the list
    if (*plast) {
        (*plast)->next = props;
        props->prev = *plast;
    }
    *plast = props;
    if (*pfirst == NULL)
        *pfirst = props;
}

INLINE void jobsRemoveFromList(JobsWaitingList* list, JobsFiberProperties* props)
{
    uint32 index = static_cast<uint32>(props->prio);
    JobsFiberProperties** pfirst = &list->waitingList[index];
    JobsFiberProperties** plast = &list->waitingListLast[index];

    if (props->prev)
        props->prev->next = props->next;
    if (props->next)
        props->next->prev = props->prev;
    if (*pfirst == props)
        *pfirst = props->next;
    if (*plast == props)
        *plast = props->prev;
    props->prev = props->next = nullptr;
}

NO_INLINE static void jobsEntryFn(mco_coro* co)
{
    ASSERT(co);
    JobsFiber* fiber = reinterpret_cast<JobsFiber*>(co->storage);
    if (fiber) {
        JobsFiberProperties* props = fiber->props;
        ASSERT(props->callback);
        props->callback(props->index, props->userData);
    }
}

NO_INLINE static JobsFiber* jobsCreateFiber(JobsFiberProperties* props)
{
    mco_desc desc = mco_desc_init(jobsEntryFn, props->stackSize);
    desc.malloc_cb = jobsMcoMallocFn;
    desc.free_cb = jobsMcoFreeFn;
    desc.allocator_data = &gJobs.runtimeAlloc;

    mco_coro* co;
    mco_result r = mco_create(&co, &desc);
    if (r != MCO_SUCCESS) {
        MEMORY_FAIL();
        return nullptr;
    }

    JobsFiber fiber {
        .ownerTid = 0,
        .co = co,
        .coDesc = desc,
        .props = props
    };

    mco_push(co, &fiber, sizeof(fiber));

    atomicFetchAdd32Explicit(&gJobs.numFibers, 1, AtomicMemoryOrder::Relaxed);
    gJobs.maxValues[0].numFibersMax = Max(gJobs.numFibers, gJobs.maxValues[0].numFibersMax);
    gJobs.maxValues[0].maxFiberHeap = Max<uint32>(uint32(gJobs.tlsfAlloc.GetAllocatedSize()), gJobs.maxValues[0].maxFiberHeap);

    return reinterpret_cast<JobsFiber*>(co->storage);
}

NO_INLINE static void jobsDestroyFiber(JobsFiber* fiber)
{
    ASSERT(fiber->props);
    ASSERT(fiber->props->next == nullptr && fiber->props->prev == nullptr);

    gJobs.fiberPropsPool->Delete(fiber->props);

    ASSERT(fiber->co);
    mco_destroy(fiber->co);

    atomicFetchSub32Explicit(&gJobs.numFibers, 1, AtomicMemoryOrder::Relaxed);
}

#if TRACY_ENABLE
static void jobsTracyEnterZone(TracyCZoneCtx* ctx, const ___tracy_source_location_data* sourceLoc)
{
    ASSERT(ctx);
    JobsThreadData* tdata = jobsGetThreadData();
    if (tdata) {
        ASSERT(tdata->curFiber);
        logDebugTracy("Enter: fiber=%p, ctx=%u", tdata->curFiber, ctx->id);

        ASSERT_MSG(!tdata->curFiber->tracyZonesStack.IsFull(), "Profile sampling stack is too deep. Either remove samples or increase the kJobsMaxTracyStackDepth");
        tdata->curFiber->tracyZonesStack.Add(JobsTracyZone {*ctx, sourceLoc});
    }
}

static bool jobsTracyExitZone(TracyCZoneCtx* ctx)
{
    JobsThreadData* tdata = jobsGetThreadData();
    if (tdata) {
        ASSERT(tdata->curFiber);
        JobsFiber* fiber = tdata->curFiber;
        if (fiber->tracyZonesStack.Count()) {
            if (fiber->tracyZonesStack.Last().ctx.id != ctx->id) {
                logDebugTracy("Exit: fiber=%p, ctx=%u", fiber, fiber->tracyZonesStack.Last().ctx.id);
                TracyCZoneEnd(fiber->tracyZonesStack.Last().ctx);
                fiber->tracyZonesStack.RemoveLast();
                return true;
            }
            else {
                logDebugTracy("Exit(*): fiber=%p, ctx=%u", fiber, ctx->id);
                // We have pop one item from the stack anyways, since the Zone has been ended by scope destructor
                fiber->tracyZonesStack.RemoveLast();
            }            
        }        
    }
    return false;
}
#endif // TRACY_ENABLE

static void jobsSetFiberToCurrentThread(JobsFiber* fiber)
{
    ASSERT(fiber);
    ASSERT(jobsGetThreadData());
    ASSERT(jobsGetThreadData()->curFiber == nullptr);

    JobsType type = jobsGetThreadData()->type;
    fiber->ownerTid = 0;
    jobsGetThreadData()->curFiber = fiber;
    ASSERT(fiber->props->next == nullptr);

    if (type == JobsType::ShortTask) {
        atomicFetchAdd32Explicit(&gJobs.numBusyShortThreads, 1, AtomicMemoryOrder::Relaxed);
        gJobs.maxValues[0].numBusyShortThreadsMax = Max(gJobs.maxValues[0].numBusyShortThreadsMax, gJobs.numBusyShortThreads);
    }
    else if (type == JobsType::LongTask) {
        atomicFetchAdd32Explicit(&gJobs.numBusyLongThreads, 1, AtomicMemoryOrder::Relaxed);
        gJobs.maxValues[0].numBusyLongThreadsMax = Max(gJobs.maxValues[0].numBusyLongThreadsMax, gJobs.numBusyLongThreads);
    }

    #ifdef TRACY_ENABLE
    if (!fiber->tracyZonesStack.IsEmpty()) {
        // Refresh all the zones in stack order
        for (JobsTracyZone& zone : fiber->tracyZonesStack)
            zone.ctx = ___tracy_emit_zone_begin_callstack(zone.sourceLoc, TRACY_CALLSTACK, zone.ctx.active);
        logDebugTracy("JumpIn: fiber=%p, ctx=%u", fiber, fiber->tracyZonesStack.Last().ctx.id);
    }
    #endif

    jobsJumpIn(fiber->co);
    
    #ifdef TRACY_ENABLE
    if (!fiber->tracyZonesStack.IsEmpty() && fiber->co->state != MCO_DEAD) {
        logDebugTracy("JumpOut: fiber=%p, ctx=%u", fiber, fiber->tracyZonesStack.Last().ctx.id);
        for (uint32 i = fiber->tracyZonesStack.Count(); i-- > 0;) 
            TracyCZoneEnd(fiber->tracyZonesStack[i].ctx);
    }
    #endif

    jobsGetThreadData()->curFiber = nullptr;
    if (type == JobsType::ShortTask)
        atomicFetchSub32Explicit(&gJobs.numBusyShortThreads, 1, AtomicMemoryOrder::Relaxed);
    else if (type == JobsType::LongTask)
        atomicFetchSub32Explicit(&gJobs.numBusyLongThreads, 1, AtomicMemoryOrder::Relaxed);
    
    JobsInstance* inst = fiber->props->instance;
    if (fiber->co->state == MCO_DEAD) {
        #ifdef TRACY_ENABLE
        ASSERT_MSG(fiber->tracyZonesStack.IsEmpty(), "Tracy zones stack currently have %u remaining items", fiber->tracyZonesStack.Count());
        #endif

        if (atomicFetchSub32(&inst->counter, 1) == 1) {     // Job is finished with all the fibers
            // Delete the job instance automatically if only indicated by the API
            if (inst->isAutoDelete) {
                gJobs.instancePool->Delete(inst);
                atomicFetchSub32Explicit(&gJobs.numInstances, 1, AtomicMemoryOrder::Relaxed);
            }
        }

        jobsDestroyFiber(fiber);
    }
    else {
        // Yielding, Coming back from WaitForCompletion
        ASSERT(fiber->co->state == MCO_SUSPENDED);
        fiber->childCounter = &jobsGetThreadData()->waitInstance->counter;
        jobsGetThreadData()->waitInstance = nullptr;
        uint32 typeIndex = uint32(inst->type);

        {
            JobsLockScope lk(gJobs.waitingListLock);
            jobsAddToList(&gJobs.waitingLists[typeIndex], fiber->props);
        }

        gJobs.semaphores[typeIndex].Post();
    }
}

static int jobsThreadFn(void* userData)
{
    // Allocate and initialize thread-data for worker threads
    if (!jobsGetThreadData()) {
        uint64 param = PtrToInt<uint64>(userData);
        gJobsThreadData = memAllocZeroTyped<JobsThreadData>(1);

        jobsGetThreadData()->threadIndex = (param >> 32) & 0xffffffff;
        jobsGetThreadData()->type = static_cast<JobsType>(uint32(param & 0xffffffff));
        jobsGetThreadData()->threadId = threadGetCurrentId();
    }

    uint32 spinCount = !PLATFORM_MOBILE;
    uint32 typeIndex = uint32(jobsGetThreadData()->type);
    while (!gJobs.quit) {
        gJobs.semaphores[typeIndex].Wait();

        bool waitingListIsLive = false;
        JobsFiber* fiber = nullptr;

        // Select and fetch the job from the waiting list
        {
            JobsLockScope lock(gJobs.waitingListLock);
            for (uint32 prioIdx = 0; prioIdx < static_cast<uint32>(JobsPriority::_Count); prioIdx++) {
                JobsWaitingList* list = &gJobs.waitingLists[typeIndex];
                JobsFiberProperties* props = list->waitingList[prioIdx];
    
                while (props) {
                    waitingListIsLive = true;

                    // Choose the fiber to continue based on these 3 conditions:
                    //  1) There is no fiber assigned to props. so it's the first run
                    //  2) Fiber is not waiting on any children jobs
                    //  3) Fiber is not waiting on a signal (if so, check if the signal is fired and reset it)
                    JobsFiber* tmpFiber = props->fiber;
                    atomicUint32 one = 1;
                    if (tmpFiber == nullptr || 
                        ((tmpFiber->childCounter == nullptr || atomicLoad32Explicit(tmpFiber->childCounter, AtomicMemoryOrder::Acquire) == 0) &&
                        (tmpFiber->signal == nullptr || atomicCompareExchange32Strong(&tmpFiber->signal->signaled, &one, 0))))
                    {
                        if (tmpFiber == nullptr)
                            props->fiber = jobsCreateFiber(props);

                        fiber = props->fiber;
                        fiber->childCounter = nullptr;
                        jobsRemoveFromList(list, props);
                        prioIdx = static_cast<uint32>(JobsPriority::_Count);    // break out of outer loop
                        break;
                    }
    
    
                    props = props->next;
                }
            }
        }

        if (fiber) {
            jobsSetFiberToCurrentThread(fiber);
        }
        else if (waitingListIsLive) {
            // Try picking another fiber cuz there are still workers in the waiting list but we couldn't pick them up
            // TODO: we probably need to modify something here to not spin the threads with waiting signals
            gJobs.semaphores[typeIndex].Post();

            if (spinCount & 1023) 
                atomicPauseCpu();
            else
                threadYield();
        }
    }

    memFree(jobsGetThreadData());
    gJobsThreadData = nullptr;
    return 0;
}

static JobsInstance* jobsDispatchInternal(bool isAutoDelete, JobsType type, JobsCallback callback, void* userData, 
                                          uint32 groupSize, JobsPriority prio, uint32 stackSize)
{
    ASSERT(groupSize);

    // Divide the job into sub-jobs with ranges
    uint32 numFibers = groupSize;
    ASSERT(numFibers);

    JobsInstance* instance = gJobs.instancePool->New();

    memset(instance, 0x0, sizeof(*instance));
    atomicFetchAdd32Explicit(&gJobs.numInstances, 1, AtomicMemoryOrder::Relaxed);
    gJobs.maxValues[0].numInstancesMax = Max(gJobs.maxValues[0].numInstancesMax, gJobs.numInstances);

    atomicExchange32Explicit(&instance->counter, numFibers, AtomicMemoryOrder::Release);
    instance->type = type;
    instance->isAutoDelete = isAutoDelete;

    // Another fiber is running on this worker thread
    // Set the running fiber as a parent to the new ones, unless we are using AutoDelete fibers, which don't have any dependencies
    JobsFiber* parent = nullptr;
    if (jobsGetThreadData() && jobsGetThreadData()->curFiber && !isAutoDelete) {
        parent = jobsGetThreadData()->curFiber;
    }

    if (stackSize == 0)
        stackSize = type == JobsType::ShortTask ? 256*kKB : 512*kKB;

    // Push workers to the end of the list, will be collected by fiber threads
    {
        JobsLockScope lock(gJobs.waitingListLock);
        for (uint32 i = 0; i < numFibers; i++) {
            JobsFiberProperties* props = gJobs.fiberPropsPool->New();
            *props = JobsFiberProperties {
                .callback = callback,
                .userData = userData,
                .instance = instance,
                .prio = prio,
                .index = i,
                .stackSize = stackSize
            };
    
            jobsAddToList(&gJobs.waitingLists[uint32(type)], props);
        }
    }

    // Fire up the worker threads
    gJobs.semaphores[uint32(type)].Post(numFibers);
    return instance;
}

void jobsWaitForCompletion(JobsHandle instance)
{
    // PROFILE_ZONE(jobsGetThreadData() == nullptr);
    ASSERT(!instance->isAutoDelete);

    uint32 spinCount = !PLATFORM_MOBILE;    // On mobile hardware, we start from yielding then proceed with Pause
    while (atomicLoad32Explicit(&instance->counter, AtomicMemoryOrder::Acquire)) {
        // If current thread has a fiber assigned and running, put it in waiting list and jump out of it 
        // so one of the threads can continue picking up more workers
        if (jobsGetThreadData()) {
            ASSERT_MSG(jobsGetThreadData()->curFiber, "Worker threads should always have a fiber assigned when 'Wait' is called");

            JobsFiber* curFiber = jobsGetThreadData()->curFiber;
            curFiber->ownerTid = jobsGetThreadData()->threadId;    // save ownerTid as a hint so we can pick this up again on the same thread context
            jobsGetThreadData()->waitInstance = instance;

            jobsJumpOut(curFiber->co);  // Back to `jobsThreadFn::jobsSetFiberToCurrentThread`
        }
        else {
            if (spinCount++ & 1023)
                atomicPauseCpu();   // Main thread just loops 
            else
                threadYield();
        }
    }

    gJobs.instancePool->Delete(instance);
    atomicFetchSub32Explicit(&gJobs.numInstances, 1, AtomicMemoryOrder::Relaxed);
}

bool jobsIsRunning(JobsHandle handle)
{
    ASSERT(!handle->isAutoDelete);    // Can't query for AutoDelete jobs
    return atomicLoad32Explicit(&handle->counter, AtomicMemoryOrder::Acquire);
}

JobsHandle jobsDispatch(JobsType type, JobsCallback callback, void* userData, uint32 groupSize, JobsPriority prio, uint32 stackSize)
{
    return jobsDispatchInternal(false, type, callback, userData, groupSize, prio, stackSize);
}

void jobsDispatchAuto(JobsType type, JobsCallback callback, void* userData, uint32 groupSize, JobsPriority prio, uint32 stackSize)
{
    jobsDispatchInternal(true, type, callback, userData, groupSize, prio, stackSize);
}

void jobsInitialize(const JobsInitParams& initParams)
{
    ASSERT(initParams.alloc);

    gJobs.initParams = initParams;
    if (initParams.alloc->GetType() == AllocatorType::Bump)
        gJobs.initHeapStart = ((MemBumpAllocatorBase*)initParams.alloc)->GetOffset();
    
    SysInfo info {};
    sysGetSysInfo(&info);
    uint32 numCores = info.coreCount;
    ASSERT(numCores);

    gJobs.numThreads[uint32(JobsType::ShortTask)] = initParams.numShortTaskThreads == 0 ? Max<uint32>(1, numCores - 1) : initParams.numShortTaskThreads;
    gJobs.numThreads[uint32(JobsType::LongTask)] =  initParams.numLongTaskThreads == 0 ? Max<uint32>(1, numCores - 1) : initParams.numLongTaskThreads;

    // TODO: On android platforms, we have different core types, performance and efficiency
    //       We can't exactly figure that out yet, but current we are following the qualcomm pattern
    //       So an 8-core cpu for example, on most modern qualcomm chips are as follows:
    //          1 - primary core
    //          3 - performance cores
    //          4 - efficiency cores
    if constexpr (PLATFORM_ANDROID)
        debugStacktraceSaveStopPoint((void*)jobsEntryFn);   // workaround for stacktrace crash bug. see `debugStacktraceSaveStopPoint`

    #ifdef JOBS_USE_ANDERSON_LOCK
    uint32 numTotalThreads = gJobs.numThreads[0] + gJobs.numThreads[1] + 1;
    atomicALockInitialize(&gJobs.waitingListLock, numTotalThreads, memAllocAlignedTyped<AtomicALockThread>(numTotalThreads, alignof(AtomicALockThread), initParams.alloc));
    if (initParams.alloc->GetType() != AllocatorType::Bump)
        gJobs.pointers.Add(Pair<void*, uint32>(gJobs.waitingListLock.slots, alignof(AtomicALockThread)));
    #endif

    gJobs.semaphores[uint32(JobsType::ShortTask)].Initialize();
    gJobs.semaphores[uint32(JobsType::LongTask)].Initialize();
    
    // Fibers stack memory pool. 
    // Reserve 1MB for each fiber stack. Be careful with maxFibers value, because the memory pool can get huge if high number is used
    {
        uint32 maxFibers = initParams.maxFibers ? initParams.maxFibers : _limits::kJobsMaxFibers;
        size_t allocSize = 2*maxFibers*kMB;
        size_t poolSize = MemTlsfAllocator::GetMemoryRequirement(allocSize);
        void* buffer = memAlloc(poolSize, initParams.alloc);
        if (initParams.alloc->GetType() != AllocatorType::Bump)
            gJobs.pointers.Add(Pair<void*, uint32>(buffer, 0));

        gJobs.tlsfAlloc.Initialize(allocSize, buffer, poolSize, initParams.debugAllocations);
        gJobs.runtimeAlloc.SetAllocator(&gJobs.tlsfAlloc);
        gJobs.runtimeHeapTotal = allocSize;
    }

    gJobs.instancePool = JobsAtomicPool<JobsInstance, _limits::kJobsMaxInstances>::Create(initParams.alloc);
    gJobs.fiberPropsPool = JobsAtomicPool<JobsFiberProperties, _limits::kJobsMaxPending>::Create(initParams.alloc);

    // Initialize and start the threads
    // LongTasks
    gJobs.threads[uint32(JobsType::LongTask)] = NEW_ARRAY(initParams.alloc, Thread, gJobs.numThreads[uint32(JobsType::LongTask)]);
    if (initParams.alloc->GetType() != AllocatorType::Bump)
        gJobs.pointers.Add(Pair<void*, uint32>(gJobs.threads[uint32(JobsType::LongTask)], 0));

    for (uint32 i = 0; i < gJobs.numThreads[uint32(JobsType::LongTask)]; i++) {
        char name[32];
        strPrintFmt(name, sizeof(name), "LongTask_%u", i+1);
        gJobs.threads[uint32(JobsType::LongTask)][i].Start(ThreadDesc {
            .entryFn = jobsThreadFn, 
            .userData = IntToPtr<uint64>((static_cast<uint64>(i+1) << 32) | uint32(JobsType::LongTask)), 
            .name = name, 
            .stackSize = 64*kKB,
            .flags = ThreadCreateFlags::None
        });
        ASSERT(gJobs.threads[uint32(JobsType::LongTask)][i].IsRunning());
        gJobs.threads[uint32(JobsType::LongTask)][i].SetPriority(ThreadPriority::Normal);
    }
    
    // ShortTasks
    gJobs.threads[uint32(JobsType::ShortTask)] = NEW_ARRAY(initParams.alloc, Thread, gJobs.numThreads[uint32(JobsType::ShortTask)]);
    if (initParams.alloc->GetType() != AllocatorType::Bump)
        gJobs.pointers.Add(Pair<void*, uint32>(gJobs.threads[uint32(JobsType::ShortTask)], 0));

    for (uint32 i = 0; i < gJobs.numThreads[uint32(JobsType::ShortTask)]; i++) {
        char name[32];
        strPrintFmt(name, sizeof(name), "ShortTask_%u", i+1);
        gJobs.threads[uint32(JobsType::ShortTask)][i].Start(ThreadDesc {
            .entryFn = jobsThreadFn, 
            .userData = IntToPtr<uint64>((static_cast<uint64>(i+1) << 32) | uint32(JobsType::ShortTask)), 
            .name = name, 
            .stackSize = 64*kKB,
            .flags = ThreadCreateFlags::None
        });
        ASSERT(gJobs.threads[uint32(JobsType::ShortTask)][i].IsRunning());
        gJobs.threads[uint32(JobsType::ShortTask)][i].SetPriority(ThreadPriority::High);
    }

    debugFiberScopeProtector_RegisterCallback([](void*)->bool { return jobsGetThreadData() && jobsGetThreadData()->curFiber != nullptr; });

    if (initParams.alloc->GetType() == AllocatorType::Bump)
        gJobs.initHeapSize = ((MemBumpAllocatorBase*)initParams.alloc)->GetOffset() - gJobs.initHeapStart;

    #if TRACY_ENABLE
    tracySetZoneCallbacks(jobsTracyEnterZone, jobsTracyExitZone);
    #endif

    logInfo("(init) Job dispatcher: %u short task threads, %u long task threads", 
            gJobs.numThreads[uint32(JobsType::ShortTask)],
            gJobs.numThreads[uint32(JobsType::LongTask)]);
}

void jobsRelease()
{
    gJobs.quit = true;

    gJobs.semaphores[uint32(JobsType::ShortTask)].Post(gJobs.numThreads[uint32(JobsType::ShortTask)]);
    gJobs.semaphores[uint32(JobsType::LongTask)].Post(gJobs.numThreads[uint32(JobsType::LongTask)]);

    if (gJobs.threads[uint32(JobsType::ShortTask)]) {
        for (uint32 i = 0; i < gJobs.numThreads[uint32(JobsType::ShortTask)]; i++)
            gJobs.threads[uint32(JobsType::ShortTask)][i].Stop();
    }

    if (gJobs.threads[uint32(JobsType::LongTask)]) {
        for (uint32 i = 0; i < gJobs.numThreads[uint32(JobsType::LongTask)]; i++)
            gJobs.threads[uint32(JobsType::LongTask)][i].Stop();
    }

    if (gJobs.instancePool)
        JobsAtomicPool<JobsInstance, _limits::kJobsMaxInstances>::Destroy(gJobs.instancePool, gJobs.initParams.alloc);
    if (gJobs.fiberPropsPool)
        JobsAtomicPool<JobsFiberProperties, _limits::kJobsMaxPending>::Destroy(gJobs.fiberPropsPool, gJobs.initParams.alloc);

    memFree(jobsGetThreadData());
    gJobsThreadData = nullptr;

    gJobs.semaphores[uint32(JobsType::ShortTask)].Release();
    gJobs.semaphores[uint32(JobsType::LongTask)].Release();

    for (Pair<void*, uint32> p : gJobs.pointers)
        memFreeAligned(p.first, p.second, gJobs.initParams.alloc);
}

void jobsGetBudgetStats(JobsBudgetStats* stats)
{
    JobsContext::MaxValues m = gJobs.maxValues[1];

    stats->maxShortTaskThreads = gJobs.numThreads[uint32(JobsType::ShortTask)];
    stats->maxLongTaskThreads = gJobs.numThreads[uint32(JobsType::LongTask)];
    stats->numBusyShortThreads = m.numBusyShortThreadsMax;
    stats->numBusyLongThreads = m.numBusyLongThreadsMax;

    stats->maxFibers = gJobs.initParams.maxFibers ? gJobs.initParams.maxFibers : _limits::kJobsMaxFibers;
    stats->numFibers = m.numFibersMax;

    stats->maxJobs = _limits::kJobsMaxInstances;
    stats->numJobs = m.numInstancesMax;

    stats->fiberHeapSize = m.maxFiberHeap;
    stats->fiberHeapMax = gJobs.runtimeHeapTotal;

    stats->initHeapStart = gJobs.initHeapStart;
    stats->initHeapSize = gJobs.initHeapSize;
}

void jobsResetBudgetStats()
{
    JobsContext::MaxValues* m = &gJobs.maxValues[0];
    gJobs.maxValues[1] = *m;
    memset(m, 0x0, sizeof(*m));    
}

uint32 jobsGetWorkerThreadsCount(JobsType type)
{
    ASSERT(type != JobsType::_Count);
    return gJobs.numThreads[uint32(type)];
}

JobsSignal::JobsSignal()
{
    JobsSignalInternal* self = reinterpret_cast<JobsSignalInternal*>(data);
    self->signaled = 0;
    self->value = 0;
}

void JobsSignal::Raise()
{
    JobsSignalInternal* self = reinterpret_cast<JobsSignalInternal*>(data);
    atomicExchange32Explicit(&self->signaled, 1, AtomicMemoryOrder::Release);
}

void JobsSignal::Wait()
{
    WaitOnCondition([](int value, int reference)->bool { return value == reference; }, 0);
}

void JobsSignal::WaitOnCondition(bool(*condFn)(int value, int reference), int reference)
{
    JobsSignalInternal* self = reinterpret_cast<JobsSignalInternal*>(data);

    uint32 spinCount = !PLATFORM_MOBILE;
    while (condFn(atomicLoad32Explicit(&self->value, AtomicMemoryOrder::Acquire), reference)) {
        if (jobsGetThreadData()) {
            ASSERT_MSG(jobsGetThreadData()->curFiber, "'Wait' should only be called during running job tasks");

            JobsFiber* curFiber = jobsGetThreadData()->curFiber;
            curFiber->ownerTid = jobsGetThreadData()->threadId;    // save ownerTid as a hint so we can pick this up again on the same thread context
            curFiber->signal = self;

            jobsJumpOut(curFiber->co);  // Back to `jobsThreadFn::jobsSetFiberToCurrentThread`

            curFiber->signal = nullptr;
        }
        else {
            if (spinCount++ & 1023)
                atomicPauseCpu();   // Main thread just loops 
            else
                threadYield();
        }
    }
}

void JobsSignal::Set(int value)
{
    JobsSignalInternal* self = reinterpret_cast<JobsSignalInternal*>(data);
    atomicExchange32Explicit(&self->value, uint32(value), AtomicMemoryOrder::Release);
}

void JobsSignal::Decrement()
{
    JobsSignalInternal* self = reinterpret_cast<JobsSignalInternal*>(data);
    atomicFetchAdd32Explicit(&self->value, 1, AtomicMemoryOrder::Release);
}

void JobsSignal::Increment()
{
    JobsSignalInternal* self = reinterpret_cast<JobsSignalInternal*>(data);
    atomicFetchSub32Explicit(&self->value, 1, AtomicMemoryOrder::Release);
}

template <typename _T, uint32 _MaxCount> 
JobsAtomicPool<_T, _MaxCount>* JobsAtomicPool<_T, _MaxCount>::Create(Allocator* alloc)
{
    MemSingleShotMalloc<JobsAtomicPool<_T, _MaxCount>> mallocator;
    using PoolT = JobsAtomicPool<_T, _MaxCount>;
    mallocator.template AddMemberField<_T*>(offsetof(PoolT, mPtrs), _MaxCount);
    mallocator.template AddMemberField<_T>(offsetof(PoolT, mProps), _MaxCount, false, alignof(_T));
    JobsAtomicPool<_T, _MaxCount>* p = mallocator.Calloc(alloc);

    p->mIndex = _MaxCount;
    for (uint32 i = 0; i < _MaxCount; i++)
        p->mPtrs[_MaxCount - i - 1] = &p->mProps[i];

    return p;
}

template <typename _T, uint32 _MaxCount> 
void JobsAtomicPool<_T, _MaxCount>::Destroy(JobsAtomicPool<_T, _MaxCount>* p, Allocator* alloc)
{
    MemSingleShotMalloc<JobsAtomicPool<_T, _MaxCount>>::Free(p, alloc);
}

template <typename _T, uint32 _MaxCount> 
inline _T* JobsAtomicPool<_T, _MaxCount>::New()
{
    uint32 idx = atomicFetchSub32(&mIndex, 1);
    ASSERT_MSG(idx != 0, "Pool is full. Increase _MaxCount (%u). See _limits namespace", _MaxCount);
    return mPtrs[idx - 1];
}

template <typename _T, uint32 _MaxCount>
inline void JobsAtomicPool<_T, _MaxCount>::Delete(_T* p)
{
    uint32 idx = atomicFetchAdd32(&mIndex, 1);
    ASSERT_MSG(idx != _MaxCount, "Pool delete fault");
    mPtrs[idx] = p;
}
