#pragma once

#include "../Graphics/Graphics.h"

// Used in AssetManager 'Load' function as extra load parameter for texture types
inline constexpr uint32 kImageAssetType = MakeFourCC('I', 'M', 'A', 'G');

struct ImageLoadParams
{
    uint32 firstMip = 0;
    GfxSamplerFilterMode samplerFilter = GfxSamplerFilterMode::Default;
    GfxSamplerWrapMode samplerWrap = GfxSamplerWrapMode::Default;
};

API AssetHandleImage assetLoadImage(const char* path, const ImageLoadParams& params, AssetBarrier barrier = AssetBarrier());
API GfxImage assetGetImage(AssetHandleImage imageHandle);
API GfxImage assetGetWhiteImage1x1();

namespace _private
{
    bool assetInitializeImageManager();
    void assetReleaseImageManager();
}




