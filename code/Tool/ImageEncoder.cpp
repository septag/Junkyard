#include "ImageEncoder.h"

#if CONFIG_TOOLMODE
#include "../External/ispc_texcomp/include/ispc_texcomp.h"

#include "../Core/StringUtil.h"
#include "../Core/Allocators.h"

struct ImageEncoderInfo
{
    ImageEncoderCompression::Enum compression;
    int blockDim;
    int blockSizeBytes;
};

static ImageEncoderInfo IMAGE_ENCODER_COMPRESS_INFO[static_cast<uint32>(ImageEncoderCompression::_Count)] = {
    {ImageEncoderCompression::BC1,      4, 8},
    {ImageEncoderCompression::BC3,      4, 8},
    {ImageEncoderCompression::BC4,      4, 8},
    {ImageEncoderCompression::BC5,      4, 16},
    {ImageEncoderCompression::BC6H,     4, 16},
    {ImageEncoderCompression::BC7,      4, 16},
    {ImageEncoderCompression::ASTC_4x4, 4, 16},
    {ImageEncoderCompression::ASTC_5x5, 5, 16},
    {ImageEncoderCompression::ASTC_6x6, 6, 16},
    {ImageEncoderCompression::ASTC_8x8, 8, 16},
};

Blob ImageEncoder::Compress(ImageEncoderCompression::Enum compression, ImageEncoderQuality quality, 
    ImageEncoderFlags flags, const ImageEncoderSurface& surface, MemAllocator* alloc)
{
    ASSERT_MSG(compression != ImageEncoderCompression::BC6H, "Floating point compression is not supported yet");

    int width = static_cast<int>(surface.width);
    int height = static_cast<int>(surface.height);
    const ImageEncoderInfo& info = IMAGE_ENCODER_COMPRESS_INFO[static_cast<uint32>(compression)];
    bc7_enc_settings bc7Settings;
    astc_enc_settings astcSettings;

    if (compression == ImageEncoderCompression::BC7) {
        if ((flags & ImageEncoderFlags::HasAlpha) == ImageEncoderFlags::HasAlpha) {
            switch (quality) {
            case ImageEncoderQuality::Fastest:  GetProfile_alpha_ultrafast(&bc7Settings);   break;
            case ImageEncoderQuality::Fast:     GetProfile_alpha_fast(&bc7Settings);        break;
            case ImageEncoderQuality::Medium:   GetProfile_alpha_basic(&bc7Settings);       break;
            case ImageEncoderQuality::Best:     GetProfile_alpha_slow(&bc7Settings);        break;
            }
        }
        else {
            switch (quality) {
            case ImageEncoderQuality::Fastest:  GetProfile_ultrafast(&bc7Settings);   break;
            case ImageEncoderQuality::Fast:     GetProfile_fast(&bc7Settings);        break;
            case ImageEncoderQuality::Medium:   GetProfile_basic(&bc7Settings);       break;
            case ImageEncoderQuality::Best:     GetProfile_slow(&bc7Settings);        break;
            }
        }
    }
    else if (ImageEncoderCompression::IsASTC(compression)) {
        if ((flags & ImageEncoderFlags::HasAlpha) == ImageEncoderFlags::HasAlpha) {
            if (quality == ImageEncoderQuality::Best)   GetProfile_astc_alpha_slow(&astcSettings, info.blockDim, info.blockDim);
            else                                        GetProfile_astc_alpha_fast(&astcSettings, info.blockDim, info.blockDim);
        }
        else {
            GetProfile_astc_fast(&astcSettings, info.blockDim, info.blockDim);
        }
    }
    
    int numBlocksX = DivCeil(width, info.blockDim);
    int numBlocksY = DivCeil(height, info.blockDim);
    // Align dimensions to the multiple of block-dimension
    int alignedWidth = numBlocksX * info.blockDim;
    int alignedHeight = numBlocksY * info.blockDim;

    int bufferSize = numBlocksX*numBlocksY*info.blockSizeBytes;
    uint8* compressed = (uint8*)Mem::Alloc(static_cast<uint32>(bufferSize), alloc);
    
    MemTempAllocator tmpAlloc;
    rgba_surface srcSurface {
        .ptr = const_cast<uint8*>(surface.pixels),
        .width = width,
        .height = height,
        .stride = width * 4
    };
    
    if (alignedWidth != width || alignedHeight != height) {
        rgba_surface borderSurface {
            .ptr = (uint8*)Mem::Alloc(alignedWidth*alignedHeight*4, &tmpAlloc),
            .width = alignedWidth,
            .height = alignedHeight,
            .stride = alignedWidth * 4
        };
        ReplicateBorders(&borderSurface, &srcSurface, 0, 0, 32);
        srcSurface = borderSurface;
    }

    switch (compression) {
    case ImageEncoderCompression::BC1:  CompressBlocksBC1(&srcSurface, compressed); break;
    case ImageEncoderCompression::BC3:  CompressBlocksBC3(&srcSurface, compressed); break;
    case ImageEncoderCompression::BC4:  CompressBlocksBC4(&srcSurface, compressed); break;
    case ImageEncoderCompression::BC5:  CompressBlocksBC5(&srcSurface, compressed); break;
    case ImageEncoderCompression::BC7:  CompressBlocksBC7(&srcSurface, compressed, &bc7Settings); break;
    case ImageEncoderCompression::ASTC_4x4:
    case ImageEncoderCompression::ASTC_5x5:
    case ImageEncoderCompression::ASTC_6x6:
    case ImageEncoderCompression::ASTC_8x8:
        CompressBlocksASTC(&srcSurface, compressed, &astcSettings);
        break;
    default: ASSERT(0); break;
    }

    Blob blob;
    blob.Attach(compressed, static_cast<size_t>(bufferSize), alloc);
    return blob;
}

bool ImageEncoderCompression::IsASTC(ImageEncoderCompression::Enum compression)
{
    return compression == ImageEncoderCompression::ASTC_4x4 ||
           compression == ImageEncoderCompression::ASTC_5x5 || 
           compression == ImageEncoderCompression::ASTC_6x6 || 
           compression == ImageEncoderCompression::ASTC_8x8;
}

ImageEncoderCompression::Enum ImageEncoderCompression::FromString(const char* estr)
{
    String32 str(estr);

    if (str.IsEqualNoCase("BC1"))             return ImageEncoderCompression::BC1;
    else if (str.IsEqualNoCase("BC3"))        return ImageEncoderCompression::BC3;
    else if (str.IsEqualNoCase("BC4"))        return ImageEncoderCompression::BC4;
    else if (str.IsEqualNoCase("BC5"))        return ImageEncoderCompression::BC5;
    else if (str.IsEqualNoCase("BC6H"))       return ImageEncoderCompression::BC6H;
    else if (str.IsEqualNoCase("BC7"))        return ImageEncoderCompression::BC7;
    else if (str.IsEqualNoCase("ASTC_4x4"))   return ImageEncoderCompression::ASTC_4x4;
    else if (str.IsEqualNoCase("ASTC_5x5"))   return ImageEncoderCompression::ASTC_5x5;
    else if (str.IsEqualNoCase("ASTC_6x6"))   return ImageEncoderCompression::ASTC_6x6;
    else if (str.IsEqualNoCase("ASTC_8x8"))   return ImageEncoderCompression::ASTC_8x8;
    else                                      return ImageEncoderCompression::_Count;
}
#endif // CONFIG_TOOLMODE