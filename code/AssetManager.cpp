#include "AssetManager.h"

#include "Core/Log.h"
#include "Core/Hash.h"
#include "Core/System.h"
#include "Core/String.h"
#include "Core/JsonParser.h"
#include "Core/Settings.h"
#include "Core/Jobs.h"

#include "Engine.h"
#include "VirtualFS.h"
#include "RemoteServices.h"

#define ASSET_HASH_SEED 0x4354a

namespace _limits
{
    static constexpr uint32 kAssetMaxTypes = 8;
    static constexpr uint32 kAssetMaxAssets = 1024;
    static constexpr uint32 kAssetMaxBarriers = 32;
    static constexpr uint32 kAssetMaxGarbage = 512;
    static constexpr size_t kAssetRuntimeSize = kMB;
}

struct AssetTypeManager
{
    String32 name;
    uint32 fourcc;
    AssetLoaderCallbacks* callbacks;
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
    uint32 cacheHash;
    uint32 numMeta;
    uint32 numDepends;
    uint32 objBufferSize;
    AssetState state;
    void* obj;
    AssetLoadParams* params;    // might include extra param buffer at the end
    AssetMetaKeyValue* metaData;
    AssetDependency* depends;
    uint64 cacheFileVersion;
};

struct AssetManager
{
    MemTlsfAllocator_ThreadSafe runtimeHeap;

    Array<AssetTypeManager> typeManagers;
    HandlePool<AssetHandle, Asset> assets;  // Holds all the active asset handles (can be 'failed', 'loading' or 'alive')
    HandlePool<AssetBarrier, Signal> barriers;
    HashTable<AssetHandle> assetLookup;     // key: hash of path+params. 
                                            // This HashTable is used for looking up already loaded assets 
    
    HashTable<uint64> cacheLookupTable;     // key: Asset hash for caches (see `assetMakeCacheHash`). value: cacheFileVersion (lastModified)
    Array<AssetGarbage> garbage;
    Mutex assetsMtx;                        // Mutex used for 'assets' HandlePool (see above)

    size_t initHeapStart;
    size_t initHeapSize;

    bool initialized;
};

static AssetManager gAssetMgr;

static void assetFileChanged(const char* filepath);

static void assetSaveCacheLookup()
{
    MemTempAllocator tempAlloc;

    Blob blob(&tempAlloc);
    blob.SetGrowPolicy(Blob::GrowPolicy::Linear, 32*kKB);
    char line[1024];

    blob.Write("[\n", 2);
    for (Asset& asset : gAssetMgr.assets) {
        strPrintFmt(line, sizeof(line), 
                    "\t{\n"
                    "\t\tfilepath: \"%s\",\n"
                    "\t\tfileVersion: 0x%llx,\n"
                    "\t\thash: 0x%x\n"
                    "\t},\n",
                    asset.params->path,
                    asset.cacheFileVersion,
                    asset.cacheHash);
        blob.Write(line, strLen(line));
    }
    blob.Write("]\n", 2);

    vfsWriteFileAsync("/cache/lookup.json5", blob, VfsFlags::TextFile, 
                      [](const char* path, size_t, const Blob&, void*) { logInfo("Asset lookup cache written to: %s", path); }, nullptr);
}

static void assetLoadCacheLookup()
{
}

bool _private::assetInitialize()
{
    gAssetMgr.initialized = true;
    gAssetMgr.assetsMtx.Initialize();

    MemBudgetAllocator* initHeap = engineGetInitHeap();
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
        size_t bufferSize = MemTlsfAllocator::GetMemoryRequirement(_limits::kAssetRuntimeSize);
        gAssetMgr.runtimeHeap.Initialize(_limits::kAssetRuntimeSize, memAlloc(bufferSize, initHeap), bufferSize,
                                         settingsGetEngine().debugAllocations);
    }

    gAssetMgr.initHeapSize = initHeap->GetOffset() - gAssetMgr.initHeapStart;

    vfsRegisterFileChangeCallback(assetFileChanged);

    // Create and mount cache directory
    if constexpr (PLATFORM_WINDOWS || PLATFORM_OSX || PLATFORM_LINUX) {
        if (!pathIsDir(".cache"))
            pathCreateDir(".cache");
        vfsMountLocal(".cache", "cache", false);
    }

    return true;
}

void _private::assetDetectAndReleaseLeaks()
{
    if (gAssetMgr.initialized) {
        assetCollectGarbage();
    
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

            memFree(a.params, &gAssetMgr.runtimeHeap);
            memFree(a.depends, &gAssetMgr.runtimeHeap);
            memFree(a.metaData, &gAssetMgr.runtimeHeap);
        }

        gAssetMgr.assets.Clear();
    }
}

void _private::assetRelease()
{
    if (gAssetMgr.initialized) {
        assetCollectGarbage();
        ASSERT(gAssetMgr.assets.Count() == 0);

        gAssetMgr.assetsMtx.Release();
        gAssetMgr.runtimeHeap.Release();

        gAssetMgr.initialized = false;
    }
}

static AssetHandle assetCreateNew(uint32 typeMgrIdx, uint32 assetHash, const AssetLoadParams& params, const void* extraParams)
{
    const AssetTypeManager& typeMgr = gAssetMgr.typeManagers[typeMgrIdx];
    
    uint8* nextParams;
    BuffersAllocPOD<AssetLoadParams> paramsAlloc;
    paramsAlloc.AddMemberField<char>(offsetof(AssetLoadParams, path), kMaxPath)
               .AddExternalPointerField<uint8>(&nextParams, typeMgr.extraParamTypeSize);
    AssetLoadParams* newParams = paramsAlloc.Calloc(&gAssetMgr.runtimeHeap);
    
    strCopy(const_cast<char*>(newParams->path), kMaxPath, params.path);
    newParams->alloc = params.alloc;
    newParams->typeId = params.typeId;
    newParams->tags = params.tags;
    newParams->barrier = params.barrier;
    newParams->next = nextParams;
    if (typeMgr.extraParamTypeSize && extraParams)
        memcpy(newParams->next.Get(), extraParams, typeMgr.extraParamTypeSize);

    // Set the asset platform to the platform that we are running on in Auto mode
    if (params.platform == AssetPlatform::Auto) {
        if constexpr (PLATFORM_WINDOWS || PLATFORM_LINUX || PLATFORM_OSX)
           newParams->platform = AssetPlatform::PC;
        else if constexpr (PLATFORM_ANDROID)
           newParams->platform = AssetPlatform::Android;
       
        ASSERT(newParams->platform != AssetPlatform::Auto);
    }
    
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

static AssetResult assetLoadObjLocal(AssetHandle handle, AssetLoaderCallbacks* callbacks, const char* filepath, 
                                     const AssetLoadParams& loadParams)
{
    { MutexScope mtx(gAssetMgr.assetsMtx);
        Asset& asset = gAssetMgr.assets.Data(handle);
        AssetMetaKeyValue* keys;
        uint32 numKeys;
        
        if (asset.metaData == nullptr && 
            assetLoadMetaData(filepath, loadParams.platform, &gAssetMgr.runtimeHeap, &keys, &numKeys) && numKeys) 
        {
            asset.numMeta = numKeys;
            asset.metaData = keys;
        }
    }

    return callbacks->Load(handle, loadParams, &gAssetMgr.runtimeHeap);
}

static AssetResult assetLoadObjRemote(AssetHandle handle, AssetLoaderCallbacks* callbacks, const AssetLoadParams& loadParams)
{
    Signal waitSignal;      // Used to serialize the async code
    waitSignal.Initialize();
    
    struct AsyncLoadData
    {
        AssetResult result;
        Signal* signal;
    };

    AsyncLoadData asyncLoadData {
        .signal = &waitSignal
    };

    callbacks->LoadRemote(handle, loadParams, &asyncLoadData, [](AssetHandle, const AssetResult& result, void* userData) {
        AsyncLoadData* data = reinterpret_cast<AsyncLoadData*>(userData);
        data->result.obj = result.obj;
        if (result.numDepends) {
            ASSERT(result.depends);
            ASSERT(result.dependsBufferSize);   // Only remote loads should implement this
            data->result.depends = (AssetDependency*)memAlloc(result.dependsBufferSize, &gAssetMgr.runtimeHeap);
            memcpy(data->result.depends, result.depends, result.dependsBufferSize);
            data->result.numDepends = result.numDepends;
        }

        data->signal->Set();
        data->signal->Raise();
    });
    waitSignal.Wait();
    waitSignal.Release();

    return asyncLoadData.result;
}

bool assetLoadMetaData(const char* filepath, AssetPlatform platform, Allocator* alloc, 
                       AssetMetaKeyValue** outData, uint32* outKeyCount)
{
    auto collectKeyValues = [](JsonNode jroot, StaticArray<AssetMetaKeyValue, 64>* keys) {
        char key[32];
        char value[32];

        if (jroot.GetChildCount()) {
            JsonNode jitem = jroot.GetChildItem();
            while (jitem.IsValid()) {
                if (!jitem.IsArray() && !jitem.IsObject()) {
                    jitem.GetKey(key, sizeof(key));
                    jitem.GetValue(value, sizeof(value));
                    keys->Add(AssetMetaKeyValue { .key = key, .value = value });
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
        JsonContext jctx;
        if (jsonParse(&jctx, (const char*)blob.Data(), uint32(blob.Size()), &tmpAlloc)) {
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
            jsonDestroy(&jctx);
            memTempPopId(tempId);
            
            // At this point we have popped the current temp allocator and can safely allocate from whatever allocator is coming in
            *outData = memAllocCopy<AssetMetaKeyValue>(keys.Ptr(), keys.Count(), alloc);
            *outKeyCount = keys.Count();
            return true;
        }
        
        blob.Free();
        JsonErrorLocation loc = jsonParseGetErrorLocation(&jctx); 
        logWarning("Invalid asset meta data: %s (Json syntax error at %u:%u)", assetMetaPath.CStr(), loc.line, loc.col);
        memTempPopId(tempId);
        return false;
    }
    else {
        memTempPopId(tempId);
        return false;
    }
}

bool assetLoadMetaData(AssetHandle handle, Allocator* alloc, AssetMetaKeyValue** outData, uint32* outKeyCount)
{
    ASSERT(handle.IsValid());

    MutexScope mtx(gAssetMgr.assetsMtx);
    Asset& asset = gAssetMgr.assets.Data(handle);
    if (asset.numMeta) {
        ASSERT(asset.metaData);

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

const char* assetGetMetaValue(const AssetMetaKeyValue* data, uint32 count, const char* key)
{
    for (uint32 i = 0; i < count; i++) {
        if (data[i].key.IsEqual(key))
            return data[i].value.CStr();
    }
    return nullptr;
}

INLINE uint32 assetMakeCacheHash(const AssetTypeManager& typeMgr, const AssetLoadParams& params)
{
    // TODO: We should also hash meta data
    HashMurmur32Incremental hasher(ASSET_HASH_SEED);

    return hasher.Add<char>(params.path, strLen(params.path))
                 .AddAny(params.next.Get(), typeMgr.extraParamTypeSize)
                 .Hash();
}

static AssetResult assetLoadFromCache(const AssetTypeManager& typeMgr, const AssetLoadParams& params)
{
    // TODO: the approach is different in remote mode
    //       we have to send the modified date to the server and that will decide wither it should make a new asset and send it to us
    //       or load the asset from the local cache
    uint64 lastModifiedOriginal = vfsGetLastModified(params.path);

    Path strippedPath;
    vfsStripMountPath(strippedPath.Ptr(), sizeof(strippedPath), params.path);

    char hashStr[64];
    strPrintFmt(hashStr, sizeof(hashStr), "_%x", assetMakeCacheHash(typeMgr, params));

    Path cachePath("/cache");
    cachePath.Append(strippedPath.GetDirectory())
             .Append(strippedPath.GetFileName())
             .Append(hashStr)
             .Append(".")
             .Append(typeMgr.name.CStr());    

    MemTempAllocator tempAlloc;
    Blob cache = vfsReadFile(cachePath.CStr(), VfsFlags::None, &tempAlloc);

    if (cache.IsValid()) {
        AssetResult result {};

        // TODO: add file versioning and whatnot 
        uint64 lastModified;
        cache.Read<uint64>(&lastModified);
        if (lastModified != lastModifiedOriginal)
            return AssetResult {};      // Cache is outdated

        cache.Read<uint32>(&result.numDepends);
        cache.Read<uint32>(&result.dependsBufferSize);
        cache.Read<uint32>(&result.objBufferSize);

        if (result.dependsBufferSize) {
            result.depends = (AssetDependency*)memAlloc(result.dependsBufferSize, params.alloc);
            cache.Read(result.depends, result.dependsBufferSize);
        }

        ASSERT(result.objBufferSize);
        result.obj = memAlloc(result.objBufferSize, params.alloc);
        cache.Read(result.obj, result.objBufferSize);

        return result;
    } 
    else {
        return AssetResult {};
    }
}

static void assetSaveToCache(const AssetTypeManager& typeMgr, const AssetLoadParams& params)
{

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
    gAssetMgr.assetsMtx.Exit();

    AssetResult result;
    if (method == AssetLoadMethod::Local) 
        result = assetLoadObjLocal(handle, typeMgr.callbacks, filepath, loadParams);
    else if (method == AssetLoadMethod::Remote)
        result = assetLoadObjRemote(handle, typeMgr.callbacks, loadParams);
    else {
        ASSERT(0);
        result = AssetResult {};
    }

    MutexScope mtx(gAssetMgr.assetsMtx);
    asset = gAssetMgr.assets.Data(handle);  
    if (asset.obj != typeMgr.asyncObj)
        prevObj = asset.obj;

    // Load external asset resources
    if (result.obj && !loadParams.dontCreateResources) {
        if (!typeMgr.callbacks->InitializeResources(result.obj, loadParams)) {
            logError("Failed creating resources for %s: %s", typeMgr.name.CStr(), filepath);
            typeMgr.callbacks->Release(result.obj, loadParams.alloc);
            result.obj = nullptr;
        }
    }

    if (result.obj) {
        asset.state = AssetState::Alive;
        asset.obj = result.obj;
        asset.objBufferSize = result.objBufferSize;
        logVerbose("(load) %s: %s (%.1f ms)", typeMgr.name.CStr(), filepath, timer.ElapsedMS());
    }
    else {
        asset.state = AssetState::LoadFailed;
        asset.obj = typeMgr.failedObj;
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
            uint64 userValue = (static_cast<uint64>(handle.id)<<32) |
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
    HashMurmur32Incremental hasher(ASSET_HASH_SEED);    
    uint32 assetHash = hasher.Add<char>(params.path, strLen(params.path))
                             .Add<uint32>(&params.tags)
                             .Add<Allocator>(params.alloc)
                             .AddAny(extraParams, typeMgr.extraParamTypeSize)
                             .Hash();

    AssetHandle handle = gAssetMgr.assetLookup.FindAndFetch(assetHash, AssetHandle());
    if (handle.IsValid()) {
        MutexScope mtx(gAssetMgr.assetsMtx);
        Asset& asset = gAssetMgr.assets.Data(handle);
        ++asset.refCount;
    }
    else {
        MutexScope mtx(gAssetMgr.assetsMtx);
        handle = assetCreateNew(typeMgrIdx, assetHash, params, extraParams);
        Asset& asset = gAssetMgr.assets.Data(handle);
        asset.state = AssetState::Loading;
        asset.obj = typeMgr.asyncObj;

        if (asset.params->barrier.IsValid()) {
            Signal& sig = gAssetMgr.barriers.Data(asset.params->barrier);
            sig.Increment();
        }

        static_assert(sizeof(void*) == sizeof(uint64), "No support for 32bits in this part");
        uint64 userValue = (static_cast<uint64>(handle.id)<<32) |
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
        ASSERT_ALWAYS(asset.state == AssetState::Alive, "Asset '%s' is either failed or already released", 
            asset.params->path);
    
        if (--asset.refCount == 0) {
            AssetLoaderCallbacks* callbacks = gAssetMgr.typeManagers[asset.typeMgrIdx].callbacks;
            if (callbacks)
                callbacks->Release(asset.obj, asset.params->alloc);

            memFree(asset.params, &gAssetMgr.runtimeHeap);
            memFree(asset.depends, &gAssetMgr.runtimeHeap);
            memFree(asset.metaData, &gAssetMgr.runtimeHeap);
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

    stats->runtimeHeapSize = gAssetMgr.runtimeHeap.GetAllocatedSize();
    stats->runtimeHeapMax = _limits::kAssetRuntimeSize;

    stats->runtimeHeap = &gAssetMgr.runtimeHeap;
}

