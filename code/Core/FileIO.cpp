#include "Base.h"

#ifdef BUILD_UNITY
    #if PLATFORM_WINDOWS
        #include "FileIOWin.cpp"
    #elif PLATFORM_POSIX
        #include "FileIOPosix.cpp"
    #else
        #error "Not implemented"
    #endif
#endif

