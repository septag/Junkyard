#pragma once

#include "../Graphics/Graphics.h"

// Freaking windows.h with it's Macros! In unity builds, it causes LoadImage to be converted to LoadImageA/W
#if PLATFORM_WINDOWS
    #ifdef LoadImage
    #define _LoadImage LoadImage
    #undef LoadImage
    #endif
#endif

struct AssetGroup;

inline constexpr uint32 IMAGE_ASSET_TYPE = MakeFourCC('I', 'M', 'A', 'G');

struct GfxImage
{
    GfxImageHandle handle;
    uint32 width;
    uint32 height;
    uint32 depth;
    uint32 numMips;
    GfxFormat format;
    uint32 contentSize;
    uint32 mipOffsets[GFX_MAX_MIPS];
    RelativePtr<uint8> content;
};

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
