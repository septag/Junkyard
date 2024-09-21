#include "AssetManager.h"

#include "../Core/Log.h"
#include "../Core/Hash.h"
#include "../Core/System.h"
#include "../Core/StringUtil.h"
#include "../Core/JsonParser.h"
#include "../Core/Settings.h"
#include "../Core/Jobs.h"
#include "../Core/Atomic.h"

#include "../Common/VirtualFS.h"
#include "../Common/RemoteServices.h"
#include "../Common/JunkyardSettings.h"
#include "../Common/Profiler.h"

#if PLATFORM_ANDROID
#include "../Common/Application.h"
#endif

#include "Image.h"
#include "Model.h"
#include "Shader.h"

#include "../Engine.h"

//     ██████╗ ██╗      ██████╗ ██████╗  █████╗ ██╗     ███████╗
//    ██╔════╝ ██║     ██╔═══██╗██╔══██╗██╔══██╗██║     ██╔════╝
//    ██║  ███╗██║     ██║   ██║██████╔╝███████║██║     ███████╗
//    ██║   ██║██║     ██║   ██║██╔══██╗██╔══██║██║     ╚════██║
//    ╚██████╔╝███████╗╚██████╔╝██████╔╝██║  ██║███████╗███████║
//     ╚═════╝ ╚══════╝ ╚═════╝ ╚═════╝ ╚═╝  ╚═╝╚══════╝╚══════╝
namespace _limits
{
    static constexpr uint32 kAssetMaxTypes = 8;
    static constexpr uint32 kAssetMaxAssets = 1024;
    static constexpr uint32 kAssetMaxBarriers = 32;
    static constexpr uint32 kAssetMaxGarbage = 512;
    static constexpr size_t kAssetRuntimeSize = SIZE_MB;
}

static constexpr uint32 kAssetHashSeed = 0x4354a;
static constexpr uint32 kAssetCacheFileId = MakeFourCC('A', 'C', 'C', 'H');
static constexpr uint32 kAssetCacheVersion = 1;
static constexpr float kAssetCacheSaveDelay = 2.0;
static constexpr const char* kAssetCacheDatabasePath = "/cache/database.json5";

struct AssetTypeManager
{
    String32 name;
    uint32 fourcc;
    AssetCallbacks* callbacks;
    AssetTypeImplBase* impl;
    uint32 extraParamTypeSize;
    String32 extraParamTypeName;
    void* failedObj;
    void* asyncObj;
    bool unregistered;
};

struct AssetGarbage
{
    uint32 typeMgrIdx;
    void* obj;
    MemAllocator* alloc;
};

enum class AssetLoadMethod : uint32
{
    Local,
    Remote
};

struct AssetItem
{
    uint32 typeMgrIdx;          // index to gAssetMgr.typeManagers
    uint32 refCount;        
    uint32 hash;
    uint32 numMeta;
    uint32 numDepends;
    uint32 objBufferSize;
    AssetState state;
    void* obj;
    AssetLoadParams* params;    // might include extra param buffer at the end
    AssetMetaKeyValue* metaData;
    AssetDependency* depends;
};

struct AssetManager
{
    ReadWriteMutex assetsMtx;               // Mutex used for 'assets' HandlePool (see above)
    ReadWriteMutex hashLookupMtx;
    MemThreadSafeAllocator runtimeAlloc;
    MemTlsfAllocator tlsfAlloc;

    Array<AssetTypeManager> typeManagers;
    HandlePool<AssetHandle, AssetItem> assets;  // Holds all the active asset handles (can be 'failed', 'loading' or 'alive')
    HandlePool<AssetBarrier, Signal> barriers;
    HashTable<AssetHandle> assetLookup;     // key: hash of the asset (path+params)
                                            // This HashTable is used for looking up already loaded assets 
                                            // It doesn't need a mutex in this context. because it's fixed and we are not accessing the same slot from multiple threads
    
    Array<AssetGarbage> garbage;
    uint8 _padding[8];

    HashTable<uint32> hashLookup;           // Key: hash of the asset(path+params), value: cache hash

    size_t initHeapStart;
    size_t initHeapSize;

    float cacheSyncDelayTm;
    bool cacheSyncInvalidated;
    bool initialized;
};

static AssetManager gAssetMgr;

//----------------------------------------------------------------------------------------------------------------------
// @fwd
static void assetFileChanged(const char* filepath);

//     ██████╗ █████╗  ██████╗██╗  ██╗███████╗
//    ██╔════╝██╔══██╗██╔════╝██║  ██║██╔════╝
//    ██║     ███████║██║     ███████║█████╗  
//    ██║     ██╔══██║██║     ██╔══██║██╔══╝  
//    ╚██████╗██║  ██║╚██████╗██║  ██║███████╗
//     ╚═════╝╚═╝  ╚═╝ ╚═════╝╚═╝  ╚═╝╚══════╝
static AssetResult assetLoadFromCache(const AssetTypeManager& typeMgr, const AssetLoadParams& params, uint32 cacheHash, bool* outSuccess)
{
    Path strippedPath;
    Vfs::StripMountPath(strippedPath.Ptr(), sizeof(strippedPath), params.path);

    char hashStr[64];
    strPrintFmt(hashStr, sizeof(hashStr), "_%x", cacheHash);

    Path cachePath("/cache");
    cachePath.Append(strippedPath.GetDirectory())
             .Append("/")
             .Append(strippedPath.GetFileName())
             .Append(hashStr)
             .Append(".")
             .Append(typeMgr.name.CStr());    

    MemTempAllocator tempAlloc;
    Blob cache = Vfs::ReadFile(cachePath.CStr(), VfsFlags::None, &tempAlloc);

    AssetResult result {
        .cacheHash = cacheHash
    };
    *outSuccess = false;

    if (cache.IsValid()) {
        uint32 fileId = 0;
        uint32 cacheVersion = 0;
        cache.Read<uint32>(&fileId);
        if (fileId == kAssetCacheFileId) {
            cache.Read<uint32>(&cacheVersion);
            if (cacheVersion == 1) {
                cache.Read<uint32>(&result.numDepends);
                cache.Read<uint32>(&result.dependsBufferSize);
                cache.Read<uint32>(&result.objBufferSize);
    
                // Allocate and Copy dependencies from runtimeHeap. This is where internal asset manager expects them to be.
                if (result.dependsBufferSize) {
                    result.depends = (AssetDependency*)Mem::Alloc(result.dependsBufferSize, &gAssetMgr.runtimeAlloc);
                    cache.Read(result.depends, result.dependsBufferSize);
                }
    
                ASSERT(result.objBufferSize);
                result.obj = Mem::Alloc(result.objBufferSize, params.alloc);
                cache.Read(result.obj, result.objBufferSize);
                *outSuccess = true;
            }
        }
    }

    if (!*outSuccess)
        LOG_ERROR("Loading asset cache failed: %s (Source: %s)", cachePath.CStr(), params.path);

    return result;
}

static void assetSaveToCache(const AssetTypeManager& typeMgr, const AssetLoadParams& params, const AssetResult& result, uint32 assetHash)
{
    Path strippedPath;
    Vfs::StripMountPath(strippedPath.Ptr(), sizeof(strippedPath), params.path);

    char hashStr[64];
    strPrintFmt(hashStr, sizeof(hashStr), "_%x", result.cacheHash);

    Path cachePath("/cache");
    cachePath.Append(strippedPath.GetDirectory())
             .Append("/")
             .Append(strippedPath.GetFileName())
             .Append(hashStr)
             .Append(".")
             .Append(typeMgr.name.CStr());    

    MemTempAllocator tempAlloc;
    Blob cache(&tempAlloc);
    cache.SetGrowPolicy(Blob::GrowPolicy::Multiply);

    cache.Write<uint32>(kAssetCacheFileId);
    cache.Write<uint32>(kAssetCacheVersion);
    cache.Write<uint32>(result.numDepends);
    cache.Write<uint32>(result.dependsBufferSize);
    cache.Write<uint32>(result.objBufferSize);
    if (result.dependsBufferSize)
        cache.Write(result.depends, result.dependsBufferSize);
    ASSERT(result.objBufferSize);
    cache.Write(result.obj, result.objBufferSize);

    uint64 userData = (uint64(assetHash) << 32) | result.cacheHash;
    
    Vfs::WriteFileAsync(cachePath.CStr(), cache, VfsFlags::CreateDirs, 
                      [](const char* path, size_t, Blob&, void* user) 
    {
        LOG_VERBOSE("(save) AssetCache: %s", path);
        
        uint64 userData = PtrToInt<uint64>(user);
        uint32 hash = uint32((userData >> 32)&0xffffffff);
        uint32 cacheHash = uint32(userData&0xffffffff);

        ReadWriteMutexWriteScope mtx(gAssetMgr.hashLookupMtx);
        if (uint32 index = gAssetMgr.hashLookup.Find(hash); index != UINT32_MAX) 
            gAssetMgr.hashLookup.Set(index, cacheHash); // TODO: get delete the old file
        else
            gAssetMgr.hashLookup.Add(hash, cacheHash);
                         
    }, IntToPtr(userData));
}

static void assetLoadCacheHashDatabase()
{
    MemTempAllocator tempAlloc;

    Blob blob = Vfs::ReadFile(kAssetCacheDatabasePath, VfsFlags::TextFile, &tempAlloc);
    if (blob.IsValid()) {
        char* json;
        size_t jsonSize;

        blob.Detach((void**)&json, &jsonSize);
        JsonContext* jctx = Json::Parse(json, uint32(jsonSize), nullptr, &tempAlloc);
        if (jctx) {
            JsonNode jroot(jctx);

            ReadWriteMutexWriteScope mtx(gAssetMgr.hashLookupMtx);
            JsonNode jitem = jroot.GetArrayItem();
            while (jitem.IsValid()) {
                uint32 hash = jitem.GetChildValue<uint32>("hash", 0);
                uint32 cacheHash = jitem.GetChildValue<uint32>("cacheHash", 0);

                if (uint32 index = gAssetMgr.hashLookup.Find(hash); index != UINT32_MAX)
                    gAssetMgr.hashLookup.Set(index, cacheHash); // TODO: can delete the old file
                else
                    gAssetMgr.hashLookup.Add(hash, cacheHash);
                
                jitem = jroot.GetNextArrayItem(jitem);
            }
            Json::Destroy(jctx);

            LOG_INFO("Loaded cache database: %s", kAssetCacheDatabasePath);
        }
    } 
}

static void assetSaveCacheHashDatabase()
{
    MemTempAllocator tempAlloc;

    Blob blob(&tempAlloc);
    blob.SetGrowPolicy(Blob::GrowPolicy::Linear, 32*SIZE_KB);
    char line[1024];
    
    blob.Write("[\n", 2);

    {
        ReadWriteMutexReadScope mtx(gAssetMgr.hashLookupMtx);
        const uint32* keys = gAssetMgr.hashLookup.Keys();
        const uint32* values = gAssetMgr.hashLookup.Values();

        for (uint32 i = 0; i < gAssetMgr.hashLookup.Capacity(); i++) {
            if (keys[i]) {
                strPrintFmt(line, sizeof(line), 
                            "\t{\n"
                            "\t\thash: 0x%x,\n"
                            "\t\tcacheHash: 0x%x\n"
                            "\t},\n", 
                            keys[i], values[i]);
                blob.Write(line, strLen(line));
            }
        }
    }
    blob.Write("]\n", 2);
    
    Vfs::WriteFileAsync(kAssetCacheDatabasePath, blob, VfsFlags::TextFile, 
                      [](const char* path, size_t, Blob&, void*) { LOG_VERBOSE("Asset cache database saved to: %s", path); }, nullptr);

}

uint32 assetMakeCacheHash(const AssetCacheDesc& desc)
{
    HashMurmur32Incremental hasher(kAssetHashSeed);

    return hasher.Add<char>(desc.filepath, strLen(desc.filepath))
                 .AddAny(desc.loadParams, desc.loadParamsSize)
                 .AddAny(desc.metaData, sizeof(AssetMetaKeyValue)*desc.numMeta)
                 .Add<uint64>(desc.lastModified)
                 .Hash();
}

void _private::assetUpdateCache(float dt)
{
    if (gAssetMgr.cacheSyncInvalidated) {
        gAssetMgr.cacheSyncDelayTm += dt;
        if (gAssetMgr.cacheSyncDelayTm >= kAssetCacheSaveDelay)  {
            gAssetMgr.cacheSyncDelayTm = 0;
            gAssetMgr.cacheSyncInvalidated = false;
            Jobs::DispatchAndForget(JobsType::LongTask, [](uint32, void*) { assetSaveCacheHashDatabase(); });
        }            
    }
}

//    ██╗███╗   ██╗██╗████████╗ ██╗██████╗ ███████╗██╗███╗   ██╗██╗████████╗
//    ██║████╗  ██║██║╚══██╔══╝██╔╝██╔══██╗██╔════╝██║████╗  ██║██║╚══██╔══╝
//    ██║██╔██╗ ██║██║   ██║  ██╔╝ ██║  ██║█████╗  ██║██╔██╗ ██║██║   ██║   
//    ██║██║╚██╗██║██║   ██║ ██╔╝  ██║  ██║██╔══╝  ██║██║╚██╗██║██║   ██║   
//    ██║██║ ╚████║██║   ██║██╔╝   ██████╔╝███████╗██║██║ ╚████║██║   ██║   
//    ╚═╝╚═╝  ╚═══╝╚═╝   ╚═╝╚═╝    ╚═════╝ ╚══════╝╚═╝╚═╝  ╚═══╝╚═╝   ╚═╝   
bool _private::assetInitialize()
{
    gAssetMgr.initialized = true;
    gAssetMgr.assetsMtx.Initialize();
    gAssetMgr.hashLookupMtx.Initialize();

    MemBumpAllocatorBase* initHeap = Engine::GetInitHeap();
    gAssetMgr.initHeapStart = initHeap->GetOffset();

    {
        size_t arraySize = Array<AssetTypeManager>::GetMemoryRequirement(_limits::kAssetMaxTypes);
        gAssetMgr.typeManagers.Reserve(_limits::kAssetMaxTypes, Mem::Alloc(arraySize, initHeap), arraySize);
    }

    {
        size_t poolSize = HandlePool<AssetHandle, AssetItem>::GetMemoryRequirement(_limits::kAssetMaxAssets);
        gAssetMgr.assets.Reserve(_limits::kAssetMaxAssets, Mem::Alloc(poolSize, initHeap), poolSize);
    }

    {
        size_t poolSize = HandlePool<AssetBarrier, Signal>::GetMemoryRequirement(_limits::kAssetMaxBarriers);
        gAssetMgr.barriers.Reserve(_limits::kAssetMaxBarriers, Mem::Alloc(poolSize, initHeap), poolSize);
    }

    {
        size_t arraySize = Array<AssetGarbage>::GetMemoryRequirement(_limits::kAssetMaxGarbage);
        gAssetMgr.garbage.Reserve(_limits::kAssetMaxGarbage, Mem::Alloc(arraySize, initHeap), arraySize);
    }

    {
        size_t tableSize = HashTable<AssetHandle>::GetMemoryRequirement(_limits::kAssetMaxAssets);
        gAssetMgr.assetLookup.Reserve(_limits::kAssetMaxAssets, Mem::Alloc(tableSize, initHeap), tableSize);
    }

    {
        size_t tableSize = HashTable<uint32>::GetMemoryRequirement(_limits::kAssetMaxAssets);
        gAssetMgr.hashLookup.Reserve(_limits::kAssetMaxAssets, Mem::Alloc(tableSize, initHeap), tableSize);
    }

    {
        size_t bufferSize = MemTlsfAllocator::GetMemoryRequirement(_limits::kAssetRuntimeSize);
        gAssetMgr.tlsfAlloc.Initialize(_limits::kAssetRuntimeSize, initHeap->Malloc(bufferSize), bufferSize, SettingsJunkyard::Get().engine.debugAllocations);
        gAssetMgr.runtimeAlloc.SetAllocator(&gAssetMgr.tlsfAlloc);
    }

    gAssetMgr.initHeapSize = initHeap->GetOffset() - gAssetMgr.initHeapStart;

    Vfs::RegisterFileChangeCallback(assetFileChanged);

    // Create and mount cache directory
    #if PLATFORM_WINDOWS || PLATFORM_OSX || PLATFORM_LINUX
        if (!Path::IsDir_CStr(".cache"))
            Path::CreateDir_CStr(".cache");
        Vfs::MountLocal(".cache", "cache", false);
    #elif PLATFORM_ANDROID
        Vfs::MountLocal(OS::AndroidGetCacheDirectory(App::AndroidGetActivity()).CStr(), "cache", false);
    #endif

    assetLoadCacheHashDatabase();

    // Initialize asset managers here
    if (!assetInitializeImageManager()) {
        LOG_ERROR("Failed to initialize ImageManager");
        return false;
    }

    if (!assetInitializeModelManager()) {
        LOG_ERROR("Failed to initialize ModelManager");
        return false;
    }

    if (!assetInitializeShaderManager()) {
        LOG_ERROR("Failed to initialize ShaderManager");
        return false;
    }

    return true;
}

void _private::assetRelease()
{
    if (gAssetMgr.initialized) {
        assetCollectGarbage();

        // Detect asset leaks and release them
        for (AssetItem& a : gAssetMgr.assets) {
            if (a.state == AssetState::Loaded) {
                LOG_WARNING("Asset '%s' (RefCount=%u) is not unloaded", a.params->path, a.refCount);
                if (a.obj) {
                    AssetTypeManager* typeMgr = &gAssetMgr.typeManagers[a.typeMgrIdx];
                    if (!typeMgr->unregistered) {
                        typeMgr->callbacks->Release(a.obj, a.params->alloc);
                    }
                }
            }

            MemSingleShotMalloc<AssetLoadParams> mallocator;
            mallocator.Free(a.params, &gAssetMgr.runtimeAlloc);

            Mem::Free(a.depends, &gAssetMgr.runtimeAlloc);
            Mem::Free(a.metaData, &gAssetMgr.runtimeAlloc);
        }

        // Release asset managers here
        assetReleaseModelManager();
        assetReleaseImageManager();
        assetReleaseShaderManager();

        gAssetMgr.hashLookupMtx.Release();
        gAssetMgr.assetsMtx.Release();
        gAssetMgr.tlsfAlloc.Release();
        gAssetMgr.runtimeAlloc.SetAllocator(nullptr);

        gAssetMgr.initialized = false;
    }
}

INLINE AssetPlatform::Enum assetGetCurrentPlatform()
{
    if constexpr (PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_OSX)
           return AssetPlatform::PC;
    else if constexpr (PLATFORM_ANDROID || PLATFORM_IOS)  
           return AssetPlatform::Mobile;
    else {
        ASSERT(0);
        return AssetPlatform::Auto;
    }
}


//    ██╗      ██████╗  █████╗ ██████╗ 
//    ██║     ██╔═══██╗██╔══██╗██╔══██╗
//    ██║     ██║   ██║███████║██║  ██║
//    ██║     ██║   ██║██╔══██║██║  ██║
//    ███████╗╚██████╔╝██║  ██║██████╔╝
//    ╚══════╝ ╚═════╝ ╚═╝  ╚═╝╚═════╝ 
static AssetHandle assetCreateNew(uint32 typeMgrIdx, uint32 assetHash, const AssetLoadParams& params, const void* extraParams)
{
    const AssetTypeManager& typeMgr = gAssetMgr.typeManagers[typeMgrIdx];
    
    uint8* nextParams;
    MemSingleShotMalloc<AssetLoadParams> mallocator;
    mallocator.AddMemberArray<char>(offsetof(AssetLoadParams, path), PATH_CHARS_MAX)
              .AddExternalPointerField<uint8>(&nextParams, typeMgr.extraParamTypeSize);
    AssetLoadParams* newParams = mallocator.Calloc(&gAssetMgr.runtimeAlloc);
    
    strCopy(const_cast<char*>(newParams->path), PATH_CHARS_MAX, params.path);
    newParams->alloc = params.alloc;
    newParams->typeId = params.typeId;
    newParams->tags = params.tags;
    newParams->barrier = params.barrier;
    newParams->next = nextParams;
    if (typeMgr.extraParamTypeSize && extraParams)
        memcpy(newParams->next.Get(), extraParams, typeMgr.extraParamTypeSize);

    // Set the asset platform to the platform that we are running on in Auto mode
    if (params.platform == AssetPlatform::Auto)
        newParams->platform = assetGetCurrentPlatform();
    
    AssetItem asset {
        .typeMgrIdx = typeMgrIdx,
        .refCount = 1,
        .hash = assetHash,
        .state = AssetState::Zombie,
        .params = newParams
    };

    AssetHandle handle;
    AssetItem prevAsset;
    {
        ReadWriteMutexWriteScope mtx(gAssetMgr.assetsMtx);
        handle = gAssetMgr.assets.Add(asset, &prevAsset);
        ASSERT(!prevAsset.params);
        ASSERT(!prevAsset.metaData);
    }

    gAssetMgr.assetLookup.Add(assetHash, handle);
    return handle;
}

static AssetResult assetLoadObjLocal(AssetHandle handle, const AssetTypeManager& typeMgr, const AssetLoadParams& loadParams, uint32 hash,
                                     bool* outLoadedFromCache)
{
    uint32 cacheHash;
    {
        ReadWriteMutexReadScope mtx(gAssetMgr.hashLookupMtx);
        cacheHash = gAssetMgr.hashLookup.FindAndFetch(hash, 0);
    }

    bool cacheOnly = SettingsJunkyard::Get().engine.useCacheOnly;
    AssetResult result {};
    if (!cacheOnly)
        result = typeMgr.callbacks->Load(handle, loadParams, cacheHash, &gAssetMgr.runtimeAlloc);
    bool loadFromCache = (cacheOnly && cacheHash != 0) || (result.cacheHash == cacheHash);
    if (loadFromCache) {
        ASSERT(result.obj == nullptr);
        result = assetLoadFromCache(typeMgr, loadParams, cacheHash, outLoadedFromCache);

        if (!*outLoadedFromCache) {
            ReadWriteMutexWriteScope mtx(gAssetMgr.hashLookupMtx);
            gAssetMgr.hashLookup.FindAndRemove(hash);

            gAssetMgr.cacheSyncInvalidated = true;
            gAssetMgr.cacheSyncDelayTm = 0;
        }
    }
    return result;
}

static AssetResult assetLoadObjRemote(AssetHandle handle, const AssetTypeManager& typeMgr, const AssetLoadParams& loadParams, uint32 hash,
                                      bool* outLoadedFromCache)
{
    JobsSignal waitSignal;      // Used to serialize the async code

    uint32 cacheHash;
    {
        ReadWriteMutexReadScope mtx(gAssetMgr.hashLookupMtx);
        cacheHash = gAssetMgr.hashLookup.FindAndFetch(hash, 0);
    }
    
    struct AsyncLoadData
    {
        AssetResult result;
        JobsSignal* signal;
        const AssetTypeManager* typeMgr;
        const AssetLoadParams* loadParams;
        uint32 hash;
        uint32 cacheHash;
        bool* loadedFromCache;
    };

    AsyncLoadData asyncLoadData {
        .signal = &waitSignal,
        .typeMgr = &typeMgr,
        .loadParams = &loadParams,
        .hash = hash,
        .cacheHash = cacheHash,
        .loadedFromCache = outLoadedFromCache
    };

    typeMgr.callbacks->LoadRemote(handle, loadParams, cacheHash, &asyncLoadData, 
                                  [](AssetHandle, const AssetResult& result, void* userData) 
    {
        AsyncLoadData* params = reinterpret_cast<AsyncLoadData*>(userData);
        
        if (result.cacheHash == params->cacheHash) {
            ASSERT(result.obj == nullptr);
            params->result = assetLoadFromCache(*params->typeMgr, *params->loadParams, result.cacheHash, params->loadedFromCache);

            if (!*params->loadedFromCache) {
                ReadWriteMutexWriteScope mtx(gAssetMgr.hashLookupMtx);
                gAssetMgr.hashLookup.FindAndRemove(params->hash);

                gAssetMgr.cacheSyncInvalidated = true;
                gAssetMgr.cacheSyncDelayTm = 0;
            }
        }
        else {
            ASSERT(result.obj);
            params->result = result;
        }

        // We need to copy the dependencies over again in order to bring them over to persistent memory (runtimeHeap)
        if (result.numDepends) {
            ASSERT(result.depends);
            ASSERT(result.dependsBufferSize);   // Only remote loads should implement this
            params->result.depends = (AssetDependency*)Mem::Alloc(result.dependsBufferSize, &gAssetMgr.runtimeAlloc);
            memcpy(params->result.depends, result.depends, result.dependsBufferSize);
            params->result.numDepends = result.numDepends;
        }

        params->signal->Set();
        params->signal->Raise();
    });
    waitSignal.Wait();

    return asyncLoadData.result;
}

// Runs from worker thread
static void assetLoadTask(uint32 groupIndex, void* userData)
{
    UNUSED(groupIndex);
    void* prevObj = nullptr;
    uint64 userValue = PtrToInt<uint64>(userData);
    AssetLoadMethod method = AssetLoadMethod(userValue & 0xffffffff);
    AssetHandle handle { uint32(userValue >> 32) };
    TimerStopWatch timer;

    gAssetMgr.assetsMtx.EnterRead();
    AssetItem& asset = gAssetMgr.assets.Data(handle);
    Path filepath = asset.params->path;
    const AssetTypeManager& typeMgr = gAssetMgr.typeManagers[asset.typeMgrIdx];
    const AssetLoadParams& loadParams = *asset.params;
    uint32 hash = asset.hash;
    gAssetMgr.assetsMtx.ExitRead();

    AssetResult result;
    bool loadedFromCache = false;

    if (method == AssetLoadMethod::Local)
        result = assetLoadObjLocal(handle, typeMgr, loadParams, hash, &loadedFromCache);
    else if (method == AssetLoadMethod::Remote)
        result = assetLoadObjRemote(handle, typeMgr, loadParams, hash, &loadedFromCache);
    else {
        ASSERT(0);
        result = AssetResult {};
    }

    ReadWriteMutexReadScope mtx(gAssetMgr.assetsMtx);
    asset = gAssetMgr.assets.Data(handle);
    if (asset.obj != typeMgr.asyncObj)
        prevObj = asset.obj;

    if (result.obj) {
        asset.state = AssetState::Loaded;
        asset.obj = result.obj;
        asset.objBufferSize = result.objBufferSize;

        if (!loadedFromCache) {
            gAssetMgr.cacheSyncInvalidated = true;
            gAssetMgr.cacheSyncDelayTm = 0;
            assetSaveToCache(typeMgr, loadParams, result, asset.hash);
        }

        // Load external asset resources right after we saved the blob to cache.
        // LoadResources can cause the original data to be changed or set some external handles/pointers. 
        if (!loadParams.dontCreateResources) {
            // TODO: This is fucked! I should probably change the design here
            //       Because this is reentrant (assets can load other assets) so we have to release the lock 
            gAssetMgr.assetsMtx.ExitRead();
            if (!typeMgr.callbacks->InitializeSystemResources(result.obj, loadParams)) {
                LOG_ERROR("Failed creating resources for %s: %s", typeMgr.name.CStr(), filepath.CStr());
                typeMgr.callbacks->Release(result.obj, loadParams.alloc);
                result.obj = nullptr;
            }
            gAssetMgr.assetsMtx.EnterRead();
            asset = gAssetMgr.assets.Data(handle);
        }
    }

    if (!result.obj) {
        asset.state = AssetState::LoadFailed;
        asset.obj = typeMgr.failedObj;
    }
    else {
        LOG_VERBOSE("(load) %s: %s (%.1f ms)%s", typeMgr.name.CStr(), filepath.CStr(), timer.ElapsedMS(), loadedFromCache ? " [cached]" : "");
    }

    asset.depends = result.depends;
    asset.numDepends = result.numDepends;
    for (uint32 i = 0; i < asset.numDepends; i++)
        asset.depends[i].params.path = asset.depends[i].path.CStr();

    // Handle reloading of the object
    if (prevObj) {
        AssetGarbage garbage {
            .typeMgrIdx = asset.typeMgrIdx,
            .alloc = asset.params->alloc
        };

        // try to reload the object, we also provide the previous handle for book keeping
        if (!typeMgr.callbacks->ReloadSync(handle, prevObj)) {
            LOG_WARNING("Asset '%s' cannot get reloaded", filepath.CStr());
            asset.obj = prevObj;
            garbage.obj = result.obj;     
        }
        else {
            garbage.obj = prevObj;
        }

        gAssetMgr.garbage.Push(garbage);
    }

    // Decrement any barrier, so we can unblock the thread waiting on load
    if (asset.params->barrier.IsValid()) {
        Signal& sig = gAssetMgr.barriers.Data(asset.params->barrier);
        sig.Decrement();
        sig.Raise();
        asset.params->barrier = AssetBarrier();
    }
}

AssetHandle assetLoad(const AssetLoadParams& params, const void* extraParams)
{
    ASSERT(gAssetMgr.initialized);

    if (params.path[0] == '\0')
        return AssetHandle();

    uint32 typeMgrIdx = gAssetMgr.typeManagers.FindIf(
        [params](const AssetTypeManager& typeMgr) { return typeMgr.fourcc == params.typeId; });
    ASSERT_MSG(typeMgrIdx != UINT32_MAX, "AssetType with FourCC %x is not registered", params.typeId);
    AssetTypeManager& typeMgr = gAssetMgr.typeManagers[typeMgrIdx];

    if (typeMgr.extraParamTypeSize && !extraParams) {
        LOG_WARNING("Extra parameters not provided for asset type '%s'. Set extra parameters in 'next' field with the type of '%s'",
            typeMgr.name.CStr(), typeMgr.extraParamTypeName.CStr());
        ASSERT_MSG(false, "AssetLoadParams.next must not be nullptr for this type of asset (%s)", typeMgr.name.CStr());
        return AssetHandle();
    }

    // check if asset is already loaded
    // Calculate asset hash. This is different from "Cache" hash. Asset hash should always retain it's value for each unique asset
    // Unique assets are also defined by their extra custom init params 
    HashMurmur32Incremental hasher(kAssetHashSeed);
    uint32 assetHash = hasher.Add<char>(params.path, strLen(params.path))
                             .Add<uint32>(params.tags)
                             .AddAny(extraParams, typeMgr.extraParamTypeSize)
                             .Hash();

    AssetHandle handle = gAssetMgr.assetLookup.FindAndFetch(assetHash, AssetHandle());
    if (handle.IsValid()) {
        ReadWriteMutexReadScope mtx(gAssetMgr.assetsMtx);
        AssetItem& asset = gAssetMgr.assets.Data(handle);
        ++asset.refCount;
    }
    else {
        handle = assetCreateNew(typeMgrIdx, assetHash, params, extraParams);
        
        ReadWriteMutexReadScope mtx(gAssetMgr.assetsMtx);
        AssetItem& asset = gAssetMgr.assets.Data(handle);
        asset.state = AssetState::Loading;
        asset.obj = typeMgr.asyncObj;

        if (asset.params->barrier.IsValid()) {
            Signal& sig = gAssetMgr.barriers.Data(asset.params->barrier);
            sig.Increment();
        }

        static_assert(sizeof(void*) == sizeof(uint64), "No support for 32bits in this part");
        uint64 userValue = (static_cast<uint64>(uint32(handle))<<32) |
                           ((Remote::IsConnected() ? (uint64)AssetLoadMethod::Remote : (uint64)AssetLoadMethod::Local) & 0xffffffff);
        Jobs::DispatchAndForget(JobsType::LongTask, assetLoadTask, IntToPtr<uint64>(userValue));
    }

    return handle;
}

// TODO: consider the case where assetLoad is called but then immediately user calls unload
//       In this case, unload should check for Loading state and postpone the operation
//       For now, we have an assert check to enforce that
void assetUnload(AssetHandle handle)
{
    if (handle.IsValid()) {
        ASSERT(gAssetMgr.initialized);
        gAssetMgr.assetsMtx.EnterRead();
        AssetItem& asset = gAssetMgr.assets.Data(handle);

        if (asset.state != AssetState::Loaded) {
            gAssetMgr.assetsMtx.ExitRead();
            LOG_WARNING("Asset is either failed or already released: %s", asset.params->path);
            return;
        }
    
        if (--asset.refCount == 0) {
            AssetCallbacks* callbacks = gAssetMgr.typeManagers[asset.typeMgrIdx].callbacks;
            gAssetMgr.assetsMtx.ExitRead();

            if (callbacks)
                callbacks->Release(asset.obj, asset.params->alloc);

            uint32 assetHash;
            {
                ReadWriteMutexWriteScope mtx(gAssetMgr.assetsMtx);
                asset = gAssetMgr.assets.Data(handle);
                Mem::Free(asset.params, &gAssetMgr.runtimeAlloc);
                Mem::Free(asset.depends, &gAssetMgr.runtimeAlloc);
                Mem::Free(asset.metaData, &gAssetMgr.runtimeAlloc);
                asset.params = nullptr;
                asset.metaData = nullptr;
                asset.depends = nullptr;
                gAssetMgr.assets.Remove(handle);
                assetHash = asset.hash;
            }
    
            gAssetMgr.assetLookup.FindAndRemove(assetHash);
        }
    }
}

//    ███╗   ███╗███████╗████████╗ █████╗ ██████╗  █████╗ ████████╗ █████╗ 
//    ████╗ ████║██╔════╝╚══██╔══╝██╔══██╗██╔══██╗██╔══██╗╚══██╔══╝██╔══██╗
//    ██╔████╔██║█████╗     ██║   ███████║██║  ██║███████║   ██║   ███████║
//    ██║╚██╔╝██║██╔══╝     ██║   ██╔══██║██║  ██║██╔══██║   ██║   ██╔══██║
//    ██║ ╚═╝ ██║███████╗   ██║   ██║  ██║██████╔╝██║  ██║   ██║   ██║  ██║
//    ╚═╝     ╚═╝╚══════╝   ╚═╝   ╚═╝  ╚═╝╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝
bool assetLoadMetaData(const char* filepath, AssetPlatform::Enum platform, MemAllocator* alloc, AssetMetaKeyValue** outData, uint32* outKeyCount)
{
    ASSERT(outData);
    ASSERT(outKeyCount);

    auto collectKeyValues = [](JsonNode jroot, StaticArray<AssetMetaKeyValue, 64>* keys) {
        char key[32];
        char value[32];

        if (jroot.GetChildCount()) {
            JsonNode jitem = jroot.GetChildItem();
            while (jitem.IsValid()) {
                if (!jitem.IsArray() && !jitem.IsObject()) {
                    jitem.GetKey(key, sizeof(key));
                    jitem.GetValue(value, sizeof(value));

                    AssetMetaKeyValue item;
                    memset(&item, 0x0, sizeof(item));
                    item.key = key;
                    item.value = value;
                    keys->Add(item);
                }

                jitem = jroot.GetNextChildItem(jitem);
            }
        }
    };

    Path path(filepath);
    Path assetMetaPath = Path::JoinUnix(path.GetDirectory(), path.GetFileName());
    assetMetaPath.Append(".asset");
    
    uint32 tempId = MemTempAllocator::PushId();
    MemTempAllocator tmpAlloc(tempId);

    Blob blob = Vfs::ReadFile(assetMetaPath.CStr(), VfsFlags::TextFile, &tmpAlloc);
    if (blob.IsValid()) {
        JsonErrorLocation loc;
        JsonContext* jctx = Json::Parse((const char*)blob.Data(), uint32(blob.Size()), &loc, &tmpAlloc);
        if (jctx) {
            JsonNode jroot(jctx);
            StaticArray<AssetMetaKeyValue, 64> keys;

            collectKeyValues(jroot, &keys);

            // Collect platform-specific keys
            JsonNode jplatform;
            switch (platform) {
            case AssetPlatform::PC:         jplatform = jroot.GetChild("pc");       break;
            case AssetPlatform::Mobile:     jplatform = jroot.GetChild("android");  break;
            default:                        break;
            }
            if (jplatform.IsValid())
                collectKeyValues(jplatform, &keys);

            blob.Free();
            Json::Destroy(jctx);
            MemTempAllocator::PopId(tempId);
            
            // At this point we have popped the current temp allocator and can safely allocate from whatever allocator is coming in
            *outData = Mem::AllocCopy<AssetMetaKeyValue>(keys.Ptr(), keys.Count(), alloc);
            *outKeyCount = keys.Count();

            return true;
        }
        else {
            *outData = nullptr;
            *outKeyCount = 0;
        
            blob.Free();
            LOG_WARNING("Invalid asset meta data: %s (Json syntax error at %u:%u)", assetMetaPath.CStr(), loc.line, loc.col);
            MemTempAllocator::PopId(tempId);
            return false;
        }
    }
    else {
        *outData = nullptr;
        *outKeyCount = 0;

        MemTempAllocator::PopId(tempId);
        return false;
    }
}

// Note: This version of LoadMetaData (provide local asset handle instead of filepath), Allocates asset's meta-data from runtimeHeap
bool assetLoadMetaData(AssetHandle handle, MemAllocator* alloc, AssetMetaKeyValue** outData, uint32* outKeyCount)
{
    ASSERT(handle.IsValid());

    ReadWriteMutexReadScope mtx(gAssetMgr.assetsMtx);
    AssetItem& asset = gAssetMgr.assets.Data(handle);
    if (asset.numMeta && asset.metaData) {
        *outData = Mem::AllocCopy<AssetMetaKeyValue>(asset.metaData, asset.numMeta, alloc);
        *outKeyCount = asset.numMeta;
        return true;
    }
    else {
        if (assetLoadMetaData(asset.params->path, assetGetCurrentPlatform(), &gAssetMgr.runtimeAlloc, &asset.metaData, &asset.numMeta)) {
            *outData = Mem::AllocCopy<AssetMetaKeyValue>(asset.metaData, asset.numMeta, alloc);
            *outKeyCount = asset.numMeta;
            return true;
        }
        else {
            *outData = nullptr;
            *outKeyCount = 0;
            return false;
        }
    }
}

const char* assetGetMetaValue(const AssetMetaKeyValue* data, uint32 count, const char* key)
{
    for (uint32 i = 0; i < count; i++) {
        if (data[i].key.IsEqual(key))
            return data[i].value.CStr();
    }
    return nullptr;
}


//    ██████╗ ███████╗ ██████╗ ██╗███████╗████████╗███████╗██████╗ 
//    ██╔══██╗██╔════╝██╔════╝ ██║██╔════╝╚══██╔══╝██╔════╝██╔══██╗
//    ██████╔╝█████╗  ██║  ███╗██║███████╗   ██║   █████╗  ██████╔╝
//    ██╔══██╗██╔══╝  ██║   ██║██║╚════██║   ██║   ██╔══╝  ██╔══██╗
//    ██║  ██║███████╗╚██████╔╝██║███████║   ██║   ███████╗██║  ██║
//    ╚═╝  ╚═╝╚══════╝ ╚═════╝ ╚═╝╚══════╝   ╚═╝   ╚══════╝╚═╝  ╚═╝
void assetRegisterType(const AssetTypeDesc& desc)
{
    ASSERT(gAssetMgr.initialized);

    if (uint32 index = gAssetMgr.typeManagers.FindIf([desc](const AssetTypeManager& typeMgr) { 
            return typeMgr.fourcc == desc.fourcc || typeMgr.name.IsEqual(desc.name); });
        index != UINT32_MAX)
    {
        ASSERT_MSG(0, "AssetType '%s' is already registered", desc.name);
        return;
    }

    gAssetMgr.typeManagers.Push(AssetTypeManager {
        .name = desc.name,
        .fourcc = desc.fourcc,
        .callbacks = desc.callbacks,
        .impl = desc.impl,
        .extraParamTypeSize = desc.extraParamTypeSize,
        .extraParamTypeName = desc.extraParamTypeName,
        .failedObj = desc.failedObj,
        .asyncObj = desc.asyncObj,
    });
}

void assetUnregisterType(uint32 fourcc)
{
    if (!gAssetMgr.initialized)
        return;

    if (uint32 index = gAssetMgr.typeManagers.FindIf([fourcc](const AssetTypeManager& typeMgr) {
            return typeMgr.fourcc == fourcc; });
        index != UINT32_MAX)
    {
        AssetTypeManager* typeMgr = &gAssetMgr.typeManagers[index];
        ASSERT_MSG(!typeMgr->unregistered, "AssetTypeManager '%s' is already unregistered", typeMgr->name.CStr());
        typeMgr->unregistered = true;
    }
}


//    ███╗   ███╗██╗███████╗ ██████╗
//    ████╗ ████║██║██╔════╝██╔════╝
//    ██╔████╔██║██║███████╗██║     
//    ██║╚██╔╝██║██║╚════██║██║     
//    ██║ ╚═╝ ██║██║███████║╚██████╗
//    ╚═╝     ╚═╝╚═╝╚══════╝ ╚═════╝
void* _private::assetGetData(AssetHandle handle)
{
    ASSERT(gAssetMgr.initialized);
    
    ReadWriteMutexReadScope mtx(gAssetMgr.assetsMtx);
    AssetItem& asset = gAssetMgr.assets.Data(handle);
    return asset.obj;
}

AssetInfo assetGetInfo(AssetHandle handle)
{
    ASSERT(gAssetMgr.initialized);
    ASSERT(handle.IsValid());

    ReadWriteMutexReadScope mtx(gAssetMgr.assetsMtx);
    AssetItem& asset = gAssetMgr.assets.Data(handle);

    return AssetInfo {
        .typeId = gAssetMgr.typeManagers[asset.typeMgrIdx].fourcc,
        .state = asset.state,
        .tags = 0, // TODO
        .refCount = asset.refCount,
        .path = asset.params->path,
        .depends = asset.depends,
        .numDepends = asset.numDepends
    };
}

bool assetIsAlive(AssetHandle handle)
{
    ASSERT(gAssetMgr.initialized);
    ASSERT(handle.IsValid());

    ReadWriteMutexReadScope mtx(gAssetMgr.assetsMtx);
    AssetItem& asset = gAssetMgr.assets.Data(handle);
    return asset.state == AssetState::Loaded;
}

AssetHandle assetAddRef(AssetHandle handle)
{
    ASSERT(gAssetMgr.initialized);
    ASSERT(handle.IsValid());

    ReadWriteMutexReadScope mtx(gAssetMgr.assetsMtx);
    AssetItem& asset = gAssetMgr.assets.Data(handle);
    ++asset.refCount;
    return handle;
}

AssetBarrier assetCreateBarrier()
{
    ASSERT(gAssetMgr.initialized);

    Signal sig;
    sig.Initialize();
    return gAssetMgr.barriers.Add(sig);
}

void assetDestroyBarrier(AssetBarrier barrier)
{
    assetWait(barrier);

    Signal& sig = gAssetMgr.barriers.Data(barrier);
    sig.Release();
    gAssetMgr.barriers.Remove(barrier);
}

bool assetWait(AssetBarrier barrier, uint32 msecs)
{
    ASSERT(gAssetMgr.initialized);
    ASSERT(barrier.IsValid());

    // Wait until value is non-zero, basically means that if there are assets being loading by this barrier
    // If there is no work (signal->value == 0) then this function would return immediately
    Signal& sig = gAssetMgr.barriers.Data(barrier);
    return sig.WaitOnCondition([](int value, int reference)->bool {return value > reference;}, 0, msecs);
}

void _private::assetCollectGarbage()
{
    ReadWriteMutexReadScope mtx(gAssetMgr.assetsMtx);
    for (AssetGarbage& garbage : gAssetMgr.garbage) {
        AssetTypeManager* typeMgr = &gAssetMgr.typeManagers[garbage.typeMgrIdx];
        if (!typeMgr->unregistered) {
            typeMgr->callbacks->Release(garbage.obj, garbage.alloc);
        } 
    }
    gAssetMgr.garbage.Clear();
}

void assetGetBudgetStats(AssetBudgetStats* stats)
{
    stats->numAssets = gAssetMgr.assets.Count();
    stats->maxAssets = _limits::kAssetMaxAssets;

    stats->numTypes = gAssetMgr.typeManagers.Count();
    stats->maxTypes = _limits::kAssetMaxTypes;

    stats->numGarbage = gAssetMgr.garbage.Count();
    stats->maxGarbage = _limits::kAssetMaxAssets;

    stats->numBarriers = gAssetMgr.barriers.Count();
    stats->maxBarriers = _limits::kAssetMaxBarriers;

    stats->initHeapStart = gAssetMgr.initHeapStart;
    stats->initHeapSize = gAssetMgr.initHeapSize;

    stats->runtimeHeapSize = gAssetMgr.tlsfAlloc.GetAllocatedSize();
    stats->runtimeHeapMax = _limits::kAssetRuntimeSize;

    stats->runtimeHeap = &gAssetMgr.tlsfAlloc;
}

static void assetFileChanged(const char* filepath)
{
    // TOOD: this implmentation can potentially get very slow when there are too many assets
    ReadWriteMutexReadScope mtx(gAssetMgr.assetsMtx);
    for (uint32 i = 0; i < gAssetMgr.assets.Count(); i++) {
        AssetHandle handle = gAssetMgr.assets.HandleAt(i);
        AssetItem& asset = gAssetMgr.assets.Data(handle);

        const char* assetPath = asset.params->path;
        if (assetPath[0] == '/')
            ++assetPath;
        if (strIsEqualNoCase(filepath, assetPath)) {
            uint64 userValue = (static_cast<uint64>(uint32(handle))<<32) |
                ((Remote::IsConnected() ? (uint64)AssetLoadMethod::Remote : (uint64)AssetLoadMethod::Local) & 0xffffffff);
            Jobs::DispatchAndForget(JobsType::LongTask, assetLoadTask, IntToPtr<uint64>(userValue));
        }
    }
}


//    ███╗   ██╗███████╗██╗    ██╗    ███████╗████████╗██╗   ██╗███████╗███████╗
//    ████╗  ██║██╔════╝██║    ██║    ██╔════╝╚══██╔══╝██║   ██║██╔════╝██╔════╝
//    ██╔██╗ ██║█████╗  ██║ █╗ ██║    ███████╗   ██║   ██║   ██║█████╗  █████╗  
//    ██║╚██╗██║██╔══╝  ██║███╗██║    ╚════██║   ██║   ██║   ██║██╔══╝  ██╔══╝  
//    ██║ ╚████║███████╗╚███╔███╔╝    ███████║   ██║   ╚██████╔╝██║     ██║     
//    ╚═╝  ╚═══╝╚══════╝ ╚══╝╚══╝     ╚══════╝   ╚═╝    ╚═════╝ ╚═╝     ╚═╝     

namespace limits
{
    static inline constexpr size_t ASSET_MAX_SCRATCH_SIZE_PER_THREAD = SIZE_GB;
    static inline constexpr size_t ASSET_HEADER_BUFFER_POOL_SIZE = SIZE_MB;
    static inline constexpr size_t ASSET_DATA_BUFFER_POOL_SIZE = SIZE_MB*128;
    static inline constexpr uint32 ASSET_SERVER_MAX_IN_FLIGHT = 128;
}

static inline constexpr uint32 ASSET_LOAD_ASSET_REMOTE_CMD = MakeFourCC('L', 'D', 'A', 'S');
static inline constexpr float ASSET_SAVE_CACHE_LOOKUP_INTERVAL = 1.0f;

// Note: serializable. All data/array should use RelativePtr
struct AssetDataInternal
{
    struct Dependency
    {
        Path path;
        uint32 typeId;
        RelativePtr<AssetHandle> bindToHandle;
        RelativePtr<uint8> typeSpecificParams;
        RelativePtr<Dependency> next;
    };

    enum class GpuObjectType : uint32
    {
        Buffer = 0,
        Texture
    };

    struct GpuBufferDesc
    {
        RelativePtr<GfxBufferHandle> bindToBuffer;
        uint32 size;
        GfxBufferType type;
        GfxBufferUsage usage;
        RelativePtr<uint8> content;
    };

    struct GpuTextureDesc
    {
        RelativePtr<GfxImageHandle> bindToImage;
        uint32 width;
        uint32 height;
        uint32 numMips;
        GfxFormat format;
        GfxBufferUsage usage;
        float anisotropy;
        GfxSamplerFilterMode samplerFilter;
        GfxSamplerWrapMode samplerWrap;
        GfxSamplerBorderColor borderColor;
        size_t size;
        RelativePtr<uint8> content;
        RelativePtr<uint32> mipOffsets;
    };

    struct GpuObject
    {
        GpuObjectType type;
        union {
            GpuBufferDesc bufferDesc;
            GpuTextureDesc textureDesc;
        };
        RelativePtr<GpuObject> next;
    };

    uint32 objDataSize;
    uint32 numMetaData;
    uint32 numDependencies;
    uint32 numGpuObjects;

    RelativePtr<AssetMetaKeyValue> metaData;
    RelativePtr<Dependency> deps;
    RelativePtr<uint8> objData;
    RelativePtr<GpuObject> gpuObjects;
};

struct AssetDataHeader
{
    AssetState state;
    uint32 paramsHash;
    uint32 refCount;
    uint32 dataSize;
    uint32 typeId;
    uint32 typeSpecificParamsSize;
    const char* typeName;
    AssetParams* params;
    AssetDataInternal* data;
};

struct AssetScratchMemArena
{
    SpinLockMutex threadToAllocatorTableMtx;
    HashTableUint threadToAllocatorTable;
    MemBumpAllocatorVM* allocators;
    uint32 maxAllocators;
    uint32 numAllocators;
};

enum AssetLoadTaskInputType
{
    Source = 0,
    Baked
};

struct AssetLoadTaskInputs
{
    JobsSignal fileReadSignal;
    const void* fileData;
    uint32 fileSize;
    AssetLoadTaskInputType type;
    AssetGroupHandle groupHandle;
    uint32 assetHash;
    AssetDataHeader* header;
    Path bakedFilepath;
    String<256> remoteLoadErrorStr;
    uint64 clientPayload;
    bool isRemoteLoad;
};

struct AssetLoadTaskOutputs
{
    String<256> errorDesc;
    uint32 dataSize;
    AssetDataInternal* data; 
};

struct AssetLoadTaskData
{
    AssetLoadTaskInputs inputs;
    AssetLoadTaskOutputs outputs;
};

struct AssetDataCopyTaskData
{
    AssetDataHeader* header;
    AssetDataInternal* destData;
    const AssetDataInternal* sourceData;
    uint32 sourceDataSize;
};

struct AssetHandleResult
{
    AssetHandle handle;
    uint32 paramsHash;
    AssetDataHeader* header;
    bool newlyCreated;
};

struct AssetGroupInternal
{
    Array<AssetDataHeader*> loadList;  
    Array<AssetHandle> handles;
    AtomicUint32 state; // AssetGroupState
};

struct AssetQueuedItem
{
    uint32 indexInLoadList;
    uint32 dataSize;
    uint32 paramsHash;
    uint32 assetHash;
    AssetDataInternal* data;
    Path bakedFilepath;
    bool saveBaked;
};


struct AssetServer
{
    SpinLockMutex pendingTasksMutex;
    Array<AssetLoadTaskData*> pendingTasks;
    AssetLoadTaskData* loadTaskDatas[limits::ASSET_SERVER_MAX_IN_FLIGHT];
    uint32 numLoadTasks;
};

// From highest priority to lowest
enum class AssetJobType : int
{
    Server = 0,
    Load,
    Unload
};

struct AssetJobItem 
{
    AssetJobType type;
    AssetGroupHandle groupHandle;
};

struct AssetMan
{
    ReadWriteMutex assetMutex;
    ReadWriteMutex groupsMutex;
    ReadWriteMutex hashLookupMutex;
    Mutex pendingJobsMutex;

    AssetScratchMemArena memArena;
    MemBumpAllocatorVM tempAlloc;   // Used in LoadAssetGroup, because we cannot use regular temp allocators due to dispatching

    HandlePool<AssetHandle, AssetDataHeader*> assetDb;
    HashTable<AssetHandle> assetLookup;     // Key: AssetParams hash. To check for availibility
    HashTableUint assetHashLookup;          // Key: AssetParams hash -> AssetHash
    MemTlsfAllocator assetHeaderAlloc;
    MemTlsfAllocator assetDataAlloc;

    HandlePool<AssetGroupHandle, AssetGroupInternal> groups;

    Array<AssetJobItem> pendingJobs;
    JobsHandle curJob;

    AssetServer server;
    bool isServerEnabled;
    bool isHashLookupUpdated;
};

static AssetMan gAssetMan;

#define ASSET_ASYNC_EXPERIMENT 0

//----------------------------------------------------------------------------------------------------------------------
// These functions should be exported for per asset type loading
using DataChunk = Pair<void*, uint32>;

namespace Asset
{
    static MemBumpAllocatorVM* _GetOrCreateScratchAllocator(AssetScratchMemArena& arena);
    static Span<AssetMetaKeyValue> _LoadMetaData(const char* assetFilepath, AssetPlatform::Enum platform, MemAllocator* alloc);
    static AssetHandleResult _CreateOrFetchHandle(const AssetParams& params);
    static void _LoadAssetTask(uint32 groupIdx, void* userData);
    static void _CreateGpuObjectTask(uint32 groupIdx, void* userData);
    static void _SaveBakedTask(uint32 groupIdx, void* userData);
    template <typename _T> _T* _TranslatePointer(_T* ptr, const void* origPtr, void* newPtr);
    static void _LoadGroupTask(uint32, void* userData);
    static void _UnloadGroupTask(uint32, void* userData);
    static void _DataCopyTask(uint32, void* userData);
    static uint32 _MakeCacheFilepath(Path* outPath, const AssetDataHeader* header, uint32 overrideAssetHash = 0);
    static uint32 _MakeParamsHash(const AssetParams& params, uint32 typeSpecificParamsSize);
    static bool _RemoteServerCallback(uint32 cmd, const Blob& incomingData, Blob*, void*, char outErrorDesc[REMOTE_ERROR_SIZE]);
    static void _RemoteClientCallback(uint32 cmd, const Blob& incomingData, void*, bool error, const char* errorDesc);
    static void _ServerLoadBatchTask(uint32, void*);
    constexpr AssetPlatform::Enum _GetCurrentPlatform();
    static void _SaveAssetHashLookup();
    static void _LoadAssetHashLookup();
} // Asset

constexpr AssetPlatform::Enum Asset::_GetCurrentPlatform()
{
    if constexpr (PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_OSX)
        return AssetPlatform::PC;
    else if constexpr (PLATFORM_ANDROID || PLATFORM_IOS)  
        return AssetPlatform::Mobile;
    else {
        ASSERT(0);
        return AssetPlatform::Auto;
    }
}

static void Asset::_SaveAssetHashLookup()
{
    PROFILE_ZONE();

    const uint32* keys = gAssetMan.assetHashLookup.Keys();
    const uint32* values = gAssetMan.assetHashLookup.Values();

    MemTempAllocator tempAlloc;
    Blob blob(&tempAlloc);
    blob.SetGrowPolicy(Blob::GrowPolicy::Multiply);

    String<64> line;
    for (uint32 i = 0; i < gAssetMan.assetHashLookup.Capacity(); i++) {
        if (keys[i]) {
            line.FormatSelf("0x%x;0x%x\n", keys[i], values[i]);
            blob.Write(line.Ptr(), line.Length());
        }
    }
    blob.Write<char>('\0');

    Vfs::WriteFileAsync("/cache/_HashLookup.txt", blob, VfsFlags::None, [](const char*, size_t, Blob&, void*) {}, nullptr);
}

static void Asset::_LoadAssetHashLookup()
{
    PROFILE_ZONE();
    
    MemTempAllocator tempAlloc;
    Blob blob = Vfs::ReadFile("cache/_HashLookup.txt", VfsFlags::TextFile, &tempAlloc);
    if (!blob.IsValid())
        return;

    Span<char*> lines = strSplit((const char*)blob.Data(), '\n', &tempAlloc);

    ReadWriteMutexWriteScope lk(gAssetMan.hashLookupMutex);
    for (char* line : lines) {
        char* semicolon = const_cast<char*>(strFindChar(line, ';'));
        if (semicolon) {
            *semicolon = 0;
            uint32 paramsHash = strToUint(line + 2, 16);
            uint32 assetHash = strToUint(semicolon + 3, 16);

            gAssetMan.assetHashLookup.Add(paramsHash, assetHash);
        }
    }
}

static uint32 Asset::_MakeParamsHash(const AssetParams& params, uint32 typeSpecificParamsSize)
{
    HashMurmur32Incremental hasher;
    hasher.Add<uint32>(params.typeId);
    hasher.AddAny(params.path.CStr(), params.path.Length());
    hasher.Add<uint32>(uint32(params.platform));
    hasher.AddAny(params.typeSpecificParams, typeSpecificParamsSize);
    return hasher.Hash();
}

static void Asset::_SaveBakedTask(uint32 groupIdx, void* userData)
{
    PROFILE_ZONE_COLOR(PROFILE_COLOR_ASSET3);

    AssetQueuedItem* qa = ((AssetQueuedItem**)userData)[groupIdx];

    Blob cache;
    cache.Reserve(32 + qa->dataSize);
    cache.SetGrowPolicy(Blob::GrowPolicy::Linear);
    cache.Write<uint32>(kAssetCacheFileId);
    cache.Write<uint32>(kAssetCacheVersion);
    cache.Write<uint32>(qa->dataSize);
    cache.Write(qa->data, qa->dataSize);

    uint64 writeFileData = (uint64(qa->paramsHash) << 32) | qa->assetHash;
    auto SaveFileCallback = [](const char* path, size_t bytesWritten, Blob& blob, void* userData)
    {
        if (bytesWritten)
            LOG_VERBOSE("(save) Baked: %s", path);
        blob.Free();

        // TODO: save to paramsHash -> assetHash lookup table        
        uint64 writeFileData = PtrToInt<uint64>(userData);
        uint32 paramsHash = uint32((writeFileData >> 32)&0xffffffff);
        uint32 assetHash = uint32(writeFileData&0xffffffff);

        ReadWriteMutexWriteScope lk(gAssetMan.hashLookupMutex);
        uint32 index = gAssetMan.assetHashLookup.AddIfNotFound(paramsHash, assetHash);

        // Overwrite the value if it exists
        // TODO: maybe have an API for that in HashTable
        if (index != -1) 
            ((uint32*)gAssetMan.assetHashLookup.mHashTable->values)[index] = assetHash;

        gAssetMan.isHashLookupUpdated = true;
    };

    Vfs::WriteFileAsync(qa->bakedFilepath.CStr(), cache, VfsFlags::CreateDirs|VfsFlags::NoCopyWriteBlob, 
                        SaveFileCallback, IntToPtr(writeFileData));
}

static uint32 Asset::_MakeCacheFilepath(Path* outPath, const AssetDataHeader* header, uint32 overrideAssetHash)
{
    uint32 assetHash = overrideAssetHash;
    const Path& assetFilepath = header->params->path;

    // If assetHash is not overriden, then try to calculate it by:
    //  - Path of the asset
    //  - Modified Time + Size of the source asset file
    //  - Asset Params hash
    //  - If meta file exists, Modified Time + size of the meta file
    if (assetHash == 0) {
        Path assetMetaPath = Path::JoinUnix(assetFilepath.GetDirectory(), assetFilepath.GetFileName());
        assetMetaPath.Append(".asset");

        PathInfo assetFileInfo = Vfs::GetFileInfo(assetFilepath.CStr());
        if (assetFileInfo.type == PathType::File) {
            PathInfo assetMetaInfo = Vfs::GetFileInfo(assetMetaPath.CStr());
    
            HashMurmur32Incremental hasher;
            hasher.AddAny(assetFilepath.CStr(), assetFilepath.Length());
            hasher.Add<uint32>(header->paramsHash);
            hasher.Add<uint64>(assetFileInfo.size);
            hasher.Add<uint64>(assetFileInfo.lastModified);

            if (assetMetaInfo.type == PathType::File) {
                hasher.Add<uint64>(assetMetaInfo.size);
                hasher.Add<uint64>(assetMetaInfo.lastModified);
            }

            assetHash = hasher.Hash();
        }
        else {
            return 0;
        }
    }

    Path strippedPath;
    Vfs::StripMountPath(strippedPath.Ptr(), strippedPath.Capacity(), assetFilepath.CStr());

    char hashStr[64];
    strPrintFmt(hashStr, sizeof(hashStr), "_%x", assetHash);

    *outPath = "/cache";
    (*outPath).Append(strippedPath.GetDirectory())
              .Append("/")
              .Append(strippedPath.GetFileName())
              .Append(hashStr)
              .Append(".")
              .Append(header->typeName);    
    return assetHash;
}

static MemBumpAllocatorVM* Asset::_GetOrCreateScratchAllocator(AssetScratchMemArena& arena)
{
    MemBumpAllocatorVM* alloc = nullptr;
    {
        uint32 tId = Thread::GetCurrentId();

        SpinLockMutexScope mtx(arena.threadToAllocatorTableMtx);
        uint32 allocIndex = arena.threadToAllocatorTable.FindAndFetch(tId, uint32(-1));

        if (allocIndex != -1) {
            alloc = &arena.allocators[allocIndex];
        }
        else {
            allocIndex = arena.numAllocators++;
            alloc = &arena.allocators[allocIndex];
            arena.threadToAllocatorTable.Add(tId, allocIndex);
        }
    }

    if (!alloc->IsInitialized())
        alloc->Initialize(limits::ASSET_MAX_SCRATCH_SIZE_PER_THREAD, 512*SIZE_KB);

    return alloc;
}


static Span<AssetMetaKeyValue> Asset::_LoadMetaData(const char* assetFilepath, AssetPlatform::Enum platform, MemAllocator* alloc)
{
    Path path(assetFilepath);
    Path assetMetaPath = Path::JoinUnix(path.GetDirectory(), path.GetFileName());
    assetMetaPath.Append(".asset");
    
    uint32 tempId = MemTempAllocator::PushId();
    MemTempAllocator tmpAlloc(tempId);

    Blob blob = Vfs::ReadFile(assetMetaPath.CStr(), VfsFlags::TextFile, &tmpAlloc);
    if (blob.IsValid()) {
        JsonErrorLocation loc;
        JsonContext* jctx = Json::Parse((const char*)blob.Data(), uint32(blob.Size()), &loc, &tmpAlloc);
        if (jctx) {
            JsonNode jroot(jctx);
            StaticArray<AssetMetaKeyValue, 64> keys;

            if (jroot.GetChildCount()) {
                JsonNode jitem = jroot.GetChildItem();
                while (jitem.IsValid()) {
                    if (!jitem.IsArray() && !jitem.IsObject()) {
                        AssetMetaKeyValue item;
                        jitem.GetKey(item.key.Ptr(), item.key.Capacity());
                        jitem.GetValue(item.value.Ptr(), item.value.Capacity());
                        keys.Add(item);
                    }

                    jitem = jroot.GetNextChildItem(jitem);
                }
            }

            // Collect platform-specific keys
            JsonNode jplatform;
            switch (platform) {
            case AssetPlatform::PC:         jplatform = jroot.GetChild("pc");      break;
            case AssetPlatform::Mobile:     jplatform = jroot.GetChild("mobile");  break;
            default:                        break;
            }
            if (jplatform.IsValid() && jplatform.GetChildCount()) {
                JsonNode jitem = jplatform.GetChildItem();
                while (jitem.IsValid()) {
                    if (!jitem.IsArray() && !jitem.IsObject()) {
                        AssetMetaKeyValue item;
                        jitem.GetKey(item.key.Ptr(), item.key.Capacity());
                        jitem.GetValue(item.value.Ptr(), item.value.Capacity());
                        keys.Add(item);
                    }

                    jitem = jplatform.GetNextChildItem(jitem);
                }
            }

            blob.Free();
            Json::Destroy(jctx);
            MemTempAllocator::PopId(tempId);
            
            // At this point we have popped the current temp allocator and can safely allocate from whatever allocator is coming in
            return Span<AssetMetaKeyValue>(Mem::AllocCopy<AssetMetaKeyValue>(keys.Ptr(), keys.Count(), alloc), keys.Count());
        }
        else {
            blob.Free();
            LOG_WARNING("Invalid asset meta data: %s (Json syntax error at %u:%u)", assetMetaPath.CStr(), loc.line, loc.col);
            MemTempAllocator::PopId(tempId);
            return Span<AssetMetaKeyValue>();
        }
    }
    else {
        MemTempAllocator::PopId(tempId);
        return Span<AssetMetaKeyValue>();
    }
}

static AssetHandleResult Asset::_CreateOrFetchHandle(const AssetParams& params)
{
    uint32 typeManIdx = gAssetMgr.typeManagers.FindIf(
        [typeId = params.typeId](const AssetTypeManager& typeMgr) { return typeMgr.fourcc == typeId; });
    ASSERT_MSG(typeManIdx != UINT32_MAX, "AssetType with FourCC %x is not registered", params.typeId);
    const AssetTypeManager& typeMan = gAssetMgr.typeManagers[typeManIdx];

    AssetHandleResult r {
        .paramsHash = _MakeParamsHash(params, typeMan.extraParamTypeSize)
    };

    // check with asset database and skip loading if it already exists
    {
        ReadWriteMutexReadScope lock(gAssetMan.assetMutex);
        r.handle = gAssetMan.assetLookup.FindAndFetch(r.paramsHash, AssetHandle());
        if (r.handle.IsValid()) {
            r.header = gAssetMan.assetDb.Data(r.handle);
            ++r.header->refCount;
            r.newlyCreated = false;
            return r;
        }
    }

    // create new asset header and handle 
    {
        ReadWriteMutexWriteScope lock(gAssetMan.assetMutex);
            
        MemSingleShotMalloc<AssetParams> paramsMallocator;
        if (typeMan.extraParamTypeSize)
            paramsMallocator.AddMemberArray<uint8>(offsetof(AssetParams, typeSpecificParams), typeMan.extraParamTypeSize);
        MemSingleShotMalloc<AssetDataHeader> mallocator;
        mallocator.AddChildStructSingleShot(paramsMallocator, offsetof(AssetDataHeader, params), 1);
        r.header = mallocator.Calloc(&gAssetMan.assetHeaderAlloc); // Note: This allocator is protected by 'assetMutex'

        r.header->paramsHash = r.paramsHash;
        r.header->refCount = 1;
        r.header->typeId = typeMan.fourcc;
        r.header->typeSpecificParamsSize = typeMan.extraParamTypeSize;
        r.header->params->typeId = params.typeId;
        r.header->params->path = params.path;
        r.header->params->platform = params.platform;
        r.header->typeName = typeMan.name.CStr();
        if (params.typeSpecificParams)
            memcpy(r.header->params->typeSpecificParams, params.typeSpecificParams, typeMan.extraParamTypeSize);

        r.handle = gAssetMan.assetDb.Add(r.header);

        gAssetMan.assetLookup.Add(r.paramsHash, r.handle);
        r.newlyCreated = true;
    }

    return r;
}

static void Asset::_DataCopyTask(uint32 groupIdx, void* userData)
{
    PROFILE_ZONE_COLOR(PROFILE_COLOR_ASSET2);
    AssetDataCopyTaskData& taskData = ((AssetDataCopyTaskData*)userData)[groupIdx];

    memcpy(taskData.destData, taskData.sourceData, taskData.sourceDataSize);

    ASSERT(taskData.header->data == nullptr);   // Data should be already null
    taskData.header->data = taskData.destData;
    taskData.header->dataSize = taskData.sourceDataSize;
}

static void Asset::_LoadAssetTask(uint32 groupIdx, void* userData)
{
    PROFILE_ZONE_COLOR(PROFILE_COLOR_ASSET2);

    AssetLoadTaskData& taskData = *(((AssetLoadTaskData**)userData)[groupIdx]);
    const AssetParams& params = *taskData.inputs.header->params;
    uint32 typeManIdx = gAssetMgr.typeManagers.FindIf([typeId = params.typeId](const AssetTypeManager& typeMan) { return typeMan.fourcc == typeId; });
    ASSERT_MSG(typeManIdx != UINT32_MAX, "AssetType with FourCC %x is not registered", params.typeId);
    const AssetTypeManager& typeMan = gAssetMgr.typeManagers[typeManIdx];

    MemBumpAllocatorVM* alloc = _GetOrCreateScratchAllocator(gAssetMan.memArena);

    if (taskData.inputs.type == AssetLoadTaskInputType::Source) {
        ASSERT(!taskData.inputs.isRemoteLoad);

        size_t startOffset = alloc->GetOffset();
        AssetData assetData {
            .mAlloc = alloc,
            .mData = Mem::AllocZeroTyped<AssetDataInternal>(1, alloc)
        };

        // Load metadata
        Span<AssetMetaKeyValue> metaData = _LoadMetaData(params.path.CStr(), params.platform, alloc);
        assetData.mData->metaData = metaData.Ptr();
        assetData.mData->numMetaData = metaData.Count();

        #if ASSET_ASYNC_EXPERIMENT
        taskData.inputs.fileReadSignal.Wait();
        const void* fileData = taskData.inputs.fileData;
        uint32 fileSize = taskData.inputs.fileSize;
        #else
        MemTempAllocator tempAlloc;
        Blob fileBlob = Vfs::ReadFile(taskData.inputs.header->params->path.CStr(), VfsFlags::None, &tempAlloc);
        const void* fileData = fileBlob.Data();
        ASSERT(fileBlob.Size() <= UINT32_MAX);
        uint32 fileSize = uint32(fileBlob.Size());
        #endif

        if (!fileData) {
            taskData.outputs.errorDesc = "Failed opening source file";
            alloc->SetOffset(startOffset);
            return;
        }

        // Parse/Bake
        Span<uint8> srcData((uint8*)const_cast<void*>(fileData), fileSize);
        if (!typeMan.impl->Bake(params, &assetData, srcData, &taskData.outputs.errorDesc)) {
            taskData.outputs.errorDesc = "Bake failed";
            alloc->SetOffset(startOffset);
            return;
        }

        taskData.outputs.dataSize = uint32(alloc->GetOffset() - startOffset);
        taskData.outputs.data = assetData.mData;
    }
    else if (taskData.inputs.type == AssetLoadTaskInputType::Baked) {
        #if ASSET_ASYNC_EXPERIMENT
        taskData.inputs.fileReadSignal.Wait();
        const void* fileData = taskData.inputs.fileData;
        uint32 fileSize = taskData.inputs.fileSize;
        #else
        const void* fileData = nullptr;
        uint32 fileSize = 0;

        if (taskData.inputs.isRemoteLoad) {
            taskData.inputs.fileReadSignal.Wait();
            fileData = taskData.inputs.fileData;
            fileSize = taskData.inputs.fileSize;
        }
        
        MemTempAllocator tempAlloc;
        if (!taskData.inputs.isRemoteLoad) {
            Blob fileBlob = Vfs::ReadFile(taskData.inputs.bakedFilepath.CStr(), VfsFlags::None, &tempAlloc);
            fileData = fileBlob.Data();
            ASSERT(fileBlob.Size() <= UINT32_MAX);
            fileSize = uint32(fileBlob.Size());
        }
        #endif // ASSET_ASYNC_EXPERIMENT

        if (!fileData) {
            taskData.outputs.errorDesc = taskData.inputs.isRemoteLoad ? taskData.inputs.remoteLoadErrorStr : "Failed opening baked file";
            return;
        }

        Blob cache(const_cast<void*>(fileData), fileSize);
        cache.SetSize(fileSize);

        uint32 fileId = 0;
        uint32 cacheVersion = 0;
        cache.Read<uint32>(&fileId);
        if (fileId != kAssetCacheFileId) {
            taskData.outputs.errorDesc = "Baked file has invalid signature";
            return;
        }

        cache.Read<uint32>(&cacheVersion);
        if (cacheVersion != kAssetCacheVersion) {
            taskData.outputs.errorDesc = "Invalid binary version for the baked file";
            return;
        }

        uint32 dataSize;
        cache.Read<uint32>(&dataSize);
        if (dataSize == 0) {
            taskData.outputs.errorDesc = "Baked data is empty";
            return;
        }

        void* data = Mem::Alloc(dataSize, alloc);
        cache.Read(data, dataSize);

        taskData.outputs.dataSize = dataSize;
        taskData.outputs.data = (AssetDataInternal*)data;
    }

    LOG_VERBOSE("(load) %s: %s%s", typeMan.name.CStr(), params.path.CStr(), taskData.inputs.type == AssetLoadTaskInputType::Baked ? " (baked)" : "");
}

static void Asset::_CreateGpuObjectTask(uint32 groupIdx, void* userData)
{
    PROFILE_ZONE_COLOR(PROFILE_COLOR_ASSET2);
    AssetDataInternal::GpuObject* gpuObj = ((AssetDataInternal::GpuObject**)userData)[groupIdx];

    switch (gpuObj->type) {
    case AssetDataInternal::GpuObjectType::Buffer:
        {
            GfxBufferDesc desc {
                .size = gpuObj->bufferDesc.size,
                .type = gpuObj->bufferDesc.type,
                .usage = gpuObj->bufferDesc.usage,
                .content = gpuObj->bufferDesc.content.Get()
            };
        
            GfxBufferHandle buffer = gfxCreateBuffer(desc);
            if (buffer.IsValid()) {
                GfxBufferHandle* targetBuffer = gpuObj->bufferDesc.bindToBuffer.Get();
                *targetBuffer = buffer;
            }

            break;
        }
    case AssetDataInternal::GpuObjectType::Texture:
        {
            GfxImageDesc desc {
                .width = gpuObj->textureDesc.width,
                .height = gpuObj->textureDesc.height,
                .numMips = gpuObj->textureDesc.numMips,
                .format = gpuObj->textureDesc.format,
                .usage = gpuObj->textureDesc.usage,
                .anisotropy = gpuObj->textureDesc.anisotropy,
                .samplerFilter = gpuObj->textureDesc.samplerFilter,
                .samplerWrap = gpuObj->textureDesc.samplerWrap,
                .borderColor = gpuObj->textureDesc.borderColor,
                .sampled = true,
                .size = gpuObj->textureDesc.size,
                .content = gpuObj->textureDesc.content.Get(),
                .mipOffsets = gpuObj->textureDesc.mipOffsets.Get()
            };

            GfxImageHandle image = gfxCreateImage(desc);
            if (image.IsValid()) {
                GfxImageHandle* targetImage = gpuObj->textureDesc.bindToImage.Get();
                *targetImage = image;
            }

            break;
        }
    }
}

static void Asset::_LoadGroupTask(uint32, void* userData)
{
    PROFILE_ZONE_COLOR(PROFILE_COLOR_ASSET1);
    TimerStopWatch timer;

    #if ASSET_ASYNC_EXPERIMENT
    auto ReadAssetFileFinished = [](AsyncFile* file, bool failed)
    {
        AssetLoadTaskData* taskData = (AssetLoadTaskData*)file->userData;
        taskData->inputs.fileData = !failed ? file->data : nullptr;
        taskData->inputs.fileSize = !failed ? file->size : 0;
        taskData->inputs.fileReadSignal.Set();
        taskData->inputs.fileReadSignal.Raise();
    };
    #endif

    // Fetch load list (pointers in the loadList are persistant through the lifetime of the group)
    AssetGroupHandle groupHandle(PtrToInt<uint32>(userData));
    Array<AssetDataHeader*> loadList;

    {
        ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
        AssetGroupInternal& group = gAssetMan.groups.Data(groupHandle);
        Atomic::StoreExplicit(&group.state, uint32(AssetGroupState::Loading), AtomicMemoryOrder::Release);
        group.loadList.CopyTo(&loadList);
        group.loadList.Clear();
    }

    // Pick a batch size 
    // This is the amount of tasks that are submitted to the task manager at a time
    uint32 batchCount = Min(128u, loadList.Count());
    uint32 allCount = loadList.Count();

    // We cannot use the actual temp allocators here. Because we are dispatching jobs and might end up in a different thread
    MemBumpAllocatorVM* tempAlloc = &gAssetMan.tempAlloc;
    Array<AssetQueuedItem> queuedAssets;
    queuedAssets.Reserve(loadList.Count());

    for (uint32 i = 0; i < loadList.Count(); i += batchCount) {
        uint32 sliceCount = Min(batchCount, loadList.Count() - i);

        AssetLoadTaskData* taskDatas = Mem::AllocZeroTyped<AssetLoadTaskData>(sliceCount, tempAlloc);
        AssetLoadTaskData** taskDataPtrs = Mem::AllocTyped<AssetLoadTaskData*>(sliceCount, tempAlloc);
        for (uint32 k = 0; k < sliceCount; k++) {
            AssetDataHeader* header = loadList[i + k];
            header->state = AssetState::Loading;

            // TODO: this part will not run on remote connection mode
            //       We will always have Baked data for remote connections

            // Returns false if source file could not be found
            // Decide if baked file exists and we should skip loading from source (with requires baking most assets)
            uint32 assetHash = 0;   // TODO: try to fetch this from a database (probably with an argument/option)
            {
                ReadWriteMutexReadScope lk(gAssetMan.hashLookupMutex);
                assetHash = gAssetMan.assetHashLookup.FindAndFetch(header->paramsHash, 0);
            }

            bool mountIsRemote = Vfs::GetMountType(header->params->path.CStr()) == VfsMountType::Remote;
            taskDatas[k].inputs.isRemoteLoad = mountIsRemote;

            if (!mountIsRemote || assetHash) {
                assetHash = _MakeCacheFilepath(&taskDatas[k].inputs.bakedFilepath, header, assetHash);
                if (assetHash) {
                    taskDatas[k].inputs.type = Vfs::FileExists(taskDatas[k].inputs.bakedFilepath.CStr()) ? AssetLoadTaskInputType::Baked : AssetLoadTaskInputType::Source;
                    taskDatas[k].inputs.assetHash = assetHash;
                    
                    // We have found the correct baked asset in the cache dir, ignore loading from remote and fetch it directly from disk
                    if (taskDatas[k].inputs.type == AssetLoadTaskInputType::Baked)
                        taskDatas[k].inputs.isRemoteLoad = false;
                }
            }
            else if (mountIsRemote) {
                taskDatas[k].inputs.type = AssetLoadTaskInputType::Baked;
            }
            
            taskDatas[k].inputs.groupHandle = groupHandle;
            taskDatas[k].inputs.header = header;

            // Ouch! we have to comply across all calls here, so _LoadAssetTask needs pointers
            taskDataPtrs[k] = &taskDatas[k];
        }

        JobsHandle batchJob = Jobs::Dispatch(JobsType::LongTask, _LoadAssetTask, taskDataPtrs, sliceCount, JobsPriority::High, JobsStackSize::Large);

        #if ASSET_ASYNC_EXPERIMENT
        // TODO: Currently ReadFile seems weird on Windows API at least
        //       It takes roughly 18ms for 11 files (11 threads)
        //       Either windows file system is lame or something wrong with my implementation
        //       Because the profiler shows that many LoadAsset tasks are overlapped, which means the file reading is pretty serial
        //       

        // TODO: For remote connection, instead of files, we need to submit requests for the server instead of files
        //       fileReadSignal will wait for that result instead. which is always baked data or error
        //       It will also receive the newest asset hash to store in some database
        //       We will also send the current asset hash in the database for the request, 
        //       so server will check if it needs to send a new data or client will load baked data from it's cache
        //       
        for (uint32 k = 0; k < sliceCount; k++) {
            AssetDataHeader* header = loadList[i + k];

            AsyncFileRequest req {
                .alloc = tempAlloc,
                .readFn = ReadAssetFileFinished,
                .userData = &taskDatas[k],
            };

            const char* assetFilepath = taskDatas[k].inputs.type == AssetLoadTaskInputType::Baked ? 
                taskDatas[k].inputs.bakedFilepath.CStr() : header->params->path.CStr();
            Path absFilepath = Vfs::ResolveFilepath(assetFilepath);
            Async::ReadFile(absFilepath.CStr(), req);
        }
        #endif

        // Make remote load requests
        for (uint32 k = 0; k < sliceCount; k++) {
            if (!taskDatas[k].inputs.isRemoteLoad) 
                continue;

            AssetDataHeader* header = loadList[i + k];
            if (header->params->platform == AssetPlatform::Auto)
                header->params->platform = _GetCurrentPlatform();

            Blob requestBlob(tempAlloc);

            requestBlob.Write<uint64>(uint64(&taskDatas[k].inputs));
            requestBlob.Write<uint32>(header->typeId);
            requestBlob.WriteStringBinary16(header->params->path.Ptr(), header->params->path.Length());
            requestBlob.Write<uint32>(header->params->platform);
            requestBlob.Write(header->params->typeSpecificParams, header->typeSpecificParamsSize);

            Remote::ExecuteCommand(ASSET_LOAD_ASSET_REMOTE_CMD, requestBlob);
        }

        Jobs::WaitForCompletionAndDelete(batchJob);

        // Gather dependency assets and add them to the queue
        // Gather items to be saved in cache
        for (uint32 k = 0; k < sliceCount; k++) {
            AssetLoadTaskInputs& in = taskDatas[k].inputs;
            AssetLoadTaskOutputs& out = taskDatas[k].outputs;
            if (!out.data) {
                in.header->state = AssetState::LoadFailed;
                LOG_ERROR("Loading %s '%s' failed: %s", in.header->typeName, in.header->params->path.CStr(), out.errorDesc.CStr());
                continue;
            }

            AssetQueuedItem qa {
                .indexInLoadList = i + k,
                .dataSize = out.dataSize,
                .paramsHash = in.header->paramsHash,
                .assetHash = in.assetHash,
                .data = out.data,
                .bakedFilepath = in.bakedFilepath,
                .saveBaked = (in.type == AssetLoadTaskInputType::Source || in.isRemoteLoad)
            };
            queuedAssets.Push(qa);

            // Add dependencies to AssetDb and the new ones to loadList
            AssetDataInternal::Dependency* dep = out.data->deps.Get();
            while (dep) {
                AssetParams params {
                    .typeId = dep->typeId,
                    .path = dep->path,
                    .platform = in.header->params->platform,
                    .typeSpecificParams = !dep->typeSpecificParams.IsNull() ? dep->typeSpecificParams.Get() : nullptr
                };

                AssetHandleResult depHandleResult = _CreateOrFetchHandle(params);
                AssetHandle* targetHandle = dep->bindToHandle.Get();
                *targetHandle = depHandleResult.handle;
                if (depHandleResult.newlyCreated)
                    loadList.Push(depHandleResult.header);

                dep = dep->next.Get();
            }
        } 

        tempAlloc->Reset();
    }

    // Save to cache
    Array<AssetQueuedItem*> saveItems(tempAlloc);
    for (uint32 i = 0; i < queuedAssets.Count(); i += batchCount) {
        uint32 sliceCount = Min(batchCount, loadList.Count() - i);

        for (uint32 k = 0; k < sliceCount; k++) {
            AssetQueuedItem& qa = queuedAssets[i + k];
            if (qa.saveBaked)
                saveItems.Push(&qa);
        }
        
        if (!saveItems.IsEmpty()) {
            JobsHandle batchJob = Jobs::Dispatch(JobsType::LongTask, _SaveBakedTask, saveItems.Ptr(), saveItems.Count());
            Jobs::WaitForCompletionAndDelete(batchJob);
            saveItems.Clear();
        }
    }
    tempAlloc->Reset();

    // Create GPU objects
    for (uint32 i = 0; i < queuedAssets.Count(); i += batchCount) {
        uint32 sliceCount = Min(batchCount, loadList.Count() - i);

        Array<AssetDataInternal::GpuObject*> gpuObjs(tempAlloc);
        for (uint32 k = 0; k < sliceCount; k++) {
            AssetQueuedItem& qa = queuedAssets[i + k];
            AssetDataInternal* data = qa.data;            
            AssetDataInternal::GpuObject* gpuObj = data->gpuObjects.Get();
            while (gpuObj) {
                gpuObjs.Push(gpuObj);
                gpuObj = gpuObj->next.Get();
            }
        }

        if (!gpuObjs.IsEmpty()) {
            JobsHandle batchJob = Jobs::Dispatch(JobsType::LongTask, _CreateGpuObjectTask, gpuObjs.Ptr(), gpuObjs.Count());
            Jobs::WaitForCompletionAndDelete(batchJob);
        }

        tempAlloc->Reset();
    }
    
    // Strip unwanted stuff and save it to persistent memory
    // Note: 'AssetDataAlloc' doesn't need any thread protection, because it is guaranteed that only one thread uses it at a time 
    // Use the magic number 4 for now. Because it's a good balance with quad-channel RAM (seems to be)
    // TODO: maybe skipe smaller memcpys because they don't worth creating a task for ? (profile)
    uint32 numCopyDispatches = Min(4u, batchCount);
    for (uint32 i = 0; i < queuedAssets.Count(); i += numCopyDispatches) {
        uint32 sliceCount = Min(numCopyDispatches, loadList.Count() - i);
        AssetDataCopyTaskData* dataCopyItems = Mem::AllocTyped<AssetDataCopyTaskData>(sliceCount, tempAlloc);

        for (uint32 k = 0; k < sliceCount; k++) {
            const AssetQueuedItem& qa = queuedAssets[i + k];
            dataCopyItems[k].header = loadList[qa.indexInLoadList];
            dataCopyItems[k].destData = (AssetDataInternal*)Mem::Alloc(qa.dataSize, &gAssetMan.assetDataAlloc);
            dataCopyItems[k].sourceData = qa.data;
            dataCopyItems[k].sourceDataSize = qa.dataSize;
        }

        JobsHandle batchJob = Jobs::Dispatch(JobsType::LongTask, _DataCopyTask, dataCopyItems, sliceCount);
        Jobs::WaitForCompletionAndDelete(batchJob);

        tempAlloc->Reset();
    }

    for (AssetQueuedItem& qa : queuedAssets) {
        AssetDataHeader* header = loadList[qa.indexInLoadList];
        header->state = AssetState::Loaded;
    }

    queuedAssets.Free();
    loadList.Free();

    // Reset arena allocators, getting ready for the next group
    for (uint32 i = 0; i < gAssetMan.memArena.numAllocators; i++) {
        if (gAssetMan.memArena.allocators[i].IsInitialized())
            gAssetMan.memArena.allocators[i].Reset();
    }

    {
        ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
        AssetGroupInternal& group = gAssetMan.groups.Data(groupHandle);
        Atomic::StoreExplicit(&group.state, uint32(AssetGroupState::Loaded), AtomicMemoryOrder::Release);
    }

    LOG_INFO("LoadGroup with %u assets finished (%.1f ms)", allCount, timer.ElapsedMS());
};

static void Asset::_UnloadGroupTask(uint32, void* userData)
{
    PROFILE_ZONE_COLOR(PROFILE_COLOR_ASSET3);

    // Fetch load list (pointers in the loadList are persistant through the lifetime of the group)
    MemTempAllocator tempAlloc;
    AssetGroupHandle groupHandle(PtrToInt<uint32>(userData));
    Array<AssetHandle> unloadList(&tempAlloc);
    Array<AssetDataInternal*> unloadDatas(&tempAlloc);

    {
        ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
        AssetGroupInternal& group = gAssetMan.groups.Data(groupHandle);
        Atomic::StoreExplicit(&group.state, uint32(AssetGroupState::Unloading), AtomicMemoryOrder::Release);
        group.handles.CopyTo(&unloadList);
        group.handles.Clear();
    }

    {
        ReadWriteMutexReadScope rlock(gAssetMan.assetMutex);
        for (uint32 i = 0; i < unloadList.Count();) {
            AssetDataHeader* header = gAssetMan.assetDb.Data(unloadList[i]);
            if (--header->refCount > 0) {
                unloadList.RemoveAndSwap(i);
            } 
            else {
                unloadDatas.Push(header->data);

                // Add dependencies to the unload list
                AssetDataInternal::Dependency* dep = header->data->deps.Get();
                while (dep) {
                    AssetHandle* targetHandle = dep->bindToHandle.Get();
                    if (targetHandle->IsValid()) 
                        unloadList.Push(*targetHandle);
                    dep = dep->next.Get();
                }

                i++;
            }
        }
    }

    // Destroy GPU objects and asset data
    for (AssetDataInternal* data : unloadDatas) {
        AssetDataInternal::GpuObject* gpuObj = data->gpuObjects.Get();
        while (gpuObj) {
            switch (gpuObj->type) {
            case AssetDataInternal::GpuObjectType::Texture:
                ASSERT(!gpuObj->textureDesc.bindToImage.IsNull());
                gfxDestroyImage(*gpuObj->textureDesc.bindToImage.Get());
                break;
            case AssetDataInternal::GpuObjectType::Buffer:
                ASSERT(!gpuObj->bufferDesc.bindToBuffer.IsNull());
                gfxDestroyBuffer(*gpuObj->bufferDesc.bindToBuffer.Get());
                break;
            }
            gpuObj = gpuObj->next.Get();
        }

        // Note: 'AssetDataAlloc' doesn't need any thread protection, because it is guaranteed that only one thread uses it at a time 
        Mem::Free(data, &gAssetMan.assetDataAlloc);
    }

    {
        ReadWriteMutexWriteScope wlock(gAssetMan.assetMutex);
        for (AssetHandle handle : unloadList) {
            AssetDataHeader* header = gAssetMan.assetDb.Data(handle);
            
            gAssetMan.assetLookup.FindAndRemove(header->paramsHash);
            gAssetMan.assetDb.Remove(handle);
            
            LOG_VERBOSE("(unload) %s (handle = %u)", header->params->path.CStr(), handle.mId);
            
            MemSingleShotMalloc<AssetDataHeader>::Free(header, &gAssetMan.assetHeaderAlloc); // Note: protected by 'assetMutex'
        }
    }

    {
        ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
        AssetGroupInternal& group = gAssetMan.groups.Data(groupHandle);
        Atomic::StoreExplicit(&group.state, uint32(AssetGroupState::Idle), AtomicMemoryOrder::Release);
    }
}

void Asset::_ServerLoadBatchTask(uint32, void*)
{
    AssetLoadTaskData** taskDatas = gAssetMan.server.loadTaskDatas;
    uint32 numTasks = gAssetMan.server.numLoadTasks;
    ASSERT(numTasks);

    for (uint32 i = 0; i < numTasks; i++) {
        AssetDataHeader* header = taskDatas[i]->inputs.header;
        uint32 assetHash = _MakeCacheFilepath(&taskDatas[i]->inputs.bakedFilepath, header);
        if (assetHash) {
            taskDatas[i]->inputs.type = Vfs::FileExists(taskDatas[i]->inputs.bakedFilepath.CStr()) ? AssetLoadTaskInputType::Baked : AssetLoadTaskInputType::Source;
            taskDatas[i]->inputs.assetHash = assetHash;
        }
    }

    JobsHandle batchJob = Jobs::Dispatch(JobsType::LongTask, _LoadAssetTask, taskDatas, numTasks);
    Jobs::WaitForCompletionAndDelete(batchJob);

    // Gather stuff that needs to be saved
    StaticArray<AssetQueuedItem, limits::ASSET_SERVER_MAX_IN_FLIGHT> saveAssets;
    for (uint32 i = 0; i < numTasks; i++) {
        const AssetLoadTaskInputs& in = taskDatas[i]->inputs;
        AssetLoadTaskOutputs& out = taskDatas[i]->outputs;

        bool saveBaked = in.type == AssetLoadTaskInputType::Source;
        if (out.data && saveBaked) {
            AssetQueuedItem qa {
                .indexInLoadList = i,
                .dataSize = out.dataSize,
                .data = out.data,
                .bakedFilepath = in.bakedFilepath,
                .saveBaked = saveBaked
            };
            saveAssets.Add(qa);
        }
    }

    // Save the baked ones
    // if (!saveAssets.IsEmpty())
    //    batchJob = Jobs::Dispatch(JobsType::LongTask, _SaveBakedTask, saveAssets.Ptr(), saveAssets.Count());

    // Send successfully loaded asset blobs to the client
    // TODO: compress the data
    {
        MemTempAllocator tempAlloc;
        for (uint32 i = 0; i < numTasks; i++) {
            const AssetLoadTaskInputs& in = taskDatas[i]->inputs;
            const AssetLoadTaskOutputs& out = taskDatas[i]->outputs;

            Blob blobs[2];

            blobs[0].SetAllocator(&tempAlloc);
            blobs[0].SetGrowPolicy(Blob::GrowPolicy::Linear, 32);
            blobs[0].Write<uint64>(in.clientPayload);

            if (!out.data) {
                String<512> errorMsg;
                errorMsg.FormatSelf("Loading %s '%s' failed: %s", in.header->typeName, in.header->params->path.CStr(), out.errorDesc.CStr());
                LOG_ERROR(errorMsg.CStr());

                Remote::SendResponse(ASSET_LOAD_ASSET_REMOTE_CMD, blobs[0], true, errorMsg.CStr());
            }
            else {
                blobs[0].Write<uint32>(in.assetHash);

                // Add Cache header
                // TODO: maybe make this cleaner. Because we are writing cache header in two places (_SaveBakedTask)
                blobs[0].Write<uint32>(kAssetCacheFileId);
                blobs[0].Write<uint32>(kAssetCacheVersion);
                blobs[0].Write<uint32>(out.dataSize);

                blobs[1].Reserve(out.data, out.dataSize);
                blobs[1].SetSize(out.dataSize);

                Remote::SendResponseMerge(ASSET_LOAD_ASSET_REMOTE_CMD, blobs, CountOf(blobs), false, nullptr);
            }
        }
    }

    // if (!saveAssets.IsEmpty())
    //    Jobs::WaitForCompletion(batchJob);

    // No need for TaskDatas (allocated in _RemoteServerCallback)
    for (uint32 i = 0; i < numTasks; i++) 
        MemSingleShotMalloc<AssetLoadTaskData>::Free(taskDatas[i]);
    gAssetMan.server.numLoadTasks = 0;

    // Reset arena allocators
    for (uint32 i = 0; i < gAssetMan.memArena.numAllocators; i++) {
        if (gAssetMan.memArena.allocators[i].IsInitialized())
            gAssetMan.memArena.allocators[i].Reset();
    }
}

bool Asset::_RemoteServerCallback([[maybe_unused]] uint32 cmd, const Blob& incomingData, Blob*, void*, char outErrorDesc[REMOTE_ERROR_SIZE])
{
    ASSERT(cmd == ASSET_LOAD_ASSET_REMOTE_CMD);
    UNUSED(outErrorDesc);

    uint32 typeId = 0;
    uint32 platformId = 0;
    uint64 clientPayload;

    incomingData.Read<uint64>(&clientPayload);
    incomingData.Read<uint32>(&typeId);
    uint32 typeManIdx = gAssetMgr.typeManagers.FindIf([typeId](const AssetTypeManager& typeMgr) { return typeMgr.fourcc == typeId; });
    ASSERT_MSG(typeManIdx != UINT32_MAX, "AssetType with FourCC %x is not registered", typeId);
    const AssetTypeManager& typeMan = gAssetMgr.typeManagers[typeManIdx];

    MemSingleShotMalloc<AssetParams> paramsMallocator;
    if (typeMan.extraParamTypeSize)
        paramsMallocator.AddMemberArray<uint8>(offsetof(AssetParams, typeSpecificParams), typeMan.extraParamTypeSize);
    MemSingleShotMalloc<AssetDataHeader> dataHeaderMallocator;
    dataHeaderMallocator.AddChildStructSingleShot(paramsMallocator, offsetof(AssetDataHeader, params), 1);
    MemSingleShotMalloc<AssetLoadTaskData> mallocator;
    mallocator.AddChildStructSingleShot(dataHeaderMallocator, offsetof(AssetLoadTaskInputs, header), 1);
    AssetLoadTaskData* taskData = mallocator.Calloc();
    AssetDataHeader* header = taskData->inputs.header;

    incomingData.ReadStringBinary16(header->params->path.Ptr(), header->params->path.Capacity());

    incomingData.Read<uint32>(&platformId);
    incomingData.Read(header->params->typeSpecificParams, typeMan.extraParamTypeSize);

    header->params->typeId = typeId;
    header->params->platform = AssetPlatform::Enum(platformId);

    header->paramsHash = _MakeParamsHash(*header->params, typeMan.extraParamTypeSize);
    header->refCount = 1;
    header->typeName = typeMan.name.CStr();

    taskData->inputs.clientPayload = clientPayload;

    {
        SpinLockMutexScope mtx(gAssetMan.server.pendingTasksMutex);
        gAssetMan.server.pendingTasks.Push(taskData);
    }

    // Trigger server job, so we can dispatch server jobs in Update when we get idle
    {
        MutexScope lock(gAssetMan.pendingJobsMutex);
        uint32 index = gAssetMan.pendingJobs.FindIf([](const AssetJobItem& item) { return item.type == AssetJobType::Server; });
        if (index == -1) {
            AssetJobItem item { .type = AssetJobType::Server };
            gAssetMan.pendingJobs.PushAndSort(item, [](const AssetJobItem& a, const AssetJobItem& b) { return int(b.type) - int(a.type); });
        }
    }

    return true;
}

void Asset::_RemoteClientCallback([[maybe_unused]] uint32 cmd, const Blob& incomingData, void*, bool error, const char* errorDesc)
{
    ASSERT(cmd == ASSET_LOAD_ASSET_REMOTE_CMD);

    uint64 payload;
    incomingData.Read<uint64>(&payload);
    AssetLoadTaskInputs* inputs = (AssetLoadTaskInputs*)reinterpret_cast<void*>(payload);

    if (!error) {
        uint32 assetHash;
        incomingData.Read<uint32>(&assetHash);
        inputs->assetHash = _MakeCacheFilepath(&inputs->bakedFilepath, inputs->header, assetHash);

        size_t bakedSize = incomingData.Size() - incomingData.ReadOffset();
        ASSERT(bakedSize <= UINT32_MAX);

        void* data = Mem::Alloc(bakedSize, &gAssetMan.tempAlloc);
        incomingData.Read(data, bakedSize);
        inputs->fileData = data;
        inputs->fileSize = uint32(bakedSize);
    }
    else {
        inputs->fileData = nullptr;
        inputs->fileSize = 0;
        inputs->remoteLoadErrorStr = errorDesc;
    }

    inputs->fileReadSignal.Set();
    inputs->fileReadSignal.Raise();
}

template <typename _T>
_T* Asset::_TranslatePointer(_T* ptr, const void* origPtr, void* newPtr)
{
    ASSERT(uintptr_t(ptr) >= uintptr_t(origPtr));
    size_t offset = uintptr_t(ptr) - uintptr_t(origPtr);
    return (_T*)((uint8*)newPtr + offset);
}

bool Asset::Initialize()
{
    gAssetMan.assetMutex.Initialize();
    gAssetMan.groupsMutex.Initialize();
    gAssetMan.hashLookupMutex.Initialize();
    gAssetMan.pendingJobsMutex.Initialize();

    // TODO: set allocator (initHeap) for all main objects
    MemAllocator* alloc = Mem::GetDefaultAlloc();

    gAssetMan.assetDb.SetAllocator(alloc);
    gAssetMan.assetLookup.SetAllocator(alloc);
    gAssetMan.assetLookup.Reserve(512);
    gAssetMan.assetHeaderAlloc.Initialize(alloc, limits::ASSET_HEADER_BUFFER_POOL_SIZE, false);
    gAssetMan.assetDataAlloc.Initialize(alloc, limits::ASSET_DATA_BUFFER_POOL_SIZE, false);
    gAssetMan.groups.SetAllocator(alloc);
    gAssetMan.assetHashLookup.SetAllocator(alloc);
    gAssetMan.assetHashLookup.Reserve(512);
    gAssetMan.pendingJobs.SetAllocator(alloc);

    gAssetMan.memArena.maxAllocators = Jobs::GetWorkerThreadsCount(JobsType::LongTask);
    gAssetMan.memArena.allocators = NEW_ARRAY(alloc, MemBumpAllocatorVM, gAssetMan.memArena.maxAllocators);
    gAssetMan.memArena.threadToAllocatorTable.SetAllocator(alloc);
    gAssetMan.memArena.threadToAllocatorTable.Reserve(gAssetMan.memArena.maxAllocators);

    gAssetMan.tempAlloc.Initialize(SIZE_GB, 4*SIZE_MB);

    RemoteCommandDesc loadRemoteDesc {
        .cmdFourCC = ASSET_LOAD_ASSET_REMOTE_CMD,
        .serverFn = Asset::_RemoteServerCallback,
        .clientFn = Asset::_RemoteClientCallback,
        .async = true
    };
    Remote::RegisterCommand(loadRemoteDesc);

    if (SettingsJunkyard::Get().tooling.enableServer) {
        // Initialize server stuff
        AssetServer& server = gAssetMan.server;
        server.pendingTasks.SetAllocator(alloc);
        gAssetMan.isServerEnabled = true;

        LOG_INFO("(init) Asset Server");
    }

    _LoadAssetHashLookup();

    LOG_INFO("(init) Asset Manager");
    return true;
}

void Asset::Release()
{
    if (gAssetMan.curJob && Jobs::IsRunning(gAssetMan.curJob)) {
        LOG_VERBOSE("Waiting for in-flight AssetMan jobs to finish ...");
        Jobs::WaitForCompletionAndDelete(gAssetMan.curJob);
        gAssetMan.curJob = nullptr;
    }

    // Perform all pending unload jobs to clean stuff up
    // We do this by removing all the pending jobs that are not Unloads and run the Update loop until no pending job remains
    uint32 numUnloadJobs = 0;
    {
        MutexScope lock(gAssetMan.pendingJobsMutex);
        for (uint32 i = 0; i < gAssetMan.pendingJobs.Count();) {
            if (gAssetMan.pendingJobs[i].type != AssetJobType::Unload)
                gAssetMan.pendingJobs.RemoveAndSwap(i);
            else
                i++;
        }
        numUnloadJobs = gAssetMan.pendingJobs.Count();
    }

    if (numUnloadJobs) {
        LOG_VERBOSE("Unloading %u AssetGroups ...");
        while (gAssetMan.curJob) {
            Update();
            Thread::Sleep(1);
        }
    }

    MemAllocator* alloc = Mem::GetDefaultAlloc();

    if (gAssetMan.isServerEnabled) {
        for (AssetLoadTaskData* taskData : gAssetMan.server.pendingTasks)
            MemSingleShotMalloc<AssetLoadTaskData>::Free(taskData);     // allocated in _RemoteServerCallback
        
        gAssetMan.server.pendingTasks.Free();
    }

    for (uint32 i = 0; i < gAssetMan.memArena.numAllocators; i++) {
        if (gAssetMan.memArena.allocators[i].IsInitialized())
            gAssetMan.memArena.allocators[i].Release();
    }

    if (gAssetMan.isHashLookupUpdated) {
        _SaveAssetHashLookup();
        gAssetMan.isHashLookupUpdated = false;
    }

    Mem::Free(gAssetMan.memArena.allocators, alloc);
    gAssetMan.memArena.threadToAllocatorTable.Free();
    gAssetMan.tempAlloc.Release();

    gAssetMan.groups.Free();
    gAssetMan.assetDb.Free();
    gAssetMan.assetLookup.Free();
    gAssetMan.assetHashLookup.Free();
    gAssetMan.pendingJobs.Free();

    gAssetMan.assetHeaderAlloc.Release();
    gAssetMan.assetDataAlloc.Release();

    gAssetMan.hashLookupMutex.Release();
    gAssetMan.assetMutex.Release();
    gAssetMan.groupsMutex.Release();
    gAssetMan.pendingJobsMutex.Release();
}

AssetGroup Asset::CreateGroup()
{
    AssetGroupInternal groupInternal {};

    MemAllocator* alloc = Mem::GetDefaultAlloc();

    groupInternal.loadList.SetAllocator(alloc);
    groupInternal.handles.SetAllocator(alloc);

    ReadWriteMutexWriteScope lock(gAssetMan.groupsMutex);
    AssetGroup group {
        .mHandle = gAssetMan.groups.Add(groupInternal)
    };
    
    return group;
}

void Asset::DestroyGroup(AssetGroup& group)
{
    ASSERT_MSG(Engine::IsMainThread(), "DestroyGroup can only be called in the main thread");

    if (!group.mHandle.IsValid())
        return;

    {
        MutexScope lock(gAssetMan.pendingJobsMutex);
        uint32 index = gAssetMan.pendingJobs.FindIf([handle = group.mHandle](const AssetJobItem& item) { 
            return item.groupHandle == handle; });
        if (index != -1)
            gAssetMan.pendingJobs.RemoveAndShift(index);
    }

    {
        ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
        AssetGroupInternal& gi = gAssetMan.groups.Data(group.mHandle);
        ASSERT_MSG(gi.state == AssetGroupState::Idle, "AssetGroup must be fully unloaded (Idle state) before getting destroyed");

        gi.loadList.Free();
        gi.handles.Free();
    }

    ReadWriteMutexWriteScope wlock(gAssetMan.groupsMutex);
    gAssetMan.groups.Remove(group.mHandle);
}

void* Asset::GetObjData(AssetHandle handle)
{
    if (!handle.IsValid())
        return nullptr;

    ReadWriteMutexReadScope rdlock(gAssetMan.assetMutex);
    AssetDataInternal* data = gAssetMan.assetDb.IsValid(handle) ? gAssetMan.assetDb.Data(handle)->data : nullptr;
    return data ? data->objData.Get() : nullptr;
}

const AssetParams* Asset::GetParams(AssetHandle handle)
{
    if (!handle.IsValid())
        return nullptr;

    ReadWriteMutexReadScope rdlock(gAssetMan.assetMutex);
    return gAssetMan.assetDb.IsValid(handle) ? gAssetMan.assetDb.Data(handle)->params : nullptr;
}

void AssetData::AddDependency(AssetHandle* bindToHandle, const AssetParams& params)
{
    ASSERT_MSG(!mData->objData.IsNull(), "You must SetObjData before adding dependencies");

    uint32 typeManIdx = gAssetMgr.typeManagers.FindIf(
        [typeId = params.typeId](const AssetTypeManager& typeMgr) { return typeMgr.fourcc == typeId; });
    ASSERT_MSG(typeManIdx != UINT32_MAX, "AssetType with FourCC %x is not registered", params.typeId);
    const AssetTypeManager& typeMan = gAssetMgr.typeManagers[typeManIdx];

    AssetDataInternal::Dependency* dep = Mem::AllocZeroTyped<AssetDataInternal::Dependency>(1, mAlloc);
    dep->path = params.path;
    dep->typeId = params.typeId;
    dep->bindToHandle = Asset::_TranslatePointer<AssetHandle>(bindToHandle, mOrigObjPtr, mData->objData.Get());
    if (params.typeSpecificParams)
        dep->typeSpecificParams = Mem::AllocCopy<uint8>((uint8*)params.typeSpecificParams, typeMan.extraParamTypeSize, mAlloc);
    
    if (mLastDependencyPtr) 
        ((AssetDataInternal::Dependency*)mLastDependencyPtr)->next = dep;
    else
        mData->deps = dep;

    mLastDependencyPtr = dep;
    
    ++mData->numDependencies;
}

void AssetData::AddGpuTextureObject(GfxImageHandle* bindToImage, const GfxImageDesc& desc)
{
    ASSERT_MSG(!mData->objData.IsNull(), "You must SetObjData before adding texture objects");

    AssetDataInternal::GpuObject* gpuObj = Mem::AllocZeroTyped<AssetDataInternal::GpuObject>(1, mAlloc);

    gpuObj->type = AssetDataInternal::GpuObjectType::Texture;
    gpuObj->textureDesc.bindToImage = Asset::_TranslatePointer<GfxImageHandle>(bindToImage, mOrigObjPtr, mData->objData.Get());
    gpuObj->textureDesc.width = desc.width;
    gpuObj->textureDesc.height = desc.height;
    gpuObj->textureDesc.numMips = desc.numMips;
    gpuObj->textureDesc.format = desc.format;
    gpuObj->textureDesc.usage = desc.usage;
    gpuObj->textureDesc.anisotropy = desc.anisotropy;
    gpuObj->textureDesc.samplerFilter = desc.samplerFilter;
    gpuObj->textureDesc.samplerWrap = desc.samplerWrap;
    gpuObj->textureDesc.borderColor = desc.borderColor;
    gpuObj->textureDesc.size = desc.size;
    ASSERT(desc.content);
    ASSERT(desc.size <= UINT32_MAX);
    gpuObj->textureDesc.content = Mem::AllocCopy<uint8>((const uint8*)desc.content, (uint32)desc.size, mAlloc);
    if (desc.mipOffsets)
        gpuObj->textureDesc.mipOffsets = Mem::AllocCopy<uint32>(desc.mipOffsets, desc.numMips,mAlloc);
    
    if (mLastGpuObjectPtr) 
        ((AssetDataInternal::GpuObject*)mLastGpuObjectPtr)->next = gpuObj;
    else
        mData->gpuObjects = gpuObj;

    mLastGpuObjectPtr = gpuObj;

    ++mData->numGpuObjects;
}

void AssetData::AddGpuBufferObject(GfxBufferHandle* bindToBuffer, const GfxBufferDesc& desc)
{
    ASSERT_MSG(!mData->objData.IsNull(), "You must SetObjData before adding buffer objects");

    AssetDataInternal::GpuObject* gpuObj = Mem::AllocZeroTyped<AssetDataInternal::GpuObject>(1, mAlloc);

    gpuObj->type = AssetDataInternal::GpuObjectType::Buffer;
    gpuObj->bufferDesc.bindToBuffer = Asset::_TranslatePointer<GfxBufferHandle>(bindToBuffer, mOrigObjPtr, mData->objData.Get());
    gpuObj->bufferDesc.size = desc.size;
    gpuObj->bufferDesc.type = desc.type;
    gpuObj->bufferDesc.usage = desc.usage;
    ASSERT(desc.content);
    ASSERT(desc.size <= UINT32_MAX);
    gpuObj->bufferDesc.content = Mem::AllocCopy<uint8>((const uint8*)desc.content, uint32(desc.size), mAlloc);
    
    if (mLastGpuObjectPtr) 
        ((AssetDataInternal::GpuObject*)mLastGpuObjectPtr)->next = gpuObj;
    else
        mData->gpuObjects = gpuObj;

    mLastGpuObjectPtr = gpuObj;

    ++mData->numGpuObjects;
}

void AssetData::SetObjData(const void* data, uint32 dataSize)
{
    mData->objData = Mem::AllocCopy<uint8>((const uint8*)data, dataSize, mAlloc);
    mOrigObjPtr = data;
}

void AssetGroup::AddToLoadQueue(const AssetParams* paramsArray, uint32 numAssets, AssetHandle* outHandles) const
{
    ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
    AssetGroupInternal& group = gAssetMan.groups.Data(mHandle);
    ASSERT_MSG(Atomic::LoadExplicit(&group.state, AtomicMemoryOrder::Acquire) == uint32(AssetGroupState::Idle), 
               "AssetGroup should only be populated while it's not loading or unloading");

    // TODO: the problem here is that while we are adding to LoadQueue, asset database can change and items are removed or added
    // So put some restrictions or management here
    for (uint32 i = 0; i < numAssets; i++) {
        const AssetParams& params = paramsArray[i];
        AssetHandleResult r = Asset::_CreateOrFetchHandle(params);
        if (r.newlyCreated)
            group.loadList.Push(r.header);
        outHandles[i] = r.handle;
    }

    group.handles.PushBatch(outHandles, numAssets);
}

AssetHandle AssetGroup::AddToLoadQueue(const AssetParams& params) const
{
    AssetHandle handle;
    AddToLoadQueue(&params, 1, &handle);
    return handle;
}

void AssetGroup::Load()
{
    MutexScope lock(gAssetMan.pendingJobsMutex);
    uint32 index = gAssetMan.pendingJobs.FindIf([handle = mHandle](const AssetJobItem& item) { return item.groupHandle == handle; });
    if (index == -1 || gAssetMan.pendingJobs[index].type == AssetJobType::Unload) {
        ReadWriteMutexReadScope groupsLock(gAssetMan.groupsMutex);
        AssetGroupInternal& group = gAssetMan.groups.Data(mHandle);

        if (!group.loadList.IsEmpty()) {
            // If we already have an unload group in the queue, remove it from the queue because we are about to load it again
            // Every Load() call should be acompanied later by an Unload() call anyways
            // This basically means that in the pending queue, the most recent command for the group, cancels the previous one
            if (index != -1 && gAssetMan.pendingJobs[index].type == AssetJobType::Unload) {
                gAssetMan.pendingJobs.RemoveAndShift(index);
            }

            AssetJobItem item {
                .type = AssetJobType::Load,
                .groupHandle = mHandle
            };
            gAssetMan.pendingJobs.PushAndSort(item, [](const AssetJobItem& a, const AssetJobItem& b) { return int(b.type) - int(a.type); });
        }            
    }
}

void AssetGroup::Unload()
{
    MutexScope lock(gAssetMan.pendingJobsMutex);
    uint32 index = gAssetMan.pendingJobs.FindIf([handle = mHandle](const AssetJobItem& item) { return item.groupHandle == handle; });

    if (index == -1 || gAssetMan.pendingJobs[index].type == AssetJobType::Load) {
        ReadWriteMutexReadScope groupsLock(gAssetMan.groupsMutex);
        AssetGroupInternal& group = gAssetMan.groups.Data(mHandle);

        if (!group.handles.IsEmpty()) {
            // If we already have a load group job in the queue, remove that one. Because we are unloading afterwards anyways
            // This basically means that in the pending queue, the most recent command for the group, cancels the previous one
            if (index != -1 && gAssetMan.pendingJobs[index].type == AssetJobType::Load) {
                gAssetMan.pendingJobs.RemoveAndShift(index);
            }

            AssetJobItem item {
                .type = AssetJobType::Unload,
                .groupHandle = mHandle
            };
            gAssetMan.pendingJobs.PushAndSort(item, [](const AssetJobItem& a, const AssetJobItem& b) { return int(b.type) - int(a.type); });
        }
    }
}

void AssetGroup::Wait()
{
    ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
    AssetGroupInternal& group = gAssetMan.groups.Data(mHandle);
    while (Atomic::LoadExplicit(&group.state, AtomicMemoryOrder::Acquire) != uint32(AssetGroupState::Loaded)) {
        OS::PauseCPU();
        if (Engine::IsMainThread())
            Asset::Update();
    }
}

bool AssetGroup::IsLoadFinished() const
{
    ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
    AssetGroupInternal& group = gAssetMan.groups.Data(mHandle);
    return Atomic::LoadExplicit(&group.state, AtomicMemoryOrder::Acquire) == uint32(AssetGroupState::Loaded);
}

bool AssetGroup::IsIdle() const
{
    ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
    AssetGroupInternal& group = gAssetMan.groups.Data(mHandle);
    return Atomic::LoadExplicit(&group.state, AtomicMemoryOrder::Acquire) == uint32(AssetGroupState::Idle);
}

AssetGroupState AssetGroup::GetState() const
{
    ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
    AssetGroupInternal& group = gAssetMan.groups.Data(mHandle);
    return (AssetGroupState)Atomic::LoadExplicit(&group.state, AtomicMemoryOrder::Acquire);
}

bool AssetGroup::HasItemsInQueue() const
{
    ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
    AssetGroupInternal& group = gAssetMan.groups.Data(mHandle);
    return !group.loadList.IsEmpty();
}

Span<AssetHandle> AssetGroup::GetAssetHandles(MemAllocator* alloc) const
{
    ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
    AssetGroupInternal& group = gAssetMan.groups.Data(mHandle);

    AssetHandle* handles = Mem::AllocCopy<AssetHandle>(group.handles.Ptr(), group.handles.Count(), alloc);
    
    return Span<AssetHandle>(handles, group.handles.Count());
}

const char* AssetData::GetMetaValue(const char* key, const char* defaultValue) const
{
    if (!mData->metaData)
        return defaultValue;

    const AssetMetaKeyValue* keyValues = mData->metaData.Get();
    uint32 count = mData->numMetaData;

    for (uint32 i = 0; i < count; i++) {
        if (keyValues[i].key.IsEqual(key))
            return keyValues[i].value.CStr();
    }

    return defaultValue;
}

void Asset::Update()
{
    PROFILE_ZONE_COLOR(PROFILE_COLOR_ASSET1);
    ASSERT_MSG(Engine::IsMainThread(), "Update can only be called in the main thread");

    if (gAssetMan.curJob && !Jobs::IsRunning(gAssetMan.curJob)) {
        Jobs::Delete(gAssetMan.curJob);
        gAssetMan.curJob = nullptr;
    }

    if (gAssetMan.curJob == nullptr) {
        // Pick a new job
        // Pending jobs are sorted in descending order, which means that higher priority jobs (see AssetJobType) are at the end of array
        // So we just need to pop one item from the end of the array
        
        MutexScope lk(gAssetMan.pendingJobsMutex);
        if (!gAssetMan.pendingJobs.IsEmpty()) {
            AssetJobItem item = gAssetMan.pendingJobs.PopLast();

            switch (item.type) {
            case AssetJobType::Load:
                gAssetMan.curJob = Jobs::Dispatch(JobsType::LongTask, Asset::_LoadGroupTask, 
                                                  IntToPtr<uint32>(item.groupHandle.mId), 1);
                break;

            case AssetJobType::Unload:
                gAssetMan.curJob = Jobs::Dispatch(JobsType::LongTask, Asset::_UnloadGroupTask, 
                                                  IntToPtr<uint32>(item.groupHandle.mId), 1);
                break;

            case AssetJobType::Server: 
            {
                // Take a batch of server tasks and run them
                AssetServer& server = gAssetMan.server;
                uint32 numLoadTasks;
                {
                    SpinLockMutexScope lock(server.pendingTasksMutex);
                    numLoadTasks = Min(limits::ASSET_SERVER_MAX_IN_FLIGHT, server.pendingTasks.Count());
                    if (numLoadTasks) {
                        for (uint32 i = 0; i < numLoadTasks; i++) 
                            server.loadTaskDatas[i] = server.pendingTasks[i];
                        server.pendingTasks.ShiftLeft(numLoadTasks);            
                    }
                }

                server.numLoadTasks = numLoadTasks;
                if (numLoadTasks)
                    gAssetMan.curJob = Jobs::Dispatch(JobsType::LongTask, _ServerLoadBatchTask);

                break;
            }
            }
        }
    }

    //------------------------------------------------------------------------------------------------------------------
    // Save hash lookup table in intervals
    if (gAssetMan.isHashLookupUpdated) {
        static uint64 lastUpdateTick = 0;
        if (lastUpdateTick == 0)
            lastUpdateTick = Timer::GetTicks();

        float secsElapsed = (float)Timer::ToSec(Timer::Diff(Timer::GetTicks(), lastUpdateTick));
        if (secsElapsed >= ASSET_SAVE_CACHE_LOOKUP_INTERVAL) {
            _SaveAssetHashLookup();
            gAssetMan.isHashLookupUpdated = false;
        }
    }
}
