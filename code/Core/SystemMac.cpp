#include "System.h"

#if PLATFORM_APPLE
#include "Allocators.h"
#include "Log.h"
#include "Arrays.h"

#include <mach/mach_time.h>
#include <mach-o/dyld.h>        // _NSGetExecutablePath
#include <unistd.h>             // sysconf
#include <dlfcn.h>              // dlopen, dlclose, dlsym
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <pthread.h>            // pthread_t and family
#include <sys/sysctl.h>
#include <pthread.h>
#include <spawn.h>
#include <signal.h>             // kill
#include <stdio.h>              // puts

//#include <Foundation/Foundation.h>

struct SemaphoreImpl
{
    dispatch_semaphore_t handle;
};

struct SignalImpl 
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int value;
};

struct TimerState 
{
    bool init;
    mach_timebase_info_data_t timebase;
    uint64 start;
};

static TimerState gTimer;

static_assert(sizeof(SemaphoreImpl) <= sizeof(Semaphore), "Sempahore size mismatch");

// Semaphore
void Semaphore::Initialize()
{
    SemaphoreImpl* sem = (SemaphoreImpl*)mData;
    sem->handle = dispatch_semaphore_create(0);
    ASSERT_MSG(sem->handle != NULL, "dispatch_semaphore_create failed");

    _private::sysCountersAddSemaphore();
}

void Semaphore::Release()
{
    SemaphoreImpl* sem = (SemaphoreImpl*)mData;
    if (sem->handle) {
        // ObjC ARC doesn't need dispatch_release (?)
        // dispatch_release(sem->handle);
        sem->handle = NULL;

        _private::sysCountersRemoveSemaphore();
    }
}

void Semaphore::Post(uint32 count)
{
    SemaphoreImpl* sem = (SemaphoreImpl*)mData;
    for (int i = 0; i < count; i++) {
        dispatch_semaphore_signal(sem->handle);
    } 
}

bool Semaphore::Wait(uint32 msecs)
{
    SemaphoreImpl* sem = (SemaphoreImpl*)mData;
    dispatch_time_t dt = msecs < 0 ? DISPATCH_TIME_FOREVER
                                   : dispatch_time(DISPATCH_TIME_NOW, (int64_t)msecs * 1000000ll);
    return !dispatch_semaphore_wait(sem->handle, dt);
}

// Tip by johaness spohr
// https://gist.github.com/jspohr/3dc4f00033d79ec5bdaf67bc46c813e3
static inline int64_t timerInt64MulDiv(int64_t value, int64_t numer, int64_t denom)
{
    int64_t q = value / denom;
    int64_t r = value % denom;
    return q * numer + r * numer / denom;
}

void _private::timerInitialize() 
{
    gTimer.init = true;
    mach_timebase_info(&gTimer.timebase);
    gTimer.start = mach_absolute_time();
}

uint64 timerGetTicks() 
{
    ASSERT_MSG(gTimer.init, "Timer not initialized. call timerInit()");
    const uint64 machNow = mach_absolute_time() - gTimer.start;
    return timerInt64MulDiv(machNow, gTimer.timebase.numer, gTimer.timebase.denom);
}

char* pathGetMyPath(char* dst, size_t dstSize)
{
    uint32 size32 = (uint32)dstSize;
    _NSGetExecutablePath(dst, (uint32_t*)&size32);
    return dst;
}

char* pathGetCurrentDir(char* dst, size_t dstSize)
{
    return getcwd(dst, dstSize);
}

void pathSetCurrentDir(const char* path)
{
    chdir(path);
}

void sysGetSysInfo(SysInfo* info)
{
    memset(info, 0x0, sizeof(*info));
    
    {
        int ncpu;
        size_t ncpuLen = sizeof(ncpu);
        if (sysctlbyname("hw.ncpu", &ncpu, &ncpuLen, nullptr, 0) == 0)
            info->coreCount = (uint32)ncpu;
    }
    
    {
        uint64 physMem;
        size_t physMemSize = sizeof(physMem);
        if (sysctlbyname("hw.memsize", &physMem, &physMemSize, nullptr, 0) == 0)
            info->physicalMemorySize = physMem;
    }
        
    info->pageSize = sysGetPageSize();
    
    // TODO
}

SysProcess::SysProcess() :
    mExitCode(-1),
    mTermSignalCode(0)
{
    mProcess = IntToPtr<int>(-1);
    mStdOutPipeRead = IntToPtr<int>(-1);
    mStdErrPipeRead = IntToPtr<int>(-1);
}

SysProcess::~SysProcess()
{
    int stdoutPipeRead = PtrToInt<int32>(mStdOutPipeRead);
    int stderrPipeRead = PtrToInt<int32>(mStdErrPipeRead);
    pid_t pid = PtrToInt<int32>(mProcess);
    
    if (stdoutPipeRead != -1)
        close(stdoutPipeRead);
    if (stderrPipeRead != -1)
        close(stderrPipeRead);
    
    if (pid != -1) {
        int status;
        waitpid(pid, &status, 0);
    }
}

bool SysProcess::Run(const char* cmdline, SysProcessFlags flags, const char* cwd)
{
    int stdoutPipes[2] = {-1, -1};
    int stderrPipes[2] = {-1, -1};
    pid_t pid;
    posix_spawn_file_actions_t fileActions;

    [[maybe_unused]] int r = posix_spawn_file_actions_init(&fileActions);
    ASSERT_MSG(r == 0, "posix_spawn_file_actions_init failed");

    if ((flags & SysProcessFlags::CaptureOutput) == SysProcessFlags::CaptureOutput) {
        r = pipe(stdoutPipes);
        ASSERT_MSG(r == 0, "Creating pipes failed");

        // tell spawned process to close unused read end of pipe
        // without this - spawned process would not receive EOF
        // when read end of the pipe is closed below,
        r = posix_spawn_file_actions_addclose(&fileActions, stdoutPipes[0]);
        ASSERT_MSG(r == 0, "posix_spawn_file_actions_addclose");
        
        r = posix_spawn_file_actions_adddup2(&fileActions, stdoutPipes[1], STDOUT_FILENO);
        ASSERT_MSG(r == 0, "posix_spawn_file_actions_addup2 failed");
        
        // Do the same for stderr
        r = pipe(stderrPipes);
        ASSERT_MSG(r == 0, "Creating pipes failed");

        r = posix_spawn_file_actions_addclose(&fileActions, stderrPipes[0]);
        ASSERT_MSG(r == 0, "posix_spawn_file_actions_addclose");
        
        r = posix_spawn_file_actions_adddup2(&fileActions, stderrPipes[1], STDERR_FILENO);
        ASSERT_MSG(r == 0, "posix_spawn_file_actions_addup2 failed");
    }
    
    if (cwd) {
        if (__builtin_available(macOS 10.15, *))
            posix_spawn_file_actions_addchdir_np(&fileActions, cwd);
        else
            ASSERT_MSG(0, "Not implemented for versions less than macOS 10.15");
    }
    
    // split command-line arguments
    MemTempAllocator tmpAlloc;
    Array<char*> argsArr(&tmpAlloc);

    char* cmdlineCopy = memAllocCopy<char>(cmdline, strLen(cmdline)+1, &tmpAlloc);
    char* str = const_cast<char*>(strSkipWhitespace(cmdlineCopy));
    while (*str) {
        // Find the next whitespace, or end of string
        char* start = str;
        while (*(++str)) {
            if (strIsWhitespace(*str)) {
                *str = 0;
                str = const_cast<char*>(strSkipWhitespace(str+1));
                break;
            }
        }
        argsArr.Push(start);
    }
    
    ASSERT(argsArr.Count());
    char** args = nullptr;
    if (argsArr.Count() > 1) {
        args = (char**)tmpAlloc.MallocTyped<char*>(argsArr.Count() + 1);
        for (uint32 i = 0; i < argsArr.Count(); i++)
            args[i] = argsArr[i];
        args[argsArr.Count()] = nullptr;
    }
    
    if (posix_spawn(&pid, argsArr[0], &fileActions, nullptr, args, nullptr) != 0) {
        logError("Running process failed: %s", cmdline);
        posix_spawn_file_actions_destroy(&fileActions);
        if (stdoutPipes[0] != -1)
            close(stdoutPipes[0]);
        if (stdoutPipes[1] != -1)
            close(stdoutPipes[1]);
        return false;
    }
    
    if ((flags & SysProcessFlags::CaptureOutput) == SysProcessFlags::CaptureOutput) {
        close(stdoutPipes[1]);
        close(stderrPipes[1]);
        mStdOutPipeRead = IntToPtr<int>(stdoutPipes[0]);
        mStdErrPipeRead = IntToPtr<int>(stderrPipes[0]);
    }
    
    posix_spawn_file_actions_destroy(&fileActions);
    mProcess = IntToPtr<int32>(pid);
    return true;
}

void SysProcess::Wait() const
{
    pid_t pid = PtrToInt<int32>(mProcess);
    ASSERT(pid != -1);
    int status;
    [[maybe_unused]] int r = waitpid(pid, &status, 0);
    ASSERT(r == pid);
    if (WIFEXITED(status))
        const_cast<SysProcess*>(this)->mExitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        const_cast<SysProcess*>(this)->mTermSignalCode = WTERMSIG(status);
    const_cast<SysProcess*>(this)->mProcess = IntToPtr<int32>(-1);
}

bool SysProcess::IsRunning() const
{
    pid_t pid = PtrToInt<int32>(mProcess);
    ASSERT(pid != -1);
    int status;
    return waitpid(pid, &status, WNOHANG) == 0;
}

int SysProcess::GetExitCode() const
{
    return mExitCode;
}

uint32 SysProcess::ReadStdOut(void* data, uint32 size) const
{
    int pipeId = PtrToInt<int>(mStdOutPipeRead);
    ASSERT(pipeId != -1);
    ssize_t r = read(pipeId, data, size);
    return r > 0 ? (uint32)r : 0;
}

uint32 SysProcess::ReadStdErr(void* data, uint32 size) const
{
    int pipeId = PtrToInt<int>(mStdErrPipeRead);
    ASSERT(pipeId != -1);
    ssize_t r = read(pipeId, data, size);
    return r > 0 ? (uint32)r : 0;
}

void SysProcess::Abort()
{
    pid_t pid = PtrToInt<int32>(mProcess);
    if (pid)
        kill(pid, 1);
}

bool sysIsDebuggerPresent()
{
    int mib[4];
    struct kinfo_proc info;
    size_t size = sizeof(info);

    // Set the sysctl arguments to request information about the current process
    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PID;
    mib[3] = getpid();

    // Retrieve the process information
    if (sysctl(mib, 4, &info, &size, NULL, 0) == -1) {
        puts("Failed to retrieve process information.");
        return false;
    }

    // Check if the P_TRACED flag is set, indicating a debugger is attached
    return (info.kp_proc.p_flag & P_TRACED) != 0;
}

void sysApplePrintToLog(const char* text)
{
    puts(text);
}

char* pathGetHomeDir(char* dst, size_t dstSize)
{
    #if PLATFORM_OSX
        const char* homeDir = getenv("HOME");
        ASSERT(homeDir);
        strCopy(dst, (uint32)dstSize, homeDir);
        return dst;
    #else
        ASSERT(0, "Not implemented in iOS");
        return nullptr;
    #endif
}

char* pathGetCacheDir(char* dst, size_t dstSize, const char* appName)
{
    #if PLATFORM_OSX
        const char* homeDir = getenv("HOME");
        ASSERT(homeDir);
        strCopy(dst, (uint32)dstSize, homeDir);
        strConcat(dst, (uint32)dstSize, "/Library/Application Support/");
        strConcat(dst, (uint32)dstSize, appName);
        return dst;
    #else
        ASSERT(0, "Not implemented");
        return nullptr;
    #endif
}
#endif // PLATFORM_APPLE

