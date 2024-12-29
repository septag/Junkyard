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

#if CONFIG_TOOLMODE
    #define STB_IMAGE_RESIZE_IMPLEMENTATION
    #define STB_IMAGE_RESIZE_STATIC
    #define STBIR_MALLOC(size,c)                Mem::Alloc(size, reinterpret_cast<MemAllocator*>(c));
    #define STBIR_FREE(ptr,c)                   Mem::Free(ptr, reinterpret_cast<MemAllocator*>(c));
    PRAGMA_DIAGNOSTIC_PUSH()
    PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4505)
    #include "../External/stb/stb_image_resize.h"
    PRAGMA_DIAGNOSTIC_POP()
#endif

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
        case GfxFormat::ASTC_4x4_UNORM_BLOCK:       return GfxFormat::ASTC_4x4_SRGB_BLOCK;
        case GfxFormat::ASTC_5x5_UNORM_BLOCK:       return GfxFormat::ASTC_5x5_SRGB_BLOCK;
        case GfxFormat::ASTC_6x6_UNORM_BLOCK:       return GfxFormat::ASTC_6x6_SRGB_BLOCK;
        case GfxFormat::ASTC_8x8_UNORM_BLOCK:       return GfxFormat::ASTC_8x8_SRGB_BLOCK;
        default:                                    return fmt;
        }
    }
} // Image

bool Asset::InitializeImageManager()
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

        GfxBackendCommandBuffer cmd = GfxBackend::BeginCommandBuffer(GfxQueueType::Transfer);
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

void Asset::ReleaseImageManager()
{
    GfxBackend::DestroyImage(gImageMgr.imageWhite);
    Asset::UnregisterType(IMAGE_ASSET_TYPE);
}

bool AssetImageImpl::Bake(const AssetParams&, AssetData* data, const Span<uint8>& srcData, String<256>* outErrorDesc)
{
    struct MipSurface
    {
        uint32 width;
        uint32 height;
        uint32 offset;
    };

    MemTempAllocator tmpAlloc;
    gStbIAlloc = &tmpAlloc;

    int imgWidth, imgHeight, imgChannels;
    stbi_uc* pixels = stbi_load_from_memory(srcData.Ptr(), (int)srcData.Count(), &imgWidth, &imgHeight, &imgChannels, STBI_rgb_alpha);
    if (!pixels) {
        *outErrorDesc = "Loading source image failed";
        return false;
    }
    
    GfxFormat imageFormat = GfxFormat::R8G8B8A8_UNORM;
    uint32 imageSize = imgWidth * imgHeight * 4;
    uint32 numMips = 1;

    MipSurface mips[GFXBACKEND_MAX_MIPS_PER_IMAGE];
    mips[0] = MipSurface { .width = static_cast<uint32>(imgWidth), .height = static_cast<uint32>(imgHeight) };

    Blob contentBlob(&tmpAlloc);
    contentBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);

    String32 formatStr = String32(data->GetMetaValue("format", ""));
    bool sRGB = data->GetMetaValue("sRGB", false);
    bool generateMips = data->GetMetaValue("generateMips", false);

    // Mip generation
    // TODO: Count in imageParams->firstMip
    if (generateMips && imgWidth > 1 && imgHeight > 1) {
        #if CONFIG_TOOLMODE
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
                    mips[numMips++] = MipSurface { 
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
        #else
            ASSERT_MSG(0, "Generate mips is not supported in non-tool builds");
            return false;
        #endif
    }
    else {
        contentBlob.Attach(pixels, imageSize, &tmpAlloc);
    }
        
    // Texture Compression
    if (!formatStr.IsEmpty()) {
        #if CONFIG_TOOLMODE
            ImageEncoderCompression::Enum compression = ImageEncoderCompression::FromString(formatStr.CStr());
            if (compression == ImageEncoderCompression::_Count) {
                *outErrorDesc = String<256>::Format("Image format not supported in MetaData '%s'", formatStr.CStr());
                return false;
            }

            switch (compression) {
            case ImageEncoderCompression::BC1:      imageFormat = GfxFormat::BC1_RGB_UNORM_BLOCK;  break;
            case ImageEncoderCompression::BC3:      imageFormat = GfxFormat::BC3_UNORM_BLOCK; break;
            case ImageEncoderCompression::BC4:      imageFormat = GfxFormat::BC4_UNORM_BLOCK; break;
            case ImageEncoderCompression::BC5:      imageFormat = GfxFormat::BC5_UNORM_BLOCK; break;
            case ImageEncoderCompression::BC6H:     imageFormat = GfxFormat::BC6H_UFLOAT_BLOCK; break;
            case ImageEncoderCompression::BC7:      imageFormat = GfxFormat::BC7_UNORM_BLOCK; break;
            case ImageEncoderCompression::ASTC_4x4: imageFormat = GfxFormat::ASTC_4x4_UNORM_BLOCK; break;
            case ImageEncoderCompression::ASTC_5x5: imageFormat = GfxFormat::ASTC_5x5_UNORM_BLOCK; break;
            case ImageEncoderCompression::ASTC_6x6: imageFormat = GfxFormat::ASTC_6x6_UNORM_BLOCK; break;
            case ImageEncoderCompression::ASTC_8x8: imageFormat = GfxFormat::ASTC_8x8_UNORM_BLOCK; break;
            }

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

// Freaking windows.h with it's Macros! In unity builds, it causes LoadImage to be converted to LoadImageA/W
#if PLATFORM_WINDOWS
    #ifdef LoadImage
    // #define _LoadImage LoadImage
    #undef LoadImage
    #endif
#endif

AssetHandleImage Asset::LoadImage(const char* path, const ImageLoadParams& params, const AssetGroup& group)
{
    AssetParams assetParams {
        .typeId = IMAGE_ASSET_TYPE,
        .path = path,
        .typeSpecificParams = const_cast<ImageLoadParams*>(&params)
    };

    return group.AddToLoadQueue(assetParams);
}

#if PLATFORM_WINDOWS
    #ifdef _LoadImage
    #define LoadImage _LoadImage
    #endif
#endif

