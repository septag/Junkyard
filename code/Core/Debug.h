#pragma once

#include "Base.h"

void debugPrint(const char* text);

struct DebugStacktraceEntry
{
    char name[kMaxPath];
    char filename[kMaxPath];
    uint64 offsetFromSymbol;
    uint32 offsetFromLine;
    uint32 line;
};

API void debugSetCaptureStacktraceForFiberProtector(bool capture);
API uint16 debugCaptureStacktrace(void** stackframes, uint16 maxStackframes, uint16 framesToSkip = 1, uint32* pHash = nullptr);
API void debugResolveStacktrace(uint16 numStacktrace, void* const* stackframes, DebugStacktraceEntry* entries);

// Call this to record the current calling function as a stop point for future callstack captures
// This is because of a stupid llvm/libunwind bugs with some android versions, where we get crash at some frames like fibers
API void debugStacktraceSaveStopPoint(void* funcPtr);

// Fiber protector callback: Return true if we are operating in the fiber, otherwise false
using DebugFiberScopeProtectorCallback = bool(*)(void* userData);
API void debugFiberScopeProtector_RegisterCallback(DebugFiberScopeProtectorCallback callback, void* userData = nullptr);
API uint16 debugFiberScopeProtector_Push(const char* name);
API void debugFiberScopeProtector_Pop(uint16 id);
API void debugFiberScopeProtector_Check();

#if PLATFORM_WINDOWS && defined(TRACY_ENABLE)
    #ifdef __cplusplus
    extern "C" {
    #endif
        void debugDbgHelpInit();
        void debugDbgHelpLock();
        void debugDbgHelpUnlock();
    #ifdef __cplusplus
    }
    #endif
#endif

// RemedyBG integration
#if PLATFORM_WINDOWS 
using DebugRemedyBG_Id = uint32;

// rdbg_ProcessorBreakpointAccessKind
enum class DebugRemedyBG_ProcessorBreakpointType : uint8
{
    Write = 1,
    ReadWrite = 2,
    Execute = 3
};

API bool debugRemedyBG_Initialize(const char* serverName, const char* remedybgPath);
API void debugRemedyBG_Release();
API bool debugRemedyBG_AttachToProcess(uint32 id = 0);
API bool debugRemedyBG_DetachFromProcess();
API bool debugRemedyBG_Break();
API bool debugRemedyBG_Continue();
API bool debugRemedyBG_RunToFileAtLine(const char* filename, uint32 line);
API DebugRemedyBG_Id debugRemedyBG_AddFunctionBreakpoint(const char* funcName, const char* conditionExpr = nullptr, uint32 overloadId = 0);
API DebugRemedyBG_Id debugRemedyBG_AddFileLineBreakpoint(const char* filename, uint32 line, const char* conditionExpr = nullptr);
API DebugRemedyBG_Id debugRemedyBG_AddAddressBreakpoint(uintptr_t addr, const char* conditionExpr = nullptr);
API DebugRemedyBG_Id debugRemedyBG_AddProcessorBreakpoint(const char* addrExpr, uint8 numBytes, 
                                                          DebugRemedyBG_ProcessorBreakpointType type, const char* conditionExpr = nullptr);
API bool debugRemedyBG_EnableBreakpoint(DebugRemedyBG_Id bId, bool enable);
API bool debugRemedyBG_SetBreakpointCondition(DebugRemedyBG_Id bId, const char* conditionExpr);
API bool debugRemedyBG_DeleteBreakpoint(DebugRemedyBG_Id bId);
API bool debugRemedyBG_DeleteAllBreakpoints();
API DebugRemedyBG_Id debugRemedyBG_AddWatch(const char* expr, const char* comment, uint8 windowNum);
API DebugRemedyBG_Id debugRemedyBG_DeleteWatch(DebugRemedyBG_Id wId);
#endif // PLATFORM_WINDOWS

