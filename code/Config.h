#pragma once

#if !defined(CONFIG_MACHINE_ALIGNMENT)
    #define CONFIG_MACHINE_ALIGNMENT 16u
#endif

#if !defined(CONFIG_CHECK_OUTOFBOUNDS)
    #if defined(_DEBUG) || defined(DEBUG)
        #define CONFIG_CHECK_OUTOFBOUNDS
    #endif
#endif

#if !defined(CONFIG_TOOLMODE)
    #define CONFIG_TOOLMODE 1
#endif

// This preprocessor applies a trick for hot functions that need to be inlined even in debug builds
// only applies to ALWAYS_INLINE functions, but leaves INLINE not inlined
// And also, the build should be compiled with /Ob1 flag
#if !defined(CONFIG_FORCE_INLINE_DEBUG)
    #define CONFIG_FORCE_INLINE_DEBUG 0
#endif

#if !defined(CONFIG_MAX_PATH)
    #define CONFIG_MAX_PATH 255
#endif

#if !defined(MEMPRO_ENABLED)
    #define MEMPRO_ENABLED 0
#endif

#if !defined(CONFIG_FINAL_BUILD)
    #define CONFIG_FINAL_BUILD 0
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

