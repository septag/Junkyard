#include "System.h"

#if PLATFORM_WINDOWS
#include "StringUtil.h"
#include "Atomic.h"
#include "IncludeWin.h"
#include "TracyHelper.h"
#include "Log.h"
#include "Arrays.h"
#include "Allocators.h"

#include <limits.h>     // LONG_MAX
#include <synchapi.h>   // InitializeCriticalSectionAndSpinCount, InitializeCriticalSection, ...
#include <sysinfoapi.h> // GetPhysicallyInstalledSystemMemory
#include <intrin.h>     // __cpuid
#include <tlhelp32.h>   // CreateToolhelp32Snapshot
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ShlObj.h>     // SH family of functions (Shell32)

#pragma comment(lib, "ws2_32.lib")

namespace _limits 
{
    const uint32 SYS_MAX_CORES = 128;
}

//
//    ██╗    ██╗██╗███╗   ██╗██████╗ ██████╗      █████╗ ██████╗ ██╗
//    ██║    ██║██║████╗  ██║╚════██╗╚════██╗    ██╔══██╗██╔══██╗██║
//    ██║ █╗ ██║██║██╔██╗ ██║ █████╔╝ █████╔╝    ███████║██████╔╝██║
//    ██║███╗██║██║██║╚██╗██║ ╚═══██╗██╔═══╝     ██╔══██║██╔═══╝ ██║
//    ╚███╔███╔╝██║██║ ╚████║██████╔╝███████╗    ██║  ██║██║     ██║
//     ╚══╝╚══╝ ╚═╝╚═╝  ╚═══╝╚═════╝ ╚══════╝    ╚═╝  ╚═╝╚═╝     ╚═╝

// Only load "AdvApi32.dll" if any of these functions are used
struct AdvApi32
{
    typedef LSTATUS (*RegGetValueAFn)(
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

    typedef BOOL (*OpenProcessTokenFn)(
        _In_ HANDLE ProcessHandle,
        _In_ DWORD DesiredAccess,
        _Outptr_ PHANDLE TokenHandle
    );

    typedef BOOL (*AdjustTokenPrivilegesFn)(
        _In_ HANDLE TokenHandle,
        _In_ BOOL DisableAllPrivileges,
        _In_opt_ PTOKEN_PRIVILEGES NewState,
        _In_ DWORD BufferLength,
        _Out_writes_bytes_to_opt_(BufferLength,*ReturnLength) PTOKEN_PRIVILEGES PreviousState,
        _Out_opt_ PDWORD ReturnLength
    );

    typedef BOOL (*LookupPrivilegeValueAFn)(
        _In_opt_ LPCSTR lpSystemName,
        _In_     LPCSTR lpName,
        _Out_    PLUID   lpLuid
    );

    HANDLE dll;
    RegGetValueAFn RegGetValueA;
    OpenProcessTokenFn OpenProcessToken;
    AdjustTokenPrivilegesFn AdjustTokenPrivileges;
    LookupPrivilegeValueAFn LookupPrivilegeValueA;
};

struct Ole32
{
    typedef int (*StringFromGUID2Fn)(
        _In_ REFGUID rguid,
        _Out_writes_to_(cchMax, return) LPOLESTR lpsz,
        _In_ int cchMax
    );

    typedef HRESULT (*CoCreateGuidFn)(
        _Out_ GUID  FAR * pguid
    );

    typedef void (*CoTaskMemFreeFn)(
        _Frees_ptr_opt_ LPVOID pv
    );

    typedef HRESULT (*CLSIDFromStringFn)(
        _In_ LPCOLESTR lpsz,
        _Out_ LPCLSID pclsid
    );

    HANDLE dll;
    StringFromGUID2Fn StringFromGUID2;
    CoCreateGuidFn CoCreateGuid;
    CoTaskMemFreeFn CoTaskMemFree;
    CLSIDFromStringFn CLSIDFromString;
};

struct Shell32
{
    typedef HINSTANCE (*ShellExecuteAFn)(_In_opt_ HWND hwnd, _In_opt_ LPCSTR lpOperation, _In_ LPCSTR lpFile, _In_opt_ LPCSTR lpParameters,
                                       _In_opt_ LPCSTR lpDirectory, _In_ INT nShowCmd);
    typedef HRESULT (*SHGetKnownFolderPathFn)(_In_ REFKNOWNFOLDERID rfid,
                            _In_ DWORD /* KNOWN_FOLDER_FLAG */ dwFlags,
                            _In_opt_ HANDLE hToken,
                            _Outptr_ PWSTR *ppszPath); // free *ppszPath with CoTaskMemFree

    HANDLE dll;
    ShellExecuteAFn ShellExecuteA;
    SHGetKnownFolderPathFn SHGetKnownFolderPath;
};

//----------------------------------------------------------------------------------------------------------------------
// AdvApi32
static AdvApi32 gAdvApi32;

static void sysLoadAdvApi32()
{
    if (gAdvApi32.dll == nullptr) {
        gAdvApi32.dll = (HANDLE)OS::LoadDLL("Advapi32.dll");
        ASSERT_ALWAYS(gAdvApi32.dll, "Could not load system DLL: Advapi32.dll");

        gAdvApi32.RegGetValueA = (AdvApi32::RegGetValueAFn)OS::GetSymbolAddress(gAdvApi32.dll, "RegGetValueA");
        gAdvApi32.OpenProcessToken = (AdvApi32::OpenProcessTokenFn)OS::GetSymbolAddress(gAdvApi32.dll, "OpenProcessToken");
        gAdvApi32.AdjustTokenPrivileges = (AdvApi32::AdjustTokenPrivilegesFn)OS::GetSymbolAddress(gAdvApi32.dll, "AdjustTokenPrivileges");
        gAdvApi32.LookupPrivilegeValueA = (AdvApi32::LookupPrivilegeValueAFn)OS::GetSymbolAddress(gAdvApi32.dll, "LookupPrivilegeValueA");

        ASSERT_ALWAYS(gAdvApi32.RegGetValueA && gAdvApi32.OpenProcessToken && gAdvApi32.AdjustTokenPrivileges && gAdvApi32.LookupPrivilegeValueA,
                      "Loading AdvApi32 API failed");
    }
}

//----------------------------------------------------------------------------------------------------------------------
// Ole32
static Ole32 gOle32;

static void sysLoadOle32()
{
    if (gOle32.dll == nullptr) {
        gOle32.dll = (HANDLE)OS::LoadDLL("Ole32.dll");
        ASSERT_ALWAYS(gOle32.dll, "Could not load system DLL: Ole32.dll");

        gOle32.StringFromGUID2 = (Ole32::StringFromGUID2Fn)OS::GetSymbolAddress(gOle32.dll, "StringFromGUID2");
        gOle32.CoCreateGuid = (Ole32::CoCreateGuidFn)OS::GetSymbolAddress(gOle32.dll, "CoCreateGuid");
        gOle32.CoTaskMemFree = (Ole32::CoTaskMemFreeFn)OS::GetSymbolAddress(gOle32.dll, "CoTaskMemFree");
        gOle32.CLSIDFromString = (Ole32::CLSIDFromStringFn)OS::GetSymbolAddress(gOle32.dll, "CLSIDFromString");

        ASSERT_ALWAYS(gOle32.StringFromGUID2 && gOle32.CoCreateGuid && gOle32.CoTaskMemFree && gOle32.CLSIDFromString,
                      "Loading Ole32 API failed");
    }
}

//----------------------------------------------------------------------------------------------------------------------
// Shell32
static Shell32 gShell32;

static void sysLoadShell32()
{
    if (gShell32.dll == nullptr) {
        gShell32.dll = (HANDLE)OS::LoadDLL("Shell32.dll");
        ASSERT_ALWAYS(gShell32.dll, "Could not load system DLL: Shell32.dll");

        gShell32.ShellExecuteA = (Shell32::ShellExecuteAFn)OS::GetSymbolAddress(gShell32.dll, "ShellExecuteA");
        gShell32.SHGetKnownFolderPath = (Shell32::SHGetKnownFolderPathFn)OS::GetSymbolAddress(gShell32.dll, "SHGetKnownFolderPath");

        ASSERT_ALWAYS(gShell32.ShellExecuteA && gShell32.SHGetKnownFolderPath, "Loading Shell32 API failed");
    }
}

//    ████████╗██╗  ██╗██████╗ ███████╗ █████╗ ██████╗ 
//    ╚══██╔══╝██║  ██║██╔══██╗██╔════╝██╔══██╗██╔══██╗
//       ██║   ███████║██████╔╝█████╗  ███████║██║  ██║
//       ██║   ██╔══██║██╔══██╗██╔══╝  ██╔══██║██║  ██║
//       ██║   ██║  ██║██║  ██║███████╗██║  ██║██████╔╝
//       ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝╚═════╝ 

struct ThreadImpl
{
    Semaphore sem;
    ThreadEntryFunc threadFn;
    HANDLE handle;
    void* userData;
    size_t stackSize;
    char name[32];
    DWORD tId;
    AtomicUint32 running;
    bool init;
};
static_assert(sizeof(ThreadImpl) <= sizeof(Thread), "Thread size mismatch");

static DWORD WINAPI threadStubFn(LPVOID arg)
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(arg);
    thrd->tId = GetCurrentThreadId();
    Thread::SetCurrentThreadName(thrd->name);

    ASSERT(thrd->threadFn);
    Atomic::StoreExplicit(&thrd->running, 1, AtomicMemoryOrder::Release);
    thrd->sem.Post();
    DWORD r = static_cast<DWORD>(thrd->threadFn(thrd->userData));
    Atomic::StoreExplicit(&thrd->running, 0, AtomicMemoryOrder::Release);

    return r;
}

Thread::Thread()
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(mData);
    thrd->threadFn = nullptr;
    thrd->handle = nullptr;
    thrd->userData = nullptr;
    thrd->stackSize = 0;
    thrd->name[0] = 0;
    thrd->tId = 0;
    thrd->running = 0;
    thrd->init = false;
}

bool Thread::Start(const ThreadDesc& desc)
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(mData);
    ASSERT(thrd->handle == nullptr && !thrd->init);

    thrd->sem.Initialize();
    thrd->threadFn = desc.entryFn;
    thrd->userData = desc.userData;
    thrd->stackSize = Max<size_t>(desc.stackSize, 64*SIZE_KB);
    strCopy(thrd->name, sizeof(thrd->name), desc.name ? desc.name : "");

    thrd->handle = CreateThread(nullptr, thrd->stackSize, (LPTHREAD_START_ROUTINE)threadStubFn, thrd, 0, nullptr);
    if (thrd->handle == nullptr) {
        thrd->sem.Release();
        return false;
    }
    ASSERT_ALWAYS(thrd->handle != nullptr, "CreateThread failed");

    thrd->sem.Wait();   // Ensure that thread callback is init
    thrd->init = true;

    _private::CountersAddThread(thrd->stackSize);

    return true;
}

int Thread::Stop()
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(mData);
    DWORD exitCode = 0;
    if (thrd->handle != nullptr) {
        ASSERT_MSG(thrd->init, "Thread is not init!");

        WaitForSingleObject(thrd->handle, INFINITE);
        GetExitCodeThread(thrd->handle, &exitCode);
        CloseHandle(thrd->handle);
        thrd->sem.Release();

        thrd->handle = nullptr;
    }

    thrd->init = false;

    _private::CountersRemoveThread(thrd->stackSize);

    return static_cast<int>(exitCode);
}

bool Thread::IsRunning()
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(mData);
    return Atomic::LoadExplicit(&thrd->running, AtomicMemoryOrder::Acquire) == 1;
}

void Thread::SetPriority(ThreadPriority prio)
{
    ThreadImpl* thrd = reinterpret_cast<ThreadImpl*>(mData);

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

void Thread::SwitchContext()
{
    SwitchToThread();
}

uint32 Thread::GetCurrentId()
{
    return GetCurrentThreadId();
}

void Thread::Sleep(uint32 msecs)
{
    ::Sleep((DWORD)msecs);
}

void Thread::SetCurrentThreadPriority(ThreadPriority prio)
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

void Thread::SetCurrentThreadName(const char* name)
{
    wchar_t namew[32];
    strUt8ToWide(name, namew, sizeof(namew));
    SetThreadDescription(GetCurrentThread(), namew);

    #if TRACY_ENABLE
    TracyCSetThreadName(name);
    #endif
}

void Thread::GetCurrentThreadName(char* nameOut, uint32 nameSize)
{
    PWSTR namew;
    if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &namew)))
        strWideToUtf8(namew, nameOut, nameSize);
    else 
        nameOut[0] = 0;
}

//    ███╗   ███╗██╗   ██╗████████╗███████╗██╗  ██╗
//    ████╗ ████║██║   ██║╚══██╔══╝██╔════╝╚██╗██╔╝
//    ██╔████╔██║██║   ██║   ██║   █████╗   ╚███╔╝ 
//    ██║╚██╔╝██║██║   ██║   ██║   ██╔══╝   ██╔██╗ 
//    ██║ ╚═╝ ██║╚██████╔╝   ██║   ███████╗██╔╝ ██╗
//    ╚═╝     ╚═╝ ╚═════╝    ╚═╝   ╚══════╝╚═╝  ╚═╝

struct MutexImpl 
{
    CRITICAL_SECTION handle;
};
static_assert(sizeof(MutexImpl) <= sizeof(Mutex), "Mutex size mismatch");

void Mutex::Initialize(uint32 spinCount)
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(mData);
    [[maybe_unused]] BOOL r = InitializeCriticalSectionAndSpinCount(&_m->handle, spinCount);
    ASSERT_ALWAYS(r, "InitializeCriticalSection failed");

    _private::CountersAddMutex();
}

void Mutex::Release()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(mData);
    DeleteCriticalSection(&_m->handle);

    _private::CountersRemoveMutex();
}

void Mutex::Enter()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(mData);
    EnterCriticalSection(&_m->handle);
}

void Mutex::Exit()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(mData);
    LeaveCriticalSection(&_m->handle);
}

bool Mutex::TryEnter()
{
    MutexImpl* _m = reinterpret_cast<MutexImpl*>(mData);
    return TryEnterCriticalSection(&_m->handle) == TRUE;
}

//    ██████╗ ███████╗ █████╗ ██████╗ ██╗    ██╗██████╗ ██╗████████╗███████╗    ███╗   ███╗██╗   ██╗████████╗███████╗██╗  ██╗
//    ██╔══██╗██╔════╝██╔══██╗██╔══██╗██║    ██║██╔══██╗██║╚══██╔══╝██╔════╝    ████╗ ████║██║   ██║╚══██╔══╝██╔════╝╚██╗██╔╝
//    ██████╔╝█████╗  ███████║██║  ██║██║ █╗ ██║██████╔╝██║   ██║   █████╗      ██╔████╔██║██║   ██║   ██║   █████╗   ╚███╔╝ 
//    ██╔══██╗██╔══╝  ██╔══██║██║  ██║██║███╗██║██╔══██╗██║   ██║   ██╔══╝      ██║╚██╔╝██║██║   ██║   ██║   ██╔══╝   ██╔██╗ 
//    ██║  ██║███████╗██║  ██║██████╔╝╚███╔███╔╝██║  ██║██║   ██║   ███████╗    ██║ ╚═╝ ██║╚██████╔╝   ██║   ███████╗██╔╝ ██╗
//    ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝╚═════╝  ╚══╝╚══╝ ╚═╝  ╚═╝╚═╝   ╚═╝   ╚══════╝    ╚═╝     ╚═╝ ╚═════╝    ╚═╝   ╚══════╝╚═╝  ╚═╝

struct ReadWriteMutexImpl
{
    SRWLOCK handle;
};
static_assert(sizeof(ReadWriteMutexImpl) < sizeof(ReadWriteMutex), "ReadWriteMutex mismatch");

void ReadWriteMutex::Initialize()
{
    ReadWriteMutexImpl* m = reinterpret_cast<ReadWriteMutexImpl*>(mData);
    InitializeSRWLock(&m->handle);
}

void ReadWriteMutex::Release()
{
}

bool ReadWriteMutex::TryRead()
{
    ReadWriteMutexImpl* m = reinterpret_cast<ReadWriteMutexImpl*>(mData);
    return TryAcquireSRWLockShared(&m->handle) != 0;
}

bool ReadWriteMutex::TryWrite()
{
    ReadWriteMutexImpl* m = reinterpret_cast<ReadWriteMutexImpl*>(mData);
    return TryAcquireSRWLockExclusive(&m->handle) != 0;
}

void ReadWriteMutex::EnterRead()
{
    ReadWriteMutexImpl* m = reinterpret_cast<ReadWriteMutexImpl*>(mData);
    AcquireSRWLockShared(&m->handle);
}

void ReadWriteMutex::ExitRead()
{
    ReadWriteMutexImpl* m = reinterpret_cast<ReadWriteMutexImpl*>(mData);
    ReleaseSRWLockShared(&m->handle);
}

void ReadWriteMutex::EnterWrite()
{
    ReadWriteMutexImpl* m = reinterpret_cast<ReadWriteMutexImpl*>(mData);
    AcquireSRWLockExclusive(&m->handle);
}

void ReadWriteMutex::ExitWrite()
{
    ReadWriteMutexImpl* m = reinterpret_cast<ReadWriteMutexImpl*>(mData);
    ReleaseSRWLockExclusive(&m->handle);
}


//    ███████╗███████╗███╗   ███╗ █████╗ ██████╗ ██╗  ██╗ ██████╗ ██████╗ ███████╗
//    ██╔════╝██╔════╝████╗ ████║██╔══██╗██╔══██╗██║  ██║██╔═══██╗██╔══██╗██╔════╝
//    ███████╗█████╗  ██╔████╔██║███████║██████╔╝███████║██║   ██║██████╔╝█████╗  
//    ╚════██║██╔══╝  ██║╚██╔╝██║██╔══██║██╔═══╝ ██╔══██║██║   ██║██╔══██╗██╔══╝  
//    ███████║███████╗██║ ╚═╝ ██║██║  ██║██║     ██║  ██║╚██████╔╝██║  ██║███████╗
//    ╚══════╝╚══════╝╚═╝     ╚═╝╚═╝  ╚═╝╚═╝     ╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═╝╚══════╝
struct SemaphoreImpl 
{
    HANDLE handle;
};
static_assert(sizeof(SemaphoreImpl) <= sizeof(Semaphore), "Sempahore size mismatch");

void Semaphore::Initialize()
{
    SemaphoreImpl* _sem = reinterpret_cast<SemaphoreImpl*>(mData);
    _sem->handle = CreateSemaphoreA(nullptr, 0, LONG_MAX, nullptr);
    ASSERT_ALWAYS(_sem->handle != INVALID_HANDLE_VALUE, "Failed to create semaphore");

    _private::CountersAddSemaphore();
}

void Semaphore::Release()
{
    SemaphoreImpl* _sem = reinterpret_cast<SemaphoreImpl*>(mData);
    if (_sem->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(_sem->handle);
        _sem->handle = INVALID_HANDLE_VALUE;

        _private::CountersRemoveSemaphore();
    }
}

void Semaphore::Post(uint32 count)
{
    SemaphoreImpl* _sem = reinterpret_cast<SemaphoreImpl*>(mData);
    ASSERT(_sem->handle != INVALID_HANDLE_VALUE);
    ReleaseSemaphore(_sem->handle, count, nullptr);
}

bool Semaphore::Wait(uint32 msecs)
{
    SemaphoreImpl* _sem = reinterpret_cast<SemaphoreImpl*>(mData);
    ASSERT(_sem->handle != INVALID_HANDLE_VALUE);
    return WaitForSingleObject(_sem->handle, (DWORD)msecs) == WAIT_OBJECT_0;
}

//    ███████╗██╗ ██████╗ ███╗   ██╗ █████╗ ██╗     
//    ██╔════╝██║██╔════╝ ████╗  ██║██╔══██╗██║     
//    ███████╗██║██║  ███╗██╔██╗ ██║███████║██║     
//    ╚════██║██║██║   ██║██║╚██╗██║██╔══██║██║     
//    ███████║██║╚██████╔╝██║ ╚████║██║  ██║███████╗
//    ╚══════╝╚═╝ ╚═════╝ ╚═╝  ╚═══╝╚═╝  ╚═╝╚══════╝
// https://github.com/mattiasgustavsson/libs/blob/master/thread.h

struct SignalImpl 
{
    CRITICAL_SECTION mutex;
    CONDITION_VARIABLE cond;
    int value;
};
static_assert(sizeof(SignalImpl) <= sizeof(Signal), "Signal size mismatch");

void Signal::Initialize()
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(mData);
    [[maybe_unused]] BOOL r = InitializeCriticalSectionAndSpinCount(&_sig->mutex, 32);
    ASSERT_ALWAYS(r, "InitializeCriticalSectionAndSpinCount failed");
    InitializeConditionVariable(&_sig->cond);
    _sig->value = 0;

    _private::CountersAddSignal();
}

void Signal::Release()
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(mData);
    DeleteCriticalSection(&_sig->mutex);

    _private::CountersRemoveSignal();
}

void Signal::Raise()
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(mData);
    WakeConditionVariable(&_sig->cond);
}

void Signal::RaiseAll()
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(mData);
    WakeAllConditionVariable(&_sig->cond);
}

bool Signal::Wait(uint32 msecs)
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(mData);

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
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(mData);
    EnterCriticalSection(&_sig->mutex);
    --_sig->value;
    LeaveCriticalSection(&_sig->mutex);
}

void Signal::Increment()
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(mData);
    EnterCriticalSection(&_sig->mutex);
    ++_sig->value;
    LeaveCriticalSection(&_sig->mutex);
}

bool Signal::WaitOnCondition(bool(*condFn)(int value, int reference), int reference, uint32 msecs)
{
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(mData);

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
    SignalImpl* _sig = reinterpret_cast<SignalImpl*>(mData);

    EnterCriticalSection(&_sig->mutex);
    _sig->value = value;
    LeaveCriticalSection(&_sig->mutex);
}

//    ████████╗██╗███╗   ███╗███████╗██████╗ 
//    ╚══██╔══╝██║████╗ ████║██╔════╝██╔══██╗
//       ██║   ██║██╔████╔██║█████╗  ██████╔╝
//       ██║   ██║██║╚██╔╝██║██╔══╝  ██╔══██╗
//       ██║   ██║██║ ╚═╝ ██║███████╗██║  ██║
//       ╚═╝   ╚═╝╚═╝     ╚═╝╚══════╝╚═╝  ╚═╝
struct TimerState
{
    bool init;
    LARGE_INTEGER freq;
    LARGE_INTEGER start;
};
static TimerState gTimer;

// Tip by johaness spohr
// https://gist.github.com/jspohr/3dc4f00033d79ec5bdaf67bc46c813e3
FORCE_INLINE int64 timerInt64MulDiv(int64 value, int64 numer, int64 denom) 
{
    int64 q = value / denom;
    int64 r = value % denom;
    return q * numer + r * numer / denom;
}

void _private::InitializeTimer() 
{
    gTimer.init = true;
    
    QueryPerformanceFrequency(&gTimer.freq);
    QueryPerformanceCounter(&gTimer.start);
}

uint64 Timer::GetTicks() 
{
    ASSERT_MSG(gTimer.init, "Timer not initialized. call timerInitialize()");
    
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return timerInt64MulDiv(li.QuadPart - gTimer.start.QuadPart, 1000000000, gTimer.freq.QuadPart);
}

//    ██╗   ██╗██╗   ██╗██╗██████╗ 
//    ██║   ██║██║   ██║██║██╔══██╗
//    ██║   ██║██║   ██║██║██║  ██║
//    ██║   ██║██║   ██║██║██║  ██║
//    ╚██████╔╝╚██████╔╝██║██████╔╝
//     ╚═════╝  ╚═════╝ ╚═╝╚═════╝ 
struct UUIDImpl
{
    GUID guid;
};
static_assert(sizeof(UUIDImpl) <= sizeof(UniqueID), "UUID size mismatch");

bool UniqueID::operator==(const UniqueID& uuid) const
{
    return memcmp(data, uuid.data, sizeof(UUIDImpl)) == 0;
}

bool uuidGenerate(UniqueID* _uuid)
{
    sysLoadOle32();

    UUIDImpl* uuid = reinterpret_cast<UUIDImpl*>(_uuid);
    if (gOle32.CoCreateGuid(&uuid->guid) != S_OK)
    return false;

    return true;
}

bool uuidToString(const UniqueID& _uuid, char* str, uint32 size)
{
    sysLoadOle32();

    const UUIDImpl& uuid = reinterpret_cast<const UUIDImpl&>(_uuid);
    wchar_t guidStr[39];

    gOle32.StringFromGUID2(uuid.guid, guidStr, CountOf(guidStr));
    if (WideCharToMultiByte(CP_UTF8, 0, guidStr, -1, str, size, nullptr, nullptr) == 0)
    return false;

    // strip brackets
    uint32 len = strLen(str);
    if (str[0] == '{') {
        memmove(str, str + 1, len + 1);
        --len;
    }
    if (str[len - 1] == '}')
    str[len - 1] = 0;
    return true;
}

bool uuidFromString(UniqueID* _uuid, const char* str)
{
    sysLoadOle32();

    ASSERT(str);

    if (str[0] == 0)
    return false;

    char strTmp[64] {};

    uint32 len = strLen(str);
    if (str[0] != '{') {
        strTmp[0] = '{';
        strConcat(strTmp, sizeof(strTmp), str);
        if (str[len - 1] != '}') 
        strConcat(strTmp, sizeof(strTmp), "}");
    }
    else {
        ASSERT(str[len - 1] == '}');
        strCopy(strTmp, sizeof(strTmp), str);
    }        

    UUIDImpl* uuid = reinterpret_cast<UUIDImpl*>(_uuid);
    wchar_t guidStr[64];
    if (MultiByteToWideChar(CP_UTF8, 0, strTmp, -1, guidStr, sizeof(guidStr)) == 0) 
    return false;
    if (gOle32.CLSIDFromString(guidStr, &uuid->guid) != S_OK) 
    return false;
    return true;
}


//     ██████╗ ███████╗███╗   ██╗███████╗██████╗  █████╗ ██╗          ██████╗ ███████╗
//    ██╔════╝ ██╔════╝████╗  ██║██╔════╝██╔══██╗██╔══██╗██║         ██╔═══██╗██╔════╝
//    ██║  ███╗█████╗  ██╔██╗ ██║█████╗  ██████╔╝███████║██║         ██║   ██║███████╗
//    ██║   ██║██╔══╝  ██║╚██╗██║██╔══╝  ██╔══██╗██╔══██║██║         ██║   ██║╚════██║
//    ╚██████╔╝███████╗██║ ╚████║███████╗██║  ██║██║  ██║███████╗    ╚██████╔╝███████║
//     ╚═════╝ ╚══════╝╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝     ╚═════╝ ╚══════╝

SysDLL OS::LoadDLL(const char* filepath, char** pErrorMsg)
{
    SysDLL dll = (SysDLL)LoadLibraryA(filepath);
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

void OS::UnloadDLL(SysDLL dll)
{
    if (dll)
        FreeLibrary((HMODULE)dll);
}

void* OS::GetSymbolAddress(SysDLL dll, const char* symbolName)
{
    return (void*)GetProcAddress((HMODULE)dll, symbolName);
}

size_t OS::GetPageSize()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (size_t)si.dwPageSize;
}

bool OS::Win32GetRegisterLocalMachineString(const char* subkey, const char* value, char* dst, size_t dstSize)
{
    sysLoadAdvApi32();

    DWORD dataSize = (DWORD)dstSize;
    return gAdvApi32.RegGetValueA(HKEY_LOCAL_MACHINE, subkey, value, RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ, nullptr, dst, &dataSize) == ERROR_SUCCESS;
}

static uint32 sysGetPhysicalCoresCount()
{
	static uint32 cachedCoreCount = UINT32_MAX;
	if (cachedCoreCount != UINT32_MAX)
		return cachedCoreCount;

	SYSTEM_LOGICAL_PROCESSOR_INFORMATION* buffer = nullptr;
	DWORD returnLen = 0;
	DWORD countCount = 0;
	if (!GetLogicalProcessorInformation(buffer, &returnLen)) {
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			buffer = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)Mem::Alloc(returnLen);
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

	Mem::Free(buffer);

	cachedCoreCount = Clamp<uint32>(countCount, 1, _limits::SYS_MAX_CORES);
    ASSERT_MSG(cachedCoreCount <= _limits::SYS_MAX_CORES, "CPU core count appears to be too high (%u). Consider increasing SYS_MAX_CORES", cachedCoreCount);
	return cachedCoreCount;
}

// https://en.wikipedia.org/wiki/CPUID
// https://docs.microsoft.com/en-us/cpp/intrinsics/cpuid-cpuidex?redirectedfrom=MSDN&view=msvc-170
void OS::GetSysInfo(SysInfo* info)
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
    mProcess(INVALID_HANDLE_VALUE),
    mStdOutPipeRead(INVALID_HANDLE_VALUE),
    mStdErrPipeRead(INVALID_HANDLE_VALUE)
{
}

SysProcess::~SysProcess()
{
    if (mStdOutPipeRead != INVALID_HANDLE_VALUE) 
        CloseHandle(mStdOutPipeRead);
    if (mStdErrPipeRead != INVALID_HANDLE_VALUE)
        CloseHandle(mStdErrPipeRead);
    if (mProcess != INVALID_HANDLE_VALUE)
        CloseHandle(mProcess);
}

bool SysProcess::Run(const char* cmdline, SysProcessFlags flags, const char* cwd)
{
    ASSERT(mProcess == INVALID_HANDLE_VALUE);

    HANDLE stdOutPipeWrite = INVALID_HANDLE_VALUE;
    HANDLE stdErrPipeWrite = INVALID_HANDLE_VALUE;
    BOOL r;
    BOOL inheritHandles = (flags & SysProcessFlags::InheritHandles) == SysProcessFlags::InheritHandles ? TRUE : FALSE;

    if ((flags & SysProcessFlags::CaptureOutput) == SysProcessFlags::CaptureOutput) {
        SECURITY_ATTRIBUTES saAttr {}; 
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
        saAttr.bInheritHandle = inheritHandles; 

        r = CreatePipe(&mStdOutPipeRead, &stdOutPipeWrite, &saAttr, 0);
        ASSERT_MSG(r, "CreatePipe failed");

        r = CreatePipe(&mStdErrPipeRead, &stdErrPipeWrite, &saAttr, 0);
        ASSERT_MSG(r, "CreatePipe failed");

        if (inheritHandles) {
            r = SetHandleInformation(mStdOutPipeRead, HANDLE_FLAG_INHERIT, 0);
            ASSERT_MSG(r, "SetHandleInformation for pipe failed");
            r = SetHandleInformation(mStdErrPipeRead, HANDLE_FLAG_INHERIT, 0);
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
    char* cmdLineCopy = Mem::AllocCopy<char>(cmdline, strLen(cmdline)+1, &tmpAlloc);
    DWORD createProcessFlags = 0; // TODO

    if ((flags & SysProcessFlags::DontCreateConsole) == SysProcessFlags::DontCreateConsole) 
        createProcessFlags |= CREATE_NO_WINDOW;
    if ((flags & SysProcessFlags::ForceCreateConsole) == SysProcessFlags::ForceCreateConsole)
        createProcessFlags |= CREATE_NEW_CONSOLE;

    r = CreateProcessA(nullptr, cmdLineCopy, nullptr, nullptr, inheritHandles, createProcessFlags, NULL, cwd, &startInfo, &procInfo);
    if (!r) {
        LOG_ERROR("Run process failed: %s", cmdline);
        return false;
    }

    CloseHandle(procInfo.hThread);
    mProcess = procInfo.hProcess;

    if ((flags & SysProcessFlags::CaptureOutput) == SysProcessFlags::CaptureOutput) {
        CloseHandle(stdOutPipeWrite);
        CloseHandle(stdErrPipeWrite);
    }

    return true;
}

void SysProcess::Wait() const
{
    ASSERT(mProcess != INVALID_HANDLE_VALUE);
    WaitForSingleObject(mProcess, INFINITE);
}

bool SysProcess::IsRunning() const
{
    ASSERT(mProcess != INVALID_HANDLE_VALUE);
    return WaitForSingleObject(mProcess, 0) != WAIT_OBJECT_0;
}

bool SysProcess::IsValid() const
{
    return mProcess != INVALID_HANDLE_VALUE;
}

static void sysTerminateChildProcesses(DWORD parentProcessId) 
{
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe { sizeof(PROCESSENTRY32) };

        if (Process32First(hSnapshot, &pe)) {
            do {
                if (pe.th32ParentProcessID == parentProcessId) {
                    HANDLE childProcess = OpenProcess(PROCESS_TERMINATE, TRUE, pe.th32ProcessID);
                    if (childProcess) {
                        LOG_DEBUG("Terminating child process: %u (%s)", pe.th32ProcessID, pe.szExeFile);
                        sysTerminateChildProcesses(pe.th32ProcessID);

                        TerminateProcess(childProcess, 1);
                        CloseHandle(childProcess);
                    }
                }
            } while (Process32Next(hSnapshot, &pe));
        }

        CloseHandle(hSnapshot);
    }
};

void SysProcess::Abort()
{
    ASSERT(mProcess != INVALID_HANDLE_VALUE);

    // Start by terminating all child processes recursively
    DWORD processId = GetProcessId(mProcess);
    sysTerminateChildProcesses(processId);

    BOOL r = TerminateProcess(mProcess, 1);
    if (!r) {
        LOG_ERROR("Process failed to terminate: 0x%x (ErrorCode: %u)", mProcess, GetLastError());
    }
    else {
        mProcess = INVALID_HANDLE_VALUE;
    }
}

int SysProcess::GetExitCode() const
{
    ASSERT(mProcess != INVALID_HANDLE_VALUE);
    DWORD exitCode = UINT32_MAX;
    GetExitCodeProcess(mProcess, &exitCode);
    return static_cast<int>(exitCode);
}

uint32 SysProcess::ReadStdOut(void* data, uint32 size) const
{
    ASSERT(mStdOutPipeRead != INVALID_HANDLE_VALUE);

    DWORD bytesRead;
    BOOL r = ReadFile((HANDLE)mStdOutPipeRead, data, size, &bytesRead, nullptr);
    return (r && bytesRead) ? bytesRead : 0; 
}

uint32 SysProcess::ReadStdErr(void* data, uint32 size) const
{
    ASSERT(mStdErrPipeRead != INVALID_HANDLE_VALUE);

    DWORD bytesRead;
    BOOL r = ReadFile((HANDLE)mStdErrPipeRead, data, size, &bytesRead, nullptr);
    return (r && bytesRead) ? bytesRead : 0;
}
#endif  // PLATFORM_DESKTOP

bool OS::Win32IsProcessRunning(const char* execName)
{
    PROCESSENTRY32 entry { sizeof(PROCESSENTRY32) };

    char execNameTrimmed[PATH_CHARS_MAX];
    strTrim(execNameTrimmed, sizeof(execNameTrimmed), execName, '\'');
    strTrim(execNameTrimmed, sizeof(execNameTrimmed), execNameTrimmed, '"');

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, TH32CS_SNAPPROCESS);
    if (!Process32First(snapshot, &entry)) {
        CloseHandle(snapshot);
        return false;
    }

    bool isRunning = false;
    do {
        if constexpr (sizeof(CHAR) == 2) {
            char exeFile[MAX_PATH];
            if (strWideToUtf8((const wchar_t*)entry.szExeFile, exeFile, sizeof(exeFile)))
                isRunning = strIsEqualNoCase(exeFile, execNameTrimmed);
        }
        else {
            isRunning = strIsEqualNoCase((const char*)entry.szExeFile, execNameTrimmed);
        }
    } while (!isRunning && Process32Next(snapshot, &entry));

    CloseHandle(snapshot);
    return false;
}

SysWin32ShellExecuteResult OS::Win32ShellExecute(const char* filepath, const char* args, 
                                                const char* cwd, SysWin32ShowWindow showFlag, 
                                                const char* operation,
                                                void** pInstance)
{
    sysLoadShell32();

    HINSTANCE hInst = gShell32.ShellExecuteA(nullptr, operation, filepath, args, cwd, (INT)showFlag);

    INT_PTR errCode = INT_PTR(hInst);
    if (errCode <= 32) {
        switch (errCode) {
        case 0:
        case SE_ERR_OOM:
            return SysWin32ShellExecuteResult::OutOfMemory;
        case SE_ERR_DLLNOTFOUND:
        case SE_ERR_FNF:
            return SysWin32ShellExecuteResult::FileNotFound;
        case SE_ERR_PNF:
            return SysWin32ShellExecuteResult::PathNotFound;
        case ERROR_BAD_FORMAT:
            return SysWin32ShellExecuteResult::BadFormat;
        case SE_ERR_ASSOCINCOMPLETE:
        case SE_ERR_NOASSOC:
            return SysWin32ShellExecuteResult::NoAssociation;
        case SE_ERR_ACCESSDENIED:
            return SysWin32ShellExecuteResult::AccessDenied;
        default:
            return SysWin32ShellExecuteResult::UnknownError;
        }
    }
    else {
        if (pInstance)
            *pInstance = hInst;
        return SysWin32ShellExecuteResult::Ok;
    }
}


bool OS::SetEnvVar(const char* name, const char* value)
{
    return SetEnvironmentVariableA(name, value) == TRUE;
}

bool OS::GetEnvVar(const char* name, char* outValue, uint32 valueSize)
{
    DWORD dwValueSize = GetEnvironmentVariableA(name, outValue, valueSize);
    return dwValueSize != 0 && dwValueSize < valueSize;
}

bool OS::IsDebuggerPresent()
{
    return ::IsDebuggerPresent();
}

void OS::Win32PrintToDebugger(const char* text)
{
    OutputDebugStringA(text);
}

// https://learn.microsoft.com/en-us/windows/win32/memory/creating-a-file-mapping-using-large-pages
// TODO: dynalically load functions from DLL to prevent linking with Advapi32.lib
bool OS::Win32SetPrivilege(const char* name, bool enable)
{
    sysLoadAdvApi32();

    HANDLE tokenHandle;
    TOKEN_PRIVILEGES tp;

    if (!gAdvApi32.OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tokenHandle)) 
        return false;

    if (!gAdvApi32.LookupPrivilegeValueA(nullptr, name, &tp.Privileges[0].Luid)) 
        return false;
    tp.PrivilegeCount = 1;

    if (enable) 
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    else
        tp.Privileges[0].Attributes = 0;

    BOOL status = gAdvApi32.AdjustTokenPrivileges(tokenHandle, FALSE, &tp, 0, nullptr, 0);
    // It is possible for AdjustTokenPrivileges to return TRUE and still not succeed.
    // So always check for the last error value.
    DWORD error = GetLastError();
    if (!status || error != ERROR_SUCCESS) {
        LOG_ERROR("AdjustTokenPrivileges failed. Code: %u", error);
    }
    
    CloseHandle(tokenHandle);
    return true;
}

//    ██████╗  █████╗ ████████╗██╗  ██╗
//    ██╔══██╗██╔══██╗╚══██╔══╝██║  ██║
//    ██████╔╝███████║   ██║   ███████║
//    ██╔═══╝ ██╔══██║   ██║   ██╔══██║
//    ██║     ██║  ██║   ██║   ██║  ██║
//    ╚═╝     ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝

char* Path::GetMyPath_CStr(char* dst, size_t dstSize)
{
    GetModuleFileNameA(NULL, dst, (DWORD)dstSize);
    return dst;
}

char* Path::Absolute_CStr(const char* path, char* dst, size_t dstSize)
{
    if (GetFullPathNameA(path, (DWORD)dstSize, dst, NULL) == 0)
        dst[0] = '\0';
    return dst;
}

char* Path::GetCurrentDir_CStr(char* dst, size_t dstSize)
{
    GetCurrentDirectoryA((DWORD)dstSize, dst);
    return dst;
}

void Path::SetCurrentDir_CStr(const char* path)
{
    SetCurrentDirectoryA(path);
}

PathInfo Path::Stat_CStr(const char* path)
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

bool Path::CreateDir_CStr(const char* path)
{
    return bool(CreateDirectoryA(path, nullptr)); 
}

bool Path::Move_CStr(const char* src, const char* dest)
{
    return bool(MoveFileExA(src, dest, MOVEFILE_REPLACE_EXISTING|MOVEFILE_COPY_ALLOWED));
}

char* Path::GetHomeDir_CStr(char* dst, size_t dstSize)
{
    sysLoadOle32();
    sysLoadShell32();

    PWSTR homeDir = nullptr;
    if (SUCCEEDED(gShell32.SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &homeDir))) {
        strWideToUtf8(homeDir, dst, (uint32)dstSize);
        gOle32.CoTaskMemFree(homeDir);
        return dst;
    }
    else {
        ASSERT_MSG(0, "Getting home directory failed");
        return nullptr;
    }
}

char* Path::GetCacheDir_CStr(char* dst, size_t dstSize, const char* appName)
{
    sysLoadOle32();
    sysLoadShell32();

    PWSTR homeDir = nullptr;
    if (SUCCEEDED(gShell32.SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &homeDir))) {
        char homeDirUtf8[CONFIG_MAX_PATH];
        strWideToUtf8(homeDir, homeDirUtf8, sizeof(homeDirUtf8));
        gOle32.CoTaskMemFree(homeDir);
        Path::Join_CStr(dst, dstSize, homeDirUtf8, appName);
        return dst;
    }
    else {
        ASSERT_MSG(0, "Getting LOCALAPPDATA directory failed");
        return nullptr;
    }
}

bool Path::MakeTemp_CStr(char* dst, [[maybe_unused]] size_t dstSize, const char* namePrefix, const char* dir)
{
    static char osTempPath[PATH_CHARS_MAX] = {};
    if (dir == nullptr) {
        if (osTempPath[0] == '\0')
            GetTempPathA(sizeof(osTempPath), osTempPath);
        dir = osTempPath;
    }

    ASSERT(dstSize >= PATH_CHARS_MAX);
    return GetTempFileNameA(dir, namePrefix, 0, dst) != 0;
}

char* OS::Win32GetFolder(SysWin32Folder folder, char* dst, size_t dstSize)
{
    sysLoadOle32();
    sysLoadShell32();

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
    if (SUCCEEDED(gShell32.SHGetKnownFolderPath(folderIds[uint32(folder)], 0, nullptr, &folderPath))) {
        strWideToUtf8(folderPath, dst, (uint32)dstSize);
        gOle32.CoTaskMemFree(folderPath);
        return dst;
    }
    else {
        ASSERT_MSG(0, "SHGetKnownFolderPath failed");
        return nullptr;
    }
}

//    ███╗   ███╗███████╗███╗   ███╗ ██████╗ ██████╗ ██╗   ██╗
//    ████╗ ████║██╔════╝████╗ ████║██╔═══██╗██╔══██╗╚██╗ ██╔╝
//    ██╔████╔██║█████╗  ██╔████╔██║██║   ██║██████╔╝ ╚████╔╝ 
//    ██║╚██╔╝██║██╔══╝  ██║╚██╔╝██║██║   ██║██╔══██╗  ╚██╔╝  
//    ██║ ╚═╝ ██║███████╗██║ ╚═╝ ██║╚██████╔╝██║  ██║   ██║   
//    ╚═╝     ╚═╝╚══════╝╚═╝     ╚═╝ ╚═════╝ ╚═╝  ╚═╝   ╚═╝   
static MemVirtualStats gVMStats;

void* Mem::VirtualReserve(size_t size, MemVirtualFlags flags)
{
    DWORD extraFlags = (flags & MemVirtualFlags::Watch) == MemVirtualFlags::Watch ? MEM_WRITE_WATCH : 0;
    void* ptr = VirtualAlloc(NULL, size, MEM_RESERVE | extraFlags, PAGE_READWRITE);
    if (!ptr) {
        MEM_FAIL();
    }

    Atomic::FetchAdd(&gVMStats.reservedBytes, size);
    return ptr;
}

void* Mem::VirtualCommit(void* ptr, size_t size)
{
    ASSERT(ptr);
    ptr = VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE);
    if (!ptr) {
        MEM_FAIL();
    }

    Atomic::FetchAdd(&gVMStats.commitedBytes, size);
    return ptr;
}

void Mem::VirtualDecommit(void* ptr, size_t size)
{
    [[maybe_unused]] BOOL r = VirtualFree(ptr, size, MEM_DECOMMIT);
    ASSERT(r);

    ASSERT(size <= gVMStats.commitedBytes);
    Atomic::FetchSub(&gVMStats.commitedBytes, size);
}

void Mem::VirtualRelease(void* ptr, size_t size)
{
    [[maybe_unused]] BOOL r = VirtualFree(ptr, 0, MEM_RELEASE);
    ASSERT(r);

    ASSERT(size <= gVMStats.reservedBytes);
    Atomic::FetchSub(&gVMStats.reservedBytes, size);
}

MemVirtualStats Mem::VirtualGetStats()
{
    return gVMStats;
}

// https://learn.microsoft.com/en-us/windows/win32/memory/large-page-support
// Example: https://learn.microsoft.com/en-us/windows/win32/memory/creating-a-file-mapping-using-large-pages
bool Mem::VirtualEnableLargePages(size_t* largePageSize)
{
    ASSERT(largePageSize);
    if (!OS::Win32SetPrivilege("SeLockMemoryPrivilege"))
        return false;

    *largePageSize = GetLargePageMinimum();
    return true;
}

//    ███████╗██╗██╗     ███████╗
//    ██╔════╝██║██║     ██╔════╝
//    █████╗  ██║██║     █████╗  
//    ██╔══╝  ██║██║     ██╔══╝  
//    ██║     ██║███████╗███████╗
//    ╚═╝     ╚═╝╚══════╝╚══════╝
struct FileWin
{
    HANDLE      handle;
    FileOpenFlags flags;
    uint64      size;
    uint64      lastModifiedTime;
};
static_assert(sizeof(FileWin) <= sizeof(File));

static inline bool fileGetInfo(HANDLE hFile, uint64* outFileSize, uint64* outModifiedTime)
{
    BY_HANDLE_FILE_INFORMATION fileInfo {};
    if (!GetFileInformationByHandle(hFile, &fileInfo)) 
        return false;

    *outFileSize = (uint64(fileInfo.nFileSizeHigh)<<32) | uint64(fileInfo.nFileSizeLow);
    *outModifiedTime = uint64(fileInfo.ftLastAccessTime.dwHighDateTime)<<32 | uint64(fileInfo.ftLastAccessTime.dwLowDateTime);
    return true;
}

File::File()
{
    FileWin* f = (FileWin*)mData;

    f->handle = INVALID_HANDLE_VALUE;
    f->flags = FileOpenFlags::None;
    f->size = 0;
    f->lastModifiedTime = 0;
}

bool File::Open(const char* filepath, FileOpenFlags flags)
{
    ASSERT((flags & (FileOpenFlags::Read|FileOpenFlags::Write)) != (FileOpenFlags::Read|FileOpenFlags::Write));
    ASSERT((flags & (FileOpenFlags::Read|FileOpenFlags::Write)) != FileOpenFlags::None);

    FileWin* f = (FileWin*)mData;

    uint32 accessFlags = 0;
    uint32 attrs = FILE_ATTRIBUTE_NORMAL;
    uint32 createFlags = 0;
    uint32 shareFlags = 0;

    if ((flags & FileOpenFlags::Read) == FileOpenFlags::Read) {
        accessFlags |= GENERIC_READ;
        createFlags = OPEN_EXISTING;
        shareFlags |= FILE_SHARE_READ;
    } else if ((flags & FileOpenFlags::Write) == FileOpenFlags::Write) {
        accessFlags |= GENERIC_WRITE;
        createFlags |= (flags & FileOpenFlags::Append) == FileOpenFlags::Append ? OPEN_EXISTING : CREATE_ALWAYS;
        shareFlags |= FILE_SHARE_WRITE;
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
    
    return fileGetInfo(hfile, &f->size, &f->lastModifiedTime);
}

void File::Close()
{
    FileWin* f = (FileWin*)mData;

    if (f->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(f->handle);
        f->handle = INVALID_HANDLE_VALUE;
    }
}

size_t File::Read(void* dst, size_t size)
{
    FileWin* f = (FileWin*)mData;
    ASSERT(f->handle != INVALID_HANDLE_VALUE);

    if ((f->flags & FileOpenFlags::NoCache) == FileOpenFlags::NoCache) {
        static size_t pagesz = 0;
        if (pagesz == 0) {
            pagesz = OS::GetPageSize();
        }
        ASSERT_ALWAYS((uintptr_t)dst % pagesz == 0, "buffers must be aligned with NoCache flag");
    }

    DWORD bytesRead;
    if (!ReadFile(f->handle, dst, (DWORD)size, &bytesRead, NULL))
        return 0;

    return size_t(bytesRead);
}

size_t File::Write(const void* src, size_t size)
{
    ASSERT(size);
    FileWin* f = (FileWin*)mData;
    ASSERT(f->handle != INVALID_HANDLE_VALUE);

    DWORD bytesWritten;
    if (!WriteFile(f->handle, src, (DWORD)size, &bytesWritten, NULL))
        return 0;
    f->size += bytesWritten;

    return bytesWritten;
}

size_t File::Seek(size_t offset, FileSeekMode mode)
{
    FileWin* f = (FileWin*)mData;
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
    FileWin* f = (FileWin*)mData;
    return size_t(f->size);    
}

uint64 File::GetLastModified() const
{
    FileWin* f = (FileWin*)mData;
    return f->lastModifiedTime;
}

bool File::IsOpen() const
{
    FileWin* f = (FileWin*)mData;
    return f->handle != INVALID_HANDLE_VALUE;
}

//----------------------------------------------------------------------------------------------------------------------
// AsyncFile
struct AsyncFileWin
{
    AsyncFile f;
    HANDLE hFile;
    OVERLAPPED overlapped;
    MemAllocator* alloc;
    AsyncFileCallback readFn;
};

// Win32 callback (Called from one of kernel's IO threads)
static void asyncReadFileCallback(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped)
{
    size_t overlappedOffset = offsetof(AsyncFileWin, overlapped);
    AsyncFileWin* file = (AsyncFileWin*)((uint8*)lpOverlapped - overlappedOffset);
    ASSERT(file->readFn);

    file->readFn(&file->f, dwErrorCode != 0 && dwNumberOfBytesTransfered == file->f.size);
}

AsyncFile* Async::ReadFile(const char* filepath, const AsyncFileRequest& request)
{
    HANDLE hFile = CreateFileA(filepath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return nullptr;
    
    uint64 fileSize;
    uint64 fileModificationTime;
    if (!fileGetInfo(hFile, &fileSize, &fileModificationTime) || fileSize == 0) {
        CloseHandle(hFile);
        return nullptr;
    }
    ASSERT_MSG(fileSize < UINT32_MAX, "Large file sizes are not supported by win32 overlapped API");
    ASSERT_MSG(!request.userDataAllocateSize || (request.userData && request.userDataAllocateSize), 
            "`userDataAllocatedSize` should be accompanied with a valid `userData` pointer");

    MemSingleShotMalloc<AsyncFileWin> mallocator;
    uint8* data;
    uint8* userData = nullptr;
    if (request.userDataAllocateSize) 
        mallocator.AddExternalPointerField<uint8>(&userData, request.userDataAllocateSize);
    mallocator.AddExternalPointerField<uint8>(&data, fileSize);
    AsyncFileWin* file = mallocator.Calloc(request.alloc);
    file->f.filepath = filepath;
    file->f.data = data;
    file->f.size = uint32(fileSize);
    file->f.lastModifiedTime = fileModificationTime;
    if (request.userData) {
        if (request.userDataAllocateSize) {
            memcpy(userData, request.userData, request.userDataAllocateSize);
            file->f.userData = userData;
        }
        else {
            file->f.userData = request.userData;
        }
    }

    file->hFile = hFile;
    file->alloc = request.alloc;

    if (request.readFn) {
        file->readFn = request.readFn;
        if (!BindIoCompletionCallback(file->hFile, asyncReadFileCallback, 0)) {
            CloseHandle(file->hFile);
            Mem::Free(file, file->alloc);
            return nullptr;
        }
    }

    if (!ReadFile(hFile, file->f.data, DWORD(file->f.size), nullptr, &file->overlapped)) {
        if (GetLastError() != ERROR_IO_PENDING) {
            CloseHandle(file->hFile);
            Mem::Free(file, file->alloc);
            return nullptr;
        }
    }

    return &file->f;
}

void Async::Close(AsyncFile* file)
{
    if (!file)
        return;

    AsyncFileWin* fw = (AsyncFileWin*)file;
    if (fw->hFile != INVALID_HANDLE_VALUE) {
        DWORD numBytesTransfered;
        if (!GetOverlappedResult(fw->hFile, &fw->overlapped, &numBytesTransfered, FALSE) && GetLastError() == ERROR_IO_PENDING)
            CancelIo(fw->hFile);

        CloseHandle(fw->hFile);
        fw->hFile = INVALID_HANDLE_VALUE;

        MemSingleShotMalloc<AsyncFileWin>::Free(fw, fw->alloc);
    }    
}

bool Async::Wait(AsyncFile* file)
{
    ASSERT(file);
    AsyncFileWin* fw = (AsyncFileWin*)file;
    ASSERT(fw->hFile != INVALID_HANDLE_VALUE);

    DWORD numBytesTransfered;
    BOOL r = GetOverlappedResult(fw->hFile, &fw->overlapped, &numBytesTransfered, TRUE);
    return r && numBytesTransfered == fw->f.size;
}

bool Async::IsFinished(AsyncFile* file, bool* outError)
{
    ASSERT(file);
    AsyncFileWin* fw = (AsyncFileWin*)file;
    ASSERT(fw->hFile != INVALID_HANDLE_VALUE);

    DWORD numBytesTransfered;
    BOOL r = GetOverlappedResult(fw->hFile, &fw->overlapped, &numBytesTransfered, FALSE);

    if (outError)
        *outError = GetLastError() != ERROR_IO_PENDING;
    return r;
}

//    ███████╗ ██████╗  ██████╗██╗  ██╗███████╗████████╗
//    ██╔════╝██╔═══██╗██╔════╝██║ ██╔╝██╔════╝╚══██╔══╝
//    ███████╗██║   ██║██║     █████╔╝ █████╗     ██║   
//    ╚════██║██║   ██║██║     ██╔═██╗ ██╔══╝     ██║   
//    ███████║╚██████╔╝╚██████╗██║  ██╗███████╗   ██║   
//    ╚══════╝ ╚═════╝  ╚═════╝╚═╝  ╚═╝╚══════╝   ╚═╝   
namespace _private
{
    static bool gSocketInitialized;
    static void socketInitializeWin32()
    {
        if (!gSocketInitialized) {
            LOG_DEBUG("SocketTCP: Initialize");
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(1, 0), &wsaData) != 0) {
                ASSERT_ALWAYS(false, "Windows sockets initialization failed");
                return;
            }
        
            gSocketInitialized = true;
        }
    }

    static SocketErrorCode::Enum socketTranslatePlatformErrorCode()
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
} // namespace _private

// Implemented in System.cpp

#define SOCKET_INVALID INVALID_SOCKET
SocketTCP::SocketTCP() :
    mSock(SOCKET_INVALID),
    mErrCode(SocketErrorCode::None),
    mLive(0)
{
}

void SocketTCP::Close()
{
    if (mSock != SOCKET_INVALID) {
        if (mLive)
            shutdown(mSock, SD_BOTH);
        closesocket(mSock);

        mSock = SOCKET_INVALID;
        mErrCode = SocketErrorCode::None;
        mLive = false;
    }
}

SocketTCP SocketTCP::CreateListener()
{
    _private::socketInitializeWin32();

    SocketTCP sock;

    sock.mSock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock.mSock == SOCKET_INVALID) {
        sock.mErrCode = _private::socketTranslatePlatformErrorCode();
        LOG_ERROR("SocketTCP: Opening the socket failed");
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

    if (bind(mSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        mErrCode = _private::socketTranslatePlatformErrorCode();
        LOG_ERROR("SocketTCP: failed binding the socket to port: %d", port);
        return false;
    }

    LOG_VERBOSE("SocketTCP: Listening on port '%d' for incoming connections ...", port);
    int _maxConnections = maxConnections > INT32_MAX ? INT32_MAX : static_cast<int>(maxConnections);
    bool success = listen(mSock, _maxConnections) >= 0;
    
    if (!success) 
        mErrCode = _private::socketTranslatePlatformErrorCode();
    else
        mLive = true;

    return success;
}

SocketTCP SocketTCP::Accept(char* clientUrl, uint32 clientUrlSize)
{
    ASSERT(IsValid());

    SocketTCP newSock;

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    newSock.mSock = accept(mSock, (struct sockaddr*)&addr, &addrlen);
    if (mLive && newSock.mSock == SOCKET_INVALID) {
        newSock.mErrCode = _private::socketTranslatePlatformErrorCode();
        LOG_ERROR("SocketTCP: failed to accept the new socket");
        return newSock;
    }

    if (clientUrl && clientUrlSize) {
        char ip[256];
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        uint16 port = htons(addr.sin_port);
        
        strPrintFmt(clientUrl, clientUrlSize, "%s:%d", ip, port);
    }

    newSock.mLive = true;
    return newSock;
}

SocketTCP SocketTCP::Connect(const char* url)
{
    _private::socketInitializeWin32();

    SocketTCP sock;

    char address[256];
    char port[16];
    if (!SocketTCP::ParseUrl(url, address, sizeof(address), port, sizeof(port))) {
        LOG_ERROR("SocketTCP: failed parsing the url: %s", url);
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
        LOG_ERROR("SocketTCP: failed to resolve url: %s", url);
        return sock;
    }

    sock.mSock = socket(addri->ai_family, addri->ai_socktype, addri->ai_protocol);
    if (sock.mSock == SOCKET_INVALID) {
        freeaddrinfo(addri);
        LOG_ERROR("SocketTCP: failed to create socket");
        return sock;
    }

    if (connect(sock.mSock, addri->ai_addr, (int)addri->ai_addrlen) == -1) {
        freeaddrinfo(addri);
        sock.mErrCode = _private::socketTranslatePlatformErrorCode();
        LOG_ERROR("SocketTCP: failed to connect to url: %s", url);
        sock.Close();
        return sock;
    }

    freeaddrinfo(addri);

    sock.mLive = true;
    return sock;
}

uint32 SocketTCP::Write(const void* src, uint32 size)
{
    ASSERT(IsValid());
    ASSERT(mLive);
    uint32 totalBytesSent = 0;

    while (size > 0) {
        int bytesSent = send(mSock, reinterpret_cast<const char*>(src) + totalBytesSent, size, 0);
        if (bytesSent == 0) {
            break;
        }
        else if (bytesSent == -1) {
            mErrCode = _private::socketTranslatePlatformErrorCode();
            if (mErrCode == SocketErrorCode::SocketShutdown ||
                mErrCode == SocketErrorCode::NotConnected)
            {
                LOG_DEBUG("SocketTCP: socket connection closed forcefully by the peer");
                mLive = false;
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
    ASSERT(mLive);

    int bytesRecv = recv(mSock, reinterpret_cast<char*>(dst), dstSize, 0);
    if (bytesRecv == -1) {
        mErrCode = _private::socketTranslatePlatformErrorCode();
        if (mErrCode == SocketErrorCode::SocketShutdown ||
            mErrCode == SocketErrorCode::NotConnected)
        {
            LOG_DEBUG("SocketTCP: socket connection closed forcefully by the peer");
            mLive = false;
        }
        return UINT32_MAX;
    }

    return static_cast<uint32>(bytesRecv);
}

bool SocketTCP::IsValid() const
{
    return mSock != SOCKET_INVALID;
}

#endif // PLATFORM_WINDOWS
