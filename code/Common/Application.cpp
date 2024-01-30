#include "../Core/Base.h"

#if BUILD_UNITY
    #if PLATFORM_WINDOWS
        #include "ApplicationWin.cpp"
    #elif PLATFORM_ANDROID
        #include "ApplicationAndroid.cpp"
    #endif
#endif
