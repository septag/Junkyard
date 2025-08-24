#include "Debug.h"

#if PLATFORM_WINDOWS

#include "External/remedybg/remedybg_driver.h"

#include "StringUtil.h"  // Str::Copy/..
#include "IncludeWin.h"
#include "System.h"
#include "Log.h"
#include "Blobs.h"
#include "Allocators.h"


//    ███████╗████████╗ █████╗  ██████╗██╗  ██╗████████╗██████╗  █████╗  ██████╗███████╗
//    ██╔════╝╚══██╔══╝██╔══██╗██╔════╝██║ ██╔╝╚══██╔══╝██╔══██╗██╔══██╗██╔════╝██╔════╝
//    ███████╗   ██║   ███████║██║     █████╔╝    ██║   ██████╔╝███████║██║     █████╗  
//    ╚════██║   ██║   ██╔══██║██║     ██╔═██╗    ██║   ██╔══██╗██╔══██║██║     ██╔══╝  
//    ███████║   ██║   ██║  ██║╚██████╗██║  ██╗   ██║   ██║  ██║██║  ██║╚██████╗███████╗
//    ╚══════╝   ╚═╝   ╚═╝  ╚═╝ ╚═════╝╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝╚══════╝
#pragma pack(push, 8)
#include <DbgHelp.h>

typedef struct _IMAGEHLP_MODULE64_V3
{
    DWORD SizeOfStruct;        // set to sizeof(IMAGEHLP_MODULE64)
    DWORD64 BaseOfImage;       // base load address of module
    DWORD ImageSize;           // virtual size of the loaded module
    DWORD TimeDateStamp;       // date/time stamp from pe header
    DWORD CheckSum;            // checksum from the pe header
    DWORD NumSyms;             // number of symbols in the symbol table
    SYM_TYPE SymType;          // type of symbols loaded
    CHAR ModuleName[32];       // module name
    CHAR ImageName[256];       // image name
    CHAR LoadedImageName[256]; // symbol file name
    // new elements: 07-Jun-2002
    CHAR LoadedPdbName[256];   // pdb file name
    DWORD CVSig;               // Signature of the CV record in the debug directories
    CHAR CVData[PATH_CHARS_MAX * 3]; // Contents of the CV record
    DWORD PdbSig;              // Signature of PDB
    GUID PdbSig70;             // Signature of PDB (VC 7 and up)
    DWORD PdbAge;              // DBI age of pdb
    BOOL PdbUnmatched;         // loaded an unmatched pdb
    BOOL DbgUnmatched;         // loaded an unmatched dbg
    BOOL LineNumbers;          // we have line number information
    BOOL GlobalSymbols;        // we have internal symbol information
    BOOL TypeInfo;             // we have type information
    // new elements: 17-Dec-2003
    BOOL SourceIndexed; // pdb supports source server
    BOOL Publics;       // contains public symbols
} IMAGEHLP_MODULE64_V3, *PIMAGEHLP_MODULE64_V3;

typedef struct _IMAGEHLP_MODULE64_V2
{
    DWORD SizeOfStruct;        // set to sizeof(IMAGEHLP_MODULE64)
    DWORD64 BaseOfImage;       // base load address of module
    DWORD ImageSize;           // virtual size of the loaded module
    DWORD TimeDateStamp;       // date/time stamp from pe header
    DWORD CheckSum;            // checksum from the pe header
    DWORD NumSyms;             // number of symbols in the symbol table
    SYM_TYPE SymType;          // type of symbols loaded
    CHAR ModuleName[32];       // module name
    CHAR ImageName[256];       // image name
    CHAR LoadedImageName[256]; // symbol file name
} IMAGEHLP_MODULE64_V2, *PIMAGEHLP_MODULE64_V2;
#pragma pack(pop)

using SymInitializeFn = BOOL(*)(IN HANDLE process, IN LPCSTR UserSearchPath, IN BOOL fInvadeProcess);
using SymCleanupFn = BOOL(*)(IN HANDLE process);
using SymGetSymFromAddr64Fn = BOOL(*)(IN HANDLE process, IN DWORD64 dwAddr, OUT PDWORD64 pdwDisplacement, OUT PIMAGEHLP_SYMBOL64 Symbol);
using UnDecorateSymbolNameFn = DWORD(WINAPI*)(PCSTR DecoratedName, PSTR UnDecoratedName, DWORD UndecoratedLength, DWORD Flags);
using SymGetLineFromAddr64Fn = BOOL(*)(IN HANDLE process, IN DWORD64 dwAddr, OUT PDWORD pdwDisplacement, OUT PIMAGEHLP_LINE64 line);  

// DbgHelp Api functions
static SymInitializeFn _SymInitialize;
static SymCleanupFn _SymCleanup;
static SymGetSymFromAddr64Fn _SymGetSymFromAddr64;
static UnDecorateSymbolNameFn _UnDecorateSymbolName;
static SymGetLineFromAddr64Fn _SymGetLineFromAddr64;

struct DebugStacktraceContext
{
    bool mInitialized;
    HINSTANCE mDbgHelp;
    HANDLE mProcess;
    CRITICAL_SECTION mMutex;
    
    DebugStacktraceContext();
    ~DebugStacktraceContext();
};

static DebugStacktraceContext gStacktrace;

namespace Debug
{
    static bool InitializeStacktrace()
    {
        if (gStacktrace.mInitialized)
        return true;
        gStacktrace.mInitialized = true;

        EnterCriticalSection(&gStacktrace.mMutex);
        ASSERT(gStacktrace.mDbgHelp == nullptr);

        // Load dbghelp.dll: this DLL should be provided and included in the repo
        gStacktrace.mDbgHelp = LoadLibraryA("dbghelp.dll");
        if (!gStacktrace.mDbgHelp) {
            Debug::PrintLine("Could not load DbgHelp.dll");
            gStacktrace.mInitialized = false;
            return false;
        }

        _SymInitialize = (SymInitializeFn)GetProcAddress(gStacktrace.mDbgHelp, "SymInitialize");
        _SymCleanup = (SymCleanupFn)GetProcAddress(gStacktrace.mDbgHelp, "SymCleanup");
        _SymGetLineFromAddr64 = (SymGetLineFromAddr64Fn)GetProcAddress(gStacktrace.mDbgHelp, "SymGetLineFromAddr64");
        _SymGetSymFromAddr64 = (SymGetSymFromAddr64Fn)GetProcAddress(gStacktrace.mDbgHelp, "SymGetSymFromAddr64");
        _UnDecorateSymbolName = (UnDecorateSymbolNameFn)GetProcAddress(gStacktrace.mDbgHelp, "UnDecorateSymbolName");
        ASSERT(_SymInitialize && _SymCleanup && _SymGetLineFromAddr64 && _SymGetSymFromAddr64 && _UnDecorateSymbolName);

        gStacktrace.mProcess = GetCurrentProcess();

        if (!_SymInitialize(gStacktrace.mProcess, NULL, TRUE)) {
            LeaveCriticalSection(&gStacktrace.mMutex);
            Debug::PrintLine("DbgHelp: _SymInitialize failed");
            gStacktrace.mInitialized = false;
            return false;
        }

        ASSERT(_SymInitialize && _SymCleanup && _SymGetLineFromAddr64 && _SymGetSymFromAddr64 && _UnDecorateSymbolName);
        LeaveCriticalSection(&gStacktrace.mMutex);

        return true;
    }
};

#ifdef TRACY_ENABLE
void DebugDbgHelpInit()
{
    if (!gStacktrace.mInitialized) {
        Debug::InitializeStacktrace();
        ASSERT_MSG(gStacktrace.mInitialized, "Failed to initialize stacktrace capture");
    }  
}

void DebugDbgHelpLock()
{
    EnterCriticalSection(&gStacktrace.mMutex);
}

void DebugDbgHelpUnlock()
{
    LeaveCriticalSection(&gStacktrace.mMutex);
}
#endif // if TRACY_ENABLE

NO_INLINE uint16 Debug::CaptureStacktrace(void** stackframes, uint16 maxStackframes, uint16 framesToSkip, uint32* pHash)
{
    static_assert(sizeof(DWORD) == sizeof(uint32));

    return (uint16)RtlCaptureStackBackTrace(framesToSkip, maxStackframes, stackframes, PDWORD(pHash));
}

void Debug::ResolveStacktrace(uint16 numStacktrace, void* const* stackframes, DebugStacktraceEntry* entries)
{
    if (!gStacktrace.mInitialized) {
        Debug::InitializeStacktrace();
        ASSERT_MSG(gStacktrace.mInitialized, "Failed to initialize stacktrace capture");
    }  

    IMAGEHLP_LINE64 line;
    uint8* symbolBuffer[sizeof(IMAGEHLP_SYMBOL64) + PATH_CHARS_MAX];
    IMAGEHLP_SYMBOL64* symbol = reinterpret_cast<IMAGEHLP_SYMBOL64*>(symbolBuffer);
    memset(symbol, 0, sizeof(IMAGEHLP_SYMBOL64) + PATH_CHARS_MAX);
    symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
    symbol->MaxNameLength = PATH_CHARS_MAX;

    EnterCriticalSection(&gStacktrace.mMutex);
    for (uint16 i = 0; i < numStacktrace; i++) {
        DebugStacktraceEntry entry = {};
        if (_SymGetSymFromAddr64(gStacktrace.mProcess, (DWORD64)stackframes[i], &entry.offsetFromSymbol, symbol)) {
            Str::Copy(entry.name, sizeof(entry.name), symbol->Name);
        } 
        else {
            DWORD gle = GetLastError();
            if (gle != ERROR_INVALID_ADDRESS && gle != ERROR_MOD_NOT_FOUND) {
                Debug::PrintLine("_SymGetSymFromAddr64 failed");
                break;
            }
            Str::Copy(entry.name, sizeof(entry.name), "[NA]");
        }

        if (_SymGetLineFromAddr64(gStacktrace.mProcess, (DWORD64)stackframes[i], (PDWORD)&(entry.offsetFromLine), &line)) {
            entry.line = line.LineNumber;
            Str::Copy(entry.filename, PATH_CHARS_MAX, line.FileName);
        } 
        else {
            DWORD gle = GetLastError();
            if (gle != ERROR_INVALID_ADDRESS && gle != ERROR_MOD_NOT_FOUND) {
                Debug::PrintLine("_SymGetLineFromAddr64 failed");
                break;
            }
            Str::Copy(entry.filename, PATH_CHARS_MAX, "[NA]");
        }

        memcpy(&entries[i], &entry, sizeof(DebugStacktraceEntry));
    }
    LeaveCriticalSection(&gStacktrace.mMutex);
}

void Debug::StacktraceSaveStopPoint(void*)
{
}

DebugStacktraceContext::DebugStacktraceContext() : mDbgHelp(nullptr), mProcess(nullptr)
{
    if constexpr (!CONFIG_FINAL_BUILD) {
        InitializeCriticalSectionAndSpinCount(&mMutex, 32);
        Debug::InitializeStacktrace();
        // TODO: I had to disable this, because in RenderDoc, this fails. Needs investigation
        // ASSERT(initialized);
    }
}

DebugStacktraceContext::~DebugStacktraceContext()
{
    if (mInitialized) {
        ASSERT(mDbgHelp);
        ASSERT(_SymCleanup);

        EnterCriticalSection(&mMutex);
        _SymCleanup(mProcess);
        FreeLibrary(mDbgHelp);
        mDbgHelp = nullptr;
        LeaveCriticalSection(&mMutex);

        // Note that we do not Delete the critical section for Tracy, because it will be used after deinitialization by Tracy
        #if defined(TRACY_ENABLE)
        DeleteCriticalSection(&mMutex);
        #endif
    }
}


//    ██████╗ ███████╗███╗   ███╗███████╗██████╗ ██╗   ██╗██████╗  ██████╗ 
//    ██╔══██╗██╔════╝████╗ ████║██╔════╝██╔══██╗╚██╗ ██╔╝██╔══██╗██╔════╝ 
//    ██████╔╝█████╗  ██╔████╔██║█████╗  ██║  ██║ ╚████╔╝ ██████╔╝██║  ███╗
//    ██╔══██╗██╔══╝  ██║╚██╔╝██║██╔══╝  ██║  ██║  ╚██╔╝  ██╔══██╗██║   ██║
//    ██║  ██║███████╗██║ ╚═╝ ██║███████╗██████╔╝   ██║   ██████╔╝╚██████╔╝
//    ╚═╝  ╚═╝╚══════╝╚═╝     ╚═╝╚══════╝╚═════╝    ╚═╝   ╚═════╝  ╚═════╝ 
static const char* RDBG_PIPE_NAME_PREFIX = "\\\\.\\pipe\\";
static constexpr uint32 RDBG_BUFFER_SIZE = 8*SIZE_KB;
static constexpr uint32 RDBG_LAUNCH_MAX_WAIT_TIME = 2000;
static constexpr uint32 RDBG_CONNECTION_RETRY_INTERVAL = 100;
static constexpr uint32 RDBG_CONNECTION_MAX_RETRIES = 5;

struct RDBG_Context
{
    OSProcess remedybgProc;
    HANDLE cmdPipe = INVALID_HANDLE_VALUE;
};
static RDBG_Context gRemedyBG;

namespace RDBG
{
    static Blob SendCommand(const Blob& cmdBuffer, MemAllocator* outBufferAlloc)
    {
        ASSERT(gRemedyBG.cmdPipe != INVALID_HANDLE_VALUE);

        uint8 tempBuffer[RDBG_BUFFER_SIZE];
        DWORD bytesRead;
        Blob outBuffer(outBufferAlloc);
        outBuffer.SetGrowPolicy(Blob::GrowPolicy::Linear);

        BOOL r = TransactNamedPipe(gRemedyBG.cmdPipe, const_cast<void*>(cmdBuffer.Data()), DWORD(cmdBuffer.Size()), tempBuffer, sizeof(tempBuffer), &bytesRead, nullptr);
        if (r)
            outBuffer.Write(tempBuffer, bytesRead);

        while (!r && GetLastError() == ERROR_MORE_DATA) {
            r = ReadFile(gRemedyBG.cmdPipe, tempBuffer, sizeof(tempBuffer), &bytesRead, nullptr);
            if (r)
                outBuffer.Write(tempBuffer, bytesRead);
        }

        if (!r) {
            LOG_ERROR("Reading RemedyBG pipe failed");
            RDBG::Release();
            return outBuffer;
        }

        return outBuffer;
    }

    static inline rdbg_CommandResult GetResult(Blob& resultBuff)
    {
        uint16 res;
        resultBuff.Read<uint16>(&res);
        return rdbg_CommandResult(res);
    }
}

bool RDBG::Initialize(const char* serverName, const char* remedybgPath)
{
    ASSERT(remedybgPath);
    ASSERT_MSG(gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE, "RemedyBG is initialized before");

    ASSERT_MSG(!OS::IsDebuggerPresent(), "Another debugger is already attached to this executable");
    ASSERT_ALWAYS(Str::Len(serverName) <= RDBG_MAX_SERVERNAME_LEN, "ServerName is too long for RemedyBG sessions: %s", serverName);

    // Open remedybg executable and attach the current process to it
    Path remedybgCmdline(remedybgPath);
    remedybgCmdline.Append(" --servername ");
    remedybgCmdline.Append(serverName);
    if (!gRemedyBG.remedybgProc.Run(remedybgCmdline.CStr(), OSProcessFlags::None)) {
        LOG_ERROR("RemedyBG: Could not run RemedyBG instance '%s'", remedybgPath);
        return false;
    }
    // wait until the program is actually running, then we can connect
    constexpr uint32 WAIT_TIME = 20;
    uint32 waitTm = 0;
    while (!gRemedyBG.remedybgProc.IsRunning() && waitTm < RDBG_LAUNCH_MAX_WAIT_TIME) {
        Thread::Sleep(WAIT_TIME);
        waitTm += WAIT_TIME;
    }
    if (!gRemedyBG.remedybgProc.IsRunning())
        return false;

    String<256> pipeName(RDBG_PIPE_NAME_PREFIX);
    pipeName.Append(serverName);

    uint32 retryCount = 0;
    while (gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE && retryCount < RDBG_CONNECTION_MAX_RETRIES) {
        gRemedyBG.cmdPipe = CreateFileA(pipeName.CStr(), GENERIC_READ|GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE) {
            LOG_ERROR("RemedyBG: Creating command pipe failed");
            return false;
        }
        Thread::Sleep(RDBG_CONNECTION_RETRY_INTERVAL);   // wait a little more so remedybg gets it's shit together
        ++retryCount;
    }
    
    DWORD newMode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(gRemedyBG.cmdPipe, &newMode, nullptr, nullptr)) {
        LOG_ERROR("RemedyBG: SetNamedPipeHandleState failed");
        return false;
    }

    if (RDBG::AttachToProcess(0)) {
        LOG_DEBUG("RemedyBG launched and attached to the process");
        return true;
    }
    else {
        LOG_ERROR("Attaching RemedyBG debugger to the current process failed");
        return false;
    }
}

void RDBG::Release()
{
    if (gRemedyBG.cmdPipe != INVALID_HANDLE_VALUE) 
        CloseHandle(gRemedyBG.cmdPipe);
    gRemedyBG.cmdPipe = INVALID_HANDLE_VALUE;
    if (gRemedyBG.remedybgProc.IsValid())
        gRemedyBG.remedybgProc.Abort();
}

#define DEBUG_REMEDYBG_BEGINCOMMAND(_cmd)   \
    MemTempAllocator tempAlloc; \
    Blob cmdBuffer(&tempAlloc); \
    cmdBuffer.SetGrowPolicy(Blob::GrowPolicy::Linear); \
    cmdBuffer.Write<uint16>(_cmd)

bool RDBG::AttachToProcess(uint32 id)
{
    if (gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE)
        return 0;

    DEBUG_REMEDYBG_BEGINCOMMAND(RDBG_COMMAND_ATTACH_TO_PROCESS_BY_PID);

    if (id == 0)
        id = GetCurrentProcessId();

    cmdBuffer.Write<uint32>(id);
    cmdBuffer.Write<rdbg_Bool>(true);
    cmdBuffer.Write<uint8>(RDBG_IF_DEBUGGING_TARGET_STOP_DEBUGGING);
    Blob res = RDBG::SendCommand(cmdBuffer, &tempAlloc);
    return RDBG::GetResult(res) == RDBG_COMMAND_RESULT_OK;
}

bool RDBG::DetachFromProcess()
{
    if (gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE)
        return 0;

    DEBUG_REMEDYBG_BEGINCOMMAND(RDBG_COMMAND_DETACH_FROM_PROCESS);
    Blob res = RDBG::SendCommand(cmdBuffer, &tempAlloc);
    return RDBG::GetResult(res) == RDBG_COMMAND_RESULT_OK;
}

bool RDBG::Break()
{
    if (gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE)
        return 0;
    
    DEBUG_REMEDYBG_BEGINCOMMAND(RDBG_COMMAND_BREAK_EXECUTION);
    Blob res = RDBG::SendCommand(cmdBuffer, &tempAlloc);
    return RDBG::GetResult(res) == RDBG_COMMAND_RESULT_OK;
}

bool RDBG::Continue()
{
    DEBUG_REMEDYBG_BEGINCOMMAND(RDBG_COMMAND_CONTINUE_EXECUTION);
    Blob res = RDBG::SendCommand(cmdBuffer, &tempAlloc);
    return RDBG::GetResult(res) == RDBG_COMMAND_RESULT_OK;
}

bool RDBG::RunToFileAtLine(const char* filename, uint32 line)
{
    if (gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE)
        return 0;

    ASSERT(filename);
    DEBUG_REMEDYBG_BEGINCOMMAND(RDBG_COMMAND_RUN_TO_FILE_AT_LINE);
    cmdBuffer.WriteStringBinary16(filename);
    cmdBuffer.Write<uint32>(line);
    Blob res = RDBG::SendCommand(cmdBuffer, &tempAlloc);
    return RDBG::GetResult(res) == RDBG_COMMAND_RESULT_OK;
}

RDBG_Id RDBG::AddFunctionBreakpoint(const char* funcName, const char* conditionExpr, uint32 overloadId)
{
    if (gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE)
        return 0;

    ASSERT(funcName);
    DEBUG_REMEDYBG_BEGINCOMMAND(RDBG_COMMAND_ADD_BREAKPOINT_AT_FUNCTION);
    cmdBuffer.WriteStringBinary16(funcName);
    cmdBuffer.Write<uint32>(overloadId);
    cmdBuffer.WriteStringBinary16(conditionExpr ? conditionExpr : "");
    Blob res = RDBG::SendCommand(cmdBuffer, &tempAlloc);

    RDBG_Id bid = 0;
    if (RDBG::GetResult(res) == RDBG_COMMAND_RESULT_OK)
        res.Read<RDBG_Id>(&bid);
    return bid;
}

RDBG_Id RDBG::AddFileLineBreakpoint(const char* filename, uint32 line, const char* conditionExpr)
{
    if (gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE)
        return 0;

    ASSERT(filename);
    DEBUG_REMEDYBG_BEGINCOMMAND(RDBG_COMMAND_ADD_BREAKPOINT_AT_FILENAME_LINE);
    cmdBuffer.WriteStringBinary16(filename);
    cmdBuffer.Write<uint32>(line);
    cmdBuffer.WriteStringBinary16(conditionExpr ? conditionExpr : "");
    Blob res = RDBG::SendCommand(cmdBuffer, &tempAlloc);

    RDBG_Id bid = 0;
    if (RDBG::GetResult(res) == RDBG_COMMAND_RESULT_OK)
        res.Read<RDBG_Id>(&bid);
    return bid;
}

RDBG_Id RDBG::AddAddressBreakpoint(uintptr_t addr, const char* conditionExpr)
{
    if (gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE)
        return 0;

    ASSERT(addr);
    DEBUG_REMEDYBG_BEGINCOMMAND(RDBG_COMMAND_ADD_BREAKPOINT_AT_ADDRESS);
    cmdBuffer.Write<uint64>(addr);
    cmdBuffer.WriteStringBinary16(conditionExpr ? conditionExpr : "");
    Blob res = RDBG::SendCommand(cmdBuffer, &tempAlloc);

    RDBG_Id bid = 0;
    if (RDBG::GetResult(res) == RDBG_COMMAND_RESULT_OK)
        res.Read<RDBG_Id>(&bid);
    return bid;
}

RDBG_Id RDBG::AddProcessorBreakpoint(const void* addr, uint8 numBytes, RDBG_ProcessorBreakpointType type, const char* conditionExpr)
{
    if (gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE)
        return 0;

    ASSERT_MSG(numBytes <= 8, "Processor breakpoints cannot be more than 8 bytes");

    ASSERT(addr);
    DEBUG_REMEDYBG_BEGINCOMMAND(RDBG_COMMAND_ADD_PROCESSOR_BREAKPOINT);

    char addrExpr[64];
    Str::PrintFmt(addrExpr, sizeof(addrExpr), "0x%llx", addr);
    cmdBuffer.WriteStringBinary16(addrExpr);
    cmdBuffer.Write<uint8>(numBytes);
    cmdBuffer.Write<uint8>(uint8(type));
    cmdBuffer.WriteStringBinary16(conditionExpr ? conditionExpr : "");
    Blob res = RDBG::SendCommand(cmdBuffer, &tempAlloc);

    RDBG_Id bid = 0;
    if (RDBG::GetResult(res) == RDBG_COMMAND_RESULT_OK)
        res.Read<RDBG_Id>(&bid);

    return bid;
}

bool RDBG::EnableBreakpoint(RDBG_Id bId, bool enable)
{
    if (gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE)
        return 0;

    DEBUG_REMEDYBG_BEGINCOMMAND(RDBG_COMMAND_ENABLE_BREAKPOINT);
    cmdBuffer.Write<rdbg_Id>(bId);
    cmdBuffer.Write<rdbg_Bool>(enable);
    Blob res = RDBG::SendCommand(cmdBuffer, &tempAlloc);
    return RDBG::GetResult(res) == RDBG_COMMAND_RESULT_OK;
}

bool RDBG::SetBreakpointCondition(RDBG_Id bId, const char* conditionExpr)
{
    if (gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE)
        return 0;

    DEBUG_REMEDYBG_BEGINCOMMAND(RDBG_COMMAND_ENABLE_BREAKPOINT);
    cmdBuffer.Write<rdbg_Id>(bId);
    cmdBuffer.WriteStringBinary16(conditionExpr ? conditionExpr : "");
    Blob res = RDBG::SendCommand(cmdBuffer, &tempAlloc);
    return RDBG::GetResult(res) == RDBG_COMMAND_RESULT_OK;
}

bool RDBG::DeleteBreakpoint(RDBG_Id bId)
{
    if (gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE)
        return 0;

    DEBUG_REMEDYBG_BEGINCOMMAND(RDBG_COMMAND_DELETE_BREAKPOINT);
    cmdBuffer.Write<rdbg_Id>(bId);
    Blob res = RDBG::SendCommand(cmdBuffer, &tempAlloc);
    return RDBG::GetResult(res) == RDBG_COMMAND_RESULT_OK;
}

bool RDBG::DeleteAllBreakpoints()
{
    if (gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE)
        return 0;

    DEBUG_REMEDYBG_BEGINCOMMAND(RDBG_COMMAND_DELETE_ALL_BREAKPOINTS);
    Blob res = RDBG::SendCommand(cmdBuffer, &tempAlloc);
    return RDBG::GetResult(res) == RDBG_COMMAND_RESULT_OK;
}

RDBG_Id RDBG::AddWatch(const char* expr, const char* comment, uint8 windowNum)
{
    if (gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE)
        return 0;

    ASSERT(expr);
    DEBUG_REMEDYBG_BEGINCOMMAND(RDBG_COMMAND_ADD_WATCH);
    cmdBuffer.Write<uint8>(windowNum);
    cmdBuffer.WriteStringBinary16(expr);
    cmdBuffer.WriteStringBinary16(comment ? comment : "");
    Blob res = RDBG::SendCommand(cmdBuffer, &tempAlloc);

    RDBG_Id wid = 0;
    if (RDBG::GetResult(res) == RDBG_COMMAND_RESULT_OK)
        res.Read<RDBG_Id>(&wid);
    return wid;
}

RDBG_Id RDBG::DeleteWatch(RDBG_Id wId)
{
    if (gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE)
        return 0;

    ASSERT(wId);
    DEBUG_REMEDYBG_BEGINCOMMAND(RDBG_COMMAND_DELETE_WATCH);
    cmdBuffer.Write<rdbg_Id>(wId);
    Blob res = RDBG::SendCommand(cmdBuffer, &tempAlloc);

    RDBG_Id wid = 0;
    if (RDBG::GetResult(res) == RDBG_COMMAND_RESULT_OK)
        res.Read<RDBG_Id>(&wid);
    return wid;
}

bool RDBG::DeleteAllWatches()
{
    if (gRemedyBG.cmdPipe == INVALID_HANDLE_VALUE)
        return 0;

    DEBUG_REMEDYBG_BEGINCOMMAND(RDBG_COMMAND_DELETE_ALL_WATCHES);
    Blob res = RDBG::SendCommand(cmdBuffer, &tempAlloc);
    return RDBG::GetResult(res) == RDBG_COMMAND_RESULT_OK;
}

#endif // PLATFORM_WINDOWS
