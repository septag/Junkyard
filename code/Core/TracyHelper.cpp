#include "TracyHelper.h"

#ifdef TRACY_ENABLE

// DbgHelpInit/Lock/Unlock are declared in Debug.h/implemented in DebugWin.cpp
#define TRACY_DBGHELP_LOCK debugDbgHelp
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4530)   // C4530: C++ exception handler used, but unwind semantics are not enabled. Specify /EHsc
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wsometimes-uninitialized")
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wunused-variable")
#ifdef PLATFORM_POSIX
    #define OLD_PLATFORM_POSIX PLATFORM_POSIX
    #undef PLATFORM_POSIX
#endif
#ifdef PLATFORM_WINDOWS
    // windows version is using "fileno" api which gives us linker errors 
    #define fileno _fileno
    #define OLD_PLATFORM_WINDOWS PLATFORM_WINDOWS
    #undef PLATFORM_WINDOWS
#endif
#define TRACY_UNWIND(_stackframes, _depth) debugCaptureStacktrace(_stackframes, _depth, 2)
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

#include <string.h>

static TracyZoneEnterCallback gZoneEnterCallback;
static TracyZoneExitCallback gZoneExitCallback;

void tracySetZoneCallbacks(TracyZoneEnterCallback zoneEnterCallback, TracyZoneExitCallback zoneExitCallback)
{
    gZoneEnterCallback = zoneEnterCallback;
    gZoneExitCallback = zoneExitCallback;
}

bool tracyRunZoneExitCallback(TracyCZoneCtx* ctx)
{
    if (gZoneExitCallback)
        return gZoneExitCallback(ctx);
    else
        return false;
}

void tracyRunZoneEnterCallback(TracyCZoneCtx* ctx, const ___tracy_source_location_data* sourceLoc)
{
    if (gZoneEnterCallback)
        gZoneEnterCallback(ctx, sourceLoc);
}


void _private::___tracy_emit_gpu_calibrate_serial(const struct ___tracy_gpu_calibrate_data data)
{
    auto item = tracy::Profiler::QueueSerial();
    tracy::MemWrite(&item->hdr.type, tracy::QueueType::GpuCalibration);
    tracy::MemWrite(&item->gpuCalibration.gpuTime, data.gpuTime);
    tracy::MemWrite(&item->gpuCalibration.cpuTime, data.cpuTime);
    tracy::MemWrite(&item->gpuCalibration.cpuDelta, data.deltaTime);
    tracy::MemWrite(&item->gpuCalibration.context, data.context);
    tracy::Profiler::QueueSerialFinish();
}

int64 _private::__tracy_get_time(void)
{
    return tracy::Profiler::GetTime();
}

uint64 _private::__tracy_alloc_source_loc(uint32 line, const char* source, const char* function, const char* name)
{
    return tracy::Profiler::AllocSourceLocation(line, source, function, name, name ? strlen(name): 0);
}

#endif  // TRACY_ENABLE
