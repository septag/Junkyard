#include "Debug.h"

#if COMPILER_CLANG

#include <unwind.h> // _Unwind_Backtrace
#include <dlfcn.h>  // dladdr
#include <cxxabi.h> // __cxa_demangle

#if PLATFORM_ANDROID
#include <malloc.h>
#elif PLATFORM_APPLE
#include <stdlib.h>    
#endif

#include "StringUtil.h"
#include "Hash.h"
#include "Arrays.h"

// skip 1st frame, because it is actually the `debugCaptureStacktrace` call
#define DEBUG_STACKTRACE_SKIP_FRAMES 1
#define DEBUG_STACKTRACE_HASH_SEED 0x0CCE41BB

struct DebugStacktraceState
{
    void** current;
    void** end;
    uint16 framesToSkip;
    uint16 numFrames;
};

static StaticArray<void*, 16> gDebugStopFuncs;

_Unwind_Reason_Code debugUnwindCallback(_Unwind_Context* context, void* arg)
{
    DebugStacktraceState* state = reinterpret_cast<DebugStacktraceState*>(arg);

    state->numFrames++;
    if (state->numFrames <= state->framesToSkip)
        return _URC_NO_REASON;

    void* ip = reinterpret_cast<void*>(_Unwind_GetIP(context));
    if (ip) {
        bool endOfStack = false;
        if (gDebugStopFuncs.Count()) {
            void* fn = _Unwind_FindEnclosingFunction(ip);
            endOfStack = gDebugStopFuncs.FindIf([fn](const void* _fn)->bool { return fn == _fn; }) != UINT32_MAX;
        }

        if (state->current == state->end || endOfStack)
            return _URC_END_OF_STACK;
        else
            *state->current++ = ip;
    }
    return _URC_NO_REASON;
}

NO_INLINE uint16 debugCaptureStacktrace(void** stackframes, uint16 maxStackframes, uint16 framesToSkip, uint32* pHash)
{
    ASSERT(maxStackframes);
    DebugStacktraceState state {stackframes, stackframes + maxStackframes, framesToSkip};
    _Unwind_Backtrace(debugUnwindCallback, &state);
    uint32 numStacktrace = PtrToInt<uint16>((void*)(state.current - stackframes));

    if (pHash)
        *pHash = hashMurmur32(stackframes, sizeof(void*)*numStacktrace, DEBUG_STACKTRACE_HASH_SEED);

    return numStacktrace;
}

void debugResolveStacktrace(uint16 numStacktrace, void* const* stackframes, DebugStacktraceEntry* entries)
{
    for (uint16 i = 0; i < numStacktrace; i++) {
        memset(&entries[i], 0x0, sizeof(entries[i]));

        const void* addr = stackframes[i];
        Dl_info info;
        if (dladdr(addr, &info)) {
            strCopy(entries[i].filename, sizeof(entries[i].filename), info.dli_fname);
            strCopy(entries[i].name, sizeof(entries[i].name), info.dli_sname);

            int status = 0;
            char* demangled = abi::__cxa_demangle(entries[i].name, 0, 0, &status);
            if (status == 0)
                strCopy(entries[i].name, sizeof(entries[i].name), demangled);
            ::free(demangled);
        }
    }
}

void debugStacktraceSaveStopPoint(void* funcPtr)
{
    ASSERT(funcPtr);
    ASSERT_MSG(gDebugStopFuncs.FindIf([funcPtr](const void* fn)->bool { return funcPtr == fn; }) == UINT32_MAX, 
               "Function pointer is already saved");
    gDebugStopFuncs.Add(funcPtr);
}

#endif // COMPILER_CLANG

