#include "System.h"

#if PLATFORM_WINDOWS
#include "String.h"
#include "Memory.h"
#include "Atomic.h"
#include "Buffers.h"
#include "IncludeWin.h"
#include "TracyHelper.h"
#include "Log.h"

#include <limits.h>     // LONG_MAX
#include <synchapi.h>   // InitializeCriticalSectionAndSpinCount, InitializeCriticalSection, ...
#include <sysinfoapi.h> // GetPhysicallyInstalledSystemMemory
#include <intrin.h>     // __cpuid
#include <tlhelp32.h>   // CreateToolhelp32Snapshot
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ShlObj.h>     // SH family of functions

namespace _limits 
{
    const uint32 kSysMaxCores = 128;
}

struct MutexImpl 
{
    CRITICAL_SECTION handle;
};

struct SemaphoreImpl 
{
    HANDLE handle;
};

struct SignalImpl 
{
    CRITICAL_SECTION mutex;
    CONDITION_VARIABLE cond;
    int value;
};

struct ThreadImpl
{
    Semaphore sem;
    ThreadEntryFunc threadFn;
    HANDLE handle;
    void* userData;
    size_t stackSize;
    char name[32];
    DWORD tId;
    atomicUint32 stopped;
    bool running;
};

static_assert(sizeof(MutexImpl) <= sizeof(Mutex), "Mutex size mismatch");
static_assert(sizeof(SemaphoreImpl) <= sizeof(Semaphore), "Sempahore size mismatch");
static_assert(sizeof(SignalImpl) <= sizeof(Signal), "Signal size mismatch");
static_assert(sizeof(ThreadImpl) <= sizeof(Thread), "Thread size mismatch");

//------------------------------------------------------------------------
// Thread
static DWORD WINAPI threadStubFn(LPVOID arg)
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(arg);
    thrd->tId = GetCurrentThreadId();
    threadSetCurrentThreadName(thrd->name);

    thrd->sem.Post();
    ASSERT(thrd->threadFn);
    return static_cast<DWORD>(thrd->threadFn(thrd->userData));
}

Thread::Thread()
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(this->data);
    thrd->threadFn = nullptr;
    thrd->handle = INVALID_HANDLE_VALUE;
    thrd->userData = nullptr;
    thrd->stackSize = 0;
    thrd->name[0] = 0;
    thrd->tId = 0;
    thrd->stopped = false;
    thrd->running = false;
}

bool Thread::Start(const ThreadDesc& desc)
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(this->data);
    ASSERT(thrd->handle == INVALID_HANDLE_VALUE && !thrd->running);

    thrd->sem.Initialize();
    thrd->threadFn = desc.entryFn;
    thrd->userData = desc.userData;
    thrd->stackSize = Max<size_t>(desc.stackSize, 64*kKB);
    thrd->stopped = 0;
    strCopy(thrd->name, sizeof(thrd->name), desc.name ? desc.name : "");

    thrd->handle = CreateThread(nullptr, thrd->stackSize, (LPTHREAD_START_ROUTINE)threadStubFn, thrd, 0, nullptr);
    if (thrd->handle == nullptr) {
        thrd->sem.Release();
        return false;
    }
    ASSERT_ALWAYS(thrd->handle != nullptr, "CreateThread failed");

    thrd->sem.Wait();   // Ensure that thread callback is running
    thrd->running = true;
    return true;
}

int Thread::Stop()
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(this->data);
    DWORD exitCode = 0;
    if (thrd->handle != INVALID_HANDLE_VALUE) {
        ASSERT_MSG(thrd->running, "Thread is not running!");

        atomicStore32Explicit(&thrd->stopped, 1, AtomicMemoryOrder::Release);
        WaitForSingleObject(thrd->handle, INFINITE);
        GetExitCodeThread(thrd->handle, &exitCode);
        CloseHandle(thrd->handle);
        thrd->sem.Release();

        thrd->handle = INVALID_HANDLE_VALUE;
    }

    thrd->running = false;
    return static_cast<int>(exitCode);
}

bool Thread::IsRunning() const
{
    const ThreadImpl* thrd = reinterpret_cast<const ThreadImpl*>(this->data);
    return thrd->running;
}

bool Thread::IsStopped()
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(this->data);
    return atomicLoad32Explicit(&thrd->stopped, AtomicMemoryOrder::Acquire) == 1;
}

void Thread::SetPriority(ThreadPriority prio)
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(this->data);

    int prioWin = 0;
    switch (prio) {
    case ThreadPriority::Normal:    prioWin = THREAD_PRIORITY_NORMAL; break;
    case ThreadPriority::Idle:      prioWin = THREAD_PRIORITY_IDLE; break;
    case ThreadPriority::Realtime:  prioWin = THREAD_PRIORITY_TIME_CRITICAL; break;
    case ThreadPriority::High:      prioWin = THREAD_PRIORITY_HIGHEST; break;
    case ThreadPriority::Low:       prioWin = THREAD_PRIORITY_LOWEST; break;
    }

    [[maybe_unused]] BOOL r = SetThreadPriority(thrd->handle, prioWin);
    ASSERT(r);
}

//--------------------------------------------------------------------------------------------------
// Mutex
void Mutex::Initialize(uint32 spinCount)
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(this->data);
    [[maybe_unused]] BOOL r = InitializeCriticalSectionAndSpinCount(&_m->handle, spinCount);
    ASSERT_ALWAYS(r, "InitializeCriticalSection failed");
}

void Mutex::Release()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(this->data);
    DeleteCriticalSection(&_m->handle);
}

void Mutex::Enter()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(this->data);
    EnterCriticalSection(&_m->handle);
}

void Mutex::Exit()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(this->data);
    LeaveCriticalSection(&_m->handle);
}

bool Mutex::TryEnter()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(this->data);
    return TryEnterCriticalSection(&_m->handle) == TRUE;
}

// Semaphore
void Semaphore::Initialize()
{
    SemaphoreImpl* _sem = reinterpret_cast<SemaphoreImpl*>(this->data);
    _sem->handle = CreateSemaphoreA(nullptr, 0, LONG_MAX, nullptr);
    ASSERT_ALWAYS(_sem->handle != INVALID_HANDLE_VALUE, "Failed to create semaphore");
}

void Semaphore::Release()
{
    SemaphoreImpl* _sem = reinterpret_cast<SemaphoreImpl*>(this->data);
    if (_sem->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(_sem->handle);
        _sem->handle = INVALID_HANDLE_VALUE;
    }
}

void Semaphore::Post(uint32 count)
{
    SemaphoreImpl* _sem = reinterpret_cast<SemaphoreImpl*>(this->data);
    ASSERT(_sem->handle != INVALID_HANDLE_VALUE);
    ReleaseSemaphore(_sem->handle, count, nullptr);
}

bool Semaphore::Wait(uint32 msecs)
{
    SemaphoreImpl* _sem = reinterpret_cast<SemaphoreImpl*>(this->data);
    ASSERT(_sem->handle != INVALID_HANDLE_VALUE);
    return WaitForSingleObject(_sem->handle, (DWORD)msecs) == WAIT_OBJECT_0;
}

//--------------------------------------------------------------------------------------------------
// Signal
// https://github.com/mattiasgustavsson/libs/blob/master/thread.h
void Signal::Initialize()
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);
    [[maybe_unused]] BOOL r = InitializeCriticalSectionAndSpinCount(&_sig->mutex, 32);
    ASSERT_ALWAYS(r, "InitializeCriticalSectionAndSpinCount failed");
    InitializeConditionVariable(&_sig->cond);
    _sig->value = 0;
}

void Signal::Release()
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);
    DeleteCriticalSection(&_sig->mutex);
}

void Signal::Raise()
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);
    WakeConditionVariable(&_sig->cond);
}

void Signal::RaiseAll()
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);
    WakeAllConditionVariable(&_sig->cond);
}

bool Signal::Wait(uint32 msecs)
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);

    bool timedOut = false;
    EnterCriticalSection(&_sig->mutex);
    while (_sig->value == 0) {
        int r = SleepConditionVariableCS(&_sig->cond, &_sig->mutex, (DWORD)msecs);
        if (!r && GetLastError() == ERROR_TIMEOUT) {
            timedOut = true;
            break;
        }
    }
    if (!timedOut)
        _sig->value = 0;
    LeaveCriticalSection(&_sig->mutex);
    return !timedOut;
}

void Signal::Decrement()
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);
    EnterCriticalSection(&_sig->mutex);
    --_sig->value;
    LeaveCriticalSection(&_sig->mutex);
}

void Signal::Increment()
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);
    EnterCriticalSection(&_sig->mutex);
    ++_sig->value;
    LeaveCriticalSection(&_sig->mutex);
}

bool Signal::WaitOnCondition(bool(*condFn)(int value, int reference), int reference, uint32 msecs)
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);

    bool timedOut = false;
    EnterCriticalSection(&_sig->mutex);
    while (condFn(_sig->value, reference)) {
        int r = SleepConditionVariableCS(&_sig->cond, &_sig->mutex, (DWORD)msecs);
        if (!r && GetLastError() == ERROR_TIMEOUT) {
            timedOut = true;
            break;
        }
    }
    if (!timedOut)
        _sig->value = reference;
    LeaveCriticalSection(&_sig->mutex);
    return !timedOut;
}

void Signal::Set(int value)
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(this->data);

    EnterCriticalSection(&_sig->mutex);
    _sig->value = value;
    LeaveCriticalSection(&_sig->mutex);
}

//--------------------------------------------------------------------------------------------------
// Thread
void threadYield()
{
    SwitchToThread();
}

uint32 threadGetCurrentId()
{
    return GetCurrentThreadId();
}

void threadSleep(uint32 msecs)
{
    Sleep((DWORD)msecs);
}

void threadSetCurrentThreadPriority(ThreadPriority prio)
{
    int prioWin = 0;
    switch (prio) {
    case ThreadPriority::Normal:    prioWin = THREAD_PRIORITY_NORMAL; break;
    case ThreadPriority::Idle:      prioWin = THREAD_PRIORITY_IDLE; break;
    case ThreadPriority::Realtime:  prioWin = THREAD_PRIORITY_TIME_CRITICAL; break;
    case ThreadPriority::High:      prioWin = THREAD_PRIORITY_HIGHEST; break;
    case ThreadPriority::Low:       prioWin = THREAD_PRIORITY_LOWEST; break;
    }

    [[maybe_unused]] BOOL r = SetThreadPriority(GetCurrentThread(), prioWin);
    ASSERT(r);
}

void threadSetCurrentThreadName(const char* name)
{
    wchar_t namew[32];
    strUt8ToWide(name, namew, sizeof(namew));
    SetThreadDescription(GetCurrentThread(), namew);

    #if TRACY_ENABLE
        TracyCSetThreadName(name);
    #endif
}

void threadGetCurrentThreadName(char* nameOut, uint32 nameSize)
{
    PWSTR namew;
    if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &namew)))
        strWideToUtf8(namew, nameOut, nameSize);
    else 
        nameOut[0] = 0;
}

//--------------------------------------------------------------------------------------------------
// Timer
struct TimerState
{
    bool init;
    LARGE_INTEGER freq;
    LARGE_INTEGER start;
};
static TimerState gTimer;

// Tip by johaness spohr
// https://gist.github.com/jspohr/3dc4f00033d79ec5bdaf67bc46c813e3
static int64 timerInt64MulDiv(int64 value, int64 numer, int64 denom) 
{
    int64 q = value / denom;
    int64 r = value % denom;
    return q * numer + r * numer / denom;
}

void timerInitialize() 
{
    gTimer.init = true;
    
    QueryPerformanceFrequency(&gTimer.freq);
    QueryPerformanceCounter(&gTimer.start);
}

uint64 timerGetTicks() 
{
    ASSERT_MSG(gTimer.init, "Timer not initialized. call timerInitialize()");
    
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return timerInt64MulDiv(li.QuadPart - gTimer.start.QuadPart, 1000000000, gTimer.freq.QuadPart);
}

DLLHandle sysLoadDLL(const char* filepath, char** pErrorMsg)
{
    auto dll = (DLLHandle)LoadLibraryA(filepath);
    if (dll == nullptr && pErrorMsg) {
        static char errMsg[64];
        strPrintFmt(errMsg, sizeof(errMsg), "GetLastError: %u", GetLastError());
        *pErrorMsg = errMsg;
    }
    else {
        if (pErrorMsg) 
            *pErrorMsg = nullptr;
    }
    return dll;
}

void sysUnloadDLL(DLLHandle dll)
{
    if (dll)
        FreeLibrary((HMODULE)dll);
}

void* sysSymbolAddress(DLLHandle dll, const char* symbolName)
{
    return (void*)GetProcAddress((HMODULE)dll, symbolName);
}

size_t sysGetPageSize()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (size_t)si.dwPageSize;
}

bool sysWin32GetRegisterLocalMachineString(const char* subkey, const char* value, char* dst, size_t dstSize)
{
    // Only load the DLL and function if it's used 
    typedef LSTATUS (*RegGetValueAFn_)(
        _In_ HKEY hkey,
        _In_opt_ LPCSTR lpSubKey,
        _In_opt_ LPCSTR lpValue,
        _In_ DWORD dwFlags,
        _Out_opt_ LPDWORD pdwType,
        _When_((dwFlags & 0x7F) == RRF_RT_REG_SZ ||
                (dwFlags & 0x7F) == RRF_RT_REG_EXPAND_SZ ||
                (dwFlags & 0x7F) == (RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ) ||
                *pdwType == REG_SZ ||
                *pdwType == REG_EXPAND_SZ, _Post_z_)
            _When_((dwFlags & 0x7F) == RRF_RT_REG_MULTI_SZ ||
                *pdwType == REG_MULTI_SZ, _Post_ _NullNull_terminated_)
        _Out_writes_bytes_to_opt_(*pcbData,*pcbData) PVOID pvData,
        _Inout_opt_ LPDWORD pcbData);
    static DLLHandle dll = nullptr;
    static RegGetValueAFn_ RegGetValueAFn = nullptr;

    if (!RegGetValueAFn) {
        dll = sysLoadDLL("Advapi32.dll");
        ASSERT_ALWAYS(dll, "Could not load system DLL: Advapi32.dll");
        RegGetValueAFn = (RegGetValueAFn_)sysSymbolAddress(dll, "RegGetValueA");
    }

    DWORD dataSize = (DWORD)dstSize;
    return RegGetValueAFn(HKEY_LOCAL_MACHINE, subkey, value, RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ, nullptr, dst, &dataSize) == ERROR_SUCCESS;
}

static uint32 sysGetPhysicalCoresCount()
{
	static uint32 cahcedCoreCount = UINT32_MAX;
	if (cahcedCoreCount != UINT32_MAX)
		return cahcedCoreCount;

	SYSTEM_LOGICAL_PROCESSOR_INFORMATION* buffer = nullptr;
	DWORD returnLen = 0;
	DWORD countCount = 0;
	if (!GetLogicalProcessorInformation(buffer, &returnLen)) {
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			buffer = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)memAlloc(returnLen);
	}

	if (buffer != nullptr && GetLogicalProcessorInformation(buffer, &returnLen)) {
		SYSTEM_LOGICAL_PROCESSOR_INFORMATION* ptr = buffer;
		DWORD byteOffset = 0;
		while (byteOffset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= returnLen) {
			if (ptr->Relationship == RelationProcessorCore)
				++countCount;

			byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
			++ptr;
		}
	}

	memFree(buffer);
	cahcedCoreCount = Clamp<uint32>(countCount, 1, _limits::kSysMaxCores);
	return cahcedCoreCount;
}

// https://en.wikipedia.org/wiki/CPUID
// https://docs.microsoft.com/en-us/cpp/intrinsics/cpuid-cpuidex?redirectedfrom=MSDN&view=msvc-170
void sysGetSysInfo(SysInfo* info)
{
    struct i4 
    {
        int i[4];
    };

    i4 cpui;
    Array<i4> data;
    Array<i4> extData;
    int ids;
    int extIds;
    uint32 f_1_ECX_ = 0;
    uint32 f_1_EDX_ = 0;
    uint32 f_7_EBX_ = 0;
    uint32 f_7_ECX_ = 0;
    uint32 f_81_ECX_ = 0;
    uint32 f_81_EDX_ = 0;

    __cpuid(cpui.i, 0);
    ids = cpui.i[0];
    for (int i = 0; i <= ids; i++) {
        __cpuidex(cpui.i, i, 0);
        memcpy(data.Push(), cpui.i, sizeof(cpui));
    }

    char vendor[0x20];
    memset(vendor, 0x0, sizeof(vendor));
    *(int*)(vendor) = data[0].i[1];
    *(int*)(vendor + 4) = data[0].i[3];
    *(int*)(vendor + 8) = data[0].i[2];
    
    strCopy(info->cpuName, sizeof(info->cpuName), vendor);
    
    if (ids >= 1) {
        f_1_ECX_ = static_cast<uint32>(data[1].i[2]);
        f_1_EDX_ = static_cast<uint32>(data[1].i[3]);
    }

    if (ids >= 7) {
        f_7_EBX_ = data[7].i[1];
        f_7_ECX_ = data[7].i[2];
    }

    // Calling __cpuid with 0x80000000 as the function_id argument
    // gets the number of the highest valid extended ID.
    __cpuid(cpui.i, 0x80000000);
    extIds = cpui.i[0];
    
    char brand[0x40];
    memset(brand, 0, sizeof(brand));
    
    for (int i = 0x80000000; i <= extIds; ++i)
    {
        __cpuidex(cpui.i, i, 0);
        memcpy(extData.Push(), cpui.i, sizeof(cpui));
    }
    
    // load bitset with flags for function 0x80000001
    if (extIds >= 0x80000001)
    {
        f_81_ECX_ = extData[1].i[2];
        f_81_EDX_ = extData[1].i[3];
    }
    
    // Interpret CPU brand string if reported
    if (extIds >= 0x80000004)
    {
        memcpy(brand, extData[2].i, sizeof(cpui));
        memcpy(brand + 16, extData[3].i, sizeof(cpui));
        memcpy(brand + 32, extData[4].i, sizeof(cpui));
    }

    strCopy(info->cpuModel, sizeof(info->cpuModel), brand);
    strTrim(info->cpuModel, sizeof(info->cpuModel), info->cpuModel);

    #if CPU_X86
        info->cpuFamily = SysCpuFamily::x86_64;
    #else
        info->cpuFamily = SysCpuFamily::ARM64;
    #endif
    info->cpuCapsSSE = ((f_1_EDX_ >> 25) & 0x1) ? true : false;
    info->cpuCapsSSE2 = ((f_1_EDX_ >> 26) & 0x1) ? true : false;
    info->cpuCapsSSE3 = (f_1_ECX_ & 0x1) ? true : false;
    info->cpuCapsSSE41 = ((f_1_ECX_ >> 19) & 0x1) ? true : false;
    info->cpuCapsSSE42 = ((f_1_ECX_ >> 20) & 0x1) ? true : false;
    info->cpuCapsAVX = ((f_1_ECX_ >> 28) & 0x1) ? true : false;
    info->cpuCapsAVX2 = ((f_7_EBX_ >> 5) & 0x1) ? true : false;
    info->cpuCapsAVX512 = ((f_7_EBX_ >> 16) & 0x1) ? true : false;

    // 
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    
    info->pageSize = sysinfo.dwPageSize;
    // info->coreCount = sysinfo.dwNumberOfProcessors;
    info->coreCount = sysGetPhysicalCoresCount();

    ULONGLONG memSizeKb;
    if (GetPhysicallyInstalledSystemMemory(&memSizeKb)) 
        info->physicalMemorySize = memSizeKb * 1024;

    data.Free();
    extData.Free();
}

#if PLATFORM_DESKTOP
SysProcess::SysProcess() :
    process(INVALID_HANDLE_VALUE),
    stdoutPipeRead(INVALID_HANDLE_VALUE),
    stderrPipeRead(INVALID_HANDLE_VALUE)
{
}

SysProcess::~SysProcess()
{
    if (this->stdoutPipeRead != INVALID_HANDLE_VALUE) 
        CloseHandle(this->stdoutPipeRead);
    if (this->stderrPipeRead != INVALID_HANDLE_VALUE)
        CloseHandle(this->stderrPipeRead);
    if (this->process != INVALID_HANDLE_VALUE)
        CloseHandle(this->process);
}

bool SysProcess::Run(const char* cmdline, SysProcessFlags flags, const char* cwd)
{
    ASSERT(this->process == INVALID_HANDLE_VALUE);

    HANDLE stdOutPipeWrite = INVALID_HANDLE_VALUE;
    HANDLE stdErrPipeWrite = INVALID_HANDLE_VALUE;
    BOOL r;
    BOOL inheritHandles = (flags & SysProcessFlags::InheritHandles) == SysProcessFlags::InheritHandles ? TRUE : FALSE;

    if ((flags & SysProcessFlags::CaptureOutput) == SysProcessFlags::CaptureOutput) {
        SECURITY_ATTRIBUTES saAttr {}; 
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
        saAttr.bInheritHandle = inheritHandles; 

        r = CreatePipe(&this->stdoutPipeRead, &stdOutPipeWrite, &saAttr, 0);
        ASSERT_MSG(r, "CreatePipe failed");

        r = CreatePipe(&this->stderrPipeRead, &stdErrPipeWrite, &saAttr, 0);
        ASSERT_MSG(r, "CreatePipe failed");

        if (inheritHandles) {
            r = SetHandleInformation(this->stdoutPipeRead, HANDLE_FLAG_INHERIT, 0);
            ASSERT_MSG(r, "SetHandleInformation for pipe failed");
            r = SetHandleInformation(this->stderrPipeRead, HANDLE_FLAG_INHERIT, 0);
            ASSERT_MSG(r, "SetHandleInformation for pipe failed");
        }
    }

    PROCESS_INFORMATION procInfo {};
    STARTUPINFOA startInfo {};
    startInfo.cb = sizeof(startInfo);
    if ((flags & SysProcessFlags::CaptureOutput) == SysProcessFlags::CaptureOutput) {
        startInfo.dwFlags = STARTF_USESTDHANDLES;
        startInfo.hStdOutput = stdOutPipeWrite;
        startInfo.hStdError = stdErrPipeWrite;
        startInfo.hStdInput = INVALID_HANDLE_VALUE; // TODO
    }

    MemTempAllocator tmpAlloc;
    char* cmdLineCopy = memAllocCopy<char>(cmdline, strLen(cmdline)+1, &tmpAlloc);
    DWORD createProcessFlags = 0; // TODO

    if ((flags & SysProcessFlags::DontCreateConsole) == SysProcessFlags::DontCreateConsole) 
        createProcessFlags |= CREATE_NO_WINDOW;
    if ((flags & SysProcessFlags::ForceCreateConsole) == SysProcessFlags::ForceCreateConsole)
        createProcessFlags |= CREATE_NEW_CONSOLE;

    r = CreateProcessA(nullptr, cmdLineCopy, nullptr, nullptr, inheritHandles, createProcessFlags, NULL, cwd, &startInfo, &procInfo);
    if (!r) {
        logError("Run process failed: %s", cmdline);
        return false;
    }

    CloseHandle(procInfo.hThread);
    this->process = procInfo.hProcess;

    if ((flags & SysProcessFlags::CaptureOutput) == SysProcessFlags::CaptureOutput) {
        CloseHandle(stdOutPipeWrite);
        CloseHandle(stdErrPipeWrite);
    }

    return true;
}

void SysProcess::Wait() const
{
    ASSERT(this->process != INVALID_HANDLE_VALUE);
    WaitForSingleObject(this->process, INFINITE);
}

bool SysProcess::IsRunning() const
{
    ASSERT(this->process != INVALID_HANDLE_VALUE);
    return WaitForSingleObject(this->process, 0) != WAIT_OBJECT_0;
}

int SysProcess::GetExitCode() const
{
    ASSERT(this->process != INVALID_HANDLE_VALUE);
    DWORD exitCode = UINT32_MAX;
    GetExitCodeProcess(this->process, &exitCode);
    return static_cast<int>(exitCode);
}

uint32 SysProcess::ReadStdOut(void* data, uint32 size) const
{
    ASSERT(this->stdoutPipeRead != INVALID_HANDLE_VALUE);

    DWORD bytesRead;
    BOOL r = ReadFile((HANDLE)this->stdoutPipeRead, data, size, &bytesRead, nullptr);
    return (r && bytesRead) ? bytesRead : 0;
}

uint32 SysProcess::ReadStdErr(void* data, uint32 size) const
{
    ASSERT(this->stderrPipeRead != INVALID_HANDLE_VALUE);

    DWORD bytesRead;
    BOOL r = ReadFile((HANDLE)this->stderrPipeRead, data, size, &bytesRead, nullptr);
    return (r && bytesRead) ? bytesRead : 0;
}
#endif  // PLATFORM_DESKTOP

bool sysWin32IsProcessRunning(const char* execName)
{
    PROCESSENTRY32 entry { sizeof(PROCESSENTRY32) };

    char execNameTrimmed[kMaxPath];
    strTrim(execNameTrimmed, sizeof(execNameTrimmed), execName, '\'');
    strTrim(execNameTrimmed, sizeof(execNameTrimmed), execNameTrimmed, '"');

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, TH32CS_SNAPPROCESS);
    if (!Process32First(snapshot, &entry)) {
        CloseHandle(snapshot);
        return false;
    }

    do {
        if (strIsEqualNoCase(entry.szExeFile, execNameTrimmed)) {
            CloseHandle(snapshot);
            return true;
        }
    } while (Process32Next(snapshot, &entry));

    CloseHandle(snapshot);
    return false;
}

char* pathGetMyPath(char* dst, size_t dstSize)
{
    GetModuleFileNameA(NULL, dst, (DWORD)dstSize);
    return dst;
}

char* pathAbsolute(const char* path, char* dst, size_t dstSize)
{
    if (GetFullPathNameA(path, (DWORD)dstSize, dst, NULL) == 0)
        dst[0] = '\0';
    return dst;
}

char* pathGetCurrentDir(char* dst, size_t dstSize)
{
    GetCurrentDirectoryA((DWORD)dstSize, dst);
    return dst;
}

void pathSetCurrentDir(const char* path)
{
    SetCurrentDirectoryA(path);
}

PathInfo pathStat(const char* path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
        return PathInfo { .type = PathType::Invalid };
    }

    PathType type = PathType::Invalid;
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        type = PathType::Directory;
    else if (!(fad.dwFileAttributes & (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_SYSTEM)))
        type = PathType::File;

    LARGE_INTEGER size;
    size.HighPart = fad.nFileSizeHigh;
    size.LowPart = fad.nFileSizeLow;

    LARGE_INTEGER tm;
    tm.HighPart = fad.ftLastWriteTime.dwHighDateTime;
    tm.LowPart = fad.ftLastWriteTime.dwLowDateTime;

    return PathInfo {
        .type = type,
        .size = static_cast<uint64>(size.QuadPart),
        .lastModified = static_cast<uint64>(tm.QuadPart / 10000000 - 11644473600LL)
    };
}

bool pathCreateDir(const char* path)
{
    return bool(CreateDirectoryA(path, nullptr)); 
}

char* pathGetHomeDir(char* dst, size_t dstSize)
{
    PWSTR homeDir = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &homeDir))) {
        strWideToUtf8(homeDir, dst, (uint32)dstSize);
        CoTaskMemFree(homeDir);
        return dst;
    }
    else {
        ASSERT_MSG(0, "Getting home directory failed");
        return nullptr;
    }
}

char* pathGetCacheDir(char* dst, size_t dstSize, const char* appName)
{
    PWSTR homeDir = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &homeDir))) {
        char homeDirUtf8[CONFIG_MAX_PATH];
        strWideToUtf8(homeDir, homeDirUtf8, sizeof(homeDirUtf8));
        CoTaskMemFree(homeDir);
        pathJoin(dst, dstSize, homeDirUtf8, appName);
        return dst;
    }
    else {
        ASSERT_MSG(0, "Getting LOCALAPPDATA directory failed");
        return nullptr;
    }
}


bool sysIsDebuggerPresent()
{
    return IsDebuggerPresent();
}

void sysWin32PrintToDebugger(const char* text)
{
    OutputDebugStringA(text);
}

char* pathWin32GetFolder(SysWin32Folder folder, char* dst, size_t dstSize)
{
    static const KNOWNFOLDERID folderIds[] = {
        FOLDERID_Documents,
        FOLDERID_Fonts,
        FOLDERID_Downloads,
        FOLDERID_RoamingAppData,
        FOLDERID_LocalAppData,
        FOLDERID_ProgramFiles,
        FOLDERID_System,
        FOLDERID_CommonStartup,
        FOLDERID_Desktop
    };
    static_assert(CountOf(folderIds) == uint32(SysWin32Folder::_Count));

    PWSTR folderPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(folderIds[uint32(folder)], 0, nullptr, &folderPath))) {
        strWideToUtf8(folderPath, dst, (uint32)dstSize);
        CoTaskMemFree(folderPath);
        return dst;
    }
    else {
        ASSERT_MSG(0, "SHGetKnownFolderPath failed");
        return nullptr;
    }
}

//------------------------------------------------------------------------
// Virtual mem
static MemVirtualStats gVMStats;

void* memVirtualReserve(size_t size, MemVirtualFlags flags)
{
    DWORD extraFlags = (flags & MemVirtualFlags::Watch) == MemVirtualFlags::Watch ? MEM_WRITE_WATCH : 0;
    void* ptr = VirtualAlloc(NULL, size, MEM_RESERVE | extraFlags, PAGE_READWRITE);
    if (!ptr) {
        MEMORY_FAIL();
    }

    atomicFetchAdd64(&gVMStats.reservedBytes, size);
    return ptr;
}

void* memVirtualCommit(void* ptr, size_t size)
{
    ASSERT(ptr);
    ptr = VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE);
    if (!ptr) {
        MEMORY_FAIL();
    }

    atomicFetchAdd64(&gVMStats.commitedBytes, size);
    return ptr;
}

void memVirtualDecommit(void* ptr, size_t size)
{
    [[maybe_unused]] BOOL r = VirtualFree(ptr, size, MEM_DECOMMIT);
    ASSERT(r);

    ASSERT(size <= gVMStats.commitedBytes);
    atomicFetchSub64(&gVMStats.commitedBytes, size);
}

void memVirtualRelease(void* ptr, size_t size)
{
    [[maybe_unused]] BOOL r = VirtualFree(ptr, 0, MEM_RELEASE);
    ASSERT(r);

    ASSERT(size <= gVMStats.reservedBytes);
    atomicFetchSub64(&gVMStats.reservedBytes, size);
}

MemVirtualStats memVirtualGetStats()
{
    return gVMStats;
}

//----------------------------------------------------------------------------
// File
struct FileWin
{
    HANDLE      handle;
    FileOpenFlags flags;
    uint64      size;
    uint64      lastModifiedTime;
};
static_assert(sizeof(FileWin) <= sizeof(File));

//------------------------------------------------------------------------
File::File()
{
    FileWin* f = (FileWin*)this->_data;

    f->handle = INVALID_HANDLE_VALUE;
    f->flags = FileOpenFlags::None;
    f->size = 0;
    f->lastModifiedTime = 0;
}

bool File::Open(const char* filepath, FileOpenFlags flags)
{
    ASSERT((flags & (FileOpenFlags::Read|FileOpenFlags::Write)) != (FileOpenFlags::Read|FileOpenFlags::Write));
    ASSERT((flags & (FileOpenFlags::Read|FileOpenFlags::Write)) != FileOpenFlags::None);

    FileWin* f = (FileWin*)this->_data;

    uint32 accessFlags = GENERIC_READ;
    uint32 attrs = FILE_ATTRIBUTE_NORMAL;
    uint32 createFlags = 0;
    uint32 shareFlags = 0;

    if ((flags & FileOpenFlags::Read) == FileOpenFlags::Read) {
        createFlags = OPEN_EXISTING;
        shareFlags |= FILE_SHARE_READ;
    } else if ((flags & FileOpenFlags::Write) == FileOpenFlags::Write) {
        shareFlags |= FILE_SHARE_WRITE;
        accessFlags |= GENERIC_WRITE;
        createFlags |= (flags & FileOpenFlags::Append) == FileOpenFlags::Append ? 
            OPEN_EXISTING : CREATE_ALWAYS;
    }

    if ((flags & FileOpenFlags::NoCache) == FileOpenFlags::NoCache)             attrs |= FILE_FLAG_NO_BUFFERING;
    if ((flags & FileOpenFlags::Writethrough) == FileOpenFlags::Writethrough)   attrs |= FILE_FLAG_WRITE_THROUGH;
    if ((flags & FileOpenFlags::SeqScan) == FileOpenFlags::SeqScan)             attrs |= FILE_FLAG_SEQUENTIAL_SCAN;
    if ((flags & FileOpenFlags::RandomAccess) == FileOpenFlags::RandomAccess)   attrs |= FILE_FLAG_RANDOM_ACCESS;
    if ((flags & FileOpenFlags::Temp) == FileOpenFlags::Temp)                   attrs |= FILE_ATTRIBUTE_TEMPORARY;

    HANDLE hfile = CreateFileA(filepath, accessFlags, shareFlags, NULL, createFlags, attrs, NULL);
    if (hfile == INVALID_HANDLE_VALUE)
        return false;

    f->handle = hfile;
    f->flags = flags;

    BY_HANDLE_FILE_INFORMATION fileInfo {};
    GetFileInformationByHandle(hfile, &fileInfo);
    f->size = (flags & (FileOpenFlags::Read|FileOpenFlags::Append)) != FileOpenFlags::None ? 
        (uint64(fileInfo.nFileSizeHigh)<<32 | uint64(fileInfo.nFileSizeLow)) : 0;
    f->lastModifiedTime = uint64(fileInfo.ftLastAccessTime.dwHighDateTime)<<32 | uint64(fileInfo.ftLastAccessTime.dwLowDateTime);

    return true;

}

void File::Close()
{
    FileWin* f = (FileWin*)this->_data;

    if (f->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(f->handle);
        f->handle = INVALID_HANDLE_VALUE;
    }
}

size_t File::Read(void* dst, size_t size)
{
    FileWin* f = (FileWin*)this->_data;
    ASSERT(f->handle != INVALID_HANDLE_VALUE);

    if ((f->flags & FileOpenFlags::NoCache) == FileOpenFlags::NoCache) {
        static size_t pagesz = 0;
        if (pagesz == 0) {
            pagesz = sysGetPageSize();
        }
        ASSERT_ALWAYS((uintptr_t)dst % pagesz == 0, "buffers must be aligned with NoCache flag");
    }

    DWORD bytesRead;
    if (!ReadFile(f->handle, dst, (DWORD)size, &bytesRead, NULL))
        return SIZE_MAX;

    return size_t(bytesRead);
}

size_t File::Write(const void* src, size_t size)
{
    FileWin* f = (FileWin*)this->_data;
    ASSERT(f->handle != INVALID_HANDLE_VALUE);

    DWORD bytesWritten;
    if (!WriteFile(f->handle, src, (DWORD)size, &bytesWritten, NULL))
        return SIZE_MAX;
    f->size += bytesWritten;

    return bytesWritten;
}

size_t File::Seek(size_t offset, FileSeekMode mode)
{
    FileWin* f = (FileWin*)this->_data;
    ASSERT(f->handle != INVALID_HANDLE_VALUE);

    DWORD moveMethod = 0;
    switch (mode) {
    case FileSeekMode::Start:
        moveMethod = FILE_BEGIN;
        break;
    case FileSeekMode::Current:
        moveMethod = FILE_CURRENT;
        break;
    case FileSeekMode::End:
        ASSERT(offset <= f->size);
        moveMethod = FILE_END;
        break;
    }

    LARGE_INTEGER largeOff;
    LARGE_INTEGER largeRet;
    largeOff.QuadPart = (LONGLONG)offset;

    if (SetFilePointerEx(f->handle, largeOff, &largeRet, moveMethod))
        return (int64_t)largeRet.QuadPart;

    return SIZE_MAX;
}

size_t File::GetSize() const
{
    FileWin* f = (FileWin*)this->_data;
    return size_t(f->size);    
}

uint64 File::GetLastModified() const
{
    FileWin* f = (FileWin*)this->_data;
    return f->lastModifiedTime;
}

bool File::IsOpen() const
{
    FileWin* f = (FileWin*)this->_data;
    return f->handle != INVALID_HANDLE_VALUE;
}

//----------------------------------------------------------------------------------------------------------------------
// SocketTCP
namespace _private
{
    static bool gSocketInitialized;
    static void socketInitializeWin32()
    {
        if (!gSocketInitialized) {
            logDebug("SocketTCP: Initialize");
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(1, 0), &wsaData) != 0) {
                ASSERT_ALWAYS(false, "Windows sockets initialization failed");
                return;
            }
        
            gSocketInitialized = true;
        }
    }

    static SocketErrorCode socketTranslatePlatformErrorCode()
    {
        int errorCode = WSAGetLastError();
        switch (errorCode) {
        case WSAEADDRINUSE:     return SocketErrorCode::AddressInUse;
        case WSAECONNREFUSED:   return SocketErrorCode::ConnectionRefused;
        case WSAEISCONN:        return SocketErrorCode::AlreadyConnected;
        case WSAENETUNREACH: 
        case WSAENETDOWN:
        case WSAEHOSTUNREACH:   return SocketErrorCode::HostUnreachable;
        case WSAETIMEDOUT:      return SocketErrorCode::Timeout;
        case WSAECONNRESET:
        case WSAEINTR:
        case WSAENETRESET:      return SocketErrorCode::ConnectionReset;
        case WSAEADDRNOTAVAIL:  return SocketErrorCode::AddressNotAvailable;
        case WSAEAFNOSUPPORT:   return SocketErrorCode::AddressUnsupported;
        case WSAESHUTDOWN:      return SocketErrorCode::SocketShutdown;
        case WSAEMSGSIZE:       return SocketErrorCode::MessageTooLarge;
        case WSAENOTCONN:       return SocketErrorCode::NotConnected;
        default:                ASSERT_MSG(0, "Unknown socket error: %d", WSAGetLastError()); return SocketErrorCode::Unknown;
        }
    }

    bool socketParseUrl(const char* url, char* address, size_t addressSize, char* port, size_t portSize, const char** pResource = nullptr);
} // namespace _private

// Implemented in System.cpp

#define SOCKET_INVALID INVALID_SOCKET
SocketTCP::SocketTCP() :
    s(SOCKET_INVALID),
    errCode(SocketErrorCode::None),
    live(0)
{
}

void SocketTCP::Close()
{
    if (this->s != SOCKET_INVALID) {
        if (this->live)
            shutdown(this->s, SD_BOTH);
        closesocket(this->s);

        this->s = SOCKET_INVALID;
        this->errCode = SocketErrorCode::None;
        this->live = false;
    }
}

SocketTCP SocketTCP::CreateListener()
{
    _private::socketInitializeWin32();

    SocketTCP sock;

    sock.s = socket(AF_INET, SOCK_STREAM, 0);
    if (sock.s == SOCKET_INVALID) {
        sock.errCode = _private::socketTranslatePlatformErrorCode();
        logError("SocketTCP: Opening the socket failed");
        return sock;
    }
    return sock;    
}

bool SocketTCP::Listen(uint16 port, uint32 maxConnections)
{
    ASSERT(IsValid());

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(this->s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        this->errCode = _private::socketTranslatePlatformErrorCode();
        logError("SocketTCP: failed binding the socket to port: %d", port);
        return false;
    }

    logVerbose("SocketTCP: Listening on port '%d' for incoming connections ...", port);
    int _maxConnections = maxConnections > INT32_MAX ? INT32_MAX : static_cast<int>(maxConnections);
    bool success = listen(this->s, _maxConnections) >= 0;
    
    if (!success) 
        this->errCode = _private::socketTranslatePlatformErrorCode();
    else
        this->live = true;

    return success;
}

SocketTCP SocketTCP::Accept(char* clientUrl, uint32 clientUrlSize)
{
    ASSERT(IsValid());

    SocketTCP newSock;

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    newSock.s = accept(this->s, (struct sockaddr*)&addr, &addrlen);
    if (this->live && newSock.s == SOCKET_INVALID) {
        newSock.errCode = _private::socketTranslatePlatformErrorCode();
        logError("SocketTCP: failed to accept the new socket");
        return newSock;
    }

    if (clientUrl && clientUrlSize) {
        char ip[256];
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        uint16 port = htons(addr.sin_port);
        
        strPrintFmt(clientUrl, clientUrlSize, "%s:%d", ip, port);
    }

    newSock.live = true;
    return newSock;
}

SocketTCP SocketTCP::Connect(const char* url)
{
    _private::socketInitializeWin32();

    SocketTCP sock;

    char address[256];
    char port[16];
    if (!_private::socketParseUrl(url, address, sizeof(address), port, sizeof(port))) {
        logError("SocketTCP: failed parsing the url: %s", url);
        return sock;
    }

    struct addrinfo hints;
    memset(&hints, 0x0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* addri = nullptr;
    if (getaddrinfo(address, port, &hints, &addri) != 0) {
        logError("SocketTCP: failed to resolve url: %s", url);
        return sock;
    }

    sock.s = socket(addri->ai_family, addri->ai_socktype, addri->ai_protocol);
    if (sock.s == SOCKET_INVALID) {
        freeaddrinfo(addri);
        logError("SocketTCP: failed to create socket");
        return sock;
    }

    if (connect(sock.s, addri->ai_addr, (int)addri->ai_addrlen) == -1) {
        freeaddrinfo(addri);
        sock.errCode = _private::socketTranslatePlatformErrorCode();
        logError("SocketTCP: failed to connect to url: %s", url);
        sock.Close();
        return sock;
    }

    freeaddrinfo(addri);

    sock.live = true;
    return sock;
}

uint32 SocketTCP::Write(const void* src, uint32 size)
{
    ASSERT(IsValid());
    ASSERT(this->live);
    uint32 totalBytesSent = 0;

    while (size > 0) {
        int bytesSent = send(this->s, reinterpret_cast<const char*>(src) + totalBytesSent, size, 0);
        if (bytesSent == 0) {
            break;
        }
        else if (bytesSent == -1) {
            this->errCode = _private::socketTranslatePlatformErrorCode();
            if (this->errCode == SocketErrorCode::SocketShutdown ||
                this->errCode == SocketErrorCode::NotConnected)
            {
                logDebug("SocketTCP: socket connection closed forcefully by the peer");
                this->live = false;
            }
            return UINT32_MAX;
        }

        totalBytesSent += static_cast<uint32>(bytesSent);
        size -= static_cast<uint32>(bytesSent);
    }

    return totalBytesSent;
}

uint32 SocketTCP::Read(void* dst, uint32 dstSize)
{
    ASSERT(IsValid());
    ASSERT(this->live);

    int bytesRecv = recv(this->s, reinterpret_cast<char*>(dst), dstSize, 0);
    if (bytesRecv == -1) {
        this->errCode = _private::socketTranslatePlatformErrorCode();
        if (this->errCode == SocketErrorCode::SocketShutdown ||
            this->errCode == SocketErrorCode::NotConnected)
        {
            logDebug("SocketTCP: socket connection closed forcefully by the peer");
            this->live = false;
        }
        return UINT32_MAX;
    }

    return static_cast<uint32>(bytesRecv);
}

bool SocketTCP::IsValid() const
{
    return this->s != SOCKET_INVALID;
}

#endif // PLATFORM_WINDOWS
