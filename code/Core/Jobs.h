#pragma once

#include "Base.h"

struct JobsInstance;
using JobsHandle = JobsInstance*;
using JobsCallback = void(*)(uint32 groupIndex, void* userData);

enum class JobsPriority : uint32
{
    High = 0,
    Normal,
    Low,
    _Count
};

enum class JobsType : uint32
{
    ShortTask = 0,
    LongTask,
    _Count
};

struct JobsBudgetStats
{
    uint32 maxThreads;
    uint32 numBusyShortThreads;
    uint32 numBusyLongThreads;

    uint32 numFibers;
    uint32 maxFibers;

    uint32 numJobs;
    uint32 maxJobs;

    size_t initHeapStart;
    size_t initHeapSize;
};

// Dispatches the job and returns the handle. Handle _must_ be waited on later, with a call to `jobsWaitForCompletion`
API [[nodiscard]] JobsHandle jobsDispatch(JobsType type, JobsCallback callback, void* userData = nullptr, 
                                          uint32 groupSize = 1, JobsPriority prio = JobsPriority::Normal);
// Might yield the current running job as well. Also deletes the JobHandle after it's finished
API void jobsWaitForCompletion(JobsHandle handle);

// In this version, we don't care about waiting on the handle. Handle will be automatically delete itself after job is finished
API void jobsDispatchAuto(JobsType type, JobsCallback callback, void* userData = nullptr, 
                          uint32 groupSize = 1, JobsPriority prio = JobsPriority::Normal);

API void jobsDebugThreadStats();    // TEMP
API void jobsGetBudgetStats(JobsBudgetStats* stats);
API uint32 jobsGetWorkerThreadsCount();

namespace _private
{
    void jobsInitialize();
    void jobsRelease();
    void jobsResetBudgetStats();
} // _private

