#ifndef __IMAGE_VK_CPP__
#define __IMAGE_VK_CPP__

#ifndef __GRAPHICS_CPP__
    #error "This file depends on Graphics.cpp for compilation"
#endif

#include "../External/vulkan/include/vulkan.h"

#include "Graphics.h"

#include "../Core/System.h"
#include "../Core/Hash.h"
#include "../Core/Jobs.h"
#include "../Core/Buffers.h"

#include "../RemoteServices.h"
#include "../AssetManager.h"
#include "../VirtualFS.h"

#include "../Tool/ImageEncoder.h"

//------------------------------------------------------------------------
static thread_local Allocator* gStbIAlloc = nullptr;

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_MALLOC(sz)                     memAlloc(sz, gStbIAlloc)
#define STBI_REALLOC(p, newsz)              memRealloc(p, newsz, gStbIAlloc)
#define STBI_FREE(p)                        memFree(p, gStbIAlloc)
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
    #define STBIR_MALLOC(size,c)                memAlloc(size, reinterpret_cast<Allocator*>(c));
    #define STBIR_FREE(ptr,c)                   memFree(ptr, reinterpret_cast<Allocator*>(c));
    PRAGMA_DIAGNOSTIC_PUSH()
    PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4505)
    #include "../External/stb/stb_image_resize.h"
    PRAGMA_DIAGNOSTIC_POP()
#endif

static constexpr uint32 kRemoteCmdLoadImage = MakeFourCC('L', 'I', 'M', 'G');

// keeps the parameters to UpdateDescriptorSet function, so we can keep the reloaded images in sync with GPU
struct GfxDescriptorUpdateCacheItem
{
    GfxDescriptorSet dset;
    uint32 numBindings;
    uint32 refCount;        // Total count of textures referencing this item in their bindings
    uint32 hash;            // Hash of the binding params (dset+numBindings+bindings)
    GfxDescriptorBindingDesc* bindings;
};

struct GfxImageLoadRequest
{
    AssetHandle handle;
    Allocator* alloc;
    AssetLoaderAsyncCallback loadCallback;
    void* loadCallbackUserData;
    ImageLoadParams loadParams;
};

struct GfxImageLoader final : AssetLoaderCallbacks
{
    AssetResult Load(AssetHandle handle, const AssetLoadParams& params, Allocator* dependsAlloc) override;
    void LoadRemote(AssetHandle handle, const AssetLoadParams& params, uint32 cacheHash, void* userData, AssetLoaderAsyncCallback loadCallback) override;
    bool InitializeResources(void* obj, const AssetLoadParams& params) override;
    bool ReloadSync(AssetHandle handle, void* prevData) override;
    void Release(void* data, Allocator*) override;
};

struct GfxImageManager
{
    GfxImage imageWhite;
    GfxImageLoader imageLoader;
    Mutex updateCacheMtx;
    Array<GfxDescriptorUpdateCacheItem*> updateCache;
    Mutex requestsMtx;
    Array<GfxImageLoadRequest> requests;
};

#pragma pack(push, 8)
struct Image
{
    GfxImage handle;
    uint32 width;
    uint32 height;
    uint32 depth;
    uint32 numMips;
    GfxFormat format;
    uint32 contentSize;
    uint32 mipOffsets[kGfxMaxMips];
    RelativePtr<uint8> content;
};
#pragma pack(pop)

static GfxImageManager gImageMgr;

INLINE GfxFormat gfxImageConvertFormatSRGB(GfxFormat fmt)
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

// This function is the main loader/baker
// Depending on the local meta-data, we either directly load the image from the disk or encode it with block compression
static Pair<Image*, uint32> gfxBakeImage(AssetHandle localHandle, const char* filepath, AssetPlatform platform, 
                                         Allocator* alloc, char* errorDesc, uint32 errorDescSize)    
{
    PROFILE_ZONE(true);

    struct MipSurface
    {
        uint32 width;
        uint32 height;
        uint32 offset;
    };

    MemTempAllocator tmpAlloc;

    Blob blob = vfsReadFile(filepath, VfsFlags::None, &tmpAlloc);
    if (!blob.IsValid()) {
        strPrintFmt(errorDesc, errorDescSize, "Opening image failed: %s", filepath);
        return {};
    }

    gStbIAlloc = &tmpAlloc;
    int imgWidth, imgHeight, imgChannels;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(blob.Data()), (int)blob.Size(), 
        &imgWidth, &imgHeight, &imgChannels, STBI_rgb_alpha);
    if (!pixels) {
        strPrintFmt(errorDesc, errorDescSize, "Loading image failed: %s", filepath);
        return {};
    }
    
    AssetMetaKeyValue* metaData;
    uint32 numMeta;

    GfxFormat imageFormat = GfxFormat::R8G8B8A8_UNORM;
    uint32 imageSize = imgWidth * imgHeight * 4;
    uint32 numMips = 1;

    MipSurface mips[kGfxMaxMips];
    mips[0] = MipSurface { .width = static_cast<uint32>(imgWidth), .height = static_cast<uint32>(imgHeight) };

    Blob contentBlob(&tmpAlloc);
    contentBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);

    // Choose different meta-data loading methods if user provides a valid localHandle
    // - When we have a localHandle, it means that asset currently has a valid asset on disk and we can fetch it's meta-data from asset-data
    // - Otherwise, we assume no meta-data is loaded in the asset database, so directly try to load it from the disk
    bool hasMetaData = localHandle.IsValid() ? 
        assetLoadMetaData(localHandle, &tmpAlloc, &metaData, &numMeta) : 
        assetLoadMetaData(filepath, platform, &tmpAlloc, &metaData, &numMeta);
    if (hasMetaData) {
        String32 formatStr = assetGetMetaValue<String32>(metaData, numMeta, "format", "");
        bool sRGB = assetGetMetaValue<bool>(metaData, numMeta, "sRGB", false);
        bool generateMips = assetGetMetaValue<bool>(metaData, numMeta, "generateMips", false);
        
        // Mip generation
        if (generateMips && imgWidth > 1 && imgHeight > 1) {
            #if CONFIG_TOOLMODE
                uint8* mipScratchBuffer = memAllocTyped<uint8>(imageSize, &tmpAlloc);

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
                        ASSERT(numMips < kGfxMaxMips);
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
               return {};
            #endif
        }
        else {
            contentBlob.Attach(pixels, imageSize, &tmpAlloc);
        }
        
        // Texture Compression
        if (!formatStr.IsEmpty()) {
            #if CONFIG_TOOLMODE
                ImageEncoderCompression compression = GetCompressionEnum(formatStr.CStr());
                if (compression == ImageEncoderCompression::_Count) {
                    strPrintFmt(errorDesc, errorDescSize, 
                        "Loading image '%s' failed. Image format not supported in meta-data '%s'", filepath, formatStr.CStr());
                    return {};
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

                    Blob compressedBlob = imageEncoderCompress(compression, ImageEncoderQuality::Fast, flags, surface, &tmpAlloc);
                    if (compressedBlob.IsValid()) {
                        mip.offset = static_cast<uint32>(compressedContentBlob.Size());
                        compressedContentBlob.Write(compressedBlob.Data(), compressedBlob.Size());
                    }
                    else {
                        strPrintFmt(errorDesc, errorDescSize, "Encoding image '%s' to '%s' failed.", filepath, formatStr.CStr());
                        return {};
                    }
                } // foreach mip

                contentBlob = compressedContentBlob;
            #else
                ASSERT_MSG(0, "Image compression baking is not supported in non-tool builds");
                return {};
            #endif // CONFIG_TOOLMODE
        }

        if (sRGB)
            imageFormat = gfxImageConvertFormatSRGB(imageFormat);
    } // hasMetaData
    else {
        contentBlob.Attach(pixels, imageSize, &tmpAlloc);
    }
    
    Image* header = tmpAlloc.MallocTyped<Image>();
    *header = Image {
        .width = uint32(imgWidth),
        .height = uint32(imgHeight),
        .depth = 1, // TODO
        .numMips = numMips, 
        .format = imageFormat,
        .contentSize = uint32(contentBlob.Size()),
    };

    for (uint32 i = 0; i < numMips; i++)
        header->mipOffsets[i] = mips[i].offset;

    header->content = memAllocCopy<uint8>((uint8*)contentBlob.Data(), (uint32)contentBlob.Size(), &tmpAlloc);

    uint32 bufferSize = uint32(tmpAlloc.GetOffset() - tmpAlloc.GetPointerOffset(header));
    return Pair<Image*, uint32>(memAllocCopyRawBytes<Image>(header, bufferSize, alloc), bufferSize);
}

// MT: runs from a task thread (server-side)
static void gfxLoadImageTask(uint32 groupIndex, void* userData)
{
    UNUSED(groupIndex);

    MemTempAllocator tmpAlloc;
    Blob* blob = reinterpret_cast<Blob*>(userData);
    Blob outgoingBlob(&tmpAlloc);
    outgoingBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);
    
    char filepath[kMaxPath];
    AssetPlatform platform;
    ImageLoadParams loadImageParams;
    char errorMsg[kRemoteErrorDescSize];

    uint32 handle;
    blob->Read<uint32>(&handle);
    blob->ReadStringBinary(filepath, sizeof(filepath));
    blob->Read<uint32>(reinterpret_cast<uint32*>(&platform));
    blob->Read(&loadImageParams, sizeof(loadImageParams));

    outgoingBlob.Write<uint32>(handle);

    TimerStopWatch timer;
    Pair<Image*, uint32> img = gfxBakeImage(AssetHandle(), filepath, platform, memDefaultAlloc(), errorMsg, sizeof(errorMsg));
   
    Image* header = img.first;
    if (header) {
        uint32 bufferSize = img.second;
        outgoingBlob.Write<uint32>(bufferSize);
        outgoingBlob.Write(header, bufferSize);

        remoteSendResponse(kRemoteCmdLoadImage, outgoingBlob, false, nullptr);
        memFree(header, memDefaultAlloc());
        logVerbose("Image loaded: %s (%.1f ms)", filepath, timer.ElapsedMS());
    }
    else {
        remoteSendResponse(kRemoteCmdLoadImage, outgoingBlob, true, errorMsg);
        logVerbose(errorMsg);
    }
    
    outgoingBlob.Free();
        
    blob->Free();
    memFree(blob);
}

// MT: runs from RemoteServices thread
static bool gfxImageHandlerServerFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, Blob*, 
                                    void*, char outgoingErrorDesc[kRemoteErrorDescSize])
{
    ASSERT(cmd == kRemoteCmdLoadImage);
    UNUSED(outgoingErrorDesc);

    // get a copy of incomingData pass it on to a task
    Blob* taskDataBlob = NEW(memDefaultAlloc(), Blob)();
    incomingData.CopyTo(taskDataBlob);
    jobsDispatchAuto(JobsType::LongTask, gfxLoadImageTask, taskDataBlob, 1, JobsPriority::Low);

    return true;
}

// MT: called from RemoteServices thread
static void gfxImageHandlerClientFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, void* userData, bool error, const char* errorDesc)
{
    ASSERT(cmd == kRemoteCmdLoadImage);
    UNUSED(userData);

    AssetHandle handle;
    incomingData.Read<uint32>(&handle.id);
    ASSERT(handle.IsValid());

    GfxImageLoadRequest request {};

    { MutexScope mtx(gImageMgr.requestsMtx);
        if (uint32 reqIndex = gImageMgr.requests.FindIf([handle](const GfxImageLoadRequest& req) { return req.handle == handle; });
            reqIndex != UINT32_MAX)
        {
            request = gImageMgr.requests[reqIndex];
            gImageMgr.requests.RemoveAndSwap(reqIndex);
        }
        else {
            ASSERT(0);
        }
    }

    if (!error) {
        uint32 bufferSize = 0;
        incomingData.Read<uint32>(&bufferSize);
        ASSERT(bufferSize);
        
        MemTempAllocator tmpAlloc;
        void* imgData = tmpAlloc.Malloc(bufferSize);
        incomingData.Read(imgData, bufferSize);

        if (request.loadCallback) 
            request.loadCallback(handle, AssetResult { .obj = imgData, .objBufferSize = bufferSize }, request.loadCallbackUserData);
    }
    else {
        logError(errorDesc);
        if (request.loadCallback) 
            request.loadCallback(handle, AssetResult {}, request.loadCallbackUserData);
    }
}

static bool gfxInitializeImageManager()
{
    // These parts should not be used in headless mode:
    // - placeholder images
    // - Asset loaders for the images
    // - Descriptor cache management for reloads
    if (!settingsGetGraphics().headless) {
        static constexpr uint32 kWhitePixel = 0xffffffff;
        gImageMgr.imageWhite = gfxCreateImage(GfxImageDesc {
            .width = 1,
            .height = 1,
            .format = GfxFormat::R8G8B8A8_UNORM,
            .sampled = true,
            .size = sizeof(kWhitePixel),
            .content = reinterpret_cast<const void*>(&kWhitePixel)
        });
    
        if (!gImageMgr.imageWhite.IsValid())
            return false;
    
        assetRegister(AssetTypeDesc {
            .fourcc = kImageAssetType,
            .name = "Image",
            .callbacks = &gImageMgr.imageLoader,
            .extraParamTypeName = "ImageLoadParams",
            .extraParamTypeSize = sizeof(ImageLoadParams),
            .failedObj = IntToPtr(gImageMgr.imageWhite.id),
            .asyncObj = IntToPtr(gImageMgr.imageWhite.id)
        });
    
        gImageMgr.updateCacheMtx.Initialize();
        gImageMgr.updateCache.SetAllocator(memDefaultAlloc());  // TODO: alloc
    }

    // initialized in all cases
    // - Remote loader/baker
    gImageMgr.requestsMtx.Initialize();
    gImageMgr.requests.SetAllocator(memDefaultAlloc());
    remoteRegisterCommand(RemoteCommandDesc {
        .cmdFourCC = kRemoteCmdLoadImage,
        .serverFn = gfxImageHandlerServerFn,
        .clientFn = gfxImageHandlerClientFn,
        .async = true
    });

    return true;
}

void _private::gfxReleaseImageManager()
{
    gImageMgr.requests.Free();
    gImageMgr.requestsMtx.Release();

    if (!settingsGetGraphics().headless) {
        gfxDestroyImage(gImageMgr.imageWhite);

        for (GfxDescriptorUpdateCacheItem* item : gImageMgr.updateCache)
            memFree(item);
        gImageMgr.updateCache.Free();
        gImageMgr.updateCacheMtx.Release();

        assetUnregister(kImageAssetType);
    }
}

static void gfxUpdateImageDescriptorSetCache(GfxDescriptorSet dset, uint32 numBindings, const GfxDescriptorBindingDesc* bindings)
{
    HashMurmur32Incremental hasher(0x1e1e);
    uint32 hash = hasher.Add<GfxDescriptorSet>(&dset)
                        .Add<GfxDescriptorBindingDesc>(bindings, numBindings)
                        .Hash();

    MutexScope mtx(gImageMgr.updateCacheMtx);
    uint32 index = gImageMgr.updateCache.FindIf([hash](const GfxDescriptorUpdateCacheItem* item)->bool{ return item->hash == hash;});
    GfxDescriptorUpdateCacheItem* item;
    if (index != UINT32_MAX) {
        item = gImageMgr.updateCache[index];
    }
    else {
        BuffersAllocPOD<GfxDescriptorUpdateCacheItem> podAlloc;
        podAlloc.AddMemberField<GfxDescriptorBindingDesc>(offsetof(GfxDescriptorUpdateCacheItem, bindings), numBindings);
        item = podAlloc.Calloc(); // TODO: alloc
        item->dset = dset;
        item->numBindings = numBindings;
        memcpy(item->bindings, bindings, sizeof(GfxDescriptorBindingDesc)*numBindings);

        gImageMgr.updateCache.Push(item);
    }
        
    for (uint32 i = 0; i < numBindings; i++) {
        const GfxDescriptorBindingDesc& desc = bindings[i];
        if (desc.type == GfxDescriptorType::SampledImage)
            ++item->refCount;
    } // for each binding
}

GfxImage gfxImageGetWhite()
{
    return gImageMgr.imageWhite;
}

AssetHandleImage assetLoadImage(const char* path, const ImageLoadParams& params, AssetBarrier barrier)
{
    AssetLoadParams loadParams {
        .path = path,
        .alloc = memDefaultAlloc(), // TODO: should be able to use custom allocator
        .typeId = kImageAssetType,
        .barrier = barrier
    };

    return AssetHandleImage { assetLoad(loadParams, &params) };
}

GfxImage assetGetImage(AssetHandleImage imageHandle)
{
    return reinterpret_cast<Image*>(_private::assetGetData(imageHandle))->handle;
}

// MT: runs from a task thread (AssetManager)
AssetResult GfxImageLoader::Load(AssetHandle handle, const AssetLoadParams& params, Allocator*)
{
    ASSERT(params.next);

    char errorDesc[512];
    Pair<Image*, uint32> img = gfxBakeImage(handle, params.path, AssetPlatform::Auto, params.alloc, errorDesc, sizeof(errorDesc));
    if (img.first == nullptr) {
        logError(errorDesc);
        return AssetResult {};
    }

    return AssetResult { .obj = img.first, .objBufferSize = img.second };
}

void GfxImageLoader::LoadRemote(AssetHandle handle, const AssetLoadParams& params, uint32 cacheHash, void* userData, 
                                AssetLoaderAsyncCallback loadCallback)
{
    ASSERT(params.next);
    ASSERT(loadCallback);
    ASSERT(remoteIsConnected());
    UNUSED(cacheHash);

    const ImageLoadParams* textureParams = reinterpret_cast<ImageLoadParams*>(params.next.Get());

    { MutexScope mtx(gImageMgr.requestsMtx);
        gImageMgr.requests.Push(GfxImageLoadRequest {
            .handle = handle,
            .alloc = params.alloc,
            .loadCallback = loadCallback,
            .loadCallbackUserData = userData,
            .loadParams = *textureParams
        });
    }

    MemTempAllocator tmpAlloc;
    Blob outgoingBlob(&tmpAlloc);
    outgoingBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);

    outgoingBlob.Write<uint32>(handle.id);
    outgoingBlob.WriteStringBinary(params.path, strLen(params.path));
    outgoingBlob.Write<uint32>(static_cast<uint32>(params.platform));
    outgoingBlob.Write(textureParams, sizeof(ImageLoadParams));

    remoteExecuteCommand(kRemoteCmdLoadImage, outgoingBlob);

    outgoingBlob.Free();
}

bool GfxImageLoader::InitializeResources(void* obj, const AssetLoadParams& params)
{
    Image* header = reinterpret_cast<Image*>(obj);
    const ImageLoadParams& loadParams = *reinterpret_cast<const ImageLoadParams*>(params.next.Get());

    GfxImage image = gfxCreateImage(GfxImageDesc {
        .width = header->width,
        .height = header->height,
        .numMips = header->numMips,
        .format = header->format,
        .samplerFilter = loadParams.samplerFilter,
        .samplerWrap = loadParams.samplerWrap,
        .sampled = true,
        .size = header->contentSize,
        .content = header->content.Get(),
        .mipOffsets = header->mipOffsets
    });

    header->handle = image;
    return image.IsValid();
}

bool GfxImageLoader::ReloadSync(AssetHandle handle, void* prevData)
{
    GfxImage oldImageHandle { PtrToInt<uint32>(prevData) };
    GfxImage newImageHandle { PtrToInt<uint32>(_private::assetGetData(handle)) };

    MutexScope mtx(gImageMgr.updateCacheMtx);

    for (uint32 i = 0; i < gImageMgr.updateCache.Count(); i++) {
        GfxDescriptorUpdateCacheItem* item = gImageMgr.updateCache[i];
        
        bool imageFound = false;
        for (uint32 k = 0; k < item->numBindings; k++) {
            if ((item->bindings[k].type == GfxDescriptorType::SampledImage || 
                 item->bindings[k].type == GfxDescriptorType::Sampler) &&
                 item->bindings[k].image == oldImageHandle)
            {
                item->bindings[k].image = newImageHandle;
                imageFound = true;
            }
        }

        if (imageFound)
            gfxUpdateDescriptorSet(item->dset, item->numBindings, item->bindings);
    }

    return true;
}

void GfxImageLoader::Release(void* data, Allocator* alloc)
{
    ASSERT(data);

    Image* image = reinterpret_cast<Image*>(data);
    GfxImage handle = image->handle;

    gfxDestroyImage(handle);

    // look in all descriptor cache items and decrease reference count. if it's zero then free it
    for (uint32 i = 0; i < gImageMgr.updateCache.Count(); i++) {
        GfxDescriptorUpdateCacheItem* item = gImageMgr.updateCache[i];

        for (uint32 k = 0; k < item->numBindings; k++) {
            if (item->bindings[k].type == GfxDescriptorType::SampledImage && item->bindings[k].image == handle) {
                if (--item->refCount == 0) {
                    gImageMgr.updateCache.RemoveAndSwap(i);
                    memFree(item);
                    i--;
                    break;
                }
            }
        } // foreach binding
    } // For each item in descriptor set update cache

    memFree(image, alloc);
}

#endif // __IMAGE_VK_CPP__