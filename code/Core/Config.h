#pragma once
//
// Build time user config
// NOTE: This file should only be modified by the Junkyard project itself (build-time) and not by the users of the binary
//
//  - CONFIG_MACHINE_ALIGNMENT (default=16): Default machine memory alignment. Memory allocations always default to this value if alignement is less than this.
//  - CONFIG_CHECK_OUTOFBOUNDS (default=Enable in debug): Check out of bounds array accessors in some containers
//  - CONFIG_TOOLMODE (default=Enable in none-final builds/none-mobile): Enables TOOLMODE which adds many extra baking/editing/tools code and dependencies
//  - CONFIG_FORCE_INLINE_DEBUG (default=0): This macro applies a trick for hot functions that need to be inlined even in debug builds
//                                           inlining only applies to FORCE_INLINE functions, but prohibits INLINE macros to get inlined
//                                           And also, the build should be compiled with /Ob1 flag with CL compiler. 
//  - CONFIG_MAX_PATH (default=255): Default length for path strings (also see Path class in System.h)
//  - MEMPRO_ENABLED (default=0): Enables MemPro integration (http://www.puredevsoftware.com/mempro/index.htm)
//  - CONFIG_VALIDATE_IO_READ_WRITES (default=1): Validates IO read/writes with ASSERT to not get truncated 
//  - CONFIG_ENABLE_ASSERT (default=1 on DEBUG and none-final, otherwise 0): Enables assertions checks, with the exception of ASSERT_ALWAYS
//  - TRACY_ENABLE: comment/uncomment this macro to enable Tracy profiler. This macro is already defined in "ReleaseDev" config
//

#if !defined(CONFIG_FINAL_BUILD)
    #define CONFIG_FINAL_BUILD 0
#endif

#if !defined(CONFIG_MACHINE_ALIGNMENT)
    #define CONFIG_MACHINE_ALIGNMENT 16u
#endif

#if !defined(CONFIG_CHECK_OUTOFBOUNDS)
    #if defined(_DEBUG) || defined(DEBUG)
        #define CONFIG_CHECK_OUTOFBOUNDS
    #endif
#endif

#if !defined(CONFIG_TOOLMODE)
    #if !CONFIG_FINAL_BUILD
        #define CONFIG_TOOLMODE 1
    #else
        #define CONFIG_TOOLMODE 0
    #endif
#endif

#if !defined(CONFIG_FORCE_INLINE_DEBUG)
    #define CONFIG_FORCE_INLINE_DEBUG 0
#endif

#if !defined(CONFIG_MAX_PATH)
    #define CONFIG_MAX_PATH 260
#endif

#if !defined(MEMPRO_ENABLED)
    #define MEMPRO_ENABLED 0
#endif

#if !defined(CONFIG_VALIDATE_IO_READ_WRITES)
    #define CONFIG_VALIDATE_IO_READ_WRITES 1
#endif

#if !defined(CONFIG_ENABLE_ASSERT) 
    #if (defined(_DEBUG) || defined(DEBUG)) && !CONFIG_FINAL_BUILD
        #define CONFIG_ENABLE_ASSERT 1
    #else
        #define CONFIG_ENABLE_ASSERT 0
    #endif
#endif

#define CONFIG_DEV_MODE CONFIG_ENABLE_ASSERT

#if !defined(CONFIG_APP_NAME)
    #define CONFIG_APP_NAME "Junkyard"
#endif

// #define TRACY_ENABLE