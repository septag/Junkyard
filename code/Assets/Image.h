#pragma once

#include "../Core/Base.h"

#include "../Graphics/GfxBackendTypes.h"

// Freaking windows.h with it's Macros! In unity builds, it causes LoadImage to be converted to LoadImageA/W
#if PLATFORM_WINDOWS
    #ifdef LoadImage
    #define _LoadImage LoadImage
    #undef LoadImage
    #endif
#endif

struct AssetGroup;

inline constexpr uint32 IMAGE_ASSET_TYPE = MakeFourCC('I', 'M', 'A', 'G');

struct ImageLoadParams
{
    uint32 firstMip = 0;
    GfxSamplerFilterMode samplerFilter = GfxSamplerFilterMode::Default;
    GfxSamplerWrapMode samplerWrap = GfxSamplerWrapMode::Default;
};

namespace Asset
{
    API bool InitializeImageManager();
    API void ReleaseImageManager();
    API AssetHandleImage LoadImage(const char* path, const ImageLoadParams& params, const AssetGroup& group);
}

#if PLATFORM_WINDOWS
    #ifdef _LoadImage
    #define LoadImage _LoadImage
    #endif
#endif
