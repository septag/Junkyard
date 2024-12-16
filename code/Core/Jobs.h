#pragma once

//
// JobSystem: Fiber based. Inspired by Naughty dog's talk: https://www.gdcvault.com/play/1022186/Parallelizing-the-Naughty-Dog-Engine
// 
// Dispatch: 
//      There are two ways of dispatching jobs. `Dispatch` and `DispatchAuto`. 
//      For regular `Dispatch`, you have to wait for job completion later using `WaitForCompletion` function. This will also delete the job handle.
//      For `DispatchAuto`, There are no handles returned, so you can't wait on the job, and the job will automatically delete itself when it finishes.
//
//      Dispatches can be done in groups. You can provide groupSize in the API, so it dispaches the job across several threads, instead of one. 
//      You will also get `groupIndex` in the job callback, so you know which group we are running on
//
//      Priority: Higher priorities have a chance of executing sooner than lower ones
//
// Thread Model:
//      threadCount will be fetched from the engine being equal to CpuCoreCount - 1 if set to 0 on initialize. Note that this is actual PhysicalCores, not the Logical ones
//      
//      ShortTask: As the name suggests, short tasks are expected to do small amount of work. They should also finish before the current frame ends. 
//                 There are numCores-1 threads of this type in the thread pool by default
//      LongTask: Long tasks are expected to do IO or longer jobs. They can span across several threads as well.
//                There are numCores-1 threads of this type in the thread pool by default
//                They also run on the lower priority threads as opposed to ShortTask types. So by nature ShortTasks have higher priority for cpu execution
//
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

enum class JobsStackSize : uint32
{
    Small = 0,  // 64kb
    Medium,     // 512kb
    Large,      // 2048kb
    _Count
};

struct alignas(CACHE_LINE_SIZE) JobsSignal
{
    JobsSignal();

    void Raise();
    void Wait();
    void Decrement();
    void Increment();
    void WaitOnCondition(bool(*condFn)(int value, int reference), int reference = 0);
    void Set(int value = 1);

private:
    uint8 data[128];
};

struct JobsInitParams
{
    MemAllocator* alloc = Mem::GetDefaultAlloc();
    uint32 numShortTaskThreads = 0; // Default: total number of cores - 1
    uint32 numLongTaskThreads = 0;  // Default: total number of cores - 1
    uint32 defaultShortTaskStackSize = SIZE_MB;
    uint32 defaultLongTaskStackSize = SIZE_MB;
    bool debugAllocations = false;
};

namespace Jobs
{
    API void Initialize(const JobsInitParams& initParams);
    API void Release();

    // Dispatches the job and returns the handle. Handle _must_ be waited on later, with a call to `jobsWaitForCompletion`
    API [[nodiscard]] JobsHandle Dispatch(JobsType type, JobsCallback callback, void* userData = nullptr, 
                                          uint32 groupSize = 1, JobsPriority prio = JobsPriority::Normal, 
                                          JobsStackSize stackSize = JobsStackSize::Medium);
    // Might yield the current running job as well. Also deletes the JobHandle after it's finished
    API void WaitForCompletionAndDelete(JobsHandle handle);
    API void YieldCurrent();

    API bool IsRunningOnCurrentThread();
    API bool IsRunning(JobsHandle handle);
    API void Delete(JobsHandle handle);

    // In this version, we don't care about waiting on the handle. Handle will be automatically delete itself after job is finished
    API void DispatchAndForget(JobsType type, JobsCallback callback, void* userData = nullptr, 
                               uint32 groupSize = 1, JobsPriority prio = JobsPriority::Normal, 
                               JobsStackSize stackSize = JobsStackSize::Medium);

    API uint32 GetWorkerThreadsCount(JobsType type);
}

