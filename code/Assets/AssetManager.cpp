#include "AssetManager.h"

#include "../Core/Log.h"
#include "../Core/Hash.h"
#include "../Core/System.h"
#include "../Core/StringUtil.h"
#include "../Core/JsonParser.h"
#include "../Core/Settings.h"
#include "../Core/Jobs.h"

#include "../Common/VirtualFS.h"
#include "../Common/RemoteServices.h"
#include "../Common/JunkyardSettings.h"

#if PLATFORM_ANDROID
#include "../Common/Application.h"
#endif

#include "Image.h"
#include "Model.h"
#include "Shader.h"

#include "../Engine.h"

#define ASSET_HASH_SEED 0x4354a

namespace _limits
{
    static constexpr uint32 kAssetMaxTypes = 8;
    static constexpr uint32 kAssetMaxAssets = 1024;
    static constexpr uint32 kAssetMaxBarriers = 32;
    static constexpr uint32 kAssetMaxGarbage = 512;
    static constexpr size_t kAssetRuntimeSize = kMB;
}

static constexpr uint32 kAssetCacheFileId = MakeFourCC('A', 'C', 'C', 'H');
static constexpr uint32 kAssetCacheVersion = 1;
static constexpr float kAssetCacheSaveDelay = 2.0;
static constexpr const char* kAssetCacheDatabasePath = "/cache/database.json5";

struct AssetTypeManager
{
    String32 name;
    uint32 fourcc;
    AssetCallbacks* callbacks;
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
    Allocator* alloc;
};

enum class AssetLoadMethod : uint32
{
    Local,
    Remote
};

struct Asset
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
    MemThreadSafeAllocator runtimeAlloc;
    MemTlsfAllocator tlsfAlloc;

    Array<AssetTypeManager> typeManagers;
    HandlePool<AssetHandle, Asset> assets;  // Holds all the active asset handles (can be 'failed', 'loading' or 'alive')
    HandlePool<AssetBarrier, Signal> barriers;
    HashTable<AssetHandle> assetLookup;     // key: hash of the asset (path+params)
                                            // This HashTable is used for looking up already loaded assets 
    
    Array<AssetGarbage> garbage;
    uint8 _padding[8];

    Mutex assetsMtx;                        // Mutex used for 'assets' HandlePool (see above)
    Mutex hashLookupMtx;
    HashTable<uint32> hashLookup;           // Key: hash of the asset(path+params), value: cache hash

    size_t initHeapStart;
    size_t initHeapSize;

    float cacheSyncDelayTm;
    bool cacheSyncInvalidated;
    bool initialized;
};

static AssetManager gAssetMgr;

static void assetFileChanged(const char* filepath);

static AssetResult assetLoadFromCache(const AssetTypeManager& typeMgr, const AssetLoadParams& params, uint32 cacheHash, bool* outSuccess)
{
    Path strippedPath;
    vfsStripMountPath(strippedPath.Ptr(), sizeof(strippedPath), params.path);

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
    Blob cache = vfsReadFile(cachePath.CStr(), VfsFlags::None, &tempAlloc);

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
                    result.depends = (AssetDependency*)memAlloc(result.dependsBufferSize, &gAssetMgr.runtimeAlloc);
                    cache.Read(result.depends, result.dependsBufferSize);
                }
    
                ASSERT(result.objBufferSize);
                result.obj = memAlloc(result.objBufferSize, params.alloc);
                cache.Read(result.obj, result.objBufferSize);
                *outSuccess = true;
            }
        }
    }

    if (!*outSuccess)
        logError("Loading asset cache failed: %s (Source: %s)", cachePath.CStr(), params.path);

    return result;
}

static void assetSaveToCache(const AssetTypeManager& typeMgr, const AssetLoadParams& params, const AssetResult& result, uint32 assetHash)
{
    Path strippedPath;
    vfsStripMountPath(strippedPath.Ptr(), sizeof(strippedPath), params.path);

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
    
    vfsWriteFileAsync(cachePath.CStr(), cache, VfsFlags::CreateDirs, 
                      [](const char* path, size_t, const Blob&, void* user) 
    {
        logVerbose("(save) AssetCache: %s", path);
        
        uint64 userData = PtrToInt<uint64>(user);
        uint32 hash = uint32((userData >> 32)&0xffffffff);
        uint32 cacheHash = uint32(userData&0xffffffff);

        MutexScope mtx(gAssetMgr.hashLookupMtx);
        if (uint32 index = gAssetMgr.hashLookup.Find(hash); index != UINT32_MAX) 
            gAssetMgr.hashLookup.Set(index, cacheHash); // TODO: get delete the old file
        else
            gAssetMgr.hashLookup.Add(hash, cacheHash);
                         
    }, IntToPtr(userData));
}

static void assetLoadCacheHashDatabase()
{
    MemTempAllocator tempAlloc;

    Blob blob = vfsReadFile(kAssetCacheDatabasePath, VfsFlags::TextFile, &tempAlloc);
    if (blob.IsValid()) {
        char* json;
        size_t jsonSize;

        blob.Detach((void**)&json, &jsonSize);
        JsonContext* jctx = jsonParse(json, uint32(jsonSize), nullptr, &tempAlloc);
        if (jctx) {
            JsonNode jroot(jctx);

            MutexScope mtx(gAssetMgr.hashLookupMtx);
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
            jsonDestroy(jctx);

            logInfo("Loaded cache database: %s", kAssetCacheDatabasePath);
        }
    } 
}

static void assetSaveCacheHashDatabase()
{
    MemTempAllocator tempAlloc;

    Blob blob(&tempAlloc);
    blob.SetGrowPolicy(Blob::GrowPolicy::Linear, 32*kKB);
    char line[1024];
    
    blob.Write("[\n", 2);

    MutexScope mtx(gAssetMgr.hashLookupMtx);
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
    blob.Write("]\n", 2);
    
    vfsWriteFileAsync(kAssetCacheDatabasePath, blob, VfsFlags::TextFile, 
                      [](const char* path, size_t, const Blob&, void*) { logVerbose("Asset cache database saved to: %s", path); }, nullptr);

}


bool _private::assetInitialize()
{
    gAssetMgr.initialized = true;
    gAssetMgr.assetsMtx.Initialize();
    gAssetMgr.hashLookupMtx.Initialize();

    MemBumpAllocatorBase* initHeap = engineGetInitHeap();
    gAssetMgr.initHeapStart = initHeap->GetOffset();

    {
        size_t arraySize = Array<AssetTypeManager>::GetMemoryRequirement(_limits::kAssetMaxTypes);
        gAssetMgr.typeManagers.Reserve(_limits::kAssetMaxTypes, memAlloc(arraySize, initHeap), arraySize);
    }

    {
        size_t poolSize = HandlePool<AssetHandle, Asset>::GetMemoryRequirement(_limits::kAssetMaxAssets);
        gAssetMgr.assets.Reserve(_limits::kAssetMaxAssets, memAlloc(poolSize, initHeap), poolSize);
    }

    {
        size_t poolSize = HandlePool<AssetBarrier, Signal>::GetMemoryRequirement(_limits::kAssetMaxBarriers);
        gAssetMgr.barriers.Reserve(_limits::kAssetMaxBarriers, memAlloc(poolSize, initHeap), poolSize);
    }

    {
        size_t arraySize = Array<AssetGarbage>::GetMemoryRequirement(_limits::kAssetMaxGarbage);
        gAssetMgr.garbage.Reserve(_limits::kAssetMaxGarbage, memAlloc(arraySize, initHeap), arraySize);
    }

    {
        size_t tableSize = HashTable<AssetHandle>::GetMemoryRequirement(_limits::kAssetMaxAssets);
        gAssetMgr.assetLookup.Reserve(_limits::kAssetMaxAssets, memAlloc(tableSize, initHeap), tableSize);
    }

    {
        size_t tableSize = HashTable<uint32>::GetMemoryRequirement(_limits::kAssetMaxAssets);
        gAssetMgr.hashLookup.Reserve(_limits::kAssetMaxAssets, memAlloc(tableSize, initHeap), tableSize);
    }

    {
        size_t bufferSize = MemTlsfAllocator::GetMemoryRequirement(_limits::kAssetRuntimeSize);
        gAssetMgr.tlsfAlloc.Initialize(_limits::kAssetRuntimeSize, initHeap->Malloc(bufferSize), bufferSize, settingsGet().engine.debugAllocations);
        gAssetMgr.runtimeAlloc.SetAllocator(&gAssetMgr.tlsfAlloc);
    }

    gAssetMgr.initHeapSize = initHeap->GetOffset() - gAssetMgr.initHeapStart;

    vfsRegisterFileChangeCallback(assetFileChanged);

    // Create and mount cache directory
    #if PLATFORM_WINDOWS || PLATFORM_OSX || PLATFORM_LINUX
        if (!pathIsDir(".cache"))
            pathCreateDir(".cache");
        vfsMountLocal(".cache", "cache", false);
    #elif PLATFORM_ANDROID
        vfsMountLocal(sysAndroidGetCacheDirectory(appAndroidGetActivity()).CStr(), "cache", false);
    #endif

    assetLoadCacheHashDatabase();

    // Initialize asset managers here
    if (!assetInitializeImageManager()) {
        logError("Failed to initialize ImageManager");
        return false;
    }

    if (!assetInitializeModelManager()) {
        logError("Failed to initialize ModelManager");
        return false;
    }

    if (!assetInitializeShaderManager()) {
        logError("Failed to initialize ShaderManager");
        return false;
    }

    return true;
}

void _private::assetRelease()
{
    if (gAssetMgr.initialized) {
        assetCollectGarbage();

        // Detect asset leaks and release them
        for (Asset& a : gAssetMgr.assets) {
            if (a.state == AssetState::Alive) {
                logWarning("Asset '%s' (RefCount=%u) is not unloaded", a.params->path, a.refCount);
                if (a.obj) {
                    AssetTypeManager* typeMgr = &gAssetMgr.typeManagers[a.typeMgrIdx];
                    if (!typeMgr->unregistered) {
                        typeMgr->callbacks->Release(a.obj, a.params->alloc);
                    }
                }
            }

            MemSingleShotMalloc<AssetLoadParams> mallocator;
            mallocator.Free(a.params, &gAssetMgr.runtimeAlloc);

            memFree(a.depends, &gAssetMgr.runtimeAlloc);
            memFree(a.metaData, &gAssetMgr.runtimeAlloc);
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

INLINE AssetPlatform assetGetCurrentPlatform()
{
    if constexpr (PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_OSX)
           return AssetPlatform::PC;
    else if constexpr (PLATFORM_ANDROID)
           return AssetPlatform::Android;
    else {
        ASSERT(0);
        return AssetPlatform::Auto;
    }
}

static AssetHandle assetCreateNew(uint32 typeMgrIdx, uint32 assetHash, const AssetLoadParams& params, const void* extraParams)
{
    const AssetTypeManager& typeMgr = gAssetMgr.typeManagers[typeMgrIdx];
    
    uint8* nextParams;
    MemSingleShotMalloc<AssetLoadParams> mallocator;
    mallocator.AddMemberField<char>(offsetof(AssetLoadParams, path), kMaxPath)
              .AddExternalPointerField<uint8>(&nextParams, typeMgr.extraParamTypeSize);
    AssetLoadParams* newParams = mallocator.Calloc(&gAssetMgr.runtimeAlloc);
    
    strCopy(const_cast<char*>(newParams->path), kMaxPath, params.path);
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
    
    Asset asset {
        .typeMgrIdx = typeMgrIdx,
        .refCount = 1,
        .hash = assetHash,
        .state = AssetState::Zombie,
        .params = newParams
    };

    AssetHandle handle;
    Asset prevAsset;
    {
        MutexScope mtx(gAssetMgr.assetsMtx);
        handle = gAssetMgr.assets.Add(asset, &prevAsset);
    }
    ASSERT(!prevAsset.params);
    ASSERT(!prevAsset.metaData);

    gAssetMgr.assetLookup.Add(assetHash, handle);
    return handle;
}

uint32 assetMakeCacheHash(const AssetCacheDesc& desc)
{
    HashMurmur32Incremental hasher(ASSET_HASH_SEED);

    return hasher.Add<char>(desc.filepath, strLen(desc.filepath))
                 .AddAny(desc.loadParams, desc.loadParamsSize)
                 .AddAny(desc.metaData, sizeof(AssetMetaKeyValue)*desc.numMeta)
                 .Add<uint64>(&desc.lastModified)
                 .Hash();
}

static AssetResult assetLoadObjLocal(AssetHandle handle, const AssetTypeManager& typeMgr, const AssetLoadParams& loadParams, uint32 hash,
                                     bool* outLoadedFromCache)
{
    uint32 cacheHash;
    {
        MutexScope mtx(gAssetMgr.hashLookupMtx);
        cacheHash = gAssetMgr.hashLookup.FindAndFetch(hash, 0);
    }

    bool cacheOnly = settingsGet().engine.useCacheOnly;
    AssetResult result {};
    if (!cacheOnly)
        result = typeMgr.callbacks->Load(handle, loadParams, cacheHash, &gAssetMgr.runtimeAlloc);
    bool loadFromCache = (cacheOnly && cacheHash != 0) || (result.cacheHash == cacheHash);
    if (loadFromCache) {
        ASSERT(result.obj == nullptr);
        result = assetLoadFromCache(typeMgr, loadParams, cacheHash, outLoadedFromCache);

        if (!*outLoadedFromCache) {
            MutexScope mtx(gAssetMgr.hashLookupMtx);
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
        MutexScope mtx(gAssetMgr.hashLookupMtx);
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
                MutexScope mtx(gAssetMgr.hashLookupMtx);
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
            params->result.depends = (AssetDependency*)memAlloc(result.dependsBufferSize, &gAssetMgr.runtimeAlloc);
            memcpy(params->result.depends, result.depends, result.dependsBufferSize);
            params->result.numDepends = result.numDepends;
        }

        params->signal->Set();
        params->signal->Raise();
    });
    waitSignal.Wait();

    return asyncLoadData.result;
}

bool assetLoadMetaData(const char* filepath, AssetPlatform platform, Allocator* alloc, AssetMetaKeyValue** outData, uint32* outKeyCount)
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
    
    uint32 tempId = memTempPushId();
    MemTempAllocator tmpAlloc(tempId);

    Blob blob = vfsReadFile(assetMetaPath.CStr(), VfsFlags::TextFile, &tmpAlloc);
    if (blob.IsValid()) {
        JsonErrorLocation loc;
        JsonContext* jctx = jsonParse((const char*)blob.Data(), uint32(blob.Size()), &loc, &tmpAlloc);
        if (jctx) {
            JsonNode jroot(jctx);
            StaticArray<AssetMetaKeyValue, 64> keys;

            collectKeyValues(jroot, &keys);

            // Collect platform-specific keys
            JsonNode jplatform;
            switch (platform) {
            case AssetPlatform::PC:         jplatform = jroot.GetChild("pc");       break;
            case AssetPlatform::Android:    jplatform = jroot.GetChild("android");  break;
            default:                        break;
            }
            if (jplatform.IsValid())
                collectKeyValues(jplatform, &keys);

            blob.Free();
            jsonDestroy(jctx);
            memTempPopId(tempId);
            
            // At this point we have popped the current temp allocator and can safely allocate from whatever allocator is coming in
            *outData = memAllocCopy<AssetMetaKeyValue>(keys.Ptr(), keys.Count(), alloc);
            *outKeyCount = keys.Count();

            return true;
        }
        else {
            *outData = nullptr;
            *outKeyCount = 0;
        
            blob.Free();
            logWarning("Invalid asset meta data: %s (Json syntax error at %u:%u)", assetMetaPath.CStr(), loc.line, loc.col);
            memTempPopId(tempId);
            return false;
        }
    }
    else {
        *outData = nullptr;
        *outKeyCount = 0;

        memTempPopId(tempId);
        return false;
    }
}

// Note: This version of LoadMetaData (provide local asset handle instead of filepath), Allocates asset's meta-data from runtimeHeap
bool assetLoadMetaData(AssetHandle handle, Allocator* alloc, AssetMetaKeyValue** outData, uint32* outKeyCount)
{
    ASSERT(handle.IsValid());

    MutexScope mtx(gAssetMgr.assetsMtx);
    Asset& asset = gAssetMgr.assets.Data(handle);
    if (asset.numMeta && asset.metaData) {
        *outData = memAllocCopy<AssetMetaKeyValue>(asset.metaData, asset.numMeta, alloc);
        *outKeyCount = asset.numMeta;
        return true;
    }
    else {
        if (assetLoadMetaData(asset.params->path, assetGetCurrentPlatform(), &gAssetMgr.runtimeAlloc, &asset.metaData, &asset.numMeta)) {
            *outData = memAllocCopy<AssetMetaKeyValue>(asset.metaData, asset.numMeta, alloc);
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

// Runs from worker thread
static void assetLoadTask(uint32 groupIndex, void* userData)
{
    UNUSED(groupIndex);
    void* prevObj = nullptr;
    uint64 userValue = PtrToInt<uint64>(userData);
    AssetLoadMethod method = AssetLoadMethod(userValue & 0xffffffff);
    AssetHandle handle { uint32(userValue >> 32) };
    TimerStopWatch timer;

    gAssetMgr.assetsMtx.Enter();
    Asset& asset = gAssetMgr.assets.Data(handle);
    const char* filepath = asset.params->path;
    const AssetTypeManager& typeMgr = gAssetMgr.typeManagers[asset.typeMgrIdx];
    const AssetLoadParams& loadParams = *asset.params;
    uint32 hash = asset.hash;
    gAssetMgr.assetsMtx.Exit();

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

    MutexScope mtx(gAssetMgr.assetsMtx);
    asset = gAssetMgr.assets.Data(handle);  
    if (asset.obj != typeMgr.asyncObj)
        prevObj = asset.obj;

    if (result.obj) {
        asset.state = AssetState::Alive;
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
            if (!typeMgr.callbacks->InitializeSystemResources(result.obj, loadParams)) {
                logError("Failed creating resources for %s: %s", typeMgr.name.CStr(), filepath);
                typeMgr.callbacks->Release(result.obj, loadParams.alloc);
                result.obj = nullptr;
            }
        }
    }

    if (!result.obj) {
        asset.state = AssetState::LoadFailed;
        asset.obj = typeMgr.failedObj;
    }
    else {
        logVerbose("(load) %s: %s (%.1f ms)%s", typeMgr.name.CStr(), filepath, timer.ElapsedMS(), loadedFromCache ? " [cached]" : "");
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
            logWarning("Asset '%s' cannot get reloaded", filepath);
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

static void assetFileChanged(const char* filepath)
{
    MutexScope mtx(gAssetMgr.assetsMtx);
    for (uint32 i = 0; i < gAssetMgr.assets.Count(); i++) {
        AssetHandle handle = gAssetMgr.assets.HandleAt(i);
        Asset& asset = gAssetMgr.assets.Data(handle);

        const char* assetPath = asset.params->path;
        if (assetPath[0] == '/')
            ++assetPath;
        if (strIsEqualNoCase(filepath, assetPath)) {
            uint64 userValue = (static_cast<uint64>(uint32(handle))<<32) |
                ((remoteIsConnected() ? (uint64)AssetLoadMethod::Remote : (uint64)AssetLoadMethod::Local) & 0xffffffff);
            jobsDispatchAuto(JobsType::LongTask, assetLoadTask, IntToPtr<uint64>(userValue));
        }
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
        logWarning("Extra parameters not provided for asset type '%s'. Set extra parameters in 'next' field with the type of '%s'",
            typeMgr.name.CStr(), typeMgr.extraParamTypeName.CStr());
        ASSERT_MSG(false, "AssetLoadParams.next must not be nullptr for this type of asset (%s)", typeMgr.name.CStr());
        return AssetHandle();
    }

    // check if asset is already loaded
    // Calculate asset hash. This is different from "Cache" hash. Asset hash should always retain it's value for each unique asset
    // Unique assets are also defined by their extra custom init params 
    HashMurmur32Incremental hasher(ASSET_HASH_SEED);
    uint32 assetHash = hasher.Add<char>(params.path, strLen(params.path))
                             .Add<uint32>(&params.tags)
                             .AddAny(extraParams, typeMgr.extraParamTypeSize)
                             .Hash();

    AssetHandle handle = gAssetMgr.assetLookup.FindAndFetch(assetHash, AssetHandle());
    if (handle.IsValid()) {
        MutexScope mtx(gAssetMgr.assetsMtx);
        Asset& asset = gAssetMgr.assets.Data(handle);
        ++asset.refCount;
    }
    else {
        handle = assetCreateNew(typeMgrIdx, assetHash, params, extraParams);
        
        MutexScope mtx(gAssetMgr.assetsMtx);
        Asset& asset = gAssetMgr.assets.Data(handle);
        asset.state = AssetState::Loading;
        asset.obj = typeMgr.asyncObj;

        if (asset.params->barrier.IsValid()) {
            Signal& sig = gAssetMgr.barriers.Data(asset.params->barrier);
            sig.Increment();
        }

        static_assert(sizeof(void*) == sizeof(uint64), "No support for 32bits in this part");
        uint64 userValue = (static_cast<uint64>(uint32(handle))<<32) |
                           ((remoteIsConnected() ? (uint64)AssetLoadMethod::Remote : (uint64)AssetLoadMethod::Local) & 0xffffffff);
        jobsDispatchAuto(JobsType::LongTask, assetLoadTask, IntToPtr<uint64>(userValue));
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
        MutexScope mtx(gAssetMgr.assetsMtx);
        Asset& asset = gAssetMgr.assets.Data(handle);

        if (asset.state != AssetState::Alive) {
            logWarning("Asset is either failed or already released: %s", asset.params->path);
            return;
        }
    
        if (--asset.refCount == 0) {
            AssetCallbacks* callbacks = gAssetMgr.typeManagers[asset.typeMgrIdx].callbacks;
            if (callbacks)
                callbacks->Release(asset.obj, asset.params->alloc);

            memFree(asset.params, &gAssetMgr.runtimeAlloc);
            memFree(asset.depends, &gAssetMgr.runtimeAlloc);
            memFree(asset.metaData, &gAssetMgr.runtimeAlloc);
            asset.params = nullptr;
            asset.metaData = nullptr;
            asset.depends = nullptr;
    
            gAssetMgr.assetLookup.FindAndRemove(asset.hash);
            gAssetMgr.assets.Remove(handle);
        }
    }
}

void* _private::assetGetData(AssetHandle handle)
{
    ASSERT(gAssetMgr.initialized);
    
    MutexScope mtx(gAssetMgr.assetsMtx);
    Asset& asset = gAssetMgr.assets.Data(handle);
    return asset.obj;
}

void* _private::assetGetDataUnsafe(AssetHandle handle)
{
    ASSERT(gAssetMgr.initialized);

    Asset& asset = gAssetMgr.assets.Data(handle);
    return asset.obj;
}

AssetInfo assetGetInfo(AssetHandle handle)
{
    ASSERT(gAssetMgr.initialized);
    ASSERT(handle.IsValid());

    MutexScope mtx(gAssetMgr.assetsMtx);
    Asset& asset = gAssetMgr.assets.Data(handle);

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

    MutexScope mtx(gAssetMgr.assetsMtx);
    Asset& asset = gAssetMgr.assets.Data(handle);
    return asset.state == AssetState::Alive;
}

AssetHandle assetAddRef(AssetHandle handle)
{
    ASSERT(gAssetMgr.initialized);
    ASSERT(handle.IsValid());

    MutexScope mtx(gAssetMgr.assetsMtx);
    Asset& asset = gAssetMgr.assets.Data(handle);
    ++asset.refCount;
    return handle;
}

void assetRegister(const AssetTypeDesc& desc)
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
        .extraParamTypeSize = desc.extraParamTypeSize,
        .extraParamTypeName = desc.extraParamTypeName,
        .failedObj = desc.failedObj,
        .asyncObj = desc.asyncObj,
    });
}

void assetUnregister(uint32 fourcc)
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

void assetCollectGarbage()
{
    MutexScope mtx(gAssetMgr.assetsMtx);
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

void _private::assetUpdateCache(float dt)
{
    if (gAssetMgr.cacheSyncInvalidated) {
        gAssetMgr.cacheSyncDelayTm += dt;
        if (gAssetMgr.cacheSyncDelayTm >= kAssetCacheSaveDelay)  {
            gAssetMgr.cacheSyncDelayTm = 0;
            gAssetMgr.cacheSyncInvalidated = false;
            jobsDispatchAuto(JobsType::LongTask, [](uint32, void*) { assetSaveCacheHashDatabase(); });
        }            
    }
}
