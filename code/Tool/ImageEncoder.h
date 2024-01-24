#pragma once

#include "../Core/Base.h"

#if CONFIG_TOOLMODE
#include "../Core/Buffers.h"

enum class ImageEncoderCompression
{
    BC1 = 0,
    BC3,
    BC4,
    BC5,
    BC6H,
    BC7,
    ASTC_4x4,
    ASTC_5x5,
    ASTC_6x6,
    ASTC_8x8,
    _Count
};
API ImageEncoderCompression imageEncoderCompressionGetEnum(const char* estr);

enum class ImageEncoderQuality
{
    Fastest,
    Fast,
    Medium,
    Best
};

enum class ImageEncoderFlags : uint32
{
    None = 0,
    PackNormalMapXY = 0x01,
    HasAlpha = 0x02
};
ENABLE_BITMASK(ImageEncoderFlags);

struct ImageEncoderSurface
{
    uint32 width;
    uint32 height;
    const uint8* pixels;   // Each pixel is a U8 channel, and full RGBA (some channels can be empty)
};

API Blob imageEncoderCompress(ImageEncoderCompression compression, ImageEncoderQuality quality, 
                              ImageEncoderFlags flags, const ImageEncoderSurface& surface, 
                              Allocator* alloc = memDefaultAlloc());

#endif // CONFIG_TOOLMODE