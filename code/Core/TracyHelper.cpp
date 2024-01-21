#include "TracyHelper.h"

#ifdef TRACY_ENABLE

// DbgHelpInit/Lock/Unlock are declared in Debug.h/implemented in DebugWin.cpp
#define TRACY_DBGHELP_LOCK DbgHelp

PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4530)   // C4530: C++ exception handler used, but unwind semantics are not enabled. Specify /EHsc
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wsometimes-uninitialized")
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wunused-variable")
#ifdef PLATFORM_POSIX
    #undef PLATFORM_POSIX
#endif
#ifdef PLATFORM_WINDOWS
    #undef PLATFORM_WINDOWS
#endif
#define TRACY_UNWIND(_stackframes, _depth) debugCaptureStacktrace(_stackframes, _depth, 2)
#include "External/tracy/TracyClient.cpp"
PRAGMA_DIAGNOSTIC_POP()

#include "StringUtil.h"

//------------------------------------------------------------------------
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
    return tracy::Profiler::AllocSourceLocation(line, source, strLen(source), function, strLen(function), 
                                                name, name ? strLen(name): 0);
}

#endif  // TRACY_ENABLE
