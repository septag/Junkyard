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

#include "../Graphics/GfxBackend.h"
#include "../Engine.h"

#include "Image.h"
#include "Model.h"
#include "Shader.h"

#define ASSET_ASYNC_EXPERIMENT 0

//     ██████╗ ██╗      ██████╗ ██████╗  █████╗ ██╗     ███████╗
//    ██╔════╝ ██║     ██╔═══██╗██╔══██╗██╔══██╗██║     ██╔════╝
//    ██║  ███╗██║     ██║   ██║██████╔╝███████║██║     ███████╗
//    ██║   ██║██║     ██║   ██║██╔══██╗██╔══██║██║     ╚════██║
//    ╚██████╔╝███████╗╚██████╔╝██████╔╝██║  ██║███████╗███████║
//     ╚═════╝ ╚══════╝ ╚═════╝ ╚═════╝ ╚═╝  ╚═╝╚══════╝╚══════╝
static inline constexpr uint32 ASSET_CACHE_FILE_ID = MakeFourCC('A', 'C', 'C', 'H');
static inline constexpr uint32 ASSET_CACHE_VERSION = 1;
static inline constexpr uint32 ASSET_LOAD_ASSET_REMOTE_CMD = MakeFourCC('L', 'D', 'A', 'S');
static inline constexpr float ASSET_SAVE_CACHE_LOOKUP_INTERVAL = 1.0f;
static inline constexpr float ASSET_HOT_RELOAD_INTERVAL = 0.2f;
static inline constexpr size_t ASSET_MAX_SCRATCH_SIZE_PER_THREAD = SIZE_GB;
static inline constexpr size_t ASSET_HEADER_BUFFER_POOL_SIZE = SIZE_MB;
static inline constexpr size_t ASSET_DATA_BUFFER_POOL_SIZE = SIZE_MB*128;
static inline constexpr uint32 ASSET_SERVER_MAX_IN_FLIGHT = 128;
static inline constexpr uint32 ASSET_HOT_RELOAD_MAX_IN_FLIGHT = 128;
static inline constexpr uint32 ASSET_MAX_TRANSFER_SIZE_PER_FRAME = 50*SIZE_MB;  

struct AssetTypeManager
{
    String32 name;
    uint32 fourcc;
    uint32 cacheVersion;
    AssetTypeImplBase* impl;
    uint32 extraParamTypeSize;
    String32 extraParamTypeName;
    void* failedObj;
    void* asyncObj;
    bool unregistered;
};

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
        Image
    };

    struct GpuBufferDesc
    {
        RelativePtr<GfxBufferHandle> bindToBuffer;
        GfxBufferDesc createDesc;
        RelativePtr<uint8> content;
    };

    struct GpuImageDesc
    {
        RelativePtr<GfxImageHandle> bindToImage;
        GfxImageDesc createDesc;
        RelativePtr<uint8> content;
        uint32 contentSize;
    };

    struct GpuObject
    {
        GpuObjectType type;
        union {
            GpuBufferDesc bufferDesc;
            GpuImageDesc imageDesc;
        };
        RelativePtr<GpuObject> next;
    };

    uint32 objDataSize;
    uint32 numMetaData;
    uint32 numDependencies;
    uint32 numGpuObjects;

    RelativePtr<AssetMetaKeyValue> metaData;
    RelativePtr<uint8> objData;
    RelativePtr<Dependency> deps;
    RelativePtr<GpuObject> gpuObjects;
};
using AssetDataPair = Pair<AssetDataInternal*, uint32>; // second=dataSize

struct alignas(CACHE_LINE_SIZE) AssetDataHeader
{
    AtomicUint32 state; // AssetState
    uint32 paramsHash;
    uint32 refCount;
    uint32 dataSize;
    uint32 typeId;
    uint32 typeSpecificParamsSize;
    const char* typeName;
    AssetParams* params;
    AssetDataInternal* data;
    uint8 _reserved[16];
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

    // specific to remote loading 
    uint64 clientPayload;
    uint32 clientAssetHash;
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

using AssetImageDescHandlePair = Pair<AssetDataInternal::GpuImageDesc*, AssetHandle>;
using AssetBufferDescHandlePair = Pair<AssetDataInternal::GpuBufferDesc*, AssetHandle>;

struct AssetHandleResult
{
    AssetDataHeader* header;
    AssetHandle handle;
    bool newlyCreated;
};

struct AssetGroupInternal
{
    Array<AssetHandle> loadList;
    Array<AssetHandle> handles;
    AtomicUint32 state; // AssetGroupState
    bool hotReloadGroup;    // HotReload group is a special group that behaves differently and destroys itself after done
};

struct AssetQueuedItem
{
    uint32 indexInLoadList;
    uint32 dataSize;
    uint32 paramsHash;
    uint32 assetHash;
    uint32 assetTypeCacheVersion;
    AssetDataInternal* data;
    Path bakedFilepath;
    bool saveBaked;
    bool hasPendingGpuResource;
};

enum class AssetServerBlobType : uint32
{
    LocalCacheIsValid = 0,
    IncludesBakedData
};

struct AssetServer
{
    SpinLockMutex pendingTasksMutex;
    Array<AssetLoadTaskData*> pendingTasks;
    AssetLoadTaskData* loadTaskDatas[ASSET_SERVER_MAX_IN_FLIGHT];
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
    MemProxyAllocator alloc;
    MemProxyAllocator assetHeaderAlloc;
    MemProxyAllocator assetDataAlloc;
    ReadWriteMutex assetMutex;
    ReadWriteMutex groupsMutex;
    ReadWriteMutex hashLookupMutex;
    Mutex pendingJobsMutex;
    Mutex hotReloadMutex;

    AssetScratchMemArena memArena;
    MemBumpAllocatorCustom tempAlloc;   // Used in LoadAssetGroup, because we cannot use regular temp allocators due to dispatching

    Array<AssetTypeManager> typeManagers;
    HandlePool<AssetHandle, AssetDataHeader*> assetDb;
    HashTable<AssetHandle> assetLookup;     // Key: AssetParams hash. To check for availibility
    HashTableUint assetHashLookup;          // Key: AssetParams hash -> AssetHash
    MemTlsfAllocator assetHeaderAllocBase;
    MemTlsfAllocator assetDataAllocBase;

    HandlePool<AssetGroupHandle, AssetGroupInternal> groups;

    Array<AssetJobItem> pendingJobs;
    JobsHandle curJob;
    JobsSignal* curJobSignal;

    Array<AssetHandle> hotReloadList;

    AssetServer server;
    bool isServerEnabled;
    bool isHashLookupUpdated;
    bool isHotReloadEnabled;
    bool isForceUseCache;
};

static AssetMan gAssetMan;

//----------------------------------------------------------------------------------------------------------------------
// These functions should be exported for per asset type loading
using DataChunk = Pair<void*, uint32>;


//    ██████╗ ██████╗ ██╗██╗   ██╗ █████╗ ████████╗███████╗    ███████╗██╗   ██╗███╗   ██╗ ██████╗███████╗
//    ██╔══██╗██╔══██╗██║██║   ██║██╔══██╗╚══██╔══╝██╔════╝    ██╔════╝██║   ██║████╗  ██║██╔════╝██╔════╝
//    ██████╔╝██████╔╝██║██║   ██║███████║   ██║   █████╗      █████╗  ██║   ██║██╔██╗ ██║██║     ███████╗
//    ██╔═══╝ ██╔══██╗██║╚██╗ ██╔╝██╔══██║   ██║   ██╔══╝      ██╔══╝  ██║   ██║██║╚██╗██║██║     ╚════██║
//    ██║     ██║  ██║██║ ╚████╔╝ ██║  ██║   ██║   ███████╗    ██║     ╚██████╔╝██║ ╚████║╚██████╗███████║
//    ╚═╝     ╚═╝  ╚═╝╚═╝  ╚═══╝  ╚═╝  ╚═╝   ╚═╝   ╚══════╝    ╚═╝      ╚═════╝ ╚═╝  ╚═══╝ ╚═════╝╚══════╝
                                                                                                        
namespace Asset
{
    static MemBumpAllocatorVM* _GetOrCreateScratchAllocator(AssetScratchMemArena& arena);
    static Span<AssetMetaKeyValue> _LoadMetaData(const char* assetFilepath, AssetPlatform::Enum platform, MemAllocator* alloc);
    static AssetHandleResult _CreateOrFetchHandle(const AssetParams& params);
    static void _LoadAssetTask(uint32 groupIdx, void* userData);
    static void _SaveBakedTask(uint32 groupIdx, void* userData);
    template <typename _T> _T* _TranslatePointer(_T* ptr, const void* origPtr, void* newPtr);
    static void _LoadGroupTask(uint32, void* userData);
    static void _UnloadGroupTask(uint32, void* userData);
    static void _CreateGpuObjects(uint32 numImages, const AssetImageDescHandlePair* images, uint32 numBuffers, const AssetBufferDescHandlePair* buffers);
    static uint32 _MakeCacheFilepath(Path* outPath, const AssetDataHeader* header, uint32 overrideAssetHash = 0);
    static uint32 _MakeParamsHash(const AssetParams& params, uint32 typeSpecificParamsSize);
    static bool _RemoteServerCallback(uint32 cmd, const Blob& incomingData, Blob*, void*, char outErrorDesc[REMOTE_ERROR_SIZE]);
    static void _RemoteClientCallback(uint32 cmd, const Blob& incomingData, void*, bool error, const char* errorDesc);
    static void _ServerLoadBatchTask(uint32, void*);
    constexpr AssetPlatform::Enum _GetCurrentPlatform();
    static void _SaveAssetHashLookup();
    static void _LoadAssetHashLookup();
    static void _OnFileChanged(const char* filepath);
    static void _UnloadDatasManually(Span<AssetDataPair> datas);
    static void _GpuResourceFinishedCallback(void* userData);
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

static void Asset::_GpuResourceFinishedCallback(void* userData)
{
    AssetHandle handle = AssetHandle(PtrToInt<uint32>(userData));
    ASSERT(handle.IsValid());

    ReadWriteMutexReadScope rdlock(gAssetMan.assetMutex);
    if (gAssetMan.assetDb.IsValid(handle)) {
        AssetDataHeader* header = gAssetMan.assetDb.Data(handle);

        AtomicUint32 expected = uint32(AssetState::Loading);
        Atomic::CompareExchange_Weak(&header->state, &expected, uint32(AssetState::Loaded));
    }
}

static void Asset::_UnloadDatasManually(Span<AssetDataPair> datas)
{
    PROFILE_ZONE_COLOR(PROFILE_COLOR_ASSET3);

    // Unload dependencies
    {
        MemTempAllocator tempAlloc;
        Array<AssetHandle> depHandles(&tempAlloc);
        for (uint32 i = 0; i < datas.Count(); i++) {
            AssetDataInternal* data = datas[i].first;
            
            AssetDataInternal::Dependency* dep = data->deps.Get();
            while (dep) {
                AssetHandle* targetHandle = dep->bindToHandle.Get();
                if (targetHandle->IsValid()) 
                    depHandles.Push(*targetHandle);
                dep = dep->next.Get();
            }
        }

        if (!depHandles.IsEmpty()) {
            AssetGroup unloadGroup = Asset::CreateGroup();

            {
                ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
                AssetGroupInternal& groupData = gAssetMan.groups.Data(unloadGroup.mHandle);

                depHandles.CopyTo(&groupData.handles);
                groupData.hotReloadGroup = true;
            }
            
            unloadGroup.Unload();
        }
    }

    // Destroy GPU objects and asset data
    for (AssetDataPair& data : datas) {
        AssetDataInternal::GpuObject* gpuObj = data.first->gpuObjects.Get();
        while (gpuObj) {
            switch (gpuObj->type) {
            case AssetDataInternal::GpuObjectType::Image:
                ASSERT(!gpuObj->imageDesc.bindToImage.IsNull());
                GfxBackend::DestroyImage(*gpuObj->imageDesc.bindToImage.Get());
                break;
            case AssetDataInternal::GpuObjectType::Buffer:
                ASSERT(!gpuObj->bufferDesc.bindToBuffer.IsNull());
                GfxBackend::DestroyBuffer(*gpuObj->bufferDesc.bindToBuffer.Get());
                break;
            }
            gpuObj = gpuObj->next.Get();
        }

        // Note: 'AssetDataAlloc' doesn't need any thread protection, because it is guaranteed that only one thread uses it at a time 
        Mem::Free(data.first, &gAssetMan.assetDataAlloc);
    }
}

static void Asset::_OnFileChanged(const char* filepath)
{
    if (!gAssetMan.isHotReloadEnabled)
        return;

    // TODO: if filepath is a metadata file, then check the asset filepath instead

    ReadWriteMutexReadScope mtx(gAssetMan.assetMutex);
    for (uint32 i = 0; i < gAssetMan.assetDb.Count(); i++) {
        AssetHandle handle = gAssetMan.assetDb.HandleAt(i);
        AssetDataHeader* header = gAssetMan.assetDb.Data(handle);

        const char* assetPath = header->params->path.CStr();
        if (assetPath[0] == '/')
            ++assetPath;
        if (Str::IsEqualNoCase(filepath, assetPath)) {
            MutexScope lk(gAssetMan.hotReloadMutex);
            gAssetMan.hotReloadList.Push(handle);
        }
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

    Span<char*> lines = Str::Split((const char*)blob.Data(), '\n', &tempAlloc);

    ReadWriteMutexWriteScope lk(gAssetMan.hashLookupMutex);
    for (char* line : lines) {
        char* semicolon = const_cast<char*>(Str::FindChar(line, ';'));
        if (semicolon) {
            *semicolon = 0;
            uint32 paramsHash = Str::ToUint(line + 2, 16);
            uint32 assetHash = Str::ToUint(semicolon + 3, 16);

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

    uint32 cacheVersion = MakeVersion(ASSET_CACHE_VERSION, qa->assetTypeCacheVersion, 0, 0);
    Blob cache;
    cache.Reserve(32 + qa->dataSize);
    cache.SetGrowPolicy(Blob::GrowPolicy::Linear);
    cache.Write<uint32>(ASSET_CACHE_FILE_ID);
    cache.Write<uint32>(cacheVersion);
    cache.Write<uint32>(qa->dataSize);
    cache.Write(qa->data, qa->dataSize);

    uint64 writeFileData = (uint64(qa->paramsHash) << 32) | qa->assetHash;
    auto SaveFileCallback = [](const char* path, size_t bytesWritten, Blob& blob, void* userData)
    {
        if (bytesWritten)
            LOG_VERBOSE("(save) Baked: %s", path);
        blob.Free();

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
        Path assetMetaPath = assetFilepath;
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
    }

    if (assetHash) {
        Path strippedPath;
        Vfs::StripMountPath(strippedPath.Ptr(), strippedPath.Capacity(), assetFilepath.CStr());

        char hashStr[64];
        Str::PrintFmt(hashStr, sizeof(hashStr), "_%x", assetHash);

        *outPath = "/cache";
        (*outPath).Append(strippedPath.GetDirectory())
                .Append("/")
                .Append(strippedPath.GetFileName())
                .Append(hashStr)
                .Append(".")
                .Append(header->typeName);    
    }

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
        alloc->Initialize(ASSET_MAX_SCRATCH_SIZE_PER_THREAD, 512*SIZE_KB);

    return alloc;
}


static Span<AssetMetaKeyValue> Asset::_LoadMetaData(const char* assetFilepath, AssetPlatform::Enum platform, MemAllocator* alloc)
{
    Path assetMetaPath(assetFilepath);
    assetMetaPath.Append(".asset");
    
    uint32 tempId = MemTempAllocator::PushId();
    MemTempAllocator tmpAlloc(tempId);
    Span<AssetMetaKeyValue> r;

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
                        keys.Push(item);
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
                        keys.Push(item);
                    }

                    jitem = jplatform.GetNextChildItem(jitem);
                }
            }

            blob.Free();
            Json::Destroy(jctx);
            MemTempAllocator::PopId(tempId);
            
            // At this point we have popped the current temp allocator and can safely allocate from whatever allocator is coming in
            r = Span<AssetMetaKeyValue>(Mem::AllocCopy<AssetMetaKeyValue>(keys.Ptr(), keys.Count(), alloc), keys.Count());
        }
        else {
            blob.Free();
            LOG_WARNING("Invalid asset meta data: %s (Json syntax error at %u:%u)", assetMetaPath.CStr(), loc.line, loc.col);
            MemTempAllocator::PopId(tempId);
        }
    }
    else {
        MemTempAllocator::PopId(tempId);
    }

    return r;
}

static AssetHandleResult Asset::_CreateOrFetchHandle(const AssetParams& params)
{
    uint32 typeManIdx = gAssetMan.typeManagers.FindIf(
        [typeId = params.typeId](const AssetTypeManager& typeMgr) { return typeMgr.fourcc == typeId; });
    ASSERT_MSG(typeManIdx != UINT32_MAX, "AssetType with FourCC %x is not registered", params.typeId);
    const AssetTypeManager& typeMan = gAssetMan.typeManagers[typeManIdx];

    uint32 paramsHash = _MakeParamsHash(params, typeMan.extraParamTypeSize);
    AssetHandleResult r {};

    // check with asset database and skip loading if it already exists
    {
        ReadWriteMutexReadScope lock(gAssetMan.assetMutex);
        r.handle = gAssetMan.assetLookup.FindAndFetch(paramsHash, AssetHandle());
        if (r.handle.IsValid()) {
            r.header = gAssetMan.assetDb.Data(r.handle);
            ++r.header->refCount;
            r.newlyCreated = false;
        }
    }

    // create new asset header and handle 
    if (!r.handle.IsValid()) {
        ReadWriteMutexWriteScope lock(gAssetMan.assetMutex);
            
        MemSingleShotMalloc<AssetParams> paramsMallocator;
        if (typeMan.extraParamTypeSize)
            paramsMallocator.AddMemberArray<uint8>(offsetof(AssetParams, typeSpecificParams), typeMan.extraParamTypeSize);
        MemSingleShotMalloc<AssetDataHeader> mallocator;
        mallocator.AddChildStructSingleShot(paramsMallocator, offsetof(AssetDataHeader, params), 1);
        r.header = mallocator.Calloc(&gAssetMan.assetHeaderAlloc); // Note: This allocator is protected by 'assetMutex'

        r.header->paramsHash = paramsHash;
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

        gAssetMan.assetLookup.Add(paramsHash, r.handle);
        r.newlyCreated = true;
    }

    return r;
}

static void Asset::_LoadAssetTask(uint32 groupIdx, void* userData)
{
    PROFILE_ZONE_COLOR(PROFILE_COLOR_ASSET2);

    AssetLoadTaskData& taskData = *(((AssetLoadTaskData**)userData)[groupIdx]);
    const AssetParams& params = *taskData.inputs.header->params;
    uint32 typeManIdx = gAssetMan.typeManagers.FindIf([typeId = params.typeId](const AssetTypeManager& typeMan) { return typeMan.fourcc == typeId; });
    ASSERT_MSG(typeManIdx != UINT32_MAX, "AssetType with FourCC %x is not registered", params.typeId);
    const AssetTypeManager& typeMan = gAssetMan.typeManagers[typeManIdx];

    MemBumpAllocatorVM* alloc = _GetOrCreateScratchAllocator(gAssetMan.memArena);

    if (taskData.inputs.type == AssetLoadTaskInputType::Source) {
        ASSERT(!taskData.inputs.isRemoteLoad);

        size_t startOffset = alloc->GetOffset();
        AssetData assetData {
            .mAlloc = alloc,
            .mData = Mem::AllocZeroTyped<AssetDataInternal>(1, alloc),
            .mParamsHash = taskData.inputs.header->paramsHash
        };

        // Load metadata
        AssetPlatform::Enum platform = params.platform != AssetPlatform::Auto ? params.platform : _GetCurrentPlatform();
        Span<AssetMetaKeyValue> metaData = _LoadMetaData(params.path.CStr(), platform, alloc);
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
        MemTempAllocator tempAlloc;

        // REMOTE: wait for file to arrive 
        if (taskData.inputs.isRemoteLoad) {
            taskData.inputs.fileReadSignal.Wait();
            fileData = taskData.inputs.fileData;
            fileSize = taskData.inputs.fileSize;
        }
        else {
            ASSERT(fileData == nullptr);
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
        if (fileId != ASSET_CACHE_FILE_ID) {
            taskData.outputs.errorDesc = "Baked file has invalid signature";
            return;
        }

        uint32 targetCacheVersion = MakeVersion(ASSET_CACHE_VERSION, typeMan.cacheVersion, 0, 0);
        cache.Read<uint32>(&cacheVersion);
        if (cacheVersion != targetCacheVersion) {
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

static void Asset::_CreateGpuObjects(uint32 numImages, const AssetImageDescHandlePair* images, 
                                     uint32 numBuffers, const AssetBufferDescHandlePair* buffers)
{
    PROFILE_ZONE_COLOR(PROFILE_COLOR_ASSET2);
    MemTempAllocator tempAlloc;

    GfxBackend::BeginRenderFrameSync();
    GfxCommandBuffer cmd = GfxBackend::BeginCommandBuffer(GfxQueueType::Transfer);

    if (numImages) {
        GfxImageDesc* descs = Mem::AllocTyped<GfxImageDesc>(numImages, &tempAlloc);
        GfxBufferDesc* stagingBufferDescs = Mem::AllocTyped<GfxBufferDesc>(numImages, &tempAlloc);
        GfxImageHandle* handles = Mem::AllocZeroTyped<GfxImageHandle>(numImages, &tempAlloc);
        GfxBufferHandle* bufferHandles = Mem::AllocZeroTyped<GfxBufferHandle>(numImages, &tempAlloc);

        for (uint32 i = 0; i < numImages; i++)  {
            descs[i] = images[i].first->createDesc;
            stagingBufferDescs[i] = {
                .sizeBytes = images[i].first->contentSize,
                .usageFlags = GfxBufferUsageFlags::TransferSrc,
                .arena = GfxMemoryArena::TransientCPU
            };
        }

        GfxBackend::BatchCreateImage(numImages, descs, handles);
        GfxBackend::BatchCreateBuffer(numImages, stagingBufferDescs, bufferHandles);

        GfxMapResult* mapResults = Mem::AllocZeroTyped<GfxMapResult>(numImages, &tempAlloc);
        cmd.BatchMapBuffer(numImages, bufferHandles, mapResults);

        for (uint32 i = 0; i < numImages; i++) {
            ASSERT(mapResults[i].dataSize == images[i].first->contentSize);
            memcpy(mapResults[i].dataPtr, images[i].first->content.Get(), mapResults[i].dataSize);
        }

        cmd.BatchFlushBuffer(numImages, bufferHandles);

        GfxCopyBufferToImageParams* copyParams = Mem::AllocTyped<GfxCopyBufferToImageParams>(numImages, &tempAlloc);
        for (uint32 i = 0; i < numImages; i++) {
            copyParams[i] = {
                .srcHandle = bufferHandles[i],
                .dstHandle = handles[i],
                .stagesUsed = GfxShaderStage::Fragment,
                .mipCount = UINT16_MAX,
                .resourceTransferedCallback = _GpuResourceFinishedCallback,
                .resourceTransferedUserData = IntToPtr(images[i].second.mId)
            };
        }

        cmd.BatchCopyBufferToImage(numImages, copyParams);            
        GfxBackend::BatchDestroyBuffer(numImages, bufferHandles);

        for (uint32 i = 0; i < numImages; i++) {
            GfxImageHandle* targetImage = images[i].first->bindToImage.Get();
            *targetImage = handles[i];
        }
    }

    if (numBuffers) {
        GfxBufferDesc* descs = Mem::AllocTyped<GfxBufferDesc>(numBuffers, &tempAlloc);
        GfxBufferDesc* stagingBufferDescs = Mem::AllocTyped<GfxBufferDesc>(numBuffers, &tempAlloc);
        GfxBufferHandle* handles = Mem::AllocZeroTyped<GfxBufferHandle>(numBuffers, &tempAlloc);
        GfxBufferHandle* stagingHandles = Mem::AllocZeroTyped<GfxBufferHandle>(numBuffers, &tempAlloc);

        for (uint32 i = 0; i < numBuffers; i++) {
            descs[i] = buffers[i].first->createDesc;
            stagingBufferDescs[i] = {
                .sizeBytes = buffers[i].first->createDesc.sizeBytes,
                .usageFlags = GfxBufferUsageFlags::TransferSrc,
                .arena = GfxMemoryArena::TransientCPU
            };
        }

        GfxBackend::BatchCreateBuffer(numBuffers, descs, handles);
        GfxBackend::BatchCreateBuffer(numBuffers, stagingBufferDescs, stagingHandles);

        GfxMapResult* mapResults = Mem::AllocZeroTyped<GfxMapResult>(numBuffers, &tempAlloc);
        cmd.BatchMapBuffer(numBuffers, stagingHandles, mapResults);

        for (uint32 i = 0; i < numBuffers; i++) {
            ASSERT(mapResults[i].dataSize == buffers[i].first->createDesc.sizeBytes);
            memcpy(mapResults[i].dataPtr, buffers[i].first->content.Get(), mapResults[i].dataSize);
        }
        cmd.BatchFlushBuffer(numBuffers, stagingHandles);

        GfxCopyBufferToBufferParams* copyParams = Mem::AllocTyped<GfxCopyBufferToBufferParams>(numBuffers, &tempAlloc);
        for (uint32 i = 0; i < numBuffers; i++) {
            copyParams[i] = {
                .srcHandle = stagingHandles[i],
                .dstHandle = handles[i],
                .stagesUsed = GfxShaderStage::Vertex,
                .resourceTransferedCallback = _GpuResourceFinishedCallback,
                .resourceTransferedUserData = IntToPtr(buffers[i].second.mId)
            };
        }

        cmd.BatchCopyBufferToBuffer(numBuffers, copyParams);            
        GfxBackend::BatchDestroyBuffer(numBuffers, stagingHandles);

        for (uint32 i = 0; i < numBuffers; i++) {
            GfxBufferHandle* targetBuffer = buffers[i].first->bindToBuffer.Get();
            *targetBuffer = handles[i];
        }
    }

    GfxBackend::EndCommandBuffer(cmd);
    GfxBackend::SubmitQueue(GfxQueueType::Transfer, GfxQueueType::Graphics);
    GfxBackend::EndRenderFrameSync();
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
    Array<AssetHandle> loadListHandles;
    bool isHotReloadGroup;

    {
        ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
        AssetGroupInternal& group = gAssetMan.groups.Data(groupHandle);
        Atomic::StoreExplicit(&group.state, uint32(AssetGroupState::Loading), AtomicMemoryOrder::Release);
        isHotReloadGroup = group.hotReloadGroup;

        ReadWriteMutexReadScope rdlockAsset(gAssetMan.assetMutex);
        loadList.Reserve(group.loadList.Count());
        loadListHandles.Reserve(group.loadList.Count());

        for (AssetHandle handle : group.loadList) {
            if (gAssetMan.assetDb.IsValid(handle)) {
                loadList.Push(gAssetMan.assetDb.Data(handle));
                loadListHandles.Push(handle);
            }
        }

        group.loadList.Clear();
    }

    // Pick a batch size 
    // This is the amount of tasks that are submitted to the task manager at a time
    uint32 batchCount = Min(128u, loadList.Count());
    uint32 allCount = loadList.Count();

    // We cannot use the actual temp allocators here. Because we are dispatching jobs and might end up in a different thread
    MemBumpAllocatorBase* tempAlloc = &gAssetMan.tempAlloc;
    Array<AssetQueuedItem> queuedAssets;
    queuedAssets.Reserve(loadList.Count());

    for (uint32 i = 0; i < loadList.Count();) {
        uint32 sliceCount = Min(batchCount, loadList.Count() - i);

        AssetLoadTaskData* taskDatas = Mem::AllocZeroTyped<AssetLoadTaskData>(sliceCount, tempAlloc);
        AssetLoadTaskData** taskDataPtrs = Mem::AllocTyped<AssetLoadTaskData*>(sliceCount, tempAlloc);
        for (uint32 k = 0; k < sliceCount; k++) {
            AssetDataHeader* header = loadList[i + k];
            Atomic::StoreExplicit(&header->state, uint32(AssetState::Loading), AtomicMemoryOrder::Relaxed);

            // Decide if baked file exists and we should skip loading from source (with requires baking most assets)
            uint32 assetHash = 0;   
            bool isRemoteLoad = Vfs::GetMountType(header->params->path.CStr()) == VfsMountType::Remote;
            taskDatas[k].inputs.isRemoteLoad = isRemoteLoad;

            // For remote loading, we are forced to use cache lookup table
            if (gAssetMan.isForceUseCache || isRemoteLoad) {
                ReadWriteMutexReadScope lk(gAssetMan.hashLookupMutex);
                assetHash = gAssetMan.assetHashLookup.FindAndFetch(header->paramsHash, 0);
            }

            if (!isRemoteLoad || assetHash) {
                if (!taskDatas[k].inputs.bakedFilepath.IsEmpty() && !isRemoteLoad) {
                    ASSERT(isHotReloadGroup);
                    OS::DeleteFilePath(taskDatas[k].inputs.bakedFilepath.CStr());
                }

                assetHash = _MakeCacheFilepath(&taskDatas[k].inputs.bakedFilepath, header, assetHash);
                if (assetHash) {
                    if (!isRemoteLoad)
                        taskDatas[k].inputs.type = Vfs::FileExists(taskDatas[k].inputs.bakedFilepath.CStr()) ? AssetLoadTaskInputType::Baked : AssetLoadTaskInputType::Source;
                    else
                        taskDatas[k].inputs.type = AssetLoadTaskInputType::Baked;

                    taskDatas[k].inputs.assetHash = assetHash;

                    // For remote assets, clearing 'isRemoteLoad' means that we don't even make any requests to server
                    if (gAssetMan.isForceUseCache && isRemoteLoad && taskDatas[k].inputs.type == AssetLoadTaskInputType::Baked)
                        taskDatas[k].inputs.isRemoteLoad = false;
                }
            }
            else if (isRemoteLoad) {
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

        // REMOTE: Make load requests
        for (uint32 k = 0; k < sliceCount; k++) {
            if (!taskDatas[k].inputs.isRemoteLoad) 
                continue;

            AssetDataHeader* header = loadList[i + k];
            if (header->params->platform == AssetPlatform::Auto)
                header->params->platform = _GetCurrentPlatform();

            Blob requestBlob(tempAlloc);

            requestBlob.Write<uint64>(uint64(&taskDatas[k].inputs));
            requestBlob.Write<uint32>(taskDatas[k].inputs.assetHash);
            requestBlob.Write<uint32>(header->typeId);
            requestBlob.WriteStringBinary16(header->params->path.Ptr(), header->params->path.Length());
            requestBlob.Write<uint32>(header->params->platform);
            requestBlob.Write(header->params->typeSpecificParams, header->typeSpecificParamsSize);

            Remote::ExecuteCommand(ASSET_LOAD_ASSET_REMOTE_CMD, requestBlob);
        }

        Jobs::WaitForCompletionAndDelete(batchJob);

        // Gather dependency assets and add them to the queue
        // Gather items to be saved in cache
        uint32 lastLoadListCount = loadList.Count();
        for (uint32 k = 0; k < sliceCount; k++) {
            AssetLoadTaskInputs& in = taskDatas[k].inputs;
            AssetLoadTaskOutputs& out = taskDatas[k].outputs;

            if (!out.data) {
                Atomic::StoreExplicit(&in.header->state, uint32(AssetState::LoadFailed), AtomicMemoryOrder::Relaxed);
                LOG_ERROR("Loading %s '%s' failed: %s", in.header->typeName, in.header->params->path.CStr(), out.errorDesc.CStr());
                continue;
            }

            AssetDataHeader* header = loadList[i + k];
            uint32 typeManIdx = gAssetMan.typeManagers.FindIf(
                [typeId = header->typeId](const AssetTypeManager& typeMgr) { return typeMgr.fourcc == typeId; });
            ASSERT_MSG(typeManIdx != UINT32_MAX, "AssetType with FourCC %x is not registered", header->typeId);
            const AssetTypeManager& typeMan = gAssetMan.typeManagers[typeManIdx];

            AssetQueuedItem qa {
                .indexInLoadList = i + k,
                .dataSize = out.dataSize,
                .paramsHash = in.header->paramsHash,
                .assetHash = in.assetHash,
                .assetTypeCacheVersion = typeMan.cacheVersion,
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
                if (depHandleResult.newlyCreated) {
                    // Before pushing to loadList, we need to check if we are running the next loop, if not, fix the index
                    if (i > loadList.Count())
                        i = loadList.Count();

                    loadList.Push(depHandleResult.header);
                    loadListHandles.Push(depHandleResult.handle);
                    ++allCount;
                }

                dep = dep->next.Get();
            }
        } 

        tempAlloc->Reset();

        i += batchCount;
        if (lastLoadListCount < loadList.Count())
            batchCount = Max(batchCount, Min(128u, loadList.Count() - i));
    }

    // Save to cache
    Array<AssetQueuedItem*> saveItems(tempAlloc);
    saveItems.Reserve(queuedAssets.Count());
    JobsHandle saveItemsJob = nullptr;

    for (uint32 i = 0; i < queuedAssets.Count(); i++) {
        AssetQueuedItem& qa = queuedAssets[i];
        if (qa.saveBaked)
            saveItems.Push(&qa);
    }

    if (!saveItems.IsEmpty())
        saveItemsJob = Jobs::Dispatch(JobsType::LongTask, _SaveBakedTask, saveItems.Ptr(), saveItems.Count());

    // Create GPU objects
    {
        Array<AssetImageDescHandlePair> images(tempAlloc);
        Array<AssetBufferDescHandlePair> buffers(tempAlloc);

        for (uint32 i = 0; i < queuedAssets.Count(); i++) {
            AssetQueuedItem& qa = queuedAssets[i];
            AssetDataInternal::GpuObject* gpuObj = qa.data->gpuObjects.Get();
            while (gpuObj) {
                if (gpuObj->type == AssetDataInternal::GpuObjectType::Buffer) {
                    buffers.Push(AssetBufferDescHandlePair(&gpuObj->bufferDesc, loadListHandles[qa.indexInLoadList]));
                    qa.hasPendingGpuResource = true;
                }
                else if (gpuObj->type == AssetDataInternal::GpuObjectType::Image) {
                    images.Push(AssetImageDescHandlePair(&gpuObj->imageDesc, loadListHandles[qa.indexInLoadList]));
                    qa.hasPendingGpuResource = true;
                }

                gpuObj = gpuObj->next.Get();
            }
        }

        JobsSignal gpuObjectsSignal;
        gAssetMan.curJobSignal = &gpuObjectsSignal;

        uint32 imageIdx = 0;
        uint32 bufferIdx = 0;
        if (images.Count() || buffers.Count()) {
            gpuObjectsSignal.Wait();
            gpuObjectsSignal.Reset();

            size_t totalTransferSize = 0;
            for (uint32 i = imageIdx; i < images.Count(); i++) {
                const AssetImageDescHandlePair& img = images[i];
                totalTransferSize += img.first->contentSize;
                if (totalTransferSize > ASSET_MAX_TRANSFER_SIZE_PER_FRAME) {
                    _CreateGpuObjects(i - imageIdx, &images[imageIdx], 0, nullptr);
                    imageIdx = i;
                    totalTransferSize = 0;
                    gpuObjectsSignal.Wait();
                    gpuObjectsSignal.Reset();
                }
            }

            for (uint32 i = bufferIdx; i < buffers.Count(); i++) {
                const AssetBufferDescHandlePair& buff = buffers[i];
                totalTransferSize += buff.first->createDesc.sizeBytes;
                if (totalTransferSize > ASSET_MAX_TRANSFER_SIZE_PER_FRAME) {
                    _CreateGpuObjects(images.Count() - imageIdx, imageIdx < images.Count() ? &images[imageIdx] : nullptr, 
                                      i - bufferIdx, &buffers[bufferIdx]);
                    imageIdx = images.Count();
                    bufferIdx = i;
                    totalTransferSize = 0;
                    gpuObjectsSignal.Wait();
                    gpuObjectsSignal.Reset();
                }
            }

            _CreateGpuObjects(images.Count() - imageIdx, imageIdx < images.Count() ? &images[imageIdx] : nullptr,
                              buffers.Count() - bufferIdx, bufferIdx < buffers.Count() ? &buffers[bufferIdx] : nullptr);
        }
    }

    if (saveItemsJob)
        Jobs::WaitForCompletionAndDelete(saveItemsJob);
    tempAlloc->Reset();

    // HOT-RELOAD: Before finalizing the data, we get all the current data pointers
    AssetDataPair* oldDatas = nullptr;
    if (isHotReloadGroup && !queuedAssets.IsEmpty()) {
        oldDatas = Mem::AllocTyped<AssetDataPair>(queuedAssets.Count());
        for (uint32 i = 0; i < queuedAssets.Count(); i++) {
            AssetDataHeader* header = loadList[queuedAssets[i].indexInLoadList];
            oldDatas[i].first = header->data;
            oldDatas[i].second = header->dataSize;
            header->data = nullptr;
            header->dataSize = 0;
        }
    }
    
    // Strip unwanted stuff and save it to persistent memory
    // Note: 'AssetDataAlloc' doesn't need any thread protection, because it is guaranteed that only one thread uses it at a time 
    for (uint32 i = 0; i < queuedAssets.Count(); i++) {
        AssetQueuedItem& qa = queuedAssets[i];
        AssetDataInternal* srcData = qa.data;

        // Make a copy of GPU objects, but strip their content
        if (srcData->numGpuObjects) {
            AssetDataInternal::GpuObject* firstGpuObj = srcData->gpuObjects.Get();
            AssetDataInternal::GpuObject* srcGpuObj = firstGpuObj;
            AssetDataInternal::GpuObject* dstGpuObjs = Mem::AllocTyped<AssetDataInternal::GpuObject>(srcData->numGpuObjects, tempAlloc);
            void** handlePointers = Mem::AllocTyped<void*>(srcData->numGpuObjects, tempAlloc);
            uint32 gpuObjIdx = 0;
            while (srcGpuObj) {
                switch (srcGpuObj->type) {
                case AssetDataInternal::GpuObjectType::Buffer:
                    handlePointers[gpuObjIdx] = srcGpuObj->bufferDesc.bindToBuffer.Get();
                    break;
                case AssetDataInternal::GpuObjectType::Image:
                    handlePointers[gpuObjIdx] = srcGpuObj->imageDesc.bindToImage.Get();
                    break;
                }

                dstGpuObjs[gpuObjIdx] = *srcGpuObj;
                if (gpuObjIdx > 0)
                    dstGpuObjs[gpuObjIdx-1].next = &dstGpuObjs[gpuObjIdx];
                ++gpuObjIdx;

                srcGpuObj = srcGpuObj->next.Get();
            }

            // Overwrite the gpu objects on the source data
            // It's garanteed that our data is smaller than the original GPU data because we stripped all content
            uint32 newGpuObjsSize = uint32(tempAlloc->GetOffset() - tempAlloc->GetPointerOffset(dstGpuObjs));
            memcpy(firstGpuObj, dstGpuObjs, newGpuObjsSize);
            uint32 dataSize = uint32(uintptr(firstGpuObj) - uintptr(srcData)) + newGpuObjsSize;
            ASSERT(dataSize <= qa.dataSize);
            qa.dataSize = dataSize;

            // Now that we have the new data, Re-assign all handle pointers
            srcGpuObj = firstGpuObj;
            gpuObjIdx = 0;
            while (srcGpuObj) {
                switch (srcGpuObj->type) {
                case AssetDataInternal::GpuObjectType::Buffer:
                    srcGpuObj->bufferDesc.bindToBuffer = (GfxBufferHandle*)handlePointers[gpuObjIdx];
                    srcGpuObj->bufferDesc.content.SetNull();
                    break;
                case AssetDataInternal::GpuObjectType::Image:
                    srcGpuObj->imageDesc.bindToImage = (GfxImageHandle*)handlePointers[gpuObjIdx];
                    srcGpuObj->imageDesc.content.SetNull();
                    break;
                }
                srcGpuObj = srcGpuObj->next.Get();
                ++gpuObjIdx;
            }
        }

        AssetDataHeader* header = loadList[qa.indexInLoadList];
        header->data = Mem::AllocCopyRawBytes<AssetDataInternal>(srcData, qa.dataSize, &gAssetMan.assetDataAlloc);
    }
    tempAlloc->Reset();

    // Set loaded flag to all newly loaded assets, except the assets that has GPU resources
    // For those we have to wait for GPU data upload to finish submission. See _GpuResourceFinishedCallback
    for (AssetQueuedItem& qa : queuedAssets) {
        if (!qa.hasPendingGpuResource) {
            AssetDataHeader* header = loadList[qa.indexInLoadList];
            Atomic::StoreExplicit(&header->state, uint32(AssetState::Loaded), AtomicMemoryOrder::Relaxed);
        }
    }

    // HOT-RELOAD: now we have the previous data and newly loaded asset. Run the callbacks to do asset post-processing 
    if (isHotReloadGroup && !queuedAssets.IsEmpty()) {
        for (uint32 i = 0; i < queuedAssets.Count(); i++) {
            AssetDataHeader* header = loadList[queuedAssets[i].indexInLoadList];
            uint32 typeManIdx = gAssetMan.typeManagers.FindIf(
                [typeId = header->typeId](const AssetTypeManager& typeMgr) { return typeMgr.fourcc == typeId; });
            ASSERT_MSG(typeManIdx != UINT32_MAX, "AssetType with FourCC %x is not registered", header->typeId);
            const AssetTypeManager& typeMan = gAssetMan.typeManagers[typeManIdx];

            if (!typeMan.impl->Reload(header->data->objData.Get(), oldDatas[i].first->objData.Get())) {
                LOG_ERROR("Reloading %s '%s' failed", typeMan.name.CStr(), header->params->path.CStr());

                // Switch back to the old data because it cannot be reloaded properly
                Swap<AssetDataInternal*>(oldDatas[i].first, header->data);
                Swap<uint32>(oldDatas[i].second, header->dataSize);
            }
        }

        // TODO: we should probably queue this to happen in later frames
        //       The reason is that GPU objects are already binded to graphics pipeline while we destroy them
        _UnloadDatasManually(Span<AssetDataPair>(oldDatas, queuedAssets.Count()));
        Mem::Free(oldDatas);
    }

    queuedAssets.Free();
    loadList.Free();
    loadListHandles.Free();

    // Reset arena allocators, getting ready for the next group
    for (uint32 i = 0; i < gAssetMan.memArena.numAllocators; i++) {
        if (gAssetMan.memArena.allocators[i].IsInitialized())
            gAssetMan.memArena.allocators[i].Reset();
    }

    if (!isHotReloadGroup) {
        ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
        AssetGroupInternal& group = gAssetMan.groups.Data(groupHandle);
        Atomic::StoreExplicit(&group.state, uint32(AssetGroupState::Loaded), AtomicMemoryOrder::Release);
    }
    else {
        {
            ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
            AssetGroupInternal& group = gAssetMan.groups.Data(groupHandle);
            group.state = AssetGroupState::Idle;
        }

        AssetGroup group { groupHandle };
        Asset::DestroyGroup(group);
    }

    LOG_INFO("%s with %u assets finished (%.1f ms)", isHotReloadGroup ? "Reload" : "Load", allCount, timer.ElapsedMS());
};

static void Asset::_UnloadGroupTask(uint32, void* userData)
{
    PROFILE_ZONE_COLOR(PROFILE_COLOR_ASSET3);

    // Fetch load list (pointers in the loadList are persistant through the lifetime of the group)
    MemTempAllocator tempAlloc;
    AssetGroupHandle groupHandle(PtrToInt<uint32>(userData));
    Array<AssetHandle> unloadList(&tempAlloc);
    Array<AssetDataInternal*> unloadDatas(&tempAlloc);

    bool isHotReloadGroup;
    {
        ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
        AssetGroupInternal& group = gAssetMan.groups.Data(groupHandle);
        Atomic::StoreExplicit(&group.state, uint32(AssetGroupState::Unloading), AtomicMemoryOrder::Release);
        group.handles.CopyTo(&unloadList);
        group.handles.Clear();

        isHotReloadGroup = group.hotReloadGroup;
    }

    // Unload assets and their dependencies
    // Omit assets that has refCount > 0
    {
        ReadWriteMutexReadScope rlock(gAssetMan.assetMutex);
        for (uint32 i = 0; i < unloadList.Count();) {
            AssetDataHeader* header = gAssetMan.assetDb.Data(unloadList[i]);
            if (--header->refCount > 0) {
                unloadList.RemoveAndSwap(i);
            } 
            else {
                // TODO: do something about hot-reloads
                ASSERT_MSG(AssetState(Atomic::LoadExplicit(&header->state, AtomicMemoryOrder::Relaxed)) != AssetState::Locked,
                           "%s asset is still locked and cannot be unloaded: %s", header->typeName, header->params->path.CStr());
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
            case AssetDataInternal::GpuObjectType::Image:
                ASSERT(!gpuObj->imageDesc.bindToImage.IsNull());
                GfxBackend::DestroyImage(*gpuObj->imageDesc.bindToImage.Get());
                break;
            case AssetDataInternal::GpuObjectType::Buffer:
                ASSERT(!gpuObj->bufferDesc.bindToBuffer.IsNull());
                GfxBackend::DestroyBuffer(*gpuObj->bufferDesc.bindToBuffer.Get());
                break;
            }
            gpuObj = gpuObj->next.Get();
        }

        // Note: 'AssetDataAlloc' doesn't need any thread protection, because it is guaranteed that only one thread uses it at a time 
        Mem::Free(data, &gAssetMan.assetDataAlloc);
    }

    // Remove the handles and free the header
    // TODO: this part seems to be crashing on some systems with Stack corruption. investigate
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

    if (!isHotReloadGroup) {
        ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
        AssetGroupInternal& group = gAssetMan.groups.Data(groupHandle);
        Atomic::StoreExplicit(&group.state, uint32(AssetGroupState::Idle), AtomicMemoryOrder::Release);
    }
    else {
        AssetGroup group { groupHandle };
        Asset::DestroyGroup(group);
    }
}

static void Asset::_ServerLoadBatchTask(uint32, void*)
{
    AssetLoadTaskData** taskDatas = gAssetMan.server.loadTaskDatas;
    uint32 numTasks = gAssetMan.server.numLoadTasks;
    ASSERT(numTasks);

    Array<AssetLoadTaskData*> tasksForLoad;
    tasksForLoad.Reserve(numTasks);

    for (uint32 i = 0; i < numTasks; i++) {
        AssetDataHeader* header = taskDatas[i]->inputs.header;
        uint32 assetHash = _MakeCacheFilepath(&taskDatas[i]->inputs.bakedFilepath, header);
        if (assetHash) {
            taskDatas[i]->inputs.type = Vfs::FileExists(taskDatas[i]->inputs.bakedFilepath.CStr()) ? AssetLoadTaskInputType::Baked : AssetLoadTaskInputType::Source;
            taskDatas[i]->inputs.assetHash = assetHash;
        }

        if (assetHash == 0 || assetHash != taskDatas[i]->inputs.clientAssetHash)
            tasksForLoad.Push(taskDatas[i]);
    }

    if (!tasksForLoad.IsEmpty()) {
        JobsHandle batchJob = Jobs::Dispatch(JobsType::LongTask, _LoadAssetTask, tasksForLoad.Ptr(), tasksForLoad.Count());
        Jobs::WaitForCompletionAndDelete(batchJob);
    }
    tasksForLoad.Free();

    // Gather stuff that needs to be saved
    StaticArray<AssetQueuedItem, ASSET_SERVER_MAX_IN_FLIGHT> saveAssets;
    for (uint32 i = 0; i < numTasks; i++) {
        const AssetLoadTaskInputs& in = taskDatas[i]->inputs;
        AssetLoadTaskOutputs& out = taskDatas[i]->outputs;

        uint32 typeManIdx = gAssetMan.typeManagers.FindIf(
            [typeId = in.header->typeId](const AssetTypeManager& typeMgr) { return typeMgr.fourcc == typeId; });
        ASSERT_MSG(typeManIdx != UINT32_MAX, "AssetType with FourCC %x is not registered", in.header->typeId);
        const AssetTypeManager& typeMan = gAssetMan.typeManagers[typeManIdx];

        bool saveBaked = in.type == AssetLoadTaskInputType::Source;
        if (out.data && saveBaked) {
            AssetQueuedItem qa {
                .indexInLoadList = i,
                .dataSize = out.dataSize,
                .assetTypeCacheVersion = typeMan.cacheVersion,
                .data = out.data,
                .bakedFilepath = in.bakedFilepath,
                .saveBaked = saveBaked
            };
            saveAssets.Push(qa);
        }
    }

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

            if (in.assetHash && in.clientAssetHash == in.assetHash) {
                blobs[0].Write<uint32>(uint32(AssetServerBlobType::LocalCacheIsValid));
                Remote::SendResponse(ASSET_LOAD_ASSET_REMOTE_CMD, blobs[0], false, nullptr);
            }
            else if (!out.data) {
                String<512> errorMsg;
                errorMsg.FormatSelf("Loading %s '%s' failed: %s", in.header->typeName, in.header->params->path.CStr(), out.errorDesc.CStr());
                LOG_ERROR(errorMsg.CStr());

                Remote::SendResponse(ASSET_LOAD_ASSET_REMOTE_CMD, blobs[0], true, errorMsg.CStr());
            }
            else {
                uint32 typeManIdx = gAssetMan.typeManagers.FindIf(
                    [typeId = in.header->typeId](const AssetTypeManager& typeMgr) { return typeMgr.fourcc == typeId; });
                ASSERT_MSG(typeManIdx != UINT32_MAX, "AssetType with FourCC %x is not registered", in.header->typeId);
                const AssetTypeManager& typeMan = gAssetMan.typeManagers[typeManIdx];

                blobs[0].Write<uint32>(uint32(AssetServerBlobType::IncludesBakedData));
                blobs[0].Write<uint32>(in.assetHash);

                // Add Cache header
                // TODO: maybe make this cleaner. Because we are writing cache header in two places (_SaveBakedTask)
                uint32 cacheVersion = MakeVersion(ASSET_CACHE_VERSION, typeMan.cacheVersion, 0, 0);
                blobs[0].Write<uint32>(ASSET_CACHE_FILE_ID);
                blobs[0].Write<uint32>(cacheVersion);
                blobs[0].Write<uint32>(out.dataSize);

                blobs[1].Reserve(out.data, out.dataSize);
                blobs[1].SetSize(out.dataSize);

                Remote::SendResponseMerge(ASSET_LOAD_ASSET_REMOTE_CMD, blobs, CountOf(blobs), false, nullptr);
            }
        }
    }

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

static bool Asset::_RemoteServerCallback([[maybe_unused]] uint32 cmd, const Blob& incomingData, Blob*, void*, char outErrorDesc[REMOTE_ERROR_SIZE])
{
    ASSERT(cmd == ASSET_LOAD_ASSET_REMOTE_CMD);
    UNUSED(outErrorDesc);

    uint32 typeId = 0;
    uint32 platformId = 0;
    uint64 clientPayload;
    uint32 clientAssetHash;

    incomingData.Read<uint64>(&clientPayload);
    incomingData.Read<uint32>(&clientAssetHash);
    incomingData.Read<uint32>(&typeId);
    uint32 typeManIdx = gAssetMan.typeManagers.FindIf([typeId](const AssetTypeManager& typeMgr) { return typeMgr.fourcc == typeId; });
    ASSERT_MSG(typeManIdx != UINT32_MAX, "AssetType with FourCC %x is not registered", typeId);
    const AssetTypeManager& typeMan = gAssetMan.typeManagers[typeManIdx];

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
    header->params->path.CalcLength();

    incomingData.Read<uint32>(&platformId);
    incomingData.Read(header->params->typeSpecificParams, typeMan.extraParamTypeSize);

    header->params->typeId = typeId;
    header->params->platform = AssetPlatform::Enum(platformId);

    header->paramsHash = _MakeParamsHash(*header->params, typeMan.extraParamTypeSize);
    header->refCount = 1;
    header->typeName = typeMan.name.CStr();

    taskData->inputs.clientPayload = clientPayload;
    taskData->inputs.clientAssetHash = clientAssetHash;

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

static void Asset::_RemoteClientCallback([[maybe_unused]] uint32 cmd, const Blob& incomingData, void*, bool error, const char* errorDesc)
{
    ASSERT(cmd == ASSET_LOAD_ASSET_REMOTE_CMD);

    uint64 payload;
    incomingData.Read<uint64>(&payload);
    AssetLoadTaskInputs* inputs = (AssetLoadTaskInputs*)reinterpret_cast<void*>(payload);

    if (!error) {
        AssetServerBlobType blobType;
        uint32 assetHash;

        incomingData.Read<uint32>((uint32*)&blobType);

        if (blobType == AssetServerBlobType::IncludesBakedData) {
            incomingData.Read<uint32>(&assetHash);

            if (!inputs->bakedFilepath.IsEmpty()) {
                Path absOldBakedFilepath = Vfs::ResolveFilepath(inputs->bakedFilepath.CStr());
                OS::DeleteFilePath(absOldBakedFilepath.CStr());
            }

            inputs->assetHash = _MakeCacheFilepath(&inputs->bakedFilepath, inputs->header, assetHash);

            size_t bakedSize = incomingData.Size() - incomingData.ReadOffset();
            ASSERT(bakedSize <= UINT32_MAX);

            void* data = Mem::Alloc(bakedSize, &gAssetMan.tempAlloc);
            incomingData.Read(data, bakedSize);
            inputs->fileData = data;
            inputs->fileSize = uint32(bakedSize);
        }
        else if (blobType == AssetServerBlobType::LocalCacheIsValid) {
            inputs->isRemoteLoad = false;
        }
        else {
            ASSERT(0);
        }            
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


//    ██████╗ ██╗   ██╗██████╗ ██╗     ██╗ ██████╗    ███████╗██╗   ██╗███╗   ██╗ ██████╗███████╗
//    ██╔══██╗██║   ██║██╔══██╗██║     ██║██╔════╝    ██╔════╝██║   ██║████╗  ██║██╔════╝██╔════╝
//    ██████╔╝██║   ██║██████╔╝██║     ██║██║         █████╗  ██║   ██║██╔██╗ ██║██║     ███████╗
//    ██╔═══╝ ██║   ██║██╔══██╗██║     ██║██║         ██╔══╝  ██║   ██║██║╚██╗██║██║     ╚════██║
//    ██║     ╚██████╔╝██████╔╝███████╗██║╚██████╗    ██║     ╚██████╔╝██║ ╚████║╚██████╗███████║
//    ╚═╝      ╚═════╝ ╚═════╝ ╚══════╝╚═╝ ╚═════╝    ╚═╝      ╚═════╝ ╚═╝  ╚═══╝ ╚═════╝╚══════╝
                                                                                               
void Asset::RegisterType(const AssetTypeDesc& desc)
{
    if (uint32 index = gAssetMan.typeManagers.FindIf([desc](const AssetTypeManager& typeMgr) { 
        return typeMgr.fourcc == desc.fourcc || typeMgr.name.IsEqual(desc.name); });
        index != UINT32_MAX)
    {
        ASSERT_MSG(0, "AssetType '%s' is already registered", desc.name);
        return;
    }

    AssetTypeManager mgr {
        .name = desc.name,
        .fourcc = desc.fourcc,
        .cacheVersion = desc.cacheVersion,
        .impl = desc.impl,
        .extraParamTypeSize = desc.extraParamTypeSize,
        .extraParamTypeName = desc.extraParamTypeName,
        .failedObj = desc.failedObj,
        .asyncObj = desc.asyncObj,
    };

    gAssetMan.typeManagers.Push(mgr);
}

void Asset::UnregisterType(uint32 fourcc)
{
    if (uint32 index = gAssetMan.typeManagers.FindIf([fourcc](const AssetTypeManager& typeMgr) {
        return typeMgr.fourcc == fourcc; });
        index != UINT32_MAX)
    {
        AssetTypeManager* typeMgr = &gAssetMan.typeManagers[index];
        ASSERT_MSG(!typeMgr->unregistered, "AssetTypeManager '%s' is already unregistered", typeMgr->name.CStr());
        typeMgr->unregistered = true;
    }
}

bool Asset::Initialize()
{
    Engine::HelperInitializeProxyAllocator(&gAssetMan.alloc, "AssetMan");
    Engine::HelperInitializeProxyAllocator(&gAssetMan.assetDataAlloc, "AssetMan.Data", &gAssetMan.assetDataAllocBase);
    Engine::HelperInitializeProxyAllocator(&gAssetMan.assetHeaderAlloc, "AssetMan.Headers", &gAssetMan.assetHeaderAllocBase);

    Engine::RegisterProxyAllocator(&gAssetMan.alloc);
    Engine::RegisterProxyAllocator(&gAssetMan.assetDataAlloc);
    Engine::RegisterProxyAllocator(&gAssetMan.assetHeaderAlloc);

    MemAllocator* alloc = &gAssetMan.alloc;

    gAssetMan.assetMutex.Initialize();
    gAssetMan.groupsMutex.Initialize();
    gAssetMan.hashLookupMutex.Initialize();
    gAssetMan.pendingJobsMutex.Initialize();

    gAssetMan.typeManagers.SetAllocator(alloc);
    gAssetMan.assetDb.SetAllocator(alloc);
    gAssetMan.assetLookup.SetAllocator(alloc);
    gAssetMan.assetLookup.Reserve(512);
    gAssetMan.assetHeaderAllocBase.Initialize(alloc, ASSET_HEADER_BUFFER_POOL_SIZE, false);
    gAssetMan.assetDataAllocBase.Initialize(alloc, ASSET_DATA_BUFFER_POOL_SIZE, false);
    gAssetMan.groups.SetAllocator(alloc);
    gAssetMan.assetHashLookup.SetAllocator(alloc);
    gAssetMan.assetHashLookup.Reserve(512);
    gAssetMan.pendingJobs.SetAllocator(alloc);

    gAssetMan.memArena.maxAllocators = Jobs::GetWorkerThreadsCount(JobsType::LongTask);
    gAssetMan.memArena.allocators = NEW_ARRAY(alloc, MemBumpAllocatorVM, gAssetMan.memArena.maxAllocators);
    gAssetMan.memArena.threadToAllocatorTable.SetAllocator(alloc);
    gAssetMan.memArena.threadToAllocatorTable.Reserve(gAssetMan.memArena.maxAllocators);

    gAssetMan.tempAlloc.mAlloc = alloc;

    if (SettingsJunkyard::Get().engine.connectToServer) {
        gAssetMan.tempAlloc.Initialize(SIZE_GB, SIZE_MB);
    }
    else {
        gAssetMan.tempAlloc.Initialize(SIZE_KB*512, SIZE_KB*64);
    }

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

    if constexpr (CONFIG_DEV_MODE) {
        gAssetMan.isHotReloadEnabled = true;
        gAssetMan.hotReloadMutex.Initialize();
        gAssetMan.hotReloadList.SetAllocator(alloc);

        Vfs::RegisterFileChangeCallback(_OnFileChanged);
    }

    gAssetMan.isForceUseCache = SettingsJunkyard::Get().engine.useCacheOnly;

    // Create and mount cache directory
    #if PLATFORM_WINDOWS || PLATFORM_OSX || PLATFORM_LINUX
    if (!OS::IsPathDir(".cache"))
        OS::CreateDir(".cache");
    Vfs::MountLocal(".cache", "cache", false);
    #elif PLATFORM_ANDROID
    Vfs::MountLocal(OS::AndroidGetCacheDirectory(App::AndroidGetActivity()).CStr(), "cache", false);
    #endif

    _LoadAssetHashLookup();

    //------------------------------------------------------------------------------------------------------------------
    // Initialize asset managers here
    if (!Image::InitializeManager()) {
        LOG_ERROR("Failed to initialize ImageManager");
        return false;
    }

    if (!Model::InitializeManager()) {
        LOG_ERROR("Failed to initialize ModelManager");
        return false;
    }

    if (!Shader::InitializeManager()) {
        LOG_ERROR("Failed to initialize ShaderManager");
        return false;
    }

    LOG_INFO("(init) Asset Manager");
    LOG_VERBOSE("Registered asset types:");
    for (AssetTypeManager& tm : gAssetMan.typeManagers)
        LOG_VERBOSE("\t%s", tm.name.CStr());
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

    MemAllocator* alloc = &gAssetMan.alloc;

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

    if (gAssetMan.isHotReloadEnabled) {
        gAssetMan.isHotReloadEnabled = false;
        gAssetMan.hotReloadMutex.Release();
        gAssetMan.hotReloadList.Free();
    }

    Image::ReleaseManager();
    Model::ReleaseManager();
    Shader::ReleaseManager();

    Mem::Free(gAssetMan.memArena.allocators, alloc);
    gAssetMan.memArena.threadToAllocatorTable.Free();
    gAssetMan.tempAlloc.Release();

    gAssetMan.groups.Free();
    gAssetMan.assetDb.Free();
    gAssetMan.assetLookup.Free();
    gAssetMan.assetHashLookup.Free();
    gAssetMan.pendingJobs.Free();
    gAssetMan.typeManagers.Free();

    gAssetMan.assetHeaderAllocBase.Release();
    gAssetMan.assetDataAllocBase.Release();

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
    // ASSERT_MSG(Engine::IsMainThread(), "DestroyGroup can only be called in the main thread");

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

void* Asset::LockObjData(AssetHandle handle)
{
    if (!handle.IsValid())
        return nullptr;

    ReadWriteMutexReadScope rdlock(gAssetMan.assetMutex);
    if (!gAssetMan.assetDb.IsValid(handle)) {
        ASSERT(0);
        return nullptr;
    }

    AssetDataHeader* header = gAssetMan.assetDb.Data(handle);

    // Failed/Loading state
    AssetState state = AssetState(Atomic::LoadExplicit(&header->state, AtomicMemoryOrder::Relaxed));
    if (state != AssetState::Loaded) {
        uint32 typeManIdx = gAssetMan.typeManagers.FindIf(
            [typeId = header->typeId](const AssetTypeManager& typeMgr) { return typeMgr.fourcc == typeId; });
        ASSERT_MSG(typeManIdx != -1, "AssetType with FourCC %x is not registered", header->typeId);
        const AssetTypeManager& typeMan = gAssetMan.typeManagers[typeManIdx];
        if (state == AssetState::Loading)           return typeMan.asyncObj;
        else if (state == AssetState::LoadFailed)   return typeMan.failedObj;
        else                                        return nullptr;
    }

    while (Atomic::ExchangeExplicit(&header->state, uint32(AssetState::Locked), AtomicMemoryOrder::Acquire) == uint32(AssetState::Locked)) {
        uint32 spinCount = 1;
        do {
            if (spinCount++ & 1023)
                OS::PauseCPU();
            else
                Thread::SwitchContext();
        } while (Atomic::LoadExplicit(&header->state, AtomicMemoryOrder::Relaxed)  == uint32(AssetState::Locked));
    }

    return header->data->objData.Get();
}

void Asset::UnlockObjData(AssetHandle handle)
{
    if (!handle.IsValid())
        return;

    ReadWriteMutexReadScope rdlock(gAssetMan.assetMutex);
    if (!gAssetMan.assetDb.IsValid(handle))
        return;

    AssetDataHeader* header = gAssetMan.assetDb.Data(handle);

    AtomicUint32 expectedState = uint32(AssetState::Locked);
    Atomic::CompareExchange_Weak(&header->state, &expectedState, uint32(AssetState::Loaded));
}

const AssetParams* Asset::GetParams(AssetHandle handle)
{
    if (!handle.IsValid())
        return nullptr;

    ReadWriteMutexReadScope rdlock(gAssetMan.assetMutex);
    return gAssetMan.assetDb.IsValid(handle) ? gAssetMan.assetDb.Data(handle)->params : nullptr;
}

void Asset::Update()
{
    PROFILE_ZONE_COLOR(PROFILE_COLOR_ASSET1);
    ASSERT_MSG(Engine::IsMainThread(), "Update can only be called in the main thread");

    if (gAssetMan.curJob) {
        if (gAssetMan.curJobSignal) {
            gAssetMan.curJobSignal->Set();
            gAssetMan.curJobSignal->Raise();
        }

        if (!Jobs::IsRunning(gAssetMan.curJob)) {
            Jobs::Delete(gAssetMan.curJob);
            gAssetMan.curJob = nullptr;
        }
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
                gAssetMan.curJob = Jobs::Dispatch(JobsType::LongTask, Asset::_LoadGroupTask, IntToPtr<uint32>(item.groupHandle.mId), 1);
                break;

            case AssetJobType::Unload:
                gAssetMan.curJob = Jobs::Dispatch(JobsType::LongTask, Asset::_UnloadGroupTask, IntToPtr<uint32>(item.groupHandle.mId), 1);
                break;

            case AssetJobType::Server: 
            {
                // Take a batch of server tasks and run them
                AssetServer& server = gAssetMan.server;
                uint32 numLoadTasks;
                {
                    SpinLockMutexScope lock(server.pendingTasksMutex);
                    numLoadTasks = Min(ASSET_SERVER_MAX_IN_FLIGHT, server.pendingTasks.Count());
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

        uint64 now = Timer::GetTicks();
        float secsElapsed = (float)Timer::ToSec(Timer::Diff(now, lastUpdateTick));
        if (secsElapsed >= ASSET_SAVE_CACHE_LOOKUP_INTERVAL) {
            _SaveAssetHashLookup();
            gAssetMan.isHashLookupUpdated = false;
            lastUpdateTick = now;
        }
    }

    //------------------------------------------------------------------------------------------------------------------
    // Gather hot reload items every N intervals and submit the 
    if (gAssetMan.isHotReloadEnabled) {
        static uint64 lastUpdateTick = 0;
        if (lastUpdateTick == 0) 
            lastUpdateTick = Timer::GetTicks();

        uint64 now = Timer::GetTicks();
        float secsElapsed = (float)Timer::ToSec(Timer::Diff(now, lastUpdateTick));
        if (secsElapsed >= ASSET_HOT_RELOAD_INTERVAL) {
            MutexScope lk(gAssetMan.hotReloadMutex);
            uint32 numItems = Min(gAssetMan.hotReloadList.Count(), ASSET_HOT_RELOAD_MAX_IN_FLIGHT);
            if (numItems) {
                AssetGroup group = Asset::CreateGroup();

                {
                    ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
                    AssetGroupInternal& groupData = gAssetMan.groups.Data(group.mHandle);

                    groupData.loadList.Reserve(numItems);
                    groupData.hotReloadGroup = true;

                    for (uint32 i = 0; i < numItems; i++)
                        groupData.loadList.Push(gAssetMan.hotReloadList[i]);
                    gAssetMan.hotReloadList.ShiftLeft(numItems);
                }

                group.Load();
            }

            lastUpdateTick = now;
        }
    }
}


//     █████╗ ███████╗███████╗███████╗████████╗    ██████╗  █████╗ ████████╗ █████╗ 
//    ██╔══██╗██╔════╝██╔════╝██╔════╝╚══██╔══╝    ██╔══██╗██╔══██╗╚══██╔══╝██╔══██╗
//    ███████║███████╗███████╗█████╗     ██║       ██║  ██║███████║   ██║   ███████║
//    ██╔══██║╚════██║╚════██║██╔══╝     ██║       ██║  ██║██╔══██║   ██║   ██╔══██║
//    ██║  ██║███████║███████║███████╗   ██║       ██████╔╝██║  ██║   ██║   ██║  ██║
//    ╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝   ╚═╝       ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═╝
                                                                                  
void AssetData::AddDependency(AssetHandle* bindToHandle, const AssetParams& params)
{
    ASSERT_MSG(!mData->objData.IsNull(), "You must SetObjData before adding dependencies");
    ASSERT_MSG(mData->numGpuObjects == 0, "AddDependency must be called before adding Gpu objects");

    uint32 typeManIdx = gAssetMan.typeManagers.FindIf(
        [typeId = params.typeId](const AssetTypeManager& typeMgr) { return typeMgr.fourcc == typeId; });
    ASSERT_MSG(typeManIdx != UINT32_MAX, "AssetType with FourCC %x is not registered", params.typeId);
    const AssetTypeManager& typeMan = gAssetMan.typeManagers[typeManIdx];

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

void AssetData::AddGpuTextureObject(GfxImageHandle* bindToImage, const GfxImageDesc& desc, uint32 contentSize, const void* content)
{
    ASSERT_MSG(!mData->objData.IsNull(), "You must SetObjData before adding texture objects");

    AssetDataInternal::GpuObject* gpuObj = Mem::AllocZeroTyped<AssetDataInternal::GpuObject>(1, mAlloc);

    gpuObj->type = AssetDataInternal::GpuObjectType::Image;
    gpuObj->imageDesc.bindToImage = Asset::_TranslatePointer<GfxImageHandle>(bindToImage, mOrigObjPtr, mData->objData.Get());
    gpuObj->imageDesc.createDesc = desc;
    ASSERT(content);
    gpuObj->imageDesc.contentSize = contentSize;
    gpuObj->imageDesc.content = Mem::AllocCopy<uint8>((const uint8*)content, contentSize, mAlloc);
    
    if (mLastGpuObjectPtr) 
        ((AssetDataInternal::GpuObject*)mLastGpuObjectPtr)->next = gpuObj;
    else
        mData->gpuObjects = gpuObj;

    mLastGpuObjectPtr = gpuObj;

    ++mData->numGpuObjects;
}

void AssetData::AddGpuBufferObject(GfxBufferHandle* bindToBuffer, const GfxBufferDesc& desc, const void * content)
{
    ASSERT_MSG(!mData->objData.IsNull(), "You must SetObjData before adding buffer objects");

    AssetDataInternal::GpuObject* gpuObj = Mem::AllocZeroTyped<AssetDataInternal::GpuObject>(1, mAlloc);

    gpuObj->type = AssetDataInternal::GpuObjectType::Buffer;
    gpuObj->bufferDesc.bindToBuffer = Asset::_TranslatePointer<GfxBufferHandle>(bindToBuffer, mOrigObjPtr, mData->objData.Get());
    gpuObj->bufferDesc.createDesc = desc;
    ASSERT(content);
    ASSERT(desc.sizeBytes <= UINT32_MAX);
    gpuObj->bufferDesc.content = Mem::AllocCopy<uint8>((const uint8*)content, uint32(desc.sizeBytes), mAlloc);
    
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


//     █████╗ ███████╗███████╗███████╗████████╗     ██████╗ ██████╗  ██████╗ ██╗   ██╗██████╗ 
//    ██╔══██╗██╔════╝██╔════╝██╔════╝╚══██╔══╝    ██╔════╝ ██╔══██╗██╔═══██╗██║   ██║██╔══██╗
//    ███████║███████╗███████╗█████╗     ██║       ██║  ███╗██████╔╝██║   ██║██║   ██║██████╔╝
//    ██╔══██║╚════██║╚════██║██╔══╝     ██║       ██║   ██║██╔══██╗██║   ██║██║   ██║██╔═══╝ 
//    ██║  ██║███████║███████║███████╗   ██║       ╚██████╔╝██║  ██║╚██████╔╝╚██████╔╝██║     
//    ╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝   ╚═╝        ╚═════╝ ╚═╝  ╚═╝ ╚═════╝  ╚═════╝ ╚═╝     
                                                                                            
void AssetGroup::AddToLoadQueue(const AssetParams* paramsArray, uint32 numAssets, AssetHandle* outHandles) const
{
    ReadWriteMutexReadScope rdlock(gAssetMan.groupsMutex);
    AssetGroupInternal& group = gAssetMan.groups.Data(mHandle);
    ASSERT_MSG(Atomic::LoadExplicit(&group.state, AtomicMemoryOrder::Acquire) == uint32(AssetGroupState::Idle), 
               "AssetGroup should only be populated while it's not loading or unloading");

    MemTempAllocator tempAlloc;
    if (outHandles == nullptr)
        outHandles = tempAlloc.MallocTyped<AssetHandle>(numAssets);

    // TODO: the problem here is that while we are adding to LoadQueue, asset database can change and items are removed or added
    // So put some restrictions or management here
    for (uint32 i = 0; i < numAssets; i++) {
        const AssetParams& params = paramsArray[i];
        AssetHandleResult r = Asset::_CreateOrFetchHandle(params);
        if (r.newlyCreated)
            group.loadList.Push(r.handle);
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

bool AssetGroup::IsValid() const
{
    return mHandle.IsValid();
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

