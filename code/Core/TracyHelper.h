#pragma once

#include "Base.h"

#ifndef TRACY_CALLSTACK
    #define TRACY_CALLSTACK 16
#endif

#ifndef TRACY_FIBERS
    #define TRACY_FIBERS
#endif

#include "External/tracy/TracyC.h"

#ifdef TRACY_ENABLE
    namespace _private
    {
        struct ___tracy_gpu_calibrate_data
        {
            int64 gpuTime;
            int64 cpuTime;
            int64 deltaTime;
            uint8 context;
        };

        void ___tracy_emit_gpu_calibrate_serial(const struct ___tracy_gpu_calibrate_data data);
        int64 __tracy_get_time(void);
        uint64 __tracy_alloc_source_loc(uint32 line, const char* source, const char* function, const char* name);

        //------------------------------------------------------------------------
        // Scoped objects
        struct TracyCZoneScope
        {
            TracyCZoneCtx _ctx;
    
            TracyCZoneScope() = delete;
            explicit TracyCZoneScope(TracyCZoneCtx ctx) : _ctx(ctx) {}
            ~TracyCZoneScope() { TracyCZoneEnd(_ctx); }
        };
    }

    #define TracyCRealloc(oldPtr, ptr, size) \
        if (oldPtr) {  \
            TracyCFree(oldPtr);    \
        }   \
        TracyCAlloc(ptr, size);   

    #if defined TRACY_HAS_CALLSTACK && defined TRACY_CALLSTACK
        #define PROFILE_ZONE(active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { NULL, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin_callstack(&CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active));
        #define PROFILE_ZONE_NAME(name, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin_callstack( &CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active ));
        #define PROFILE_ZONE_COLOR(color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { NULL, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin_callstack( &CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active ));
        #define PROFILE_ZONE_NAME_COLOR(name, color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin_callstack( &CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active ));
        #define PROFILE_ZONE_WITH_TEXT(text, textLen, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { NULL, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin_callstack(&CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active)); \
            TracyCZoneText(CONCAT(__tracy_ctx,__LINE__)._ctx, text, textLen)
    #else
        #define PROFILE_ZONE(active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { NULL, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin( &CONCAT(__tracy_source_location,__LINE__), active ));
        #define PROFILE_ZONE_NAME(name, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin( &CONCAT(__tracy_source_location,__LINE__), active ));
        #define PROFILE_ZONE_COLOR(color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { NULL, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin( &CONCAT(__tracy_source_location,__LINE__), active ));
        #define PROFILE_ZONE_NAME_COLOR(name, color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin( &CONCAT(__tracy_source_location,__LINE__), active ));
        #define PROFILE_ZONE(text, textLen, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { NULL, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin( &CONCAT(__tracy_source_location,__LINE__), active )); \
            TracyCZoneText(CONCAT(__tracy_ctx,__LINE__)._ctx, text, textLen)
    #endif // else: TRACY_HAS_CALLBACK
#else
    #define PROFILE_ZONE(active)
    #define PROFILE_ZONE_NAME(name, active)
    #define PROFILE_ZONE_COLOR(color, active)
    #define PROFILE_ZONE_NAME_COLOR(name, color, active)
    #define PROFILE_ZONE_WITH_TEXT(text, textLen, active)

    #define TracyCRealloc(oldPtr, ptr, size)
#endif  // TRACY_ENABLE
