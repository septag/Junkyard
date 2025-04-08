#pragma once

#include "Base.h"

#ifndef TRACY_CALLSTACK
    #define TRACY_CALLSTACK 16
#endif

#ifndef TRACY_FIBERS
    #define TRACY_FIBERS
#endif

#if PLATFORM_LINUX
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
                explicit TracyCZoneScope(TracyCZoneCtx ctx, const ___tracy_source_location_data* sourceLoc) : mCtx(ctx), mSourceLoc(sourceLoc) { Tracy::RunZoneEnterCallback(&ctx, sourceLoc); }
                ~TracyCZoneScope() { if (!Tracy::RunZoneExitCallback(&mCtx)) { TracyCZoneEnd(mCtx); }}
            };
        }
    }

    #define TracyCRealloc(oldPtr, ptr, size) if (oldPtr) { TracyCFree(oldPtr); }  TracyCAlloc(ptr, size)

    #if defined TRACY_HAS_CALLSTACK && defined TRACY_CALLSTACK
        #define PROFILE_ZONE_OPT(active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { NULL, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            Tracy::_private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin_callstack(&CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active), &CONCAT(__tracy_source_location,__LINE__));
        #define PROFILE_ZONE_NAME_OPT(name, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            Tracy::_private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin_callstack( &CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active ), &CONCAT(__tracy_source_location,__LINE__));
        #define PROFILE_ZONE_COLOR_OPT(color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { NULL, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            Tracy::_private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin_callstack( &CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active ), &CONCAT(__tracy_source_location,__LINE__));
        #define PROFILE_ZONE_NAME_COLOR_OPT(name, color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            Tracy::_private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin_callstack( &CONCAT(__tracy_source_location,__LINE__), TRACY_CALLSTACK, active ), &CONCAT(__tracy_source_location,__LINE__));

        #define PROFILE_ZONE() PROFILE_ZONE_OPT(true)
        #define PROFILE_ZONE_NAME(name) PROFILE_ZONE_NAME_OPT(name, true)
        #define PROFILE_ZONE_COLOR(color) PROFILE_ZONE_COLOR_OPT(color, true)
        #define PROFILE_ZONE_NAME_COLOR(name, color) PROFILE_ZONE_NAME_COLOR_OPT(name, color, true)

        #define PROFILE_ZONE_WITH_TEXT_OPT(text, textLen, active) \
            PROFILE_ZONE_OPT(active) \
            TracyCZoneText(CONCAT(__tracy_ctx,__LINE__).mCtx, text, textLen)
        #define PROFILE_ZONE_NAME_WITH_TEXT_OPT(name, text, textLen, active) \
            PROFILE_ZONE_NAME_OPT(name, active) \
            TracyCZoneText(CONCAT(__tracy_ctx,__LINE__).mCtx, text, textLen)
        #define PROFILE_ZONE_COLOR_WITH_TEXT_OPT(color, text, textLen, active) \
            PROFILE_ZONE_COLOR_OPT(color, active) \
            TracyCZoneText(CONCAT(__tracy_ctx,__LINE__).mCtx, text, textLen)
        #define PROFILE_ZONE_NAME_COLOR_WITH_TEXT_OPT(name, color, text, textLen, active) \
            PROFILE_ZONE_NAME_COLOR_OPT(name, color, active) \
            TracyCZoneText(CONCAT(__tracy_ctx,__LINE__).mCtx, text, textLen)

        #define PROFILE_ZONE_WITH_TEXT(text, textLen) PROFILE_ZONE_WITH_TEXT_OPT(text, textLen, true)
        #define PROFILE_ZONE_NAME_WITH_TEXT(name, text, textLen) PROFILE_ZONE_NAME_WITH_TEXT_OPT(name, text, textLen, true)
        #define PROFILE_ZONE_COLOR_WITH_TEXT(color, text, textLen) PROFILE_ZONE_COLOR_WITH_TEXT_OPT(color, text, textLen, true)
        #define PROFILE_ZONE_NAME_COLOR_WITH_TEXT(name, color, text, textLen) PROFILE_ZONE_NAME_COLOR_WITH_TEXT(name, color, text, textLen, true)
    #else
        #define PROFILE_ZONE_OPT(active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { NULL, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            Tracy::_private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin( &CONCAT(__tracy_source_location,__LINE__), active ), &CONCAT(__tracy_source_location,__LINE__));
        #define PROFILE_ZONE_NAME_OPT(name, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; \
            Tracy::_private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin( &CONCAT(__tracy_source_location,__LINE__), active ), &CONCAT(__tracy_source_location,__LINE__));
        #define PROFILE_ZONE_COLOR_OPT(color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { NULL, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            Tracy::_private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin( &CONCAT(__tracy_source_location,__LINE__), active ), &CONCAT(__tracy_source_location,__LINE__));
        #define PROFILE_ZONE_NAME_COLOR_OPT(name, color, active) \
            static constexpr struct ___tracy_source_location_data CONCAT(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, color }; \
            Tracy::_private::TracyCZoneScope CONCAT(__tracy_ctx,__LINE__)(___tracy_emit_zone_begin( &CONCAT(__tracy_source_location,__LINE__), active ), &CONCAT(__tracy_source_location,__LINE__));

        #define PROFILE_ZONE() PROFILE_ZONE_OPT(true)
        #define PROFILE_ZONE_NAME(name) PROFILE_ZONE_NAME_OPT(name, true)
        #define PROFILE_ZONE_COLOR(color) PROFILE_ZONE_COLOR_OPT(color, true)
        #define PROFILE_ZONE_NAME_COLOR(name, color) PROFILE_ZONE_NAME_COLOR_OPT(name, color, true)

        #define PROFILE_ZONE_WITH_TEXT_OPT(text, textLen, active) \
            PROFILE_ZONE_OPT(active) \
            TracyCZoneText(CONCAT(__tracy_ctx,__LINE__).mCtx, text, textLen)
        #define PROFILE_ZONE_NAME_WITH_TEXT_OPT(name, text, textLen, active) \
            PROFILE_ZONE_NAME_OPT(name, active) \
            TracyCZoneText(CONCAT(__tracy_ctx,__LINE__).mCtx, text, textLen)
        #define PROFILE_ZONE_COLOR_WITH_TEXT_OPT(color, text, textLen, active) \
            PROFILE_ZONE_COLOR_OPT(color, active) \
            TracyCZoneText(CONCAT(__tracy_ctx,__LINE__).mCtx, text, textLen)
        #define PROFILE_ZONE_NAME_COLOR_WITH_TEXT_OPT(name, color, text, textLen, active) \
            PROFILE_ZONE_NAME_COLOR_WITH_TEXT_OPT(name, color, active) \
            TracyCZoneText(CONCAT(__tracy_ctx,__LINE__).mCtx, text, textLen)

        #define PROFILE_ZONE_WITH_TEXT(text, textLen) PROFILE_ZONE_WITH_TEXT_OPT(text, textLen, true)
        #define PROFILE_ZONE_NAME_WITH_TEXT(name, text, textLen) PROFILE_ZONE_NAME_WITH_TEXT_OPT(name, text, textLen, true)
        #define PROFILE_ZONE_COLOR_WITH_TEXT(color, text, textLen) PROFILE_ZONE_COLOR_WITH_TEXT_OPT(color, text, textLen, true)
        #define PROFILE_ZONE_NAME_COLOR_WITH_TEXT(name, color, text, textLen) PROFILE_ZONE_NAME_COLOR_WITH_TEXT(name, color, text, textLen, true)
    #endif // else: TRACY_HAS_CALLBACK
#else
    #define PROFILE_ZONE_OPT(active)
    #define PROFILE_ZONE_NAME_OPT(name, active)
    #define PROFILE_ZONE_COLOR_OPT(color, active)
    #define PROFILE_ZONE_NAME_COLOR_OPT(name, color, active)

    #define PROFILE_ZONE_WITH_TEXT_OPT(text, textLen, active)
    #define PROFILE_ZONE_NAME_WITH_TEXT_OPT(name, text, textLen, active)
    #define PROFILE_ZONE_COLOR_WITH_TEXT_OPT(color, text, textLen, active)
    #define PROFILE_ZONE_NAME_COLOR_WITH_TEXT_OPT(name, color, text, textLen, active)

    #define PROFILE_ZONE()
    #define PROFILE_ZONE_NAME(name)
    #define PROFILE_ZONE_COLOR(color)
    #define PROFILE_ZONE_NAME_COLOR(name, color)

    #define PROFILE_ZONE_WITH_TEXT(text, textLen)
    #define PROFILE_ZONE_NAME_WITH_TEXT(name, text, textLen)
    #define PROFILE_ZONE_COLOR_WITH_TEXT(color, text, textLen)
    #define PROFILE_ZONE_NAME_COLOR_WITH_TEXT(name, color, text, textLen)    

    #define TracyCRealloc(oldPtr, ptr, size)
#endif  // TRACY_ENABLE
