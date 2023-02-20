#include "Jobs.h"

#define MINICORO_IMPL
#include "../External/minicoro/minicoro.h"

#include "Memory.h"
#include "System.h"
#include "Buffers.h"
#include "Atomic.h"
#include "String.h"
#include "Array.h"
#include "Log.h"
#include "Settings.h"
#include "TracyHelper.h"

// TODO: remove this dependency
#include "../Engine.h"

namespace _limits
{
    static constexpr uint32 kJobsMaxFibers = 128;
    static constexpr uint32 kJobsMaxInstances = 128;
    static constexpr uint32 kJobsFiberStackSize = kMB;
}

#define JOBS_USE_ANDERSON_LOCK
#ifdef JOBS_USE_ANDERSON_LOCK
    using JobsLock = AtomicALock;
    using JobsLockScope = AtomicALockScope;
#else
    using JobsLock = AtomicLock;
    using JobsLockScope = AtomicLockScope;
#endif

struct JobsFiber
{
    uint32 ownerTid;
    uint32 index;
    JobsPriority prio;
    mco_coro* co;
    mco_desc coDesc;
    JobsInstance* instance;
    JobsCallback callback;
    void* userData;
    JobsFiber* parent;
    JobsFiber* next;
    JobsFiber* prev;
    atomicUint32* childCounter;

    const char* debugName;              // Tracy debug name: Stays resident in memory
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
    uint8 reserved[CACHE_LINE_SIZE - 4];        // padding for the atomic var to fit inside a cache line
    JobsType type;
    bool isAutoDelete;
};

struct JobsWaitingList
{
    JobsFiber* waitingList[static_cast<uint32>(JobsPriority::_Count)];
    JobsFiber* waitingListLast[static_cast<uint32>(JobsPriority::_Count)];
};

struct JobsFiberCreateParams
{
    JobsCallback callback;
    void* userData;
    JobsInstance* instance;
    JobsPriority prio;
    JobsFiber* parent;
    uint32 index;
};

struct JobsPending
{
    JobsType type;
    JobsFiberCreateParams params;
};

struct JobsState
{
    Allocator* alloc;
    Thread* threads[static_cast<uint32>(JobsType::_Count)];
    uint32 numThreads;
    PoolBuffer<JobsFiber> fiberPool;
    PoolBuffer<JobsInstance> instancePool;
    JobsWaitingList waitingLists[static_cast<uint32>(JobsType::_Count)];
    JobsLock fiberLock;
    AtomicLock instanceLock;
    Semaphore semaphores[static_cast<uint32>(JobsType::_Count)];
    Array<JobsPending> pending;

    // Stats
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
    };

    MaxValues maxValues[2];
    bool quit;
};

static JobsState gJobs;

static thread_local JobsThreadData* gJobsThreadData = nullptr;      // Only worker threads initialize this
NO_INLINE static JobsThreadData* JobsGetThreadData() { return gJobsThreadData; }

static void* jobsMcoMallocFn(size_t size, void* allocator_data)
{
    Allocator* alloc = reinterpret_cast<Allocator*>(allocator_data);
    return memAllocAligned(size, 16, alloc);
}
    
static void jobsMcoFreeFn(void* ptr, void* allocator_data)
{
    Allocator* alloc = reinterpret_cast<Allocator*>(allocator_data);
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

NO_OPT_BEGIN
NO_INLINE static void jobsEntryFn(mco_coro* co)
{
    ASSERT(co);
    JobsFiber* fiber = reinterpret_cast<JobsFiber*>(co->user_data);
    if (fiber) {
        ASSERT(fiber->callback);
        fiber->callback(fiber->index, fiber->userData);
    }
}
NO_OPT_END

static inline void jobsAddToList(JobsWaitingList* list, JobsFiber* node, JobsPriority prio)
{
    uint32 index = static_cast<uint32>(prio);
    JobsFiber** pfirst = &list->waitingList[index];
    JobsFiber** plast = &list->waitingListLast[index];

    // Add to the end of the list
    if (*plast) {
        (*plast)->next = node;
        node->prev = *plast;
    }
    *plast = node;
    if (*pfirst == NULL)
        *pfirst = node;
}

static inline void jobsRemoveFromList(JobsWaitingList* list, JobsFiber* node, JobsPriority prio)
{
    uint32 index = static_cast<uint32>(prio);
    JobsFiber** pfirst = &list->waitingList[index];
    JobsFiber** plast = &list->waitingListLast[index];

    if (node->prev)
        node->prev->next = node->next;
    if (node->next)
        node->next->prev = node->prev;
    if (*pfirst == node)
        *pfirst = node->next;
    if (*plast == node)
        *plast = node->prev;
    node->prev = node->next = nullptr;
}

static JobsFiber* jobsCreateFiberUnsafe(const JobsFiberCreateParams& params)
{
    JobsFiber* fiber = gJobs.fiberPool.New();
    ASSERT(fiber);
    
    fiber->ownerTid = 0;
    fiber->index = params.index;
    ASSERT(fiber->co);
    mco_init(fiber->co, &fiber->coDesc);
    fiber->co->user_data = fiber;
    fiber->instance = params.instance;
    fiber->callback = params.callback;
    fiber->userData = params.userData;
    fiber->prio = params.prio;
    fiber->parent = params.parent;
    fiber->prev = fiber->next = nullptr;

    #ifdef TRACY_ENABLE
        static uint32 fiberIndex = 0;
        if (fiber->debugName == nullptr) {
            fiber->debugName = (const char*)calloc(16, 1);  // We don't free this part, that's why we are not using our own memory proxies
            strPrintFmt(const_cast<char*>(fiber->debugName), 16, "Fiber_%u", ++fiberIndex);
        }
    #endif

    atomicFetchAdd32Explicit(&gJobs.numFibers, 1, AtomicMemoryOrder::Relaxed);
    gJobs.maxValues[0].numFibersMax = Max(gJobs.numFibers, gJobs.maxValues[0].numFibersMax);

    return fiber;
}

INLINE JobsFiber* jobsSelect(JobsType type, uint32 threadId, bool* waitingListIsLive)
{
    UNUSED(threadId);

    JobsFiber* fiber = nullptr;
    JobsLockScope lock(gJobs.fiberLock);
    for (uint32 prioIdx = 0; prioIdx < static_cast<uint32>(JobsPriority::_Count); prioIdx++) {
        JobsWaitingList* list = &gJobs.waitingLists[uint32(type)];
        JobsFiber* fiberNode = list->waitingList[prioIdx];

        while (fiberNode) {
            *waitingListIsLive = true;

            if (fiberNode->childCounter == nullptr || 
                atomicLoad32Explicit(fiberNode->childCounter, AtomicMemoryOrder::Acquire) == 0)
            {
                fiber = fiberNode;
                fiber->childCounter = nullptr;
                jobsRemoveFromList(list, fiberNode, static_cast<JobsPriority>(prioIdx));
                prioIdx = static_cast<uint32>(JobsPriority::_Count);    // break out of outer loop
                break;
            }


            fiberNode = fiberNode->next;
        }
    }

    return fiber;
}


static void jobsSetFiberToCurrentThread(JobsFiber* fiber)
{
    ASSERT(fiber);
    ASSERT(JobsGetThreadData());
    ASSERT(JobsGetThreadData()->curFiber == nullptr);

    JobsType type = JobsGetThreadData()->type;
    fiber->ownerTid = 0;
    JobsGetThreadData()->curFiber = fiber;

    if (type == JobsType::ShortTask) {
        atomicFetchAdd32Explicit(&gJobs.numBusyShortThreads, 1, AtomicMemoryOrder::Relaxed);
        gJobs.maxValues[0].numBusyShortThreadsMax = Max(gJobs.maxValues[0].numBusyShortThreadsMax, gJobs.numBusyShortThreads);
    }
    else if (type == JobsType::LongTask) {
        atomicFetchAdd32Explicit(&gJobs.numBusyLongThreads, 1, AtomicMemoryOrder::Relaxed);
        gJobs.maxValues[0].numBusyLongThreadsMax = Max(gJobs.maxValues[0].numBusyLongThreadsMax, gJobs.numBusyLongThreads);
    }

    TracyCFiberEnter(fiber->debugName);
    jobsJumpIn(fiber->co);
    TracyCFiberLeave;

    JobsGetThreadData()->curFiber = nullptr;
    if (type == JobsType::ShortTask)
        atomicFetchSub32Explicit(&gJobs.numBusyShortThreads, 1, AtomicMemoryOrder::Relaxed);
    else if (type == JobsType::LongTask)
        atomicFetchSub32Explicit(&gJobs.numBusyLongThreads, 1, AtomicMemoryOrder::Relaxed);
    
    JobsInstance* inst = fiber->instance;
    if (fiber->co->state == MCO_DEAD) {
        if (atomicFetchSub32(&inst->counter, 1) == 1) {     // Job is finished with all the fibers
            // Delete the job instance automatically if only indicated by the API
            if (inst->isAutoDelete) {
                AtomicLockScope instanceLock(gJobs.instanceLock);
                gJobs.instancePool.Delete(fiber->instance);
                atomicFetchSub32Explicit(&gJobs.numInstances, 1, AtomicMemoryOrder::Relaxed);
            }
        }

        {
            JobsLockScope lock(gJobs.fiberLock);
            gJobs.fiberPool.Delete(fiber);

            atomicFetchSub32Explicit(&gJobs.numFibers, 1, AtomicMemoryOrder::Relaxed);
            
            // Try to pick a pending job
            if (gJobs.pending.Count()) {
                 JobsPending pending = gJobs.pending.PopLast();
                 JobsFiber* newFiber = jobsCreateFiberUnsafe(pending.params);
                 uint32 typeIndex = uint32(pending.type);
                 jobsAddToList(&gJobs.waitingLists[typeIndex], newFiber, pending.params.prio);
                 gJobs.semaphores[typeIndex].Post();
            }
        }
    }
    else {
        // Yielding, Coming back from WaitForCompletion
        ASSERT(fiber->co->state == MCO_SUSPENDED);
        ASSERT(JobsGetThreadData()->waitInstance);
        fiber->childCounter = &JobsGetThreadData()->waitInstance->counter;
        JobsGetThreadData()->waitInstance = nullptr;
        uint32 typeIndex = uint32(inst->type);

        {
            JobsLockScope lk(gJobs.fiberLock);
            jobsAddToList(&gJobs.waitingLists[typeIndex], fiber, fiber->prio);
        }

        gJobs.semaphores[typeIndex].Post();
    }
}

static int jobsThreadFn(void* userData)
{
    // Allocate and initialize thread-data for worker threads
    if (!JobsGetThreadData()) {
        uint64 param = PtrToInt<uint64>(userData);
        gJobsThreadData = memAllocZeroTyped<JobsThreadData>(1, gJobs.alloc);

        JobsGetThreadData()->threadIndex = (param >> 32) & 0xffffffff;
        JobsGetThreadData()->type = static_cast<JobsType>(uint32(param & 0xffffffff));
        JobsGetThreadData()->threadId = threadGetCurrentId();
    }

    uint32 typeIndex = uint32(JobsGetThreadData()->type);
    while (!gJobs.quit) {
        gJobs.semaphores[typeIndex].Wait();

        bool waitingListIsLive = false;
        JobsFiber* fiber = jobsSelect((JobsType)typeIndex, JobsGetThreadData()->threadId, &waitingListIsLive);

        if (fiber) {
            jobsSetFiberToCurrentThread(fiber);
        }
        else if (waitingListIsLive) {
            // Try picking another fiber cuz there are still workers in the waiting list but we couldn't pick them up
            gJobs.semaphores[typeIndex].Post();
            atomicPauseCpu();
        }
    }

    memFree(JobsGetThreadData());
    gJobsThreadData = nullptr;
    return 0;
}

static JobsInstance* jobsDispatchInternal(bool isAutoDelete, JobsType type, JobsCallback callback, void* userData, 
                                          uint32 groupSize, JobsPriority prio)
{
    ASSERT(groupSize);

    // Divide the job into sub-jobs with ranges
    uint32 numFibers = groupSize;
    ASSERT(numFibers);

    JobsInstance* instance;
    {
        AtomicLockScope lock(gJobs.instanceLock);
        ASSERT_MSG(!gJobs.instancePool.IsFull(), "Too many active job instances, increase `kJobsMaxInstances` or spawn less active jobs.");
        instance = gJobs.instancePool.New();
    }

    memset(instance, 0x0, sizeof(*instance));
    atomicFetchAdd32Explicit(&gJobs.numInstances, 1, AtomicMemoryOrder::Relaxed);
    gJobs.maxValues[0].numInstancesMax = Max(gJobs.maxValues[0].numInstancesMax, gJobs.numInstances);

    atomicExchange32Explicit(&instance->counter, numFibers, AtomicMemoryOrder::Release);
    instance->type = type;
    instance->isAutoDelete = isAutoDelete;

    // Another fiber is running on this worker thread
    // Set the running fiber as a parent to the new ones, unless we are using AutoDelete fibers, which don't have any dependencies
    JobsFiber* parent = nullptr;
    if (JobsGetThreadData() && JobsGetThreadData()->curFiber && !isAutoDelete) {
        parent = JobsGetThreadData()->curFiber;
    }

    // Push workers to the end of the list, will be collected by fiber threads
    uint32 numAddedToList = 0;
    {
        JobsLockScope lock(gJobs.fiberLock);

        for (uint32 i = 0; i < numFibers; i++) {
            JobsFiberCreateParams params {
                .callback = callback,
                .userData = userData,
                .instance = instance,
                .prio = prio,
                .parent = parent,
                .index = i
            };
    
            if (!gJobs.fiberPool.IsFull()) {
                JobsFiber* fiber = jobsCreateFiberUnsafe(params);
                jobsAddToList(&gJobs.waitingLists[uint32(type)], fiber, prio);
                numAddedToList++;
            }
            else {
                JobsPending pending {
                    .type = type,
                    .params = params
                };
                gJobs.pending.Push(pending);  // TODO: we PopLast() when we try to get a pending, maybe add in reverse to keep the original order ? 
            }
        }
    }

    if (numAddedToList < numFibers) {
        logWarning("Job %p (numFibers=%u, type=%u) is requesting too many fibers that are already in use, balance your dispatches", 
                   instance, numFibers, static_cast<uint32>(type));
    }

    // Fire up the worker threads
    gJobs.semaphores[uint32(type)].Post(numAddedToList);
    return instance;
}

void jobsWaitForCompletion(JobsInstance* instance)
{
    PROFILE_ZONE(JobsGetThreadData() == nullptr);
    ASSERT(!instance->isAutoDelete);

    uint32 spinCount = 0;
    while (atomicLoad32Explicit(&instance->counter, AtomicMemoryOrder::Acquire)) {
        // If current thread has a fiber assigned and running, put it in waiting list and jump out of it 
        // so one of the threads can continue picking up more workers
        if (JobsGetThreadData()) {
            ASSERT_MSG(JobsGetThreadData()->curFiber, "Worker threads should always have a fiber assigned when 'Wait' is called");

            JobsFiber* curFiber = JobsGetThreadData()->curFiber;
            curFiber->ownerTid = JobsGetThreadData()->threadId;    // save ownerTid as a hint so we can pick this up again on the same thread context
            JobsGetThreadData()->waitInstance = instance;

            jobsJumpOut(curFiber->co);  // Back to `jobsThreadFn::jobsSetFiberToCurrentThread`
        }
        else {
            if (spinCount++ < 32) {
                atomicPauseCpu();   // Main thread just loops 
            }
            else {
                spinCount = 0;
                threadYield();
            }
            // TODO: use better approach here
        }
    }

    {
        AtomicLockScope lock(gJobs.instanceLock);
        gJobs.instancePool.Delete(instance);
    }

    atomicFetchSub32Explicit(&gJobs.numInstances, 1, AtomicMemoryOrder::Relaxed);
}

JobsHandle jobsDispatch(JobsType type, JobsCallback callback, void* userData, uint32 numItemsInJob, JobsPriority prio)
{
    return jobsDispatchInternal(false, type, callback, userData, numItemsInJob, prio);
}

void jobsDispatchAuto(JobsType type, JobsCallback callback, void* userData, uint32 groupSize, JobsPriority prio)
{
    jobsDispatchInternal(true, type, callback, userData, groupSize, prio);
}

void _private::jobsInitialize()
{
    gJobs.alloc = memDefaultAlloc();
    gJobs.pending.SetAllocator(gJobs.alloc);

    MemBudgetAllocator* initHeap = engineGetInitHeap();
    gJobs.initHeapStart = initHeap->GetOffset();

    const SettingsEngine& engineSettings = settingsGetEngine();
    uint32 numThreads = engineSettings.jobsThreadCount ? engineSettings.jobsThreadCount : (engineGetSysInfo().coreCount - 1);
    numThreads = Max<uint32>(1, numThreads);        // We should have at least 1 worker thread, come on!

    // TODO: On android platforms, we have different core types, performance and efficiency
    //       We can't exactly figure that out yet, but current we are following the qualcomm pattern
    //       So an 8-core cpu for example, on most modern qualcomm chips are as follows:
    //          1 - primary core
    //          3 - performance cores
    //          4 - efficiency cores
    //       For now, we divide by two, so longTasks/shortTask threads each one would hopefully take the correct core
    if constexpr (PLATFORM_ANDROID) {
        debugStacktraceSaveStopPoint((void*)jobsEntryFn);   // workaround for stacktrace crash bug. see `debugStacktraceSaveStopPoint`

        numThreads /= 2;
    }

    #ifdef JOBS_USE_ANDERSON_LOCK
        atomicALockInitialize(&gJobs.fiberLock, numThreads+1, memAllocTyped<AtomicALockThread>(numThreads+1, initHeap));
    #endif

    gJobs.semaphores[uint32(JobsType::ShortTask)].Initialize();
    gJobs.semaphores[uint32(JobsType::LongTask)].Initialize();
    
    {
        size_t poolSize = PoolBuffer<JobsFiber>::GetMemoryRequirement(_limits::kJobsMaxFibers);
        gJobs.fiberPool.Reserve(memAlloc(poolSize, initHeap), poolSize, _limits::kJobsMaxFibers);
    }

    {
        size_t poolSize = PoolBuffer<JobsInstance>::GetMemoryRequirement(_limits::kJobsMaxInstances);
        gJobs.instancePool.Reserve(memAlloc(poolSize, initHeap), poolSize, _limits::kJobsMaxInstances);
    }

    gJobs.numThreads = numThreads;

    // Initialize and start the threads
    // LongTasks
    gJobs.threads[uint32(JobsType::LongTask)] = memAllocZeroTyped<Thread>(numThreads, initHeap);

    for (uint32 i = 0; i < numThreads; i++) {
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
        gJobs.threads[uint32(JobsType::LongTask)][i].SetPriority(ThreadPriority::Low);
    }
    
    // ShortTasks
    gJobs.threads[uint32(JobsType::ShortTask)] = memAllocZeroTyped<Thread>(numThreads, initHeap);

    for (uint32 i = 0; i < numThreads; i++) {
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
        gJobs.threads[uint32(JobsType::ShortTask)][i].SetPriority(ThreadPriority::Normal);
    }

    // Initialize fibers
    // TODO: there is leak in mco_create. since we are changing the whole system to fixed pools, I will fix this in future
    JobsFiber* tmpFibers[_limits::kJobsMaxFibers];
    for (uint32 i = 0; i < _limits::kJobsMaxFibers; i++) {
        JobsFiber* fiber = gJobs.fiberPool.New();
        fiber->coDesc = mco_desc_init(jobsEntryFn, _limits::kJobsFiberStackSize);
        fiber->coDesc.malloc_cb = jobsMcoMallocFn;
        fiber->coDesc.free_cb = jobsMcoFreeFn;
        fiber->coDesc.allocator_data = initHeap;
        mco_create(&fiber->co, &fiber->coDesc);
        tmpFibers[i] = fiber;
    }

    for (uint32 i = 0; i < _limits::kJobsMaxFibers; i++) {
       gJobs.fiberPool.Delete(tmpFibers[i]);
    }

    debugFiberScopeProtector_RegisterCallback([](void*)->bool { return JobsGetThreadData() && JobsGetThreadData()->curFiber != nullptr; });

    gJobs.initHeapSize = initHeap->GetOffset() - gJobs.initHeapStart;

    logInfo("(init) Job dispatcher: %u threads", numThreads);
}

void _private::jobsRelease()
{
    gJobs.quit = true;

    gJobs.semaphores[uint32(JobsType::ShortTask)].Post(gJobs.numThreads);
    gJobs.semaphores[uint32(JobsType::LongTask)].Post(gJobs.numThreads);

    if (gJobs.threads[uint32(JobsType::ShortTask)]) {
        for (uint32 i = 0; i < gJobs.numThreads; i++)
            gJobs.threads[uint32(JobsType::ShortTask)][i].Stop();
    }

    if (gJobs.threads[uint32(JobsType::LongTask)]) {
        for (uint32 i = 0; i < gJobs.numThreads; i++)
            gJobs.threads[uint32(JobsType::LongTask)][i].Stop();
    }

    memFree(JobsGetThreadData());
    gJobsThreadData = nullptr;

    gJobs.semaphores[uint32(JobsType::ShortTask)].Release();
    gJobs.semaphores[uint32(JobsType::LongTask)].Release();
}

void jobsDebugThreadStats()
{
    if (JobsGetThreadData()) {
        logInfo("Thread Index: %u, Id: %u, JobsGetThreadData(): %p", 
            JobsGetThreadData()->threadIndex, JobsGetThreadData()->threadId, JobsGetThreadData());
    }
}

void jobsGetBudgetStats(JobsBudgetStats* stats)
{
    JobsState::MaxValues m = gJobs.maxValues[1];

    stats->maxThreads = gJobs.numThreads;
    stats->numBusyShortThreads = m.numBusyShortThreadsMax;
    stats->numBusyLongThreads = m.numBusyLongThreadsMax;

    stats->maxFibers = _limits::kJobsMaxFibers;
    stats->numFibers = m.numFibersMax;

    stats->maxJobs = _limits::kJobsMaxInstances;
    stats->numJobs = m.numInstancesMax;

    stats->initHeapStart = gJobs.initHeapStart;
    stats->initHeapSize = gJobs.initHeapSize;
}

void _private::jobsResetBudgetStats()
{
    JobsState::MaxValues* m = &gJobs.maxValues[0];
    gJobs.maxValues[1] = *m;
    memset(m, 0x0, sizeof(*m));    
}

uint32 jobsGetWorkerThreadsCount()
{
    return gJobs.numThreads;
}