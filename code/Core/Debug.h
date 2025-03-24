#pragma once

#include "Base.h"

struct DebugStacktraceEntry
{
    char name[PATH_CHARS_MAX];
    char filename[PATH_CHARS_MAX];
    uint64 offsetFromSymbol;
    uint32 offsetFromLine;
    uint32 line;
};

using DebugFiberScopeProtectorCallback = bool(*)(void* userData);

namespace Debug
{
    API void PrintLine(const char* text);
    API void PrintLineFmt(const char* fmt, ...);
    API void SetCaptureStacktraceForFiberProtector(bool capture);
    API uint16 CaptureStacktrace(void** stackframes, uint16 maxStackframes, uint16 framesToSkip = 1, uint32* pHash = nullptr);
    API void ResolveStacktrace(uint16 numStacktrace, void* const* stackframes, DebugStacktraceEntry* entries);

    // Call this to record the current calling function as a stop point for future callstack captures
    // This is because of a stupid llvm/libunwind bugs with some android versions, where we get crash at some frames like fibers
    API void StacktraceSaveStopPoint(void* funcPtr);

    // Fiber protector callback: Return true if we are operating in the fiber, otherwise false
    API void FiberScopeProtector_RegisterCallback(DebugFiberScopeProtectorCallback callback, void* userData = nullptr);
    API uint16 FiberScopeProtector_Push(const char* name);
    API void FiberScopeProtector_Pop(uint16 id);
    API void FiberScopeProtector_Check();
}

#if PLATFORM_WINDOWS && defined(TRACY_ENABLE)
    #ifdef __cplusplus
    extern "C" {
    #endif
        void DebugDbgHelpInit();
        void DebugDbgHelpLock();
        void DebugDbgHelpUnlock();
    #ifdef __cplusplus
    }
    #endif
#endif


//    ██████╗ ███████╗███╗   ███╗███████╗██████╗ ██╗   ██╗██████╗  ██████╗ 
//    ██╔══██╗██╔════╝████╗ ████║██╔════╝██╔══██╗╚██╗ ██╔╝██╔══██╗██╔════╝ 
//    ██████╔╝█████╗  ██╔████╔██║█████╗  ██║  ██║ ╚████╔╝ ██████╔╝██║  ███╗
//    ██╔══██╗██╔══╝  ██║╚██╔╝██║██╔══╝  ██║  ██║  ╚██╔╝  ██╔══██╗██║   ██║
//    ██║  ██║███████╗██║ ╚═╝ ██║███████╗██████╔╝   ██║   ██████╔╝╚██████╔╝
//    ╚═╝  ╚═╝╚══════╝╚═╝     ╚═╝╚══════╝╚═════╝    ╚═╝   ╚═════╝  ╚═════╝ 
#if PLATFORM_WINDOWS 
using RDBG_Id = uint32;

// rdbg_ProcessorBreakpointAccessKind
enum class RDBG_ProcessorBreakpointType : uint8
{
    Write = 1,
    ReadWrite = 2,
    Execute = 3
};

namespace RDBG
{
    API bool Initialize(const char* serverName, const char* remedybgPath);
    API void Release();
    API bool AttachToProcess(uint32 id = 0);
    API bool DetachFromProcess();
    API bool Break();
    API bool Continue();
    API bool RunToFileAtLine(const char* filename, uint32 line);
    API RDBG_Id AddFunctionBreakpoint(const char* funcName, const char* conditionExpr = nullptr, uint32 overloadId = 0);
    API RDBG_Id AddFileLineBreakpoint(const char* filename, uint32 line, const char* conditionExpr = nullptr);
    API RDBG_Id AddAddressBreakpoint(uintptr_t addr, const char* conditionExpr = nullptr);
    API RDBG_Id AddProcessorBreakpoint(const void* addr, uint8 numBytes, 
                                       RDBG_ProcessorBreakpointType type, const char* conditionExpr = nullptr);
    API bool EnableBreakpoint(RDBG_Id bId, bool enable);
    API bool SetBreakpointCondition(RDBG_Id bId, const char* conditionExpr);
    API bool DeleteBreakpoint(RDBG_Id bId);
    API bool DeleteAllBreakpoints();
    API RDBG_Id AddWatch(const char* expr, const char* comment, uint8 windowNum);
    API RDBG_Id DeleteWatch(RDBG_Id wId);
    bool DeleteAllWatches();
}

#endif // PLATFORM_WINDOWS

