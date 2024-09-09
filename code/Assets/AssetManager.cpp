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
            if (a.state == AssetState::Alive) {
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

        if (asset.state != AssetState::Alive) {
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
    return asset.state == AssetState::Alive;
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
    inline constexpr uint32 ASSET_MAX_GROUPS = 1024;
    inline constexpr uint32 ASSET_MAX_THREADS = 128;
    inline constexpr size_t ASSET_MAX_SCRATCH_SIZE_PER_THREAD = SIZE_GB;
    inline constexpr size_t ASSET_HEADER_BUFFER_POOL_SIZE = SIZE_MB;
    inline constexpr size_t ASSET_DATA_BUFFER_POOL_SIZE = SIZE_MB*128;
}

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
        RelativePtr<GfxBuffer> bindToBuffer;
        uint32 size;
        GfxBufferType type;
        GfxBufferUsage usage;
        RelativePtr<uint8> content;
    };

    struct GpuTextureDesc
    {
        RelativePtr<GfxImage> bindToImage;
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
    AssetParams* params;
    AssetDataInternal* data;
    const char* typeName;
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
    AssetDataHeader* header;
    Path bakedFilepath;
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
    AssetDataInternal* data;
    Path bakedFilepath;
    bool saveBaked;
};

using AssetJobItem = Pair<JobsHandle, AssetGroupHandle>;

struct AssetMan
{
    ReadWriteMutex assetMutex;
    ReadWriteMutex groupsMutex;
    Mutex pendingLoadsMutex;
    Mutex pendingUnloadsMutex;
    Mutex dataAllocMutex;

    AssetScratchMemArena memArena;
    MemBumpAllocatorVM tempAlloc;   // Used in LoadAssetGroup, because we cannot use regular temp allocators due to dispatching

    HandlePool<AssetHandle, AssetDataHeader*> assetDb;
    HashTable<AssetHandle> assetLookup;     // Key: AssetParams hash. To check for availibility
    MemTlsfAllocator assetHeaderAlloc;
    MemTlsfAllocator assetDataAlloc;

    HandlePool<AssetGroupHandle, AssetGroupInternal> groups;

    Array<AssetGroupHandle> pendingLoads;
    Array<AssetGroupHandle> pendingUnloads;
    AssetJobItem loadJob;
    AssetJobItem unloadJob;

    AtomicUint32 state;     // Type is actually AssetManState
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
    static bool _MakeCacheFilepath(Path* outPath, const AssetDataHeader* header, uint32 overrideAssetHash = 0);
} // Asset

static void Asset::_SaveBakedTask(uint32 groupIdx, void* userData)
{
    PROFILE_ZONE_COLOR(PROFILE_COLOR_ASSET3);

    AssetQueuedItem* qa = ((AssetQueuedItem**)userData)[groupIdx];

    // MemTempAllocator tempAlloc;
    Blob cache;
    cache.Reserve(32 + qa->dataSize);
    cache.SetGrowPolicy(Blob::GrowPolicy::Linear);
    cache.Write<uint32>(kAssetCacheFileId);
    cache.Write<uint32>(kAssetCacheVersion);
    cache.Write<uint32>(qa->dataSize);
    cache.Write(qa->data, qa->dataSize);

    uint64 writeFileData = 0;//(uint64(assetHash) << 32) | result.cacheHash;
    // Vfs::WriteFile(qa->bakedFilepath.CStr(), cache, VfsFlags::CreateDirs);
    auto SaveFileCallback = [](const char* path, size_t bytesWritten, Blob& blob, void*)
    {
        if (bytesWritten)
            LOG_VERBOSE("(save) Baked: %s", path);
        blob.Free();
        
        /*
            uint64 writeFileData = PtrToInt<uint64>(user);
            uint32 hash = uint32((writeFileData >> 32)&0xffffffff);
            uint32 cacheHash = uint32(writeFileData&0xffffffff);
        */
    };

    Vfs::WriteFileAsync(qa->bakedFilepath.CStr(), cache, VfsFlags::CreateDirs|VfsFlags::NoCopyWriteBlob, 
                        SaveFileCallback, IntToPtr(writeFileData));
}

static bool Asset::_MakeCacheFilepath(Path* outPath, const AssetDataHeader* header, uint32 overrideAssetHash)
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
            return false;
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
    return true;
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

    AssetHandleResult r {};

    HashMurmur32Incremental hasher;
    hasher.Add<uint32>(params.typeId);
    hasher.AddAny(params.path.CStr(), params.path.Length());
    hasher.Add<uint32>(uint32(params.platform));
    hasher.AddAny(params.typeSpecificParams, typeMan.extraParamTypeSize);
    r.paramsHash = hasher.Hash();

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
    AssetLoadTaskData& taskData = ((AssetLoadTaskData*)userData)[groupIdx];
    const AssetParams& params = *taskData.inputs.header->params;
    uint32 typeManIdx = gAssetMgr.typeManagers.FindIf([typeId = params.typeId](const AssetTypeManager& typeMan) { return typeMan.fourcc == typeId; });
    ASSERT_MSG(typeManIdx != UINT32_MAX, "AssetType with FourCC %x is not registered", params.typeId);
    const AssetTypeManager& typeMan = gAssetMgr.typeManagers[typeManIdx];

    PROFILE_ZONE_COLOR(PROFILE_COLOR_ASSET2);

    MemBumpAllocatorVM* alloc = _GetOrCreateScratchAllocator(gAssetMan.memArena);

    #if !ASSET_ASYNC_EXPERIMENT
    MemTempAllocator tempAlloc;
    #endif

    if (taskData.inputs.type == AssetLoadTaskInputType::Source) {
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
        Blob fileBlob = Vfs::ReadFile(taskData.inputs.bakedFilepath.CStr(), VfsFlags::None, &tempAlloc);
        const void* fileData = fileBlob.Data();
        ASSERT(fileBlob.Size() <= UINT32_MAX);
        uint32 fileSize = uint32(fileBlob.Size());
        #endif

        if (!fileData) {
            taskData.outputs.errorDesc = "Failed opening baked file";
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
        
            GfxBuffer buffer = gfxCreateBuffer(desc);
            if (buffer.IsValid()) {
                GfxBuffer* targetBuffer = gpuObj->bufferDesc.bindToBuffer.Get();
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

            GfxImage image = gfxCreateImage(desc);
            if (image.IsValid()) {
                GfxImage* targetImage = gpuObj->textureDesc.bindToImage.Get();
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
    uint32 batchCount = Min(128u, loadList.Count());//Jobs::GetWorkerThreadsCount(JobsType::LongTask);
    uint32 allCount = loadList.Count();

    // We cannot use the actual temp allocators here. Because we are dispatching jobs and might end up in a different thread
    MemBumpAllocatorVM* tempAlloc = &gAssetMan.tempAlloc;
    Array<AssetQueuedItem> queuedAssets;
    queuedAssets.Reserve(loadList.Count());

    for (uint32 i = 0; i < loadList.Count(); i += batchCount) {
        uint32 sliceCount = Min(batchCount, loadList.Count() - i);

        AssetLoadTaskData* taskDatas = nullptr;
        taskDatas = Mem::AllocZeroTyped<AssetLoadTaskData>(sliceCount, tempAlloc);
        for (uint32 k = 0; k < sliceCount; k++) {
            AssetDataHeader* header = loadList[i + k];

            // TODO: this part will not run on remote connection mode
            //       We will always have Baked data for remote connections

            // Returns false if source file could not be found
            // Decide if baked file exists and we should skip loading from source
            if (_MakeCacheFilepath(&taskDatas[k].inputs.bakedFilepath, header))
                taskDatas[k].inputs.type = Vfs::FileExists(taskDatas[k].inputs.bakedFilepath.CStr()) ? AssetLoadTaskInputType::Baked : AssetLoadTaskInputType::Source;

            taskDatas[k].inputs.groupHandle = groupHandle;
            taskDatas[k].inputs.header = header;
        }

        JobsHandle batchJob = Jobs::Dispatch(JobsType::LongTask, _LoadAssetTask, taskDatas, sliceCount, JobsPriority::High);

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

        Jobs::WaitForCompletion(batchJob);

        for (uint32 k = 0; k < sliceCount; k++) {
            AssetLoadTaskInputs& in = taskDatas[k].inputs;
            AssetLoadTaskOutputs& out = taskDatas[k].outputs;
            if (!out.data) {
                LOG_ERROR("Loading asset '%s' failed: %s", in.header->params->path.CStr(), out.errorDesc.CStr());
                continue;
            }

            AssetQueuedItem qa {
                .indexInLoadList = i + k,
                .dataSize = out.dataSize,
                .data = out.data,
                .bakedFilepath = in.bakedFilepath,
                .saveBaked = in.type == AssetLoadTaskInputType::Source
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
        } // gather deps for each loaded asset

        tempAlloc->Reset();
    }

    // Save to cache
    Array<AssetQueuedItem*> saveItems(tempAlloc);
    for (uint32 i = 0; i < queuedAssets.Count(); i += batchCount) {
        uint32 sliceCount = Min(batchCount, loadList.Count() - i);

        for (uint32 k = 0; k < sliceCount; k++) {
            AssetQueuedItem& qa = queuedAssets[i + k];
            if (!qa.saveBaked)
                continue;
            saveItems.Push(&qa);
        }
        
        if (!saveItems.IsEmpty()) {
            JobsHandle batchJob = Jobs::Dispatch(JobsType::LongTask, _SaveBakedTask, saveItems.Ptr(), saveItems.Count());
            Jobs::WaitForCompletion(batchJob);
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
            Jobs::WaitForCompletion(batchJob);
        }

        tempAlloc->Reset();
    }
    
    // Strip unwanted stuff and save it to persistant memory
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
        Jobs::WaitForCompletion(batchJob);

        tempAlloc->Reset();
    }

    queuedAssets.Free();
    loadList.Free();

    // Reset area allocators, getting ready for the next group
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
    gAssetMan.pendingLoadsMutex.Initialize();
    gAssetMan.pendingUnloadsMutex.Initialize();
    gAssetMan.dataAllocMutex.Initialize();

    // TODO: set allocator (initHeap) for all main objects
    MemAllocator* alloc = Mem::GetDefaultAlloc();

    gAssetMan.assetDb.SetAllocator(alloc);
    gAssetMan.assetLookup.SetAllocator(alloc);
    gAssetMan.assetLookup.Reserve(512);
    gAssetMan.assetHeaderAlloc.Initialize(alloc, limits::ASSET_HEADER_BUFFER_POOL_SIZE, false);
    gAssetMan.assetDataAlloc.Initialize(alloc, limits::ASSET_DATA_BUFFER_POOL_SIZE, false);
    gAssetMan.groups.SetAllocator(alloc);
    gAssetMan.pendingLoads.SetAllocator(alloc);
    gAssetMan.pendingUnloads.SetAllocator(alloc);

    gAssetMan.memArena.maxAllocators = Jobs::GetWorkerThreadsCount(JobsType::LongTask);
    gAssetMan.memArena.allocators = NEW_ARRAY(alloc, MemBumpAllocatorVM, gAssetMan.memArena.maxAllocators);
    gAssetMan.memArena.threadToAllocatorTable.SetAllocator(alloc);
    gAssetMan.memArena.threadToAllocatorTable.Reserve(gAssetMan.memArena.maxAllocators);

    gAssetMan.tempAlloc.Initialize(SIZE_GB, 4*SIZE_MB);

    return true;
}

void Asset::Release()
{
    // Perform all pending unload jobs to clean stuff up
    {
        JobsHandle& loadJob = gAssetMan.loadJob.first;
        JobsHandle& unloadJob = gAssetMan.unloadJob.first;

        if (loadJob && Jobs::IsRunning(loadJob)) 
            Jobs::WaitForCompletion(loadJob);

        if (unloadJob && Jobs::IsRunning(unloadJob))
            Jobs::WaitForCompletion(unloadJob);

        auto PickUnloadJob = []()->AssetGroupHandle
        {
            MutexScope lock(gAssetMan.pendingUnloadsMutex);
            if (!gAssetMan.pendingUnloads.IsEmpty()) {
                AssetGroupHandle handle = gAssetMan.pendingUnloads[0];
                gAssetMan.pendingUnloads.RemoveAndShift(0);
                return handle;
            }

            return AssetGroupHandle();
        };

        AssetGroupHandle unloadJobGroupHandle = PickUnloadJob();
        while (unloadJobGroupHandle.IsValid()) {
            unloadJob = Jobs::Dispatch(JobsType::LongTask, Asset::_UnloadGroupTask, IntToPtr<uint32>(unloadJobGroupHandle.mId), 1);
            Jobs::WaitForCompletion(unloadJob);
            unloadJobGroupHandle = PickUnloadJob();
        }
    }

    MemAllocator* alloc = Mem::GetDefaultAlloc();

    for (uint32 i = 0; i < gAssetMan.memArena.numAllocators; i++) {
        if (gAssetMan.memArena.allocators[i].IsInitialized())
            gAssetMan.memArena.allocators[i].Release();
    }
    Mem::Free(gAssetMan.memArena.allocators, alloc);
    gAssetMan.memArena.threadToAllocatorTable.Free();
    gAssetMan.tempAlloc.Release();

    gAssetMan.groups.Free();
    gAssetMan.assetDb.Free();
    gAssetMan.assetLookup.Free();
    gAssetMan.pendingUnloads.Free();
    gAssetMan.pendingLoads.Free();

    gAssetMan.assetHeaderAlloc.Release();
    gAssetMan.assetDataAlloc.Release();

    gAssetMan.dataAllocMutex.Release();
    gAssetMan.assetMutex.Release();
    gAssetMan.groupsMutex.Release();
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
    
    // Wait for any tasks to finish
    if (gAssetMan.loadJob.second == group.mHandle) {
        Jobs::WaitForCompletion(gAssetMan.loadJob.first);
        gAssetMan.loadJob = AssetJobItem(nullptr, AssetGroupHandle());
    }

    if (gAssetMan.unloadJob.second == group.mHandle) {
        Jobs::WaitForCompletion(gAssetMan.unloadJob.first);
        gAssetMan.unloadJob = AssetJobItem(nullptr, AssetGroupHandle());
    }

    // Remove from pending lists
    {
        MutexScope lock(gAssetMan.pendingLoadsMutex);
        uint32 index = gAssetMan.pendingLoads.Find(group.mHandle);
        if (index != -1)
            gAssetMan.pendingLoads.RemoveAndShift(index);
    }

    {
        MutexScope lock(gAssetMan.pendingUnloadsMutex);
        uint32 index = gAssetMan.pendingUnloads.Find(group.mHandle);
        if (index != -1)
            gAssetMan.pendingUnloads.RemoveAndShift(index);
    }

    {
        ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
        AssetGroupInternal& gi = gAssetMan.groups.Data(group.mHandle);

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

void AssetData::AddGpuTextureObject(GfxImage* bindToImage, const GfxImageDesc& desc)
{
    ASSERT_MSG(!mData->objData.IsNull(), "You must SetObjData before adding texture objects");

    AssetDataInternal::GpuObject* gpuObj = Mem::AllocZeroTyped<AssetDataInternal::GpuObject>(1, mAlloc);

    gpuObj->type = AssetDataInternal::GpuObjectType::Texture;
    gpuObj->textureDesc.bindToImage = Asset::_TranslatePointer<GfxImage>(bindToImage, mOrigObjPtr, mData->objData.Get());
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

void AssetData::AddGpuBufferObject(GfxBuffer* bindToBuffer, const GfxBufferDesc& desc)
{
    ASSERT_MSG(!mData->objData.IsNull(), "You must SetObjData before adding buffer objects");

    AssetDataInternal::GpuObject* gpuObj = Mem::AllocZeroTyped<AssetDataInternal::GpuObject>(1, mAlloc);

    gpuObj->type = AssetDataInternal::GpuObjectType::Buffer;
    gpuObj->bufferDesc.bindToBuffer = Asset::_TranslatePointer<GfxBuffer>(bindToBuffer, mOrigObjPtr, mData->objData.Get());
    gpuObj->bufferDesc.size = desc.size;
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
    MutexScope lock(gAssetMan.pendingLoadsMutex);
    uint32 index = gAssetMan.pendingLoads.Find(mHandle);
    if (index == -1) {
        ReadWriteMutexReadScope groupsLock(gAssetMan.groupsMutex);
        AssetGroupInternal& group = gAssetMan.groups.Data(mHandle);

        if (!group.loadList.IsEmpty())
            gAssetMan.pendingLoads.Push(mHandle);
    }
}

void AssetGroup::Unload()
{
    MutexScope lock(gAssetMan.pendingUnloadsMutex);
    uint32 index = gAssetMan.pendingUnloads.Find(mHandle);
    if (index == -1) {
        ReadWriteMutexReadScope groupsLock(gAssetMan.groupsMutex);
        AssetGroupInternal& group = gAssetMan.groups.Data(mHandle);

        if (!group.handles.IsEmpty())
            gAssetMan.pendingUnloads.Push(mHandle);
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

    JobsHandle& loadJob = gAssetMan.loadJob.first;
    AssetGroupHandle& loadJobGroupHandle = gAssetMan.loadJob.second;

    JobsHandle& unloadJob = gAssetMan.unloadJob.first;
    AssetGroupHandle& unloadJobGroupHandle = gAssetMan.unloadJob.second;

    if (loadJob && !Jobs::IsRunning(loadJob)) {
        Jobs::WaitForCompletion(loadJob);
        gAssetMan.loadJob = AssetJobItem(nullptr, AssetGroupHandle());        
    }

    if (unloadJob && !Jobs::IsRunning(unloadJob)) {
        Jobs::WaitForCompletion(unloadJob);
        gAssetMan.unloadJob = AssetJobItem(nullptr, AssetGroupHandle());
    }

    auto PickLoadJob = []()->AssetGroupHandle
    {
        MutexScope lock(gAssetMan.pendingLoadsMutex);
        if (!gAssetMan.pendingLoads.IsEmpty()) {
            AssetGroupHandle handle = gAssetMan.pendingLoads[0];
            gAssetMan.pendingLoads.RemoveAndShift(0);
            return handle;
        }

        return AssetGroupHandle();
    };

    auto PickUnloadJob = []()->AssetGroupHandle
    {
        MutexScope lock(gAssetMan.pendingUnloadsMutex);
        if (!gAssetMan.pendingUnloads.IsEmpty()) {
            AssetGroupHandle handle = gAssetMan.pendingUnloads[0];
            gAssetMan.pendingUnloads.RemoveAndShift(0);
            return handle;
        }

        return AssetGroupHandle();
    };

    // Pick a load job if no job is in progress
    // Note: we don't pick any jobs when we are releasing the AssetMan
    if (!loadJob && !unloadJob) {
        loadJobGroupHandle = PickLoadJob();
        if (loadJobGroupHandle.IsValid())
            loadJob = Jobs::Dispatch(JobsType::LongTask, Asset::_LoadGroupTask, IntToPtr<uint32>(loadJobGroupHandle.mId), 1);
    }
    
    // Pick an unload job if no job is in progress
    // When we are releasing the AssetMan, perform all unload jobs and wait for them to finish
    if (!loadJob && !unloadJob) {
        unloadJobGroupHandle = PickUnloadJob();
        if (unloadJobGroupHandle.IsValid()) 
            unloadJob = Jobs::Dispatch(JobsType::LongTask, Asset::_UnloadGroupTask, IntToPtr<uint32>(unloadJobGroupHandle.mId), 1);
    }
}
