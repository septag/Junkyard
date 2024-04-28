#include "Shader.h"
#include "AssetManager.h"

#include "../Core/System.h"
#include "../Core/Settings.h"
#include "../Core/Log.h"
#include "../Core/Jobs.h"

#include "../Common/RemoteServices.h"
#include "../Common/VirtualFS.h"
#include "../Common/JunkyardSettings.h"

#include "../Tool/ShaderCompiler.h"

#include "../Graphics/Graphics.h"

static constexpr uint32 SHADER_ASSET_TYPE = MakeFourCC('S', 'H', 'A', 'D');
static constexpr uint32 RCMD_COMPILE_SHADER = MakeFourCC('C', 'S', 'H', 'D');

struct ShaderLoadRequest
{
    AssetHandle handle;
    Allocator* alloc;
    AssetLoaderAsyncCallback loadCallback;
    void* loadCallbackUserData;
};

struct ShaderLoader final : AssetCallbacks
{
    AssetResult Load(AssetHandle handle, const AssetLoadParams& params, uint32 cacheHash, Allocator* dependsAlloc) override;
    void LoadRemote(AssetHandle handle, const AssetLoadParams& params, uint32 cacheHash, void* userData, AssetLoaderAsyncCallback loadCallback) override;
    bool InitializeSystemResources(void*, const AssetLoadParams&) override { return true; }
    bool ReloadSync(AssetHandle handle, void* prevData) override;
    void Release(void* data, Allocator*) override;

    Array<ShaderLoadRequest> requests;
    uint8 _padding[32];
    Mutex requestsMtx;
};

static ShaderLoader gShaderLoader;

// MT: runs in task threads, dispatched by asset server for remote loads
#if CONFIG_TOOLMODE
static void shaderCompileLoadTask(uint32 groupIndex, void* userData)
{
    UNUSED(groupIndex);

    MemTempAllocator tmpAlloc;
    Blob* blob = reinterpret_cast<Blob*>(userData);
    Blob outgoingBlob(&tmpAlloc);
    outgoingBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);

    char filepath[kMaxPath];
    ShaderCompileDesc compileDesc;
    AssetPlatform platform;

    uint32 handle;
    uint32 oldCacheHash;
    blob->Read<uint32>(&handle);
    blob->Read<uint32>(&oldCacheHash);
    blob->ReadStringBinary(filepath, sizeof(filepath));
    blob->Read<uint32>(reinterpret_cast<uint32*>(&platform));
    blob->Read(&compileDesc, sizeof(compileDesc));

    outgoingBlob.Write<uint32>(handle);

    AssetMetaKeyValue* metaData;
    uint32 numMeta;
    if (assetLoadMetaData(filepath, platform, &tmpAlloc, &metaData, &numMeta)) {
        compileDesc.dumpIntermediates |= assetGetMetaValue(metaData, numMeta, "dumpIntermediates", false);
        compileDesc.debug |= assetGetMetaValue(metaData, numMeta, "debug", false);
    }

    uint32 cacheHash = assetMakeCacheHash(AssetCacheDesc {
        .filepath = filepath,
        .loadParams = &compileDesc,
        .loadParamsSize = sizeof(ShaderCompileDesc),
        .metaData = metaData,
        .numMeta = numMeta,
        .lastModified = vfsGetLastModified(filepath)
    });
    
    if (cacheHash != oldCacheHash) {
        TimerStopWatch timer;
        Path shaderAbsolutePath;
        Blob fileBlob = vfsReadFile(filepath, VfsFlags::None, &tmpAlloc, &shaderAbsolutePath);
        if (fileBlob.IsValid()) {
            #if PLATFORM_WINDOWS
            shaderAbsolutePath.ConvertToWin();
            #endif

            // Compilation
            char compileErrorDesc[512];
            Pair<GfxShader*, uint32> shaderCompileResult = shaderCompile(fileBlob, shaderAbsolutePath.CStr(), compileDesc, 
                                                                         compileErrorDesc, sizeof(compileErrorDesc), memDefaultAlloc());
            GfxShader* shader = shaderCompileResult.first;
            uint32 shaderDataSize = shaderCompileResult.second;

            if (shader) {
                outgoingBlob.Write<uint32>(cacheHash);
                outgoingBlob.Write<uint32>(shaderDataSize);
                outgoingBlob.Write(shader, shaderDataSize);
                remoteSendResponse(RCMD_COMPILE_SHADER, outgoingBlob, false, nullptr);
                logVerbose("Shader loaded: %s (%.1f ms)", filepath, timer.ElapsedMS());
            }
            else {
                char errorMsg[kRemoteErrorDescSize];
                strPrintFmt(errorMsg, sizeof(errorMsg), "Compiling shader '%s' failed: %s", filepath, compileErrorDesc);
                remoteSendResponse(RCMD_COMPILE_SHADER, outgoingBlob, true, errorMsg);
                logVerbose(errorMsg);
            }
            memFree(shader);
        }
        else {
            char errorMsg[kRemoteErrorDescSize];
            strPrintFmt(errorMsg, sizeof(errorMsg), "Opening shader file failed: %s", filepath);
            remoteSendResponse(RCMD_COMPILE_SHADER, outgoingBlob, true, errorMsg);
            logVerbose(errorMsg);
        }
    }
    else {
        outgoingBlob.Write<uint32>(cacheHash);
        outgoingBlob.Write<uint32>(0);  // nothing has loaded. it's safe to load from client's local cache
        remoteSendResponse(RCMD_COMPILE_SHADER, outgoingBlob, false, nullptr);
        logVerbose("Shader: %s [cached]", filepath);
    }

    blob->Free();
    memFree(blob);
}
#else
static void shaderCompileLoadTask(uint32, void*)
{
    ASSERT_MSG(0, "None ToolMode builds does not support shader compilation");
}
#endif

static bool shaderCompileShaderHandlerServerFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, Blob*, 
                                               void*, char outgoingErrorDesc[kRemoteErrorDescSize])
{
    ASSERT(cmd == RCMD_COMPILE_SHADER);
    UNUSED(outgoingErrorDesc);
    
    // Get a copy of incomingData and pass it on to a task
    Blob* taskDataBlob = NEW(memDefaultAlloc(), Blob)();
    incomingData.CopyTo(taskDataBlob);
    jobsDispatchAndForget(JobsType::LongTask, shaderCompileLoadTask, taskDataBlob, 1, JobsPriority::Low);
    
    return true;
}

// MT: runs within RemoteServices client-thread context
static void shaderCompileShaderHandlerClientFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, void* userData, bool error, const char* errorDesc)
{
    ASSERT(cmd == RCMD_COMPILE_SHADER);
    UNUSED(userData);
    
    AssetHandle handle {};
    incomingData.Read<uint32>(&handle.mId);
    ASSERT(handle.IsValid());

    Allocator* alloc = nullptr;
    AssetLoaderAsyncCallback loadCallback = nullptr;
    void* loadCallbackUserData = nullptr;

    { MutexScope mtx(gShaderLoader.requestsMtx);
        if (uint32 reqIndex = gShaderLoader.requests.FindIf([handle](const ShaderLoadRequest& req) { return req.handle == handle; });
            reqIndex != UINT32_MAX)
        {
            alloc = gShaderLoader.requests[reqIndex].alloc;
            loadCallback = gShaderLoader.requests[reqIndex].loadCallback;
            loadCallbackUserData = gShaderLoader.requests[reqIndex].loadCallbackUserData;
            gShaderLoader.requests.RemoveAndSwap(reqIndex);
        }
        else {
            alloc = memDefaultAlloc();
            ASSERT(0);
        }
    }

    if (!error) {
        uint32 cacheHash = 0;
        uint32 shaderBufferSize = 0;
        void* shaderData = nullptr;
        incomingData.Read<uint32>(&cacheHash);
        incomingData.Read<uint32>(&shaderBufferSize);

        if (shaderBufferSize) {
            shaderData = memAlloc(shaderBufferSize, alloc);
            incomingData.Read(shaderData, shaderBufferSize);
            ((GfxShader*)shaderData)->hash = uint32(handle);
        }

        if (loadCallback) {
            loadCallback(handle, AssetResult { .obj = shaderData, .objBufferSize = shaderBufferSize, .cacheHash = cacheHash }, 
                         loadCallbackUserData);
        }
    }
    else {
        logError(errorDesc);
        if (loadCallback)
            loadCallback(handle, AssetResult {}, loadCallbackUserData);
    }
}

bool _private::assetInitializeShaderManager()
{
    #if CONFIG_TOOLMODE
        if (!shaderInitializeCompiler())
            return false;
    #endif

    // Register asset loader
    assetRegisterType(AssetTypeDesc {
        .fourcc = SHADER_ASSET_TYPE,
        .name = "Shader",
        .callbacks = &gShaderLoader,
        .extraParamTypeName = "ShaderCompileDesc",
        .extraParamTypeSize = sizeof(ShaderCompileDesc),
        .failedObj = nullptr,
        .asyncObj = nullptr
    });

    remoteRegisterCommand(RemoteCommandDesc {
        .cmdFourCC = RCMD_COMPILE_SHADER,
        .serverFn = shaderCompileShaderHandlerServerFn,
        .clientFn = shaderCompileShaderHandlerClientFn,
        .async = true
    });
    gShaderLoader.requestsMtx.Initialize();

    logInfo("(init) Shader asset manager");

    return true;
}

void _private::assetReleaseShaderManager()
{
    #if CONFIG_TOOLMODE
        shaderReleaseCompiler();
    #endif
    assetUnregisterType(SHADER_ASSET_TYPE);
    gShaderLoader.requestsMtx.Release();
    gShaderLoader.requests.Free();
}

AssetHandleShader assetLoadShader(const char* path, const ShaderLoadParams& desc, AssetBarrier barrier)
{
    AssetLoadParams loadParams {
        .path = path,
        .alloc = memDefaultAlloc(), // TODO: should be able to use custom allocator
        .typeId = SHADER_ASSET_TYPE,
        .barrier = barrier
    };

    return AssetHandleShader { assetLoad(loadParams, &desc) };
}

GfxShader* assetGetShader(AssetHandleShader shaderHandle)
{
    return reinterpret_cast<GfxShader*>(_private::assetGetData(shaderHandle));
}

// MT: Runs from a task thread (AssetManager) for local loads
AssetResult ShaderLoader::Load(AssetHandle handle, const AssetLoadParams& params, uint32 cacheHash, Allocator*)
{
    #if CONFIG_TOOLMODE
        ASSERT(params.next);
        MemTempAllocator tmpAlloc;
        ShaderCompileDesc compileDesc = *reinterpret_cast<ShaderCompileDesc*>(params.next.Get());

        AssetMetaKeyValue* metaData;
        uint32 numMeta;
        const SettingsGraphics& graphicsSettings = settingsGet().graphics;
        if (assetLoadMetaData(handle, &tmpAlloc, &metaData, &numMeta)) {
            compileDesc.dumpIntermediates |= assetGetMetaValue(metaData, numMeta, "dumpIntermediates", false);
            compileDesc.debug |= assetGetMetaValue(metaData, numMeta, "debug", false);
        }
        
        compileDesc.dumpIntermediates |= graphicsSettings.shaderDumpIntermediates;
        compileDesc.debug |= graphicsSettings.shaderDebug;

        uint32 newCacheHash = assetMakeCacheHash(AssetCacheDesc {
            .filepath = params.path,
            .loadParams = params.next.Get(), 
            .loadParamsSize = sizeof(ShaderCompileDesc),
            .metaData = metaData,
            .numMeta = numMeta,
            .lastModified = vfsGetLastModified(params.path)
        });

        if (newCacheHash != cacheHash) {
            Path shaderAbsolutePath;
            Blob blob = vfsReadFile(params.path, VfsFlags::None, &tmpAlloc, &shaderAbsolutePath);
            if (!blob.IsValid()) {
                logError("Opening shader file failed: %s", params.path);
                return AssetResult {};
            }

            #if PLATFORM_WINDOWS
            shaderAbsolutePath.ConvertToWin();
            #endif

            char errorDiag[1024];
            Pair<GfxShader*, uint32> shader = shaderCompile(blob, shaderAbsolutePath.CStr(), compileDesc, errorDiag, sizeof(errorDiag), params.alloc);
            if (shader.first)
                shader.first->hash = uint32(handle);
            else 
                logError("Compiling shader '%s' failed: %s", params.path, errorDiag);
            return AssetResult { .obj = shader.first, .objBufferSize = shader.second, .cacheHash = newCacheHash };
        }
        else {
            return AssetResult { .cacheHash = newCacheHash };
        }
    #else
        UNUSED(cacheHash);
        UNUSED(params);
        UNUSED(handle);
        ASSERT_MSG(0, "None ToolMode builds does not support shader compilation");
        return AssetResult {};
    #endif
}

void ShaderLoader::LoadRemote(AssetHandle handle, const AssetLoadParams& params, uint32 cacheHash, void* userData, 
                              AssetLoaderAsyncCallback loadCallback)
{
    ASSERT(params.next);
    ASSERT(loadCallback);
    ASSERT(remoteIsConnected());

    ShaderCompileDesc compileDesc = *reinterpret_cast<ShaderCompileDesc*>(params.next.Get());

    { 
        MutexScope mtx(gShaderLoader.requestsMtx);
        gShaderLoader.requests.Push(ShaderLoadRequest {
            .handle = handle,
            .alloc = params.alloc,
            .loadCallback = loadCallback,
            .loadCallbackUserData = userData
        });
    }

    const SettingsGraphics& graphicsSettings = settingsGet().graphics;
    compileDesc.debug |= graphicsSettings.shaderDebug;
    compileDesc.dumpIntermediates |= graphicsSettings.shaderDumpIntermediates;

    MemTempAllocator tmpAlloc;
    Blob outgoingBlob(&tmpAlloc);
    outgoingBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);

    outgoingBlob.Write<uint32>(uint32(handle));
    outgoingBlob.Write<uint32>(cacheHash);
    outgoingBlob.WriteStringBinary(params.path, strLen(params.path));
    outgoingBlob.Write<uint32>(static_cast<uint32>(params.platform));
    outgoingBlob.Write(&compileDesc, sizeof(compileDesc));

    remoteExecuteCommand(RCMD_COMPILE_SHADER, outgoingBlob);

    outgoingBlob.Free();
}

bool ShaderLoader::ReloadSync(AssetHandle handle, void* prevData)
{
    UNUSED(handle);
    UNUSED(prevData);

    GfxShader* oldShader = reinterpret_cast<GfxShader*>(prevData);
    GfxShader* newShader = reinterpret_cast<GfxShader*>(_private::assetGetData(handle));

    if (newShader == nullptr)
        return false;
    ASSERT(oldShader);

    // Compare the two, if any gloval state, like vertex layout or input params don't match, do not reload
    if (oldShader->numStages != newShader->numStages)
        return false;

    if (oldShader->numParams != newShader->numParams)
        return false;

    if (oldShader->numVertexAttributes != newShader->numVertexAttributes)
        return false;

    if (memcmp(oldShader->vertexAttributes.Get(), newShader->vertexAttributes.Get(), 
        newShader->numVertexAttributes*sizeof(GfxShaderVertexAttributeInfo)) != 0)
    {
        return false;
    }

    if (memcmp(oldShader->params.Get(), newShader->params.Get(), 
        newShader->numParams*sizeof(GfxShaderParameterInfo)) != 0)
    {
        return false;
    }

    _private::gfxRecreatePipelinesWithNewShader(newShader->hash, newShader);
    return true;
}

void ShaderLoader::Release(void* data, Allocator* alloc)
{
    memFree(data, alloc);
}


