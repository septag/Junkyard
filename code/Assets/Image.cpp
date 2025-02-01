#include "Image.h"
#include "AssetManager.h"

#include "../Core/TracyHelper.h"
#include "../Core/Log.h"

#include "../Common/VirtualFS.h"
#include "../Common/JunkyardSettings.h"

#include "../Tool/ImageEncoder.h"

#include "../Graphics/GfxBackend.h"

static thread_local MemAllocator* gStbIAlloc = nullptr;

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_MALLOC(sz)                     Mem::Alloc(sz, gStbIAlloc)
#define STBI_REALLOC(p, newsz)              Mem::Realloc(p, newsz, gStbIAlloc)
#define STBI_FREE(p)                        Mem::Free(p, gStbIAlloc)
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wshadow")
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wunused-function")
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wtype-limits")
PRAGMA_DIAGNOSTIC_IGNORED_GCC("-Wmaybe-uninitialized")
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4505)    // unreferenced function with internal linkage has been removed
#include "../External/stb/stb_image.h"
PRAGMA_DIAGNOSTIC_POP()

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_STATIC
#define STBIR_MALLOC(size,c)                Mem::Alloc(size, reinterpret_cast<MemAllocator*>(c));
#define STBIR_FREE(ptr,c)                   Mem::Free(ptr, reinterpret_cast<MemAllocator*>(c));
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4505)
#include "../External/stb/stb_image_resize.h"
PRAGMA_DIAGNOSTIC_POP()

#define DDSKTX_IMPLEMENT
#define ddsktx_assert(e) ASSERT(e)
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4127)
#include "../External/dds-ktx/dds-ktx.h"
PRAGMA_DIAGNOSTIC_POP()

struct AssetImageImpl final : AssetTypeImplBase
{
    bool Bake(const AssetParams& params, AssetData* data, const Span<uint8>& srcData, String<256>* outErrorDesc) override;
    bool Reload(void* newData, void* oldData) override;
};

struct AssetImageManager
{
    AssetImageImpl imageImpl;
    GfxImageHandle imageWhite;
};

static AssetImageManager gImageMgr;

namespace Image
{
    INLINE GfxFormat _ConvertFormatSRGB(GfxFormat fmt)
    {
        switch (fmt) {
        case GfxFormat::R8G8B8A8_UNORM:             return GfxFormat::R8G8B8A8_SRGB;
        case GfxFormat::BC1_RGB_UNORM_BLOCK:        return GfxFormat::BC1_RGB_SRGB_BLOCK;
        case GfxFormat::BC1_RGBA_UNORM_BLOCK:       return GfxFormat::BC1_RGBA_SRGB_BLOCK;
        case GfxFormat::BC3_UNORM_BLOCK:            return GfxFormat::BC3_SRGB_BLOCK;
        case GfxFormat::BC7_UNORM_BLOCK:            return GfxFormat::BC7_SRGB_BLOCK;
        case GfxFormat::ETC2_R8G8B8_UNORM_BLOCK:    return GfxFormat::ETC2_R8G8B8_SRGB_BLOCK;
        case GfxFormat::ETC2_R8G8B8A8_UNORM_BLOCK:  return GfxFormat::ETC2_R8G8B8A8_SRGB_BLOCK;
        case GfxFormat::ETC2_R8G8B8A1_UNORM_BLOCK:  return GfxFormat::ETC2_R8G8B8A1_SRGB_BLOCK;
        case GfxFormat::ASTC_4x4_UNORM_BLOCK:       return GfxFormat::ASTC_4x4_SRGB_BLOCK;
        case GfxFormat::ASTC_5x5_UNORM_BLOCK:       return GfxFormat::ASTC_5x5_SRGB_BLOCK;
        case GfxFormat::ASTC_6x6_UNORM_BLOCK:       return GfxFormat::ASTC_6x6_SRGB_BLOCK;
        case GfxFormat::ASTC_8x8_UNORM_BLOCK:       return GfxFormat::ASTC_8x8_SRGB_BLOCK;
        default:                                    return fmt;
        }
    }

    static GfxFormat _ConvertDDSFormat(ddsktx_format fmt)
    {
        switch (fmt) {
        case DDSKTX_FORMAT_BC1:     return GfxFormat::BC1_RGB_UNORM_BLOCK;
        case DDSKTX_FORMAT_BC2:     return GfxFormat::BC2_UNORM_BLOCK;
        case DDSKTX_FORMAT_BC3:     return GfxFormat::BC3_UNORM_BLOCK;
        case DDSKTX_FORMAT_BC4:     return GfxFormat::BC4_UNORM_BLOCK;
        case DDSKTX_FORMAT_BC5:     return GfxFormat::BC5_UNORM_BLOCK;
        case DDSKTX_FORMAT_BC6H:    return GfxFormat::BC6H_UFLOAT_BLOCK;
        case DDSKTX_FORMAT_BC7:     return GfxFormat::BC7_UNORM_BLOCK;
        case DDSKTX_FORMAT_ETC2:    return GfxFormat::ETC2_R8G8B8_UNORM_BLOCK;
        case DDSKTX_FORMAT_ETC2A:   return GfxFormat::ETC2_R8G8B8A8_UNORM_BLOCK;
        case DDSKTX_FORMAT_ETC2A1:  return GfxFormat::ETC2_R8G8B8A1_UNORM_BLOCK;
        case DDSKTX_FORMAT_ASTC4x4: return GfxFormat::ASTC_4x4_UNORM_BLOCK;
        case DDSKTX_FORMAT_ASTC5x5: return GfxFormat::ASTC_5x5_UNORM_BLOCK;
        case DDSKTX_FORMAT_ASTC6x6: return GfxFormat::ASTC_6x6_UNORM_BLOCK;
        case DDSKTX_FORMAT_R8:      return GfxFormat::R8_UNORM;
        case DDSKTX_FORMAT_RGBA8:   return GfxFormat::R8G8B8A8_UNORM;
        case DDSKTX_FORMAT_RGBA8S:  return GfxFormat::R8G8B8A8_SINT;
        case DDSKTX_FORMAT_RG16:    return GfxFormat::R16G16_UNORM;
        case DDSKTX_FORMAT_RGB8:    return GfxFormat::R8G8B8_UNORM;
        case DDSKTX_FORMAT_R16:     return GfxFormat::R16_UNORM;
        case DDSKTX_FORMAT_R32F:    return GfxFormat::R32_SFLOAT;
        case DDSKTX_FORMAT_R16F:    return GfxFormat::R16_SFLOAT;
        case DDSKTX_FORMAT_RG16F:   return GfxFormat::R16G16_SFLOAT;
        case DDSKTX_FORMAT_RG16S:   return GfxFormat::R16G16_SINT;
        case DDSKTX_FORMAT_RGBA16F: return GfxFormat::R16G16B16_SFLOAT;
        case DDSKTX_FORMAT_RGBA16:  return GfxFormat::R16G16B16A16_UNORM;
        case DDSKTX_FORMAT_BGRA8:   return GfxFormat::B8G8R8A8_UNORM;
        case DDSKTX_FORMAT_RGB10A2: return GfxFormat::A2R10G10B10_UNORM_PACK32;
        case DDSKTX_FORMAT_RG8:     return GfxFormat::R8G8_UNORM;
        case DDSKTX_FORMAT_RG8S:    return GfxFormat::R8G8_SINT;
        default: 
            ASSERT_MSG(0, "Unsupported Format");
            return GfxFormat::Undefined;
        }
    }
} // Image

bool Image::InitializeManager()
{
    static GfxImage whiteImage {};

    if (SettingsJunkyard::Get().graphics.IsGraphicsEnabled()) {
        const uint32 kWhitePixel = 0xffffffff;
        GfxImageDesc imageDesc {
            .width = 1,
            .height = 1,
            .format = GfxFormat::R8G8B8A8_UNORM,
            .usageFlags = GfxImageUsageFlags::TransferDst|GfxImageUsageFlags::Sampled
        };
        gImageMgr.imageWhite = GfxBackend::CreateImage(imageDesc);
        if (!gImageMgr.imageWhite.IsValid())
            return false;

        whiteImage = {
            .handle = gImageMgr.imageWhite,
            .width = 1,
            .height = 1,
            .depth = 1,
            .numMips = 1,
            .format = GfxFormat::R8G8B8A8_UNORM
        };

        GfxCommandBuffer cmd = GfxBackend::BeginCommandBuffer(GfxQueueType::Transfer);
        void* stagingData;
        size_t stagingDataSize;
        GfxBufferDesc stagingBufferDesc {
            .sizeBytes = 4,
            .usageFlags = GfxBufferUsageFlags::TransferSrc,
            .arena = GfxMemoryArena::TransientCPU
        };
        GfxBufferHandle stagingBuffer = GfxBackend::CreateBuffer(stagingBufferDesc);
        cmd.MapBuffer(stagingBuffer, &stagingData, &stagingDataSize);
        memcpy(stagingData, &kWhitePixel, stagingBufferDesc.sizeBytes);
        cmd.FlushBuffer(stagingBuffer);
        cmd.CopyBufferToImage(stagingBuffer, gImageMgr.imageWhite, GfxShaderStage::Fragment);
        GfxBackend::EndCommandBuffer(cmd);
        GfxBackend::SubmitQueue(GfxQueueType::Transfer);

        GfxBackend::DestroyBuffer(stagingBuffer);
    }

    AssetTypeDesc assetDesc {
        .fourcc = IMAGE_ASSET_TYPE,
        .name = "Image",
        .impl = &gImageMgr.imageImpl,
        .extraParamTypeName = "ImageLoadParams",
        .extraParamTypeSize = sizeof(ImageLoadParams),
        .failedObj = &whiteImage,
        .asyncObj = &whiteImage
    };
    Asset::RegisterType(assetDesc);

    return true;
}

void Image::ReleaseManager()
{
    GfxBackend::DestroyImage(gImageMgr.imageWhite);
    Asset::UnregisterType(IMAGE_ASSET_TYPE);
}

uint32 Image::CalculateMipCount(uint32 width, uint32 height)
{
    uint32 maxDimension = (width > height) ? width : height;
    uint32 mipCount = 0;

    while (maxDimension > 0) {
        mipCount++;
        maxDimension >>= 1;
    }

    return mipCount;
}

bool AssetImageImpl::Bake(const AssetParams& params, AssetData* data, const Span<uint8>& srcData, String<256>* outErrorDesc)
{
    const ImageLoadParams* imageParams = (ImageLoadParams*)params.typeSpecificParams;

    struct MipSurface
    {
        uint32 width;
        uint32 height;
        uint32 offset;
    };

    MemTempAllocator tmpAlloc;

    gStbIAlloc = &tmpAlloc;
    int imgWidth = 0, imgHeight = 0, imgChannels = 4;
    GfxFormat imageFormat = GfxFormat::R8G8B8A8_UNORM;
    uint32 imageSize = 0;
    uint32 numMips = 1;
    uint8* pixels = nullptr;
    bool isLoadedFromContainer = false;
    MipSurface mips[GFXBACKEND_MAX_MIPS_PER_IMAGE];
    Blob contentBlob(&tmpAlloc);
    contentBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);

    String32 formatStr = String32(data->GetMetaValue("format", ""));
    bool sRGB = data->GetMetaValue("sRGB", false);
    bool generateMips = data->GetMetaValue("generateMips", false);
    uint32 firstMip = imageParams->firstMip ? imageParams->firstMip : data->GetMetaValue("firstMip", 0u);

    // Load source image
    Path fileExt = params.path.GetFileExtension();
    if (fileExt.IsEqualNoCase(".dds") || fileExt.IsEqualNoCase(".ktx")) {
        ddsktx_texture_info texInfo {};
        ddsktx_error texError;
        if (!ddsktx_parse(&texInfo, srcData.Ptr(), int(srcData.Count()), &texError)) {
            outErrorDesc->FormatSelf("Loading source image (dds/ktx) failed: %s", texError.msg);
            return false;
        }

        ASSERT(!(texInfo.flags & DDSKTX_TEXTURE_FLAG_CUBEMAP)); // TODO
        ASSERT(texInfo.depth == 1); // TODO
        ASSERT(texInfo.num_layers == 1);    // TODO
        int startMipIdx = int(firstMip) < texInfo.num_mips ? int(firstMip) : (texInfo.num_mips - 1);
        numMips = uint32(texInfo.num_mips - startMipIdx);
        imageFormat = Image::_ConvertDDSFormat(texInfo.format);
        if (texInfo.flags & DDSKTX_TEXTURE_FLAG_SRGB)
            imageFormat = Image::_ConvertFormatSRGB(imageFormat);

        for (int mip = startMipIdx; mip < texInfo.num_mips; mip++) {
            ddsktx_sub_data subData;
            ddsktx_get_sub(&texInfo, &subData, srcData.Ptr(), int(srcData.Count()), 0, 0, mip);

            if (mip == startMipIdx) {
                imgWidth = uint32(subData.width);
                imgHeight = uint32(subData.height);
                pixels = const_cast<uint8*>((const uint8*)subData.buff);
            }

            int index = mip - startMipIdx;
            mips[index].width = uint32(subData.width);
            mips[index].height = uint32(subData.height);
            mips[index].offset = uint32(uintptr(subData.buff) - uintptr(pixels));

            if (mip == (texInfo.num_mips - 1)) 
                imageSize = mips[index].offset + subData.size_bytes;
        }

        isLoadedFromContainer = true;
    }
    else {
        pixels = stbi_load_from_memory(srcData.Ptr(), (int)srcData.Count(), &imgWidth, &imgHeight, &imgChannels, STBI_rgb_alpha);
        if (!pixels) {
            *outErrorDesc = "Loading source image failed";
            return false;
        }

        // Downsize the source image if firstMip is set
        if (firstMip) {
            int srcWidth = imgWidth;
            int srcHeight = imgHeight;

            for (uint32 i = 0; i < firstMip; i++) {
                int nextWidth = Max(imgWidth >> 1, 1);
                int nextHeight = Max(imgHeight >> 1, 1);
                if (nextWidth == imgWidth && nextHeight == imgHeight)
                    break;
                imgWidth = nextWidth;
                imgHeight = nextHeight;
            }
        
            if (imgWidth != srcWidth || imgHeight != srcHeight) {
                uint8* newPixels = Mem::AllocTyped<uint8>(imgWidth*imgHeight*4);
                int alphaChannel = imgChannels == 4 ? 3 : STBIR_ALPHA_CHANNEL_NONE;
                stbir_colorspace colorspace = sRGB ? STBIR_COLORSPACE_SRGB : STBIR_COLORSPACE_LINEAR;
                [[maybe_unused]] int r = stbir_resize_uint8_generic(pixels, srcWidth, srcHeight, 0, 
                                                                    newPixels, imgWidth, imgHeight, 0, 
                                                                    4, alphaChannel, 0,
                                                                    STBIR_EDGE_CLAMP, STBIR_FILTER_MITCHELL, colorspace, &tmpAlloc);
                ASSERT(r);
                pixels = newPixels;
            }
        }

        imageSize = imgWidth * imgHeight * 4;
        mips[0] = { .width = uint32(imgWidth), .height = uint32(imgHeight) };
    } 

    // Mip generation
    if (generateMips && imgWidth > 1 && imgHeight > 1 && !isLoadedFromContainer) {
        uint8* mipScratchBuffer = Mem::AllocTyped<uint8>(imageSize, &tmpAlloc);

        contentBlob.Write(pixels, imageSize);

        uint32 mipWidth = Max(static_cast<uint32>(imgWidth) >> 1, 1u);
        uint32 mipHeight = Max(static_cast<uint32>(imgHeight) >> 1, 1u);
        while (mipWidth && mipHeight) {
            uint32 mipSize = mipWidth * mipHeight * 4;
            const MipSurface& lastMip = mips[numMips - 1];

            int alphaChannel = imgChannels == 4 ? 3 : STBIR_ALPHA_CHANNEL_NONE;
            stbir_colorspace colorspace = sRGB ? STBIR_COLORSPACE_SRGB : STBIR_COLORSPACE_LINEAR;

            if (stbir_resize_uint8_generic(
                reinterpret_cast<const uint8*>(contentBlob.Data()) + lastMip.offset, 
                static_cast<int>(lastMip.width), static_cast<int>(lastMip.height), 0,
                mipScratchBuffer,
                static_cast<int>(mipWidth), static_cast<int>(mipHeight), 0,
                4, alphaChannel, 0,
                STBIR_EDGE_CLAMP, STBIR_FILTER_MITCHELL, colorspace, 
                &tmpAlloc))
            {
                ASSERT(numMips < GFXBACKEND_MAX_MIPS_PER_IMAGE);
                mips[numMips++] = { 
                    .width = mipWidth, 
                    .height = mipHeight, 
                    .offset = static_cast<uint32>(contentBlob.Size())
                };
                contentBlob.Write(mipScratchBuffer, mipSize);
            }
            else {
                ASSERT(0);
                break;
            }
                    
            uint32 nextWidth = Max(mipWidth >> 1, 1u);
            uint32 nextHeight = Max(mipHeight >> 1, 1u);
            if (nextWidth == mipWidth && nextHeight == mipHeight)
                break;
            mipWidth = nextWidth;
            mipHeight = nextHeight;
        }
    }
    else {
        contentBlob.Attach(pixels, imageSize, &tmpAlloc);
    }
        
    // Texture Compression
    if (!formatStr.IsEmpty() && !isLoadedFromContainer) {
        #if CONFIG_TOOLMODE
        ImageEncoderCompression::Enum compression = ImageEncoderCompression::FromString(formatStr.CStr());
        if (compression == ImageEncoderCompression::_Count) {
            *outErrorDesc = String<256>::Format("Image format not supported in MetaData '%s'", formatStr.CStr());
            return false;
        }

        GfxFormat compressedFormat = imageFormat;
        switch (compression) {
        case ImageEncoderCompression::BC1:      compressedFormat = GfxFormat::BC1_RGB_UNORM_BLOCK;  break;
        case ImageEncoderCompression::BC3:      compressedFormat = GfxFormat::BC3_UNORM_BLOCK; break;
        case ImageEncoderCompression::BC4:      compressedFormat = GfxFormat::BC4_UNORM_BLOCK; break;
        case ImageEncoderCompression::BC5:      compressedFormat = GfxFormat::BC5_UNORM_BLOCK; break;
        case ImageEncoderCompression::BC6H:     compressedFormat = GfxFormat::BC6H_UFLOAT_BLOCK; break;
        case ImageEncoderCompression::BC7:      compressedFormat = GfxFormat::BC7_UNORM_BLOCK; break;
        case ImageEncoderCompression::ASTC_4x4: compressedFormat = GfxFormat::ASTC_4x4_UNORM_BLOCK; break;
        case ImageEncoderCompression::ASTC_5x5: compressedFormat = GfxFormat::ASTC_5x5_UNORM_BLOCK; break;
        case ImageEncoderCompression::ASTC_6x6: compressedFormat = GfxFormat::ASTC_6x6_UNORM_BLOCK; break;
        case ImageEncoderCompression::ASTC_8x8: compressedFormat = GfxFormat::ASTC_8x8_UNORM_BLOCK; break;
        }

        if (compressedFormat != imageFormat) {
            imageFormat = compressedFormat;

            Blob compressedContentBlob(&tmpAlloc);
            compressedContentBlob.Reserve(contentBlob.Size());

            ImageEncoderFlags flags = ImageEncoderFlags::None;
            if (imgChannels == 4) 
                flags |= ImageEncoderFlags::HasAlpha;

            for (uint32 i = 0; i < numMips; i++) {
                MipSurface& mip = mips[i];

                ImageEncoderSurface surface {
                    .width = mip.width,
                    .height = mip.height,
                    .pixels = reinterpret_cast<const uint8*>(contentBlob.Data()) + mip.offset
                };

                Blob compressedBlob = ImageEncoder::Compress(compression, ImageEncoderQuality::Fast, flags, surface, &tmpAlloc);
                if (compressedBlob.IsValid()) {
                    mip.offset = static_cast<uint32>(compressedContentBlob.Size());
                    compressedContentBlob.Write(compressedBlob.Data(), compressedBlob.Size());
                }
                else {
                    *outErrorDesc = String<256>::Format("Encoding image to format '%s' failed", formatStr.CStr());
                    return false;
                }
            } // foreach mip

            contentBlob = compressedContentBlob;
        }
        #else
        ASSERT_MSG(0, "Image compression baking is not supported in non-tool builds");
        return false;
        #endif // CONFIG_TOOLMODE
    }

    if (sRGB)
        imageFormat = Image::_ConvertFormatSRGB(imageFormat);

    if (!contentBlob.IsValid())
        contentBlob.Attach(pixels, imageSize, &tmpAlloc);

    // Create image header and serialize memory. So header comes first, then re-copy the final contents at the end
    // We have to do this because there is also a lot of scratch work in between image buffers creation
    GfxImage* header = Mem::AllocZeroTyped<GfxImage>(1, &tmpAlloc);
    *header = {
        .width = uint32(imgWidth),
        .height = uint32(imgHeight),
        .depth = 1, // TODO
        .numMips = numMips, 
        .format = imageFormat,
        .contentSize = uint32(contentBlob.Size()),
    };

    size_t headerTotalSize = tmpAlloc.GetOffset() - tmpAlloc.GetPointerOffset(header);
    ASSERT(headerTotalSize <= UINT32_MAX);
    data->SetObjData(header, uint32(headerTotalSize));

    GfxImageDesc imageDesc {
        .width = uint16(header->width),
        .height = uint16(header->height),
        .numMips = uint16(header->numMips),
        .format = header->format,
        .usageFlags = GfxImageUsageFlags::TransferDst|GfxImageUsageFlags::Sampled,
        .arena = GfxMemoryArena::DynamicImageGPU,
    };
    for (uint32 i = 0; i < numMips; i++)
        imageDesc.mipOffsets[i] = mips[i].offset;

    data->AddGpuTextureObject(&header->handle, imageDesc, uint32(contentBlob.Size()), contentBlob.Data());

    return true;
}

bool AssetImageImpl::Reload(void*, void*)
{
    return true;
}

AssetHandleImage Image::Load(const char* path, const ImageLoadParams& params, const AssetGroup& group)
{
    AssetParams assetParams {
        .typeId = IMAGE_ASSET_TYPE,
        .path = path,
        .typeSpecificParams = const_cast<ImageLoadParams*>(&params)
    };

    return group.AddToLoadQueue(assetParams);
}

GfxImageHandle Image::GetWhite1x1()
{
    return gImageMgr.imageWhite;
}