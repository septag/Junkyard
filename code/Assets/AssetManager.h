#pragma once

#include "../Core/Base.h"
#include "../Core/System.h"

#include "../Common/CommonTypes.h"

struct AssetDataInternal;
struct GfxImageDesc;
struct GfxBufferDesc;
struct AssetData;
struct AssetTypeImplBase;
struct GfxBufferDesc;

struct AssetMetaKeyValue
{
    String32 key;
    String32 value;
};

struct AssetMetaData
{
    uint32 numKeyValues;
    AssetMetaKeyValue keyValues;
};

struct AssetPlatform 
{
    enum Enum {
        Auto = 0,
        PC,
        Mobile 
    };

    static const char* ToStr(Enum platform)
    {
        switch (platform) {
        case AssetPlatform::PC:        return "pc";
        case AssetPlatform::Mobile:    return "mobile";
        default:                       return "unknown";
        }
    }
};

enum class AssetState : uint32
{
    Zombie = 0,
    Loading,
    Loaded,
    LoadFailed,
    Locked
};

DEFINE_HANDLE(AssetGroupHandle);
DEFINE_HANDLE(AssetHandle);

struct AssetParams
{
    uint32 typeId;                  // FourCC code of the asset type
    Path path;                      
    AssetPlatform::Enum platform;
    void* extraParams;               // Pointer to extra asset loading parameters. See `AssetTypeDesc.extraParamTypeSize`
};

enum AssetGroupState : uint32
{
    Idle = 0,
    Loading,
    Loaded,
    Unloading
};

struct AssetTypeDesc
{
    uint32 fourcc;                  // Unique asset type identifier
    uint32 cacheVersion;            // Cache version per asset type. You should bump this cache version if the underlying asset data has changed
    const char* name;               // For verbosity (logging, debugging and such)
    AssetTypeImplBase* impl;        // Asset type implementation. Since every asset type has it's own data and is baked differently
    const char* extraParamTypeName; // (optional) For verbosity. "Param types" are custom types that are passed to asset load function
    uint32 extraParamTypeSize;      // (optional) Extra paramters struct size for loading an asset. Basically `sizeof` of that struct.
    void* failedObj;                // the pointer to a static data that asset manager returns when asset fails to load
    void* asyncObj;                 // the pointer to a static data that asset manager returns while it's loading the asset
};


//     █████╗ ██████╗ ██╗
//    ██╔══██╗██╔══██╗██║
//    ███████║██████╔╝██║
//    ██╔══██║██╔═══╝ ██║
//    ██║  ██║██║     ██║
//    ╚═╝  ╚═╝╚═╝     ╚═╝

struct AssetGroup
{
    AssetGroupHandle mHandle;

    void AddToLoadQueue(const AssetParams* paramsArray, uint32 numAssets, AssetHandle* outHandles = nullptr) const;
    AssetHandle AddToLoadQueue(const AssetParams& params) const;

    void Load();
    void Unload();

    // Waits for assets to load. This is not recommended since it blocks the thread, unless you really have to
    void Wait();    
    bool IsValid() const;
    bool IsLoadFinished() const;
    bool IsIdle() const;
    AssetGroupState GetState() const;
    bool HasItemsInQueue() const;

    Span<AssetHandle> GetAssetHandles(MemAllocator* alloc) const;
};

struct NO_VTABLE AssetTypeImplBase
{
    // Main implementation. Parsing asset data file format and extra baking all happens inside this function. 
    // Inputs are `params` and `srcData` (raw asset binary data). Outputs are `data` and `outErrorDesc` in case of errors
    // Return true if assets loads succesfully and false if failed (then fill out `outErrorDesc`)
    virtual bool Bake(const AssetParams& params, AssetData* data, const Span<uint8>& srcData, String<256>* outErrorDesc) = 0;

    // [optional] Mainly used for extra book keeping that engine might need after reloading an asset type. 
    virtual bool Reload(void* newData, void* oldData) = 0;
};

struct AssetData
{
    void AddDependency(AssetHandle* bindToHandle, const AssetParams& params);
    void AddGpuTextureObject(GfxImageHandle* bindToImage, const GfxImageDesc& desc, uint32 contentSize, const void* content);
    void AddGpuBufferObject(GfxBufferHandle* bindToBuffer, const GfxBufferDesc& desc, const void* content);
    void SetObjData(const void* data, uint32 dataSize);

    const char* GetMetaValue(const char* key, const char* defaultValue) const;
    inline uint32 GetMetaValue(const char* key, uint32 defaultValue) const;
    inline float GetMetaValue(const char* key, float defaultValue) const;
    inline bool GetMetaValue(const char* key, bool defaultValue) const;

    MemAllocator* mAlloc;
    AssetDataInternal* mData;
    void* mLastDependencyPtr;
    void* mLastGpuObjectPtr;
    const void* mOrigObjPtr;      // Original pointer that has passed to SetObjData. We use it later for calculating handle pointer offsets
    uint32 mParamsHash;
};

namespace Asset
{
    bool Initialize();
    void Release();

    void RegisterType(const AssetTypeDesc& desc);
    void UnregisterType(uint32 fourcc);

    API AssetGroup CreateGroup();
    API void DestroyGroup(AssetGroup& group);

    API void Update();

    const AssetParams* GetParams(AssetHandle handle);

    void* LockObjData(AssetHandle handle);
    void UnlockObjData(AssetHandle handle);
}

template <typename _T>
struct AssetObjPtrScope
{
    AssetObjPtrScope() = delete;
    AssetObjPtrScope(const AssetObjPtrScope&) = delete;

    explicit AssetObjPtrScope(AssetHandle handle) : mHandle(handle), mObjPtr((_T*)Asset::LockObjData(handle)) {}
    ~AssetObjPtrScope() { Asset::UnlockObjData(mHandle); }

    _T* operator->() { return Get(); }
    const _T* operator->() const { return Get(); }
    _T& operator*() const { return *Get(); }
    bool operator!() const { return IsNull(); }
    operator bool() const { return !IsNull(); }

    bool IsNull() const { return mObjPtr == nullptr; };
    operator _T*() { return Get(); }
    operator const _T*() const { return Get(); }

    inline _T* Get() const
    {
        ASSERT(!IsNull());
        return mObjPtr;
    }

private:
    AssetHandle mHandle;
    _T* mObjPtr;
};


//    ██╗███╗   ██╗██╗     ██╗███╗   ██╗███████╗███████╗
//    ██║████╗  ██║██║     ██║████╗  ██║██╔════╝██╔════╝
//    ██║██╔██╗ ██║██║     ██║██╔██╗ ██║█████╗  ███████╗
//    ██║██║╚██╗██║██║     ██║██║╚██╗██║██╔══╝  ╚════██║
//    ██║██║ ╚████║███████╗██║██║ ╚████║███████╗███████║
//    ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝╚═╝  ╚═══╝╚══════╝╚══════╝
inline uint32 AssetData::GetMetaValue(const char* key, uint32 defaultValue) const
{
    const char* value = GetMetaValue(key, "");
    return value ? Str::ToUint(value) : defaultValue;
}

inline float AssetData::GetMetaValue(const char* key, float defaultValue) const
{
    const char* value = GetMetaValue(key, "");
    return value ? static_cast<float>(Str::ToDouble(value)) : defaultValue;
}

inline bool AssetData::GetMetaValue(const char* key, bool defaultValue) const
{
    const char* value = GetMetaValue(key, "");
    return value ? Str::ToBool(value) : defaultValue;
}
