#pragma once

#include "../Core/Base.h"
#include "../Core/System.h"

#include "../CommonTypes.h"

struct Blob;
struct MemTlsfAllocator;

struct AssetMetaKeyValue
{
    String32 key;
    String32 value;
};

enum class AssetPlatform : uint32
{
    Auto = 0,
    PC,
    Android
};

INLINE const char* assetPlatformGetStr(AssetPlatform platform)
{
    switch (platform) {
    case AssetPlatform::PC:         return "pc";
    case AssetPlatform::Android:    return "android";
    default:                        return "unknown";
    }
}

struct AssetLoadParams  
{
    const char* path;
    Allocator* alloc;
    uint32 typeId;
    uint32 tags;
    AssetPlatform platform;
    AssetBarrier barrier;       // Barriers are a way to sync and group multiple assets. With barriers, you can wait on a group of assets to get loaded
    RelativePtr<uint8> next;    // pointer to the next arbitary struct. the type of 'next' depends on user-defined asset-loader 
    bool dontCreateResources;   // Skip creating GPU/External resources (LoadResources function is not called)
};

enum class AssetState : uint32
{
    Zombie = 0,
    Alive,
    LoadFailed,
    Loading
};

struct AssetDependency
{
    Path path;
    AssetLoadParams params;
};

struct AssetInfo
{
    uint32 typeId;
    AssetState state;
    uint32 tags;
    uint32 refCount;
    const char* path;
    const AssetDependency* depends;
    uint32 numDepends;
};

struct AssetBudgetStats
{
    uint32 numAssets;
    uint32 maxAssets;
    uint32 numTypes;
    uint32 maxTypes;
    uint32 numBarriers;
    uint32 maxBarriers;
    uint32 maxGarbage;
    uint32 numGarbage;
    size_t initHeapStart;
    size_t initHeapSize;
    size_t runtimeHeapSize;
    size_t runtimeHeapMax;
    MemTlsfAllocator* runtimeHeap;
};

struct AssetResult
{
    void* obj;
    AssetDependency* depends;
    uint32 numDepends;
    uint32 dependsBufferSize;   // Only used in assetLoadObjRemote where we need to copy the whole depends buffer 
    uint32 objBufferSize;
    uint32 cacheHash;
};

using AssetLoaderAsyncCallback = void(*)(AssetHandle handle, const AssetResult& result, void* userData);

// This is a way to extend asset types. Override from this struct and implement the routines for your own asset type
// See assetRegister/assetUnregister functions
struct NO_VTABLE AssetCallbacks 
{
    virtual AssetResult Load(AssetHandle handle, const AssetLoadParams& params, uint32 cacheHash, Allocator* dependsAlloc) = 0;
    virtual void LoadRemote(AssetHandle handle, const AssetLoadParams& params, uint32 cacheHash, 
                            void* userData, AssetLoaderAsyncCallback loadCallback) = 0;
    virtual bool InitializeSystemResources(void* obj, const AssetLoadParams& params) = 0;
    virtual void Release(void* obj, Allocator* alloc) = 0;

    // Return true if the asset can be reloaded, otherwise returning 'false' indicates that the asset could not get reloaded and old one stays in memory
    virtual bool ReloadSync(AssetHandle handle, void* prevData) = 0;
};

struct AssetTypeDesc
{
    uint32 fourcc;
    const char* name;
    AssetCallbacks* callbacks;
    const char* extraParamTypeName;
    uint32 extraParamTypeSize;          // Note: be careful that in order for asset caching to work properly. this size must exactly match the real underlying struct size with no extra padding
    void* failedObj;
    void* asyncObj;
};

struct AssetCacheDesc
{
    const char* filepath;
    const void* loadParams;
    uint32 loadParamsSize;
    const AssetMetaKeyValue* metaData;
    uint32 numMeta;
    uint64 lastModified;
};

API void assetRegister(const AssetTypeDesc& desc);
API void assetUnregister(uint32 fourcc);

API AssetHandle assetLoad(const AssetLoadParams& params, const void* extraParams);
API void assetUnload(AssetHandle handle);
API AssetInfo assetGetInfo(AssetHandle handle);
API bool assetIsAlive(AssetHandle handle);
API AssetHandle assetAddRef(AssetHandle handle);

API AssetBarrier assetCreateBarrier();
API void assetDestroyBarrier(AssetBarrier barrier);
API bool assetWait(AssetBarrier barrier, uint32 msecs = UINT32_MAX);

API void assetCollectGarbage();

API bool assetLoadMetaData(const char* filepath, AssetPlatform platform, Allocator* alloc,
                           AssetMetaKeyValue** outData, uint32* outKeyCount);
API bool assetLoadMetaData(AssetHandle handle, Allocator* alloc, AssetMetaKeyValue** outData, uint32* outKeyCount);
API const char* assetGetMetaValue(const AssetMetaKeyValue* data, uint32 count, const char* key);
template <typename _T> _T assetGetMetaValue(const AssetMetaKeyValue* data, uint32 count, const char* key, _T defaultValue);

API uint32 assetMakeCacheHash(const AssetCacheDesc& desc);

API void assetGetBudgetStats(AssetBudgetStats* stats);

// Scope RAII classes are only recommended for use within a single function scope only
struct AssetBarrierScope
{
    inline AssetBarrierScope() : _barrier(assetCreateBarrier()), _ownsBarrier(true) {}
    inline AssetBarrierScope(AssetBarrier& barrier) : _barrier(barrier), _ownsBarrier(false) {}
    inline ~AssetBarrierScope() { assetWait(_barrier); if (_ownsBarrier) assetDestroyBarrier(_barrier); }

    AssetBarrierScope(const AssetBarrier&) = delete;
    AssetBarrierScope& operator=(const AssetBarrier&) = delete;

    AssetBarrier Barrier() const { return _barrier; }

private:
    AssetBarrier _barrier;
    bool _ownsBarrier;
};

namespace _private
{
    bool assetInitialize();
    void assetRelease();

    // This is the type-unsafe and generic version to get asset data
    // We wrap this function inside whatever asset implementation is out there
    // See Graphics.h:assetGetImage for an example
    void* assetGetData(AssetHandle handle);

    void* assetGetDataUnsafe(AssetHandle handle);

    void assetUpdateCache(float dt);
}

//------------------------------------------------------------------------
template <> inline bool assetGetMetaValue(const AssetMetaKeyValue* data, uint32 count, const char* key, bool defaultValue)
{
    const char* value = assetGetMetaValue(data, count, key);
    return value ? strToBool(value) : defaultValue;
}

template <> inline uint32 assetGetMetaValue(const AssetMetaKeyValue* data, uint32 count, const char* key, uint32 defaultValue)
{
    const char* value = assetGetMetaValue(data, count, key);
    return value ? strToUint(value) : defaultValue;
}

template <> inline float assetGetMetaValue(const AssetMetaKeyValue* data, uint32 count, const char* key, float defaultValue)
{
    const char* value = assetGetMetaValue(data, count, key);
    return value ? static_cast<float>(strToDouble(value)) : defaultValue;
}

template <> inline String32 assetGetMetaValue(const AssetMetaKeyValue* data, uint32 count, const char* key, String32 defaultValue)
{
    const char* value = assetGetMetaValue(data, count, key);
    return value ? String32(value) : defaultValue;
}
