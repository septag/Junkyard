#pragma once

#include "../Core/Base.h"

#include "../Graphics/GfxBackendTypes.h"

struct AssetGroup;

inline constexpr uint32 IMAGE_ASSET_TYPE = MakeFourCC('I', 'M', 'A', 'G');
inline constexpr uint32 IMAGE_ASSET_CACHE_VERSION = 2;

// uint32, because ImageLoadParams is hashed as raw bytes (asset cache), so it shouldn't have any padding
enum class ImageColorSpace : uint32
{
    Default = 0,    // From the asset metadata ("sRGB" key). sRGB if there is no metadata
    SRGB,
    Linear
};

struct ImageLoadParams
{
    uint32 firstMip = 0;
    GfxSamplerFilterMode samplerFilter = GfxSamplerFilterMode::Default;
    GfxSamplerWrapMode samplerWrap = GfxSamplerWrapMode::Default;
    ImageColorSpace colorSpace = ImageColorSpace::Default;     // Overrides the asset metadata if not Default
};

namespace Image
{
    API bool InitializeManager();
    API void ReleaseManager();

    // DataType: AssetObjPtrScope<GfxImage>
    API AssetHandleImage Load(const char* path, const ImageLoadParams& params, const AssetGroup& group);
    API GfxImageHandle GetWhite1x1();
    API GfxImageHandle CreateCheckerTexture(uint32 textureSize, uint32 checkerSize, Color4u color0, Color4u color1);

    API uint32 CalculateMipCount(uint32 width, uint32 height);
}

