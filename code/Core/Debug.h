#pragma once

#include "Base.h"

#if PLATFORM_ANDROID
    #include <signal.h> // raise
#endif

struct DebugStacktraceEntry
{
    char name[kMaxPath];
    char filename[kMaxPath];
    uint64 offsetFromSymbol;
    uint32 offsetFromLine;
    uint32 line;
};

API void debugBreakMessage(const char* fmt, ...);
API void debugPrint(const char* text);
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

#if PLATFORM_ANDROID
    #define DEBUG_BREAK() raise(SIGINT)
#elif COMPILER_MSVC
    #define DEBUG_BREAK() __debugbreak()
#elif COMPILER_CLANG
    #if (__has_builtin(__builtin_debugtrap))
        #define DEBUG_BREAK() __builtin_debugtrap()
    #else
        #define DEBUG_BREAK() __builtin_trap()    // This cannot be used in constexpr functions
    #endif 
#elif COMPILER_GCC
    #define DEBUG_BREAK() __builtin_trap()
#endif

// Assert macros
// ASSERT: regular assert
// ASSERT_MSG: Assert with a message spitted into debug output
// ASSERT_ALWAYS: Assert even in release builds

#ifdef ASSERT
    #undef ASSERT
#endif

#if CONFIG_ENABLE_ASSERT
    #define ASSERT(_expr) do { if (!(_expr)) { debugBreakMessage(#_expr); DEBUG_BREAK(); }} while(0)
    #define ASSERT_MSG(_expr, ...) do { if (!(_expr)) { debugBreakMessage(__VA_ARGS__); DEBUG_BREAK(); }} while(0)
#else
    #define ASSERT(_expr)
    #define ASSERT_MSG(_expr, ...)
#endif

#define ASSERT_ALWAYS(_expr, ...) do { if (!(_expr)) { debugBreakMessage(__VA_ARGS__); DEBUG_BREAK(); }} while(0)

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
