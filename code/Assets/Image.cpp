#include "Image.h"
#include "AssetManager.h"

#include "../Core/System.h"
#include "../Core/Hash.h"
#include "../Core/Jobs.h"
#include "../Core/TracyHelper.h"
#include "../Core/Log.h"

#include "../Common/RemoteServices.h"
#include "../Common/VirtualFS.h"
#include "../Common/JunkyardSettings.h"

#include "../Tool/ImageEncoder.h"


//----------------------------------------------------------------------------------------------------------------------
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

static constexpr uint32 RCMD_LOAD_IMAGE = MakeFourCC('L', 'I', 'M', 'G');

// keeps the parameters to UpdateDescriptorSet function, so we can keep the reloaded images in sync with GPU
struct AssetDescriptorUpdateCacheItem
{
    GfxDescriptorSetHandle dset;
    uint32 numBindings;
    uint32 refCount;        // Total count of textures referencing this item in their bindings
    uint32 hash;            // Hash of the binding params (dset+numBindings+bindings)
    GfxDescriptorBindingDesc* bindings;
};

struct AssetImageLoadRequest
{
    AssetHandle handle;
    MemAllocator* alloc;
    AssetLoaderAsyncCallback loadCallback;
    void* loadCallbackUserData;
    ImageLoadParams loadParams;
};

struct AssetImageCallbacks final : AssetCallbacks
{
    AssetResult Load(AssetHandle handle, const AssetLoadParams& params, uint32 cacheHash, MemAllocator* dependsAlloc) override;
    void LoadRemote(AssetHandle handle, const AssetLoadParams& params, uint32 cacheHash, void* userData, AssetLoaderAsyncCallback loadCallback) override;
    bool InitializeSystemResources(void* obj, const AssetLoadParams& params) override;
    bool ReloadSync(AssetHandle handle, void* prevData) override;
    void Release(void* data, MemAllocator*) override;
};

struct AssetImageImpl final : AssetTypeImplBase
{
    bool Bake(const AssetParams& params, AssetData* data, const Span<uint8>& srcData, String<256>* outErrorDesc) override;
};

struct AssetImageManager
{
    Mutex updateCacheMtx;
    Mutex requestsMtx;

    MemAllocator* runtimeAlloc;
    AssetImageCallbacks imageLoader;
    AssetImageImpl imageImpl;
    Array<AssetDescriptorUpdateCacheItem*> updateCache;
    Array<AssetImageLoadRequest> requests;

    GfxImageHandle imageWhite;
};

static AssetImageManager gImageMgr;

INLINE GfxFormat assetImageConvertFormatSRGB(GfxFormat fmt)
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
static Pair<GfxImage*, uint32> assetBakeImage(const char* filepath, MemAllocator* alloc, const AssetMetaKeyValue* metaData, uint32 numMeta,
                                                char* outErrorDesc, uint32 errorDescSize)    
{
    struct MipSurface
    {
        uint32 width;
        uint32 height;
        uint32 offset;
    };

    MemTempAllocator tmpAlloc;

    Blob blob = Vfs::ReadFile(filepath, VfsFlags::None, &tmpAlloc);
    if (!blob.IsValid()) {
        strPrintFmt(outErrorDesc, errorDescSize, "Opening image failed: %s", filepath);
        return {};
    }

    gStbIAlloc = &tmpAlloc;
    int imgWidth, imgHeight, imgChannels;
    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(blob.Data()), (int)blob.Size(), 
        &imgWidth, &imgHeight, &imgChannels, STBI_rgb_alpha);
    if (!pixels) {
        strPrintFmt(outErrorDesc, errorDescSize, "Loading image failed: %s", filepath);
        return {};
    }
    
    GfxFormat imageFormat = GfxFormat::R8G8B8A8_UNORM;
    uint32 imageSize = imgWidth * imgHeight * 4;
    uint32 numMips = 1;

    MipSurface mips[GFX_MAX_MIPS];
    mips[0] = MipSurface { .width = static_cast<uint32>(imgWidth), .height = static_cast<uint32>(imgHeight) };

    Blob contentBlob(&tmpAlloc);
    contentBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);

    if (metaData) {
        String32 formatStr = assetGetMetaValue<String32>(metaData, numMeta, "format", "");
        bool sRGB = assetGetMetaValue<bool>(metaData, numMeta, "sRGB", false);
        bool generateMips = assetGetMetaValue<bool>(metaData, numMeta, "generateMips", false);
        
        // Mip generation
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
                        ASSERT(numMips < GFX_MAX_MIPS);
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
                ImageEncoderCompression::Enum compression = ImageEncoderCompression::FromString(formatStr.CStr());
                if (compression == ImageEncoderCompression::_Count) {
                    strPrintFmt(outErrorDesc, errorDescSize, 
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

                    Blob compressedBlob = ImageEncoder::Compress(compression, ImageEncoderQuality::Fast, flags, surface, &tmpAlloc);
                    if (compressedBlob.IsValid()) {
                        mip.offset = static_cast<uint32>(compressedContentBlob.Size());
                        compressedContentBlob.Write(compressedBlob.Data(), compressedBlob.Size());
                    }
                    else {
                        strPrintFmt(outErrorDesc, errorDescSize, "Encoding image '%s' to '%s' failed.", filepath, formatStr.CStr());
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
            imageFormat = assetImageConvertFormatSRGB(imageFormat);
    } // hasMetaData
    else {
        contentBlob.Attach(pixels, imageSize, &tmpAlloc);
    }
    
    // Create image header and serialize memory. So header comes first, then re-copy the final contents at the end
    // We have to do this because there is also a lot of scratch work in between image buffers creation
    GfxImage* header = tmpAlloc.MallocTyped<GfxImage>();
    *header = GfxImage {
        .width = uint32(imgWidth),
        .height = uint32(imgHeight),
        .depth = 1, // TODO
        .numMips = numMips, 
        .format = imageFormat,
        .contentSize = uint32(contentBlob.Size()),
    };

    for (uint32 i = 0; i < numMips; i++)
        header->mipOffsets[i] = mips[i].offset;

    header->content = Mem::AllocCopy<uint8>((uint8*)contentBlob.Data(), (uint32)contentBlob.Size(), &tmpAlloc);

    uint32 bufferSize = uint32(tmpAlloc.GetOffset() - tmpAlloc.GetPointerOffset(header));
    return Pair<GfxImage*, uint32>(Mem::AllocCopyRawBytes<GfxImage>(header, bufferSize, alloc), bufferSize);
}

// MT: runs from a task thread (server-side)
static void assetLoadImageTask(uint32 groupIndex, void* userData)
{
    UNUSED(groupIndex);

    MemTempAllocator tmpAlloc;
    Blob* blob = reinterpret_cast<Blob*>(userData);
    Blob outgoingBlob(&tmpAlloc);
    outgoingBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);
    
    char filepath[PATH_CHARS_MAX];
    AssetPlatform::Enum platform;
    ImageLoadParams loadImageParams;
    char errorMsg[kRemoteErrorDescSize];

    uint32 handle;
    uint32 oldCacheHash;
    blob->Read<uint32>(&handle);
    blob->Read<uint32>(&oldCacheHash);
    blob->ReadStringBinary(filepath, sizeof(filepath));
    blob->Read<uint32>(reinterpret_cast<uint32*>(&platform));
    blob->Read(&loadImageParams, sizeof(loadImageParams));

    outgoingBlob.Write<uint32>(handle);

    AssetMetaKeyValue* metaData;
    uint32 numMeta;
    assetLoadMetaData(filepath, platform, &tmpAlloc, &metaData, &numMeta);
    
    uint32 cacheHash = assetMakeCacheHash(AssetCacheDesc {
        .filepath = filepath,
        .loadParams = &loadImageParams,
        .loadParamsSize = sizeof(loadImageParams),
        .metaData = metaData,
        .numMeta = numMeta,
        .lastModified = Vfs::GetLastModified(filepath)
    });
    
    if (cacheHash != oldCacheHash) {
        TimerStopWatch timer;
        Pair<GfxImage*, uint32> img = assetBakeImage(filepath, Mem::GetDefaultAlloc(), metaData, numMeta, errorMsg, sizeof(errorMsg));
        GfxImage* header = img.first;
        if (header) {
            uint32 bufferSize = img.second;
            outgoingBlob.Write<uint32>(cacheHash);
            outgoingBlob.Write<uint32>(bufferSize);
            outgoingBlob.Write(header, bufferSize);

            Remote::SendResponse(RCMD_LOAD_IMAGE, outgoingBlob, false, nullptr);
            LOG_VERBOSE("Image loaded: %s (%.1f ms)", filepath, timer.ElapsedMS());

            Mem::Free(header, Mem::GetDefaultAlloc());
        }
        else {
            Remote::SendResponse(RCMD_LOAD_IMAGE, outgoingBlob, true, errorMsg);
            LOG_VERBOSE(errorMsg);
        }
    }
    else {
        outgoingBlob.Write<uint32>(cacheHash);
        outgoingBlob.Write<uint32>(0);  // nothing has loaded. it's safe to load from client's local cache
        Remote::SendResponse(RCMD_LOAD_IMAGE, outgoingBlob, false, nullptr);
        LOG_VERBOSE("Image: %s [cached]", filepath);
    }
       
    blob->Free();
    Mem::Free(blob);
}

// MT: runs from RemoteServices thread
static bool assetImageHandlerServerFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, Blob*, 
                                    void*, char outgoingErrorDesc[kRemoteErrorDescSize])
{
    ASSERT(cmd == RCMD_LOAD_IMAGE);
    UNUSED(outgoingErrorDesc);

    // get a copy of incomingData pass it on to a task
    Blob* taskDataBlob = NEW(Mem::GetDefaultAlloc(), Blob)();
    incomingData.CopyTo(taskDataBlob);
    Jobs::DispatchAndForget(JobsType::LongTask, assetLoadImageTask, taskDataBlob, 1, JobsPriority::Low);

    return true;
}

// MT: called from RemoteServices thread
static void assetImageHandlerClientFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, void* userData, bool error, const char* errorDesc)
{
    ASSERT(cmd == RCMD_LOAD_IMAGE);
    UNUSED(userData);

    AssetHandle handle;
    incomingData.Read<uint32>(&handle.mId);
    ASSERT(handle.IsValid());

    AssetImageLoadRequest request {};

    { 
        MutexScope mtx(gImageMgr.requestsMtx);
        if (uint32 reqIndex = gImageMgr.requests.FindIf([handle](const AssetImageLoadRequest& req) { return req.handle == handle; });
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
        uint32 cacheHash = 0;
        uint32 bufferSize = 0;
        void* imgData = nullptr;
        incomingData.Read<uint32>(&cacheHash);
        incomingData.Read<uint32>(&bufferSize);
        
        if (bufferSize) {
            imgData = Mem::Alloc(bufferSize, request.alloc);
            incomingData.Read(imgData, bufferSize);
        }

        if (request.loadCallback) {
            request.loadCallback(handle, AssetResult { .obj = imgData, .objBufferSize = bufferSize, .cacheHash = cacheHash }, 
                                 request.loadCallbackUserData);
        }
    }
    else {
        LOG_ERROR(errorDesc);
        if (request.loadCallback) 
            request.loadCallback(handle, AssetResult {}, request.loadCallbackUserData);
    }
}

static void assetUpdateImageDescriptorSetCache(GfxDescriptorSetHandle dset, uint32 numBindings, const GfxDescriptorBindingDesc* bindings)
{
    HashMurmur32Incremental hasher(0x1e1e);
    uint32 hash = hasher.Add<GfxDescriptorSetHandle>(dset)
                        .Add<GfxDescriptorBindingDesc>(bindings, numBindings)
                        .Hash();

    MutexScope mtx(gImageMgr.updateCacheMtx);
    uint32 index = gImageMgr.updateCache.FindIf([hash](const AssetDescriptorUpdateCacheItem* item)->bool{ return item->hash == hash;});
    AssetDescriptorUpdateCacheItem* item;
    if (index != UINT32_MAX) {
        item = gImageMgr.updateCache[index];
    }
    else {
        MemSingleShotMalloc<AssetDescriptorUpdateCacheItem> mallocator;
        mallocator.AddMemberArray<GfxDescriptorBindingDesc>(offsetof(AssetDescriptorUpdateCacheItem, bindings), numBindings);
        item = mallocator.Calloc(gImageMgr.runtimeAlloc);
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

GfxImageHandle assetGetWhiteImage1x1()
{
    return gImageMgr.imageWhite;
}

AssetHandleImage assetLoadImage(const char* path, const ImageLoadParams& params, AssetBarrier barrier)
{
    AssetLoadParams loadParams {
        .path = path,
        .alloc = Mem::GetDefaultAlloc(), // TODO: should be able to use custom allocator
        .typeId = IMAGE_ASSET_TYPE,
        .barrier = barrier
    };

    return AssetHandleImage { assetLoad(loadParams, &params) };
}

GfxImageHandle assetGetImage(AssetHandleImage imageHandle)
{
    return reinterpret_cast<GfxImage*>(_private::assetGetData(imageHandle))->handle;
}

bool _private::assetInitializeImageManager()
{
    gImageMgr.runtimeAlloc = Mem::GetDefaultAlloc(); // TODO: maybe use a tlsf allocator or something

    // These parts should not be used in headless mode:
    // - placeholder images
    // - Asset loaders for the images
    // - Descriptor cache management for reloads
    if (SettingsJunkyard::Get().graphics.enable && !SettingsJunkyard::Get().graphics.headless) {
        const uint32 kWhitePixel = 0xffffffff;
        GfxImageDesc imageDesc = GfxImageDesc {
            .width = 1,
            .height = 1,
            .format = GfxFormat::R8G8B8A8_UNORM,
            .sampled = true,
            .size = sizeof(kWhitePixel),
            .content = &kWhitePixel
        };
        //imageDesc.content = &kWhitePixel;
        gImageMgr.imageWhite = gfxCreateImage(imageDesc);
        if (!gImageMgr.imageWhite.IsValid())
            return false;

        static GfxImage whiteImage = {
            .handle = gImageMgr.imageWhite,
            .width = 1,
            .height = 1,
            .depth = 1,
            .numMips = 1,
            .format = GfxFormat::R8G8B8A8_UNORM
        };

        assetRegisterType(AssetTypeDesc {
            .fourcc = IMAGE_ASSET_TYPE,
            .name = "Image",
            .callbacks = &gImageMgr.imageLoader,
            .impl = &gImageMgr.imageImpl,
            .extraParamTypeName = "ImageLoadParams",
            .extraParamTypeSize = sizeof(ImageLoadParams),
            .failedObj = &whiteImage,
            .asyncObj = &whiteImage
        });
    
        gImageMgr.updateCacheMtx.Initialize();
        gImageMgr.updateCache.SetAllocator(gImageMgr.runtimeAlloc);
    }

    // initialized in all cases
    // - Remote loader/baker
    gImageMgr.requestsMtx.Initialize();
    gImageMgr.requests.SetAllocator(gImageMgr.runtimeAlloc);
    Remote::RegisterCommand(RemoteCommandDesc {
        .cmdFourCC = RCMD_LOAD_IMAGE,
        .serverFn = assetImageHandlerServerFn,
        .clientFn = assetImageHandlerClientFn,
        .async = true
    });

    _private::gfxSetUpdateImageDescriptorCallback(assetUpdateImageDescriptorSetCache);

    LOG_INFO("(init) Image asset manager");
    return true;
}

void _private::assetReleaseImageManager()
{
    gImageMgr.requests.Free();
    gImageMgr.requestsMtx.Release();

    if (!SettingsJunkyard::Get().graphics.headless) {
        gfxDestroyImage(gImageMgr.imageWhite);

        MemSingleShotMalloc<AssetDescriptorUpdateCacheItem> mallocator;
        for (AssetDescriptorUpdateCacheItem* item : gImageMgr.updateCache)
            mallocator.Free(item, gImageMgr.runtimeAlloc);
        gImageMgr.updateCache.Free();
        gImageMgr.updateCacheMtx.Release();

        assetUnregisterType(IMAGE_ASSET_TYPE);
    }
}

// MT: runs from a task thread (AssetManager)
AssetResult AssetImageCallbacks::Load(AssetHandle handle, const AssetLoadParams& params, uint32 cacheHash, MemAllocator*)
{
    ASSERT(params.next);

    MemTempAllocator tmpAlloc;

    AssetMetaKeyValue* metaData;
    uint32 numMeta;
    assetLoadMetaData(handle, &tmpAlloc, &metaData, &numMeta);

    uint32 newCacheHash = assetMakeCacheHash(AssetCacheDesc {
        .filepath = params.path,
        .loadParams = params.next.Get(), 
        .loadParamsSize = sizeof(ImageLoadParams),
        .metaData = metaData,
        .numMeta = numMeta,
        .lastModified = Vfs::GetLastModified(params.path)
    });

    if (newCacheHash != cacheHash) {
        char errorDesc[512];
        Pair<GfxImage*, uint32> img = assetBakeImage(params.path, params.alloc, metaData, numMeta, errorDesc, sizeof(errorDesc));
        if (img.first != nullptr) {
            return AssetResult { .obj = img.first, .objBufferSize = img.second, .cacheHash = newCacheHash };
        }
        else {
            LOG_ERROR(errorDesc);
            return AssetResult {};
        }
    }
    else {
        return AssetResult { .cacheHash = newCacheHash };
    }

}

void AssetImageCallbacks::LoadRemote(AssetHandle handle, const AssetLoadParams& params, uint32 cacheHash, void* userData, 
                                     AssetLoaderAsyncCallback loadCallback)
{
    ASSERT(params.next);
    ASSERT(loadCallback);
    ASSERT(Remote::IsConnected());

    const ImageLoadParams* textureParams = reinterpret_cast<ImageLoadParams*>(params.next.Get());

    { 
        MutexScope mtx(gImageMgr.requestsMtx);
        gImageMgr.requests.Push(AssetImageLoadRequest {
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

    outgoingBlob.Write<uint32>(uint32(handle));
    outgoingBlob.Write<uint32>(cacheHash);
    outgoingBlob.WriteStringBinary(params.path, strLen(params.path));
    outgoingBlob.Write<uint32>(static_cast<uint32>(params.platform));
    outgoingBlob.Write(textureParams, sizeof(ImageLoadParams));

    Remote::ExecuteCommand(RCMD_LOAD_IMAGE, outgoingBlob);

    outgoingBlob.Free();
}

bool AssetImageCallbacks::InitializeSystemResources(void* obj, const AssetLoadParams& params)
{
    GfxImage* header = reinterpret_cast<GfxImage*>(obj);
    const ImageLoadParams& loadParams = *reinterpret_cast<const ImageLoadParams*>(params.next.Get());

    GfxImageHandle image = gfxCreateImage(GfxImageDesc {
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

bool AssetImageCallbacks::ReloadSync(AssetHandle handle, void* prevData)
{
    GfxImageHandle oldImageHandle { PtrToInt<uint32>(prevData) };
    GfxImageHandle newImageHandle { PtrToInt<uint32>(_private::assetGetData(handle)) };

    MutexScope mtx(gImageMgr.updateCacheMtx);

    for (uint32 i = 0; i < gImageMgr.updateCache.Count(); i++) {
        AssetDescriptorUpdateCacheItem* item = gImageMgr.updateCache[i];
        
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

void AssetImageCallbacks::Release(void* data, MemAllocator* alloc)
{
    ASSERT(data);

    GfxImage* image = reinterpret_cast<GfxImage*>(data);
    GfxImageHandle handle = image->handle;

    gfxDestroyImage(handle);

    // look in all descriptor cache items and decrease reference count. if it's zero then free it
    for (uint32 i = 0; i < gImageMgr.updateCache.Count(); i++) {
        AssetDescriptorUpdateCacheItem* item = gImageMgr.updateCache[i];

        for (uint32 k = 0; k < item->numBindings; k++) {
            if (item->bindings[k].type == GfxDescriptorType::SampledImage && item->bindings[k].image == handle) {
                if (--item->refCount == 0) {
                    gImageMgr.updateCache.RemoveAndSwap(i);
                    Mem::Free(item);
                    i--;
                    break;
                }
            }
        } // foreach binding
    } // For each item in descriptor set update cache

    Mem::Free(image, alloc);
}

bool AssetImageImpl::Bake(const AssetParams& params, AssetData* data, const Span<uint8>& srcData, String<256>* outErrorDesc)
{
    struct MipSurface
    {
        uint32 width;
        uint32 height;
        uint32 offset;
    };

    const ImageLoadParams* imageParams = (const ImageLoadParams*)params.typeSpecificParams;
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

    MipSurface mips[GFX_MAX_MIPS];
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
                    ASSERT(numMips < GFX_MAX_MIPS);
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
        imageFormat = assetImageConvertFormatSRGB(imageFormat);

    if (!contentBlob.IsValid())
        contentBlob.Attach(pixels, imageSize, &tmpAlloc);

    // Create image header and serialize memory. So header comes first, then re-copy the final contents at the end
    // We have to do this because there is also a lot of scratch work in between image buffers creation
    GfxImage* header = Mem::AllocZeroTyped<GfxImage>(1, &tmpAlloc);
    *header = GfxImage {
        .width = uint32(imgWidth),
        .height = uint32(imgHeight),
        .depth = 1, // TODO
        .numMips = numMips, 
        .format = imageFormat,
        .contentSize = uint32(contentBlob.Size()),
    };

    uint32* mipOffsets = Mem::AllocTyped<uint32>(numMips, &tmpAlloc);
    for (uint32 i = 0; i < numMips; i++)
        mipOffsets[i] = mips[i].offset;

    size_t headerTotalSize = tmpAlloc.GetOffset() - tmpAlloc.GetPointerOffset(header);
    ASSERT(headerTotalSize <= UINT32_MAX);
    data->SetObjData(header, uint32(headerTotalSize));

    GfxImageDesc imageDesc {
        .width = header->width,
        .height = header->height,
        .numMips = header->numMips,
        .format = header->format,
        .samplerFilter = imageParams->samplerFilter,
        .samplerWrap = imageParams->samplerWrap,
        .sampled = true,
        .size = contentBlob.Size(),
        .content = contentBlob.Data(),
        .mipOffsets = mipOffsets
    };

    data->AddGpuTextureObject(&header->handle, imageDesc);

    return true;
}

AssetHandleImage Asset::LoadImage(const char* path, const ImageLoadParams& params, const AssetGroup& group)
{
    AssetParams assetParams {
        .typeId = IMAGE_ASSET_TYPE,
        .path = path,
        .typeSpecificParams = const_cast<ImageLoadParams*>(&params)
    };

    return (AssetHandleImage)group.AddToLoadQueue(assetParams);
}

GfxImage* Asset::GetImage(AssetHandleImage handle)
{
    return (GfxImage*)Asset::GetObjData(handle);
}
