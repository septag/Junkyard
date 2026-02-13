#pragma once

#include "Core/Config.h"

#ifndef CONFIG_LIVEAPP_ROOT_PATH
    #define CONFIG_LIVEPP_ROOT_PATH ""
#endif

#ifndef CONFIG_ENABLE_LIVEPP
    // We check if the header exists then enable it, because this is not part of the repo and will be installed with Setup.bat
    #if __has_include("External/LivePP/API/x64/LPP_API_x64_CPP.h")
        #define CONFIG_ENABLE_LIVEPP (!CONFIG_FINAL_BUILD && PLATFORM_WINDOWS)
    #else
        #define CONFIG_ENABLE_LIVEPP 0
    #endif
#endif

#ifndef CONFIG_DEBUG_GFXBACKEND
    #define CONFIG_DEBUG_GFXBACKEND 0
#endif

// Graphics
// Enables this feature (pImmutableSamplers): https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorSetLayoutBinding.html
#ifndef CONFIG_GFX_IMMUTABLE_SAMPLERS
    #define CONFIG_GFX_IMMUTABLE_SAMPLERS 0
#endif