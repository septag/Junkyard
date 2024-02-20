#pragma once

#include "Base.h"

#ifndef TRACY_CALLSTACK
    #define TRACY_CALLSTACK 16
#endif

#ifndef TRACY_FIBERS
    #define TRACY_FIBERS
#endif

#include "External/tracy/tracy/TracyC.h"

#ifdef TRACY_ENABLE
    using TracyZoneEnterCallback = void(*)(TracyCZoneCtx* ctx, const ___tracy_source_location_data* sourceLoc);
    using TracyZoneExitCallback = bool(*)(TracyCZoneCtx* ctx);

    API void tracySetZoneCallbacks(TracyZoneEnterCallback zoneEnterCallback, TracyZoneExitCallback zoneExitCallback);
    API void tracyRunZoneEnterCallback(TracyCZoneCtx* ctx, const ___tracy_source_location_data* sourceLoc);
    API bool tracyRunZoneExitCallback(TracyCZoneCtx* ctx);

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

        struct TracyCZoneScope
        {
            TracyCZoneCtx mCtx;
            const ___tracy_source_location_data* mSourceLoc;
    
            TracyCZoneScope() = delete;
            explicit TracyCZoneScope(TracyCZoneCtx ctx, const ___tracy_source_location_data* sourceLoc) : mCtx(ctx), mSourceLoc(sourceLoc) { tracyRunZoneEnterCallback(&ctx, sourceLoc); }
            ~TracyCZoneScope() { if (!tracyRunZoneExitCallback(&mCtx)) { TracyCZoneEnd(mCtx); }}
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
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin_callstack(&CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active), &CONCAT(__tracy_source_location,__LINE__));
        #define PROFILE_ZONE_NAME(name, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin_callstack( &CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active ), &CONCAT(__tracy_source_location,__LINE__));
        #define PROFILE_ZONE_COLOR(color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { NULL, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin_callstack( &CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active ), &CONCAT(__tracy_source_location,__LINE__));
        #define PROFILE_ZONE_NAME_COLOR(name, color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin_callstack( &CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active ), &CONCAT(__tracy_source_location,__LINE__));
        #define PROFILE_ZONE_WITH_TEXT(text, textLen, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { NULL, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin_callstack(&CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active), &CONCAT(__tracy_source_location,__LINE__)); \
            TracyCZoneText(CONCAT(__tracy_ctx,__LINE__).mCtx, text, textLen)
    #else
        #define PROFILE_ZONE(active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { NULL, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin( &CONCAT(__tracy_source_location,__LINE__), active ), &CONCAT(__tracy_source_location,__LINE__));
        #define PROFILE_ZONE_NAME(name, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin( &CONCAT(__tracy_source_location,__LINE__), active ), &CONCAT(__tracy_source_location,__LINE__));
        #define PROFILE_ZONE_COLOR(color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { NULL, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin( &CONCAT(__tracy_source_location,__LINE__), active ), &CONCAT(__tracy_source_location,__LINE__));
        #define PROFILE_ZONE_NAME_COLOR(name, color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin( &CONCAT(__tracy_source_location,__LINE__), active ), &CONCAT(__tracy_source_location,__LINE__));
        #define PROFILE_ZONE(text, textLen, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { NULL, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            _private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin( &CONCAT(__tracy_source_location,__LINE__), active ), &CONCAT(__tracy_source_location,__LINE__)); \
            TracyCZoneText(CONCAT(__tracy_ctx,__LINE__).mCtx, text, textLen)
    #endif // else: TRACY_HAS_CALLBACK
#else
    #define PROFILE_ZONE(active)
    #define PROFILE_ZONE_NAME(name, active)
    #define PROFILE_ZONE_COLOR(color, active)
    #define PROFILE_ZONE_NAME_COLOR(name, color, active)
    #define PROFILE_ZONE_WITH_TEXT(text, textLen, active)

    #define TracyCRealloc(oldPtr, ptr, size)
#endif  // TRACY_ENABLE
