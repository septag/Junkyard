#include "System.h"

#if PLATFORM_WINDOWS
#include "String.h"
#include "Memory.h"
#include "Atomic.h"
#include "IncludeWin.h"
#include "TracyHelper.h"

#include <limits.h>     // LONG_MAX
#include <synchapi.h>   // InitializeCriticalSectionAndSpinCount, InitializeCriticalSection, ...
#include <sysinfoapi.h> // GetPhysicallyInstalledSystemMemory
#include <intrin.h>     // __cpuid
#include <tlhelp32.h>   // CreateToolhelp32Snapshot

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
    memset(this->data, 0x0, sizeof(Thread));
}

bool Thread::Start(const ThreadDesc& desc)
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(this->data);
    ASSERT(thrd->handle == nullptr && !thrd->running);

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
    if (thrd->handle) {
        ASSERT_MSG(thrd->running, "Thread is not running!");

        atomicStore32Explicit(&thrd->stopped, 1, AtomicMemoryOrder::Release);
        WaitForSingleObject(thrd->handle, INFINITE);
        GetExitCodeThread(thrd->handle, &exitCode);
        CloseHandle(thrd->handle);
        thrd->sem.Release();

        thrd->handle = nullptr;
        thrd->running = false;
    }
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
    ASSERT_ALWAYS(_sem->handle != nullptr, "Failed to create semaphore");
}

void Semaphore::Release()
{
    SemaphoreImpl* _sem = reinterpret_cast<SemaphoreImpl*>(this->data);
    CloseHandle(_sem->handle);
}

void Semaphore::Post(uint32 count)
{
    SemaphoreImpl* _sem = reinterpret_cast<SemaphoreImpl*>(this->data);
    ReleaseSemaphore(_sem->handle, count, nullptr);
}

bool Semaphore::Wait(uint32 msecs)
{
    SemaphoreImpl* _sem = reinterpret_cast<SemaphoreImpl*>(this->data);
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

void* sysWin32RunProcess(int argc, const char* argv[])
{
    ASSERT(argc > 0);

    STARTUPINFOA si { sizeof(STARTUPINFOA) };
    PROCESS_INFORMATION pi {};

    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;

    // trim any quote/double-quotes from argv[0]
    char procPath[kMaxPath];
    strTrim(procPath, sizeof(procPath), argv[0], '\'');
    strTrim(procPath, sizeof(procPath), procPath, '"');
    strReplaceChar(procPath, sizeof(procPath), '/', '\\');

    // join argv into a single command-line with space as separator
    uint32 totalLen = strLen(procPath) + 1;
    for (int i = 1; i < argc; i++)
        totalLen += strLen(argv[i]) + 1;

    MemTempAllocator tmpAlloc;
    char* cmdline = tmpAlloc.MallocTyped<char>(totalLen + 1);
    cmdline[0] = 0;

    char* _cmdline = strCopy(cmdline, totalLen + 1, procPath);
    totalLen -= strLen(procPath);
    if (argc > 1)
        _cmdline = strConcat(_cmdline, totalLen + 1, " ");

    for (int i = 1; i < argc; i++) {
        _cmdline = strConcat(_cmdline, totalLen + 1, argv[i]);
        totalLen -= strLen(argv[i]);
        if (i < argc - 1) {
            _cmdline = strConcat(_cmdline, totalLen + 1, " ");
            totalLen -= 1;
        }
    }

    bool ok = !!CreateProcessA(procPath, cmdline, nullptr, nullptr, false, CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi);
    return ok ? pi.hProcess : nullptr;
}

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

bool sysIsDebuggerPresent()
{
    return IsDebuggerPresent();
}

void sysWin32PrintToDebugger(const char* text)
{
    OutputDebugStringA(text);
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

void sysWin32SetConsoleColor(void* handle, SysWin32ConsoleColor color)
{
    SetConsoleTextAttribute(handle, uint16(color));
}

#endif // PLATFORM_WINDOWS