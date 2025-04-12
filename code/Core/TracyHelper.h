#pragma once

#include "Base.h"

#ifndef TRACY_CALLSTACK
    #define TRACY_CALLSTACK 16
#endif

#ifndef TRACY_FIBERS
    #define TRACY_FIBERS
#endif

// TODO: I had to disable callstacks, because of Crashes that I got from the Tracy profiler side. Have to investigate
#if PLATFORM_LINUX || PLATFORM_WINDOWS
    #define TRACY_NO_CALLSTACK
#endif

#include "External/tracy/tracy/TracyC.h"

#ifdef TRACY_ENABLE
    using TracyZoneEnterCallback = void(*)(TracyCZoneCtx* ctx, const ___tracy_source_location_data* sourceLoc);
    using TracyZoneExitCallback = bool(*)(TracyCZoneCtx* ctx);

    namespace Tracy
    {
        API void SetZoneCallbacks(TracyZoneEnterCallback zoneEnterCallback, TracyZoneExitCallback zoneExitCallback);
        API void RunZoneEnterCallback(TracyCZoneCtx* ctx, const ___tracy_source_location_data* sourceLoc);
        API bool RunZoneExitCallback(TracyCZoneCtx* ctx);

        API int64 GetTime();

        struct CpuProfilerScope
        {
            TracyCZoneCtx mCtx;

            CpuProfilerScope() = delete;

            explicit CpuProfilerScope(const ___tracy_source_location_data* sourceLoc, int callstackDepth, bool isActive, bool isAlloc);
            ~CpuProfilerScope();
        };
    }

    #define TracyCRealloc(oldPtr, ptr, size) if (oldPtr) { TracyCFree(oldPtr); }  TracyCAlloc(ptr, size)

    TRACY_API uint64_t ___tracy_alloc_srcloc_name( uint32_t line, const char* source, size_t sourceSz, const char* function, size_t functionSz, const char* name, size_t nameSz, uint32_t color );

    // TODO: Change _NAME macros so that they stop using static constexpr structs and instead use ___tracy_alloc_srcloc_name, ___tracy_emit_zone_begin_alloc_callstack APIs
    // NOTE: uint64 Id for __tracy_source_location_data is actually the pointer to it. So for static ones, we just cast it. For dynamics we use ___tracy_alloc_srcloc_name
    #if defined TRACY_HAS_CALLSTACK && defined TRACY_CALLSTACK
        #define PROFILE_ZONE_OPT(active, name) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            Tracy::CpuProfilerScope CONCAT(__cpu_profiler,__LINE__)(&CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active, false)
        #define PROFILE_ZONE_ALLOC_OPT(name, active) \
            struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            Tracy::CpuProfilerScope CONCAT(__cpu_profiler,__LINE__)(&CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active, true)
        #define PROFILE_ZONE_COLOR_OPT(name, color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            Tracy::CpuProfilerScope CONCAT(__cpu_profiler,__LINE__)(&CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active, false)
        #define PROFILE_ZONE_ALLOC_COLOR_OPT(name, color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            Tracy::CpuProfilerScope CONCAT(__cpu_profiler,__LINE__)(&CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active, true)

        #define PROFILE_ZONE(name) PROFILE_ZONE_OPT(name, true)
        #define PROFILE_ZONE_ALLOC(name) PROFILE_ZONE_ALLOC_OPT(name, true)
        #define PROFILE_ZONE_COLOR(name, color) PROFILE_ZONE_COLOR_OPT(name, color, true)
        #define PROFILE_ZONE_ALLOC_COLOR(name, color) PROFILE_ZONE_ALLOC_COLOR_OPT(name, color, true)
    #else
        #define PROFILE_ZONE_OPT(name, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            Tracy::CpuProfilerScope CONCAT(__cpu_profiler,__LINE__)(&CONCAT(__tracy_source_location,__LINE__), 0, active, false)
        #define PROFILE_ZONE_ALLOC_OPT(name, active) \
            struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            Tracy::CpuProfilerScope CONCAT(__cpu_profiler,__LINE__)(&CONCAT(__tracy_source_location,__LINE__), 0, active, true)
        #define PROFILE_ZONE_COLOR_OPT(name, color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            Tracy::CpuProfilerScope CONCAT(__cpu_profiler,__LINE__)(&CONCAT(__tracy_source_location,__LINE__), 0, active, false)
        #define PROFILE_ZONE_ALLOC_COLOR_OPT(name, color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            Tracy::CpuProfilerScope CONCAT(__cpu_profiler,__LINE__)(&CONCAT(__tracy_source_location,__LINE__), 0, active, true)

        #define PROFILE_ZONE(name) PROFILE_ZONE_OPT(name, true)
        #define PROFILE_ZONE_ALLOC(name) PROFILE_ZONE_ALLOC_OPT(name, true)
        #define PROFILE_ZONE_COLOR(name, color) PROFILE_ZONE_COLOR_OPT(name, color, true)
        #define PROFILE_ZONE_ALLOC_COLOR(name, color) PROFILE_ZONE_ALLOC_COLOR_OPT(name, color, true)
    #endif // else: TRACY_HAS_CALLBACK
#else
    #define PROFILE_ZONE_OPT(name, active)
    #define PROFILE_ZONE_ALLOC_OPT(name, active)
    #define PROFILE_ZONE_COLOR_OPT(name, color, active)
    #define PROFILE_ZONE_ALLOC_COLOR_OPT(name, color, active)

    #define PROFILE_ZONE(name)
    #define PROFILE_ZONE_ALLOC(name)
    #define PROFILE_ZONE_COLOR(name, color)
    #define PROFILE_ZONE_ALLOC_COLOR(name, color)

    #define TracyCRealloc(oldPtr, ptr, size)
#endif  // TRACY_ENABLE
