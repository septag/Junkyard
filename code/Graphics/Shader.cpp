#include "Shader.h"

#include "../Core/Buffers.h"
#include "../Core/System.h"
#include "../Core/Settings.h"
#include "../Core/Log.h"
#include "../Core/Jobs.h"

#include "../RemoteServices.h"
#include "../AssetManager.h"
#include "../VirtualFS.h"

#include "../Tool/ShaderCompiler.h"

static constexpr uint32 kShaderAssetType = MakeFourCC('S', 'H', 'A', 'D');
static constexpr uint32 kRemoteCmdCompileShader = MakeFourCC('C', 'S', 'H', 'D');

struct ShaderLoadRequest
{
    AssetHandle handle;
    Allocator* alloc;
    AssetLoaderAsyncCallback loadCallback;
    void* loadCallbackUserData;
};

struct ShaderLoader final : AssetLoaderCallbacks
{
    AssetResult Load(AssetHandle handle, const AssetLoadParams& params, Allocator* dependsAlloc) override;
    void LoadRemote(AssetHandle handle, const AssetLoadParams& params, void* userData, AssetLoaderAsyncCallback loadCallback) override;
    bool InitializeResources(void*, const AssetLoadParams&) override { return true; }
    bool ReloadSync(AssetHandle handle, void* prevData) override;
    void Release(void* data, Allocator*) override;

    Mutex requestsMtx;
    Array<ShaderLoadRequest> requests;
};

static ShaderLoader gShaderLoader;

// MT: runs in task threads, dispatched by asset server
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
    blob->Read<uint32>(&handle);
    blob->ReadStringBinary(filepath, sizeof(filepath));
    blob->Read<uint32>(reinterpret_cast<uint32*>(&platform));
    blob->Read(&compileDesc, sizeof(compileDesc));

    outgoingBlob.Write<uint32>(handle);
    
    TimerStopWatch timer;
    Blob fileBlob = vfsReadFile(filepath, VfsFlags::None, &tmpAlloc);
    if (fileBlob.IsValid()) {
        // Load meta-data
        AssetMetaKeyValue* metaData;
        uint32 numMeta;
        if (assetLoadMetaData(filepath, platform, &tmpAlloc, &metaData, &numMeta)) {
            compileDesc.dumpIntermediates |= assetGetMetaValue(metaData, numMeta, "dumpIntermediates", false);
            compileDesc.debug |= assetGetMetaValue(metaData, numMeta, "debug", false);
        }

        // Compilation
        char compileErrorDesc[512];
        Pair<Shader*, uint32> shaderCompileResult = shaderCompile(fileBlob, filepath, compileDesc, compileErrorDesc, 
                                                                  sizeof(compileErrorDesc), memDefaultAlloc());
        Shader* shader = shaderCompileResult.first;
        uint32 shaderDataSize = shaderCompileResult.second;

        if (shader) {
            outgoingBlob.Write<uint32>(shaderDataSize);
            outgoingBlob.Write(shader, shaderDataSize);
            remoteSendResponse(kRemoteCmdCompileShader, outgoingBlob, false, nullptr);
            logVerbose("Shader loaded: %s (%.1f ms)", filepath, timer.ElapsedMS());
        }
        else {
            char errorMsg[kRemoteErrorDescSize];
            strPrintFmt(errorMsg, sizeof(errorMsg), "Compiling shader '%s' failed: %s", filepath, compileErrorDesc);
            remoteSendResponse(kRemoteCmdCompileShader, outgoingBlob, true, errorMsg);
            logVerbose(errorMsg);
        }
        memFree(shader);
    }
    else {
        char errorMsg[kRemoteErrorDescSize];
        strPrintFmt(errorMsg, sizeof(errorMsg), "Opening shader file failed: %s", filepath);
        remoteSendResponse(kRemoteCmdCompileShader, outgoingBlob, true, errorMsg);
        logVerbose(errorMsg);
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
    ASSERT(cmd == kRemoteCmdCompileShader);
    UNUSED(outgoingErrorDesc);
    
    // Get a copy of incomingData and pass it on to a task
    Blob* taskDataBlob = NEW(memDefaultAlloc(), Blob)();
    incomingData.CopyTo(taskDataBlob);
    jobsDispatchAuto(JobsType::LongTask, shaderCompileLoadTask, taskDataBlob, 1, JobsPriority::Low);
    
    return true;
}

// MT: runs within RemoteServices client-thread context
static void shaderCompileShaderHandlerClientFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, void* userData, bool error, const char* errorDesc)
{
    ASSERT(cmd == kRemoteCmdCompileShader);
    UNUSED(userData);
    
    AssetHandle handle {};
    incomingData.Read<uint32>(&handle.id);
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
        uint32 shaderBufferSize;
        [[maybe_unused]] size_t readBytes = incomingData.Read<uint32>(&shaderBufferSize);
        ASSERT(readBytes == sizeof(shaderBufferSize));

        void* shaderData = memAlloc(shaderBufferSize, alloc);
        incomingData.Read(shaderData, shaderBufferSize);

        ((Shader*)shaderData)->hash = handle.id;
        if (loadCallback)
            loadCallback(handle, AssetResult { .obj = shaderData, .objBufferSize = shaderBufferSize }, loadCallbackUserData);
    }
    else {
        logError(errorDesc);
        if (loadCallback)
            loadCallback(handle, AssetResult {}, loadCallbackUserData);
    }
}

bool _private::shaderInitialize()
{
    #if CONFIG_TOOLMODE
        if (!_private::shaderInitializeCompiler())
            return false;
    #endif

    // Register asset loader
    assetRegister(AssetTypeDesc {
        .fourcc = kShaderAssetType,
        .name = "Shader",
        .callbacks = &gShaderLoader,
        .extraParamTypeName = "ShaderCompileDesc",
        .extraParamTypeSize = sizeof(ShaderCompileDesc),
        .failedObj = nullptr,
        .asyncObj = nullptr
    });

    remoteRegisterCommand(RemoteCommandDesc {
        .cmdFourCC = kRemoteCmdCompileShader,
        .serverFn = shaderCompileShaderHandlerServerFn,
        .clientFn = shaderCompileShaderHandlerClientFn,
        .async = true
    });
    gShaderLoader.requestsMtx.Initialize();

    return true;
}

void _private::shaderRelease()
{
    #if CONFIG_TOOLMODE
        _private::shaderReleaseCompiler();
    #endif
    assetUnregister(kShaderAssetType);
    gShaderLoader.requestsMtx.Release();
    gShaderLoader.requests.Free();
}

AssetHandleShader assetLoadShader(const char* path, const ShaderCompileDesc& desc, AssetBarrier barrier)
{
    AssetLoadParams loadParams {
        .path = path,
        .alloc = memDefaultAlloc(), // TODO: should be able to use custom allocator
        .typeId = kShaderAssetType,
        .barrier = barrier
    };

    return AssetHandleShader { assetLoad(loadParams, &desc) };
}

Shader* assetGetShader(AssetHandleShader shaderHandle)
{
    return reinterpret_cast<Shader*>(_private::assetGetData(shaderHandle));
}

const ShaderStageInfo* shaderGetStage(const Shader& info, ShaderStage stage)
{
    for (uint32 i = 0; i < info.numStages; i++) {
        if (info.stages[i].stage == stage) {
            return &info.stages[i];
        }
    }
    return nullptr;
}

const ShaderParameterInfo* shaderGetParam(const Shader& info, const char* name)
{
    for (uint32 i = 0; i < info.numParams; i++) {
        if (strIsEqual(info.params[i].name, name))
            return &info.params[i];
    }
    return nullptr;
}

// MT: Runs from a task thread (AssetManager)
AssetResult ShaderLoader::Load(AssetHandle handle, const AssetLoadParams& params, Allocator*)
{
    #if CONFIG_TOOLMODE
        ASSERT(params.next);
        ShaderCompileDesc compileDesc = *reinterpret_cast<ShaderCompileDesc*>(params.next.Get());
        MemTempAllocator tmpAlloc;
        Blob blob = vfsReadFile(params.path, VfsFlags::None, &tmpAlloc);
        if (!blob.IsValid()) {
            logError("Opening shader file failed: %s", params.path);
            return AssetResult {};
        }

        // Load meta-data
        const SettingsGraphics& graphicsSettings = settingsGetGraphics();
        AssetMetaKeyValue* metaData;
        uint32 numMeta;
        if (assetLoadMetaData(handle, &tmpAlloc, &metaData, &numMeta)) {
            compileDesc.dumpIntermediates |= assetGetMetaValue(metaData, numMeta, "dumpIntermediates", graphicsSettings.shaderDumpIntermediates);
            compileDesc.debug |= assetGetMetaValue(metaData, numMeta, "debug", graphicsSettings.shaderDebug);
        }
        else {
            compileDesc.dumpIntermediates |= graphicsSettings.shaderDumpIntermediates;
            compileDesc.debug |= graphicsSettings.shaderDebug;
        }

        char errorDiag[512];
        Pair<Shader*, uint32> shader = shaderCompile(blob, params.path, compileDesc, errorDiag, sizeof(errorDiag), params.alloc);
        if (shader.first) {
            shader.first->hash = handle.id;
        }
        else {
            logError("Compiling shader '%s' failed: %s", params.path, errorDiag);
        }
        return AssetResult { .obj = shader.first, .objBufferSize = shader.second };
    #else
        ASSERT_MSG(0, "None ToolMode builds does not support shader compilation");
        return AssetResult {};
    #endif
}

void ShaderLoader::LoadRemote(AssetHandle handle, const AssetLoadParams& params, void* userData, AssetLoaderAsyncCallback loadCallback)
{
    ASSERT(params.next);
    ASSERT(loadCallback);
    ASSERT(remoteIsConnected());

    ShaderCompileDesc compileDesc = *reinterpret_cast<ShaderCompileDesc*>(params.next.Get());

    { MutexScope mtx(gShaderLoader.requestsMtx);
        gShaderLoader.requests.Push(ShaderLoadRequest {
            .handle = handle,
            .alloc = params.alloc,
            .loadCallback = loadCallback,
            .loadCallbackUserData = userData
        });
    }

    const SettingsGraphics& graphicsSettings = settingsGetGraphics();
    compileDesc.debug |= graphicsSettings.shaderDebug;
    compileDesc.dumpIntermediates |= graphicsSettings.shaderDumpIntermediates;

    MemTempAllocator tmpAlloc;
    Blob outgoingBlob(&tmpAlloc);
    outgoingBlob.SetGrowPolicy(Blob::GrowPolicy::Multiply);

    outgoingBlob.Write<uint32>(handle.id);
    outgoingBlob.WriteStringBinary(params.path, strLen(params.path));
    outgoingBlob.Write<uint32>(static_cast<uint32>(params.platform));
    outgoingBlob.Write(&compileDesc, sizeof(compileDesc));

    remoteExecuteCommand(kRemoteCmdCompileShader, outgoingBlob);

    outgoingBlob.Free();
}

bool ShaderLoader::ReloadSync(AssetHandle handle, void* prevData)
{
    UNUSED(handle);
    UNUSED(prevData);

    Shader* oldShader = reinterpret_cast<Shader*>(prevData);
    Shader* newShader = reinterpret_cast<Shader*>(_private::assetGetData(handle));

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
        newShader->numVertexAttributes*sizeof(ShaderVertexAttributeInfo)) != 0)
    {
        return false;
    }

    if (memcmp(oldShader->params.Get(), newShader->params.Get(), 
        newShader->numParams*sizeof(ShaderParameterInfo)) != 0)
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


