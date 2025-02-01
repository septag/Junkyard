#pragma once

#include "../Core/Base.h"

#include "../Graphics/GfxBackendTypes.h"

struct AssetGroup;

inline constexpr uint32 IMAGE_ASSET_TYPE = MakeFourCC('I', 'M', 'A', 'G');

struct ImageLoadParams
{
    uint32 firstMip = 0;
    GfxSamplerFilterMode samplerFilter = GfxSamplerFilterMode::Default;
    GfxSamplerWrapMode samplerWrap = GfxSamplerWrapMode::Default;
};

namespace Image
{
    API bool InitializeManager();
    API void ReleaseManager();

    // DataType: AssetObjPtrScope<GfxImage>
    API AssetHandleImage Load(const char* path, const ImageLoadParams& params, const AssetGroup& group);
    API GfxImageHandle GetWhite1x1();

    API uint32 CalculateMipCount(uint32 width, uint32 height);
}

