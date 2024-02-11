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
        void DbgHelpInit();
        void DbgHelpLock();
        void DbgHelpUnlock();
    #ifdef __cplusplus
    }
    #endif
#endif
