#include "TracyHelper.h"
#include "StringUtil.h"

#ifdef TRACY_ENABLE

// DbgHelpInit/Lock/Unlock are declared in Debug.h/implemented in DebugWin.cpp
#define TRACY_DBGHELP_LOCK DebugDbgHelp
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4530)   // C4530: C++ exception handler used, but unwind semantics are not enabled. Specify /EHsc
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wsometimes-uninitialized")
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wunused-variable")
#if PLATFORM_POSIX
    #define OLD_PLATFORM_POSIX PLATFORM_POSIX
    #undef PLATFORM_POSIX
#endif

#if PLATFORM_WINDOWS
    // windows version is using "fileno" api which gives us linker errors 
    #define fileno _fileno
    #define OLD_PLATFORM_WINDOWS PLATFORM_WINDOWS
    #undef PLATFORM_WINDOWS
#endif
// #define TRACY_UNWIND(_stackframes, _depth) Debug::CaptureStacktrace(_stackframes, _depth, 2)
#define TRACY_VK_USE_SYMBOL_TABLE

#include "External/tracy/TracyClient.cpp"
PRAGMA_DIAGNOSTIC_POP()

// Restore our PLATFORM_ macros
#ifdef OLD_PLATFORM_POSIX
    #undef PLATFORM_POSIX
    #define PLATFORM_POSIX OLD_PLATFORM_POSIX
#endif

#ifdef OLD_PLATFORM_WINDOWS
    #undef PLATFORM_WINDOWS
    #define PLATFORM_WINDOWS OLD_PLATFORM_WINDOWS
#endif

static TracyZoneEnterCallback gZoneEnterCallback;
static TracyZoneExitCallback gZoneExitCallback;

void Tracy::SetZoneCallbacks(TracyZoneEnterCallback zoneEnterCallback, TracyZoneExitCallback zoneExitCallback)
{
    gZoneEnterCallback = zoneEnterCallback;
    gZoneExitCallback = zoneExitCallback;
}

bool Tracy::RunZoneExitCallback(TracyCZoneCtx* ctx)
{
    if (gZoneExitCallback)
        return gZoneExitCallback(ctx);
    else
        return false;
}

void Tracy::RunZoneEnterCallback(TracyCZoneCtx* ctx, const ___tracy_source_location_data* sourceLoc)
{
    if (gZoneEnterCallback)
        gZoneEnterCallback(ctx, sourceLoc);
}


int64 Tracy::GetTime()
{
    return tracy::Profiler::GetTime();
}

Tracy::CpuProfilerScope::CpuProfilerScope(const ___tracy_source_location_data* sourceLoc, int callstackDepth, bool isActive, bool isAlloc)
{ 
    mCtx = {};

    if (callstackDepth > 0) {
        if (isAlloc) {
            mCtx = ___tracy_emit_zone_begin_alloc_callstack(
                ___tracy_alloc_srcloc_name(sourceLoc->line, sourceLoc->file, Str::Len(sourceLoc->file), 
                                           sourceLoc->function, Str::Len(sourceLoc->function), sourceLoc->name, Str::Len(sourceLoc->name),
                                           sourceLoc->color), callstackDepth, isActive);
        }
        else {
            mCtx = ___tracy_emit_zone_begin_callstack(sourceLoc, callstackDepth, isActive);
        }
    }
    else {
        if (isAlloc) {
            mCtx = ___tracy_emit_zone_begin_alloc(
                ___tracy_alloc_srcloc_name(sourceLoc->line, sourceLoc->file, Str::Len(sourceLoc->file), 
                                           sourceLoc->function, Str::Len(sourceLoc->function), sourceLoc->name, Str::Len(sourceLoc->name),
                                           sourceLoc->color), isActive);
        }
        else {
            mCtx = ___tracy_emit_zone_begin(sourceLoc, isActive);
        }
    }

    Tracy::RunZoneEnterCallback(&mCtx, sourceLoc); 
}

Tracy::CpuProfilerScope::~CpuProfilerScope() 
{ 
    if (!Tracy::RunZoneExitCallback(&mCtx)) 
        ___tracy_emit_zone_end(mCtx); 
}


#endif  // TRACY_ENABLE
