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

struct ShaderLoader : AssetLoaderCallbacks
{
    AssetResult Load(AssetHandle handle, const AssetLoadParams& params, Allocator* dependsAlloc) override;
    void  LoadRemote(AssetHandle handle, const AssetLoadParams& params, void* userData, AssetLoaderAsyncCallback loadCallback) override;
    bool  ReloadSync(AssetHandle handle, void* prevData) override;
    void  Release(void* data, Allocator*) override;

    Mutex requestsMtx;
    Array<ShaderLoadRequest> requests;
};

static ShaderLoader gShaderLoader;

[[maybe_unused]] static void shaderSerializeToBlob(Shader* info, Blob* blob)
{
    ASSERT(info);
    blob->Write<uint32>(info->numStages);
    blob->Write<uint32>(info->numParams);
    blob->Write<uint32>(info->numVertexAttributes);

    for (uint32 i = 0; i < info->numStages; i++) {
        const ShaderStageInfo& stage = info->stages[i];
        blob->Write<ShaderStage>(stage.stage);
        blob->Write<char[32]>(stage.entryName);
        blob->Write<int64>(stage.blob.size);
        if (stage.blob.size > 0) 
            blob->Write(stage.blob.data, stage.blob.size);
    }

    if (info->numParams)
        blob->Write(info->params, sizeof(ShaderParameterInfo)*info->numParams);

    if (info->numVertexAttributes)
        blob->Write(info->vertexAttributes, sizeof(ShaderVertexAttributeInfo)*info->numVertexAttributes);
}

static Shader* shaderSerializeFromBlob(const Blob& blob, Allocator* alloc)
{
    ASSERT(alloc);

    uint32 numStages = 0, numParams = 0, numVertexAttributes = 0;
    blob.Read<uint32>(&numStages);
    blob.Read<uint32>(&numParams);
    blob.Read<uint32>(&numVertexAttributes);

    BuffersAllocPOD<Shader> shaderInfoAlloc;
    if (numStages)
        shaderInfoAlloc.AddMemberField<ShaderStageInfo>(offsetof(Shader, stages), numStages);
    if (numParams)
        shaderInfoAlloc.AddMemberField<ShaderParameterInfo>(offsetof(Shader, params), numParams);
    if (numVertexAttributes)
        shaderInfoAlloc.AddMemberField<ShaderVertexAttributeInfo>(offsetof(Shader, vertexAttributes), numVertexAttributes);
    Shader* info = shaderInfoAlloc.Calloc(alloc);
    
    for (uint32 i = 0; i < numStages; i++) {
        ShaderStageInfo& stage = info->stages[i];
        blob.Read<ShaderStage>(&stage.stage);
        blob.Read(stage.entryName, sizeof(stage.entryName));
        blob.Read<int64>(&stage.blob.size);
        if (stage.blob.size) {
            stage.blob.data = memAlloc(stage.blob.size, alloc);
            blob.Read(stage.blob.data, stage.blob.size);
        }
    }

    if (numParams)
        blob.Read(info->params, sizeof(ShaderParameterInfo)*numParams);
    if (numVertexAttributes)
        blob.Read(info->vertexAttributes, sizeof(ShaderVertexAttributeInfo)*numVertexAttributes);

    info->numStages = numStages;
    info->numParams = numParams;
    info->numVertexAttributes = numVertexAttributes;
    info->alloc = alloc;
    return info;
}

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
        Pair<Shader*, uint32> shader = shaderCompile(fileBlob, filepath, compileDesc, compileErrorDesc, 
                                                     sizeof(compileErrorDesc), &tmpAlloc);
        if (shader.first) {
            outgoingBlob.Write<uint32>(shader.second);
            shaderSerializeToBlob(shader.first, &outgoingBlob);
            remoteSendResponse(kRemoteCmdCompileShader, outgoingBlob, false, nullptr);
            logVerbose("Shader loaded: %s (%.1f ms)", filepath, timer.ElapsedMS());
        }
        else {
            char errorMsg[kRemoteErrorDescSize];
            strPrintFmt(errorMsg, sizeof(errorMsg), "Compiling shader '%s' failed: %s", filepath, compileErrorDesc);
            remoteSendResponse(kRemoteCmdCompileShader, outgoingBlob, true, errorMsg);
            logVerbose(errorMsg);
        }
    }
    else {
        char errorMsg[kRemoteErrorDescSize];
        strPrintFmt(errorMsg, sizeof(errorMsg), "Opening shader file failed: %s", filepath);
        remoteSendResponse(kRemoteCmdCompileShader, outgoingBlob, true, errorMsg);
        logVerbose(errorMsg);
    }

    fileBlob.Free();
    outgoingBlob.Free();
    blob->Free();
    blob->~Blob();
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
static void shaderCompileShaderHandlerClientFn([[maybe_unused]] uint32 cmd, const Blob& incomingData, void* userData, 
    bool error, const char* errorDesc)
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
        uint32 bufferSize;
        [[maybe_unused]] size_t readBytes = incomingData.Read<uint32>(&bufferSize);
        ASSERT(readBytes == sizeof(bufferSize));

        Shader* info = shaderSerializeFromBlob(incomingData, alloc);
        ASSERT(info);

        info->hash = handle.id;
        if (loadCallback)
            loadCallback(handle, AssetResult {info}, loadCallbackUserData);
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

static void shaderDestroy(Shader* info)
{
    ASSERT(info);

    if (info->alloc) {
        for (uint32 i = 0; i < info->numStages; i++) {
            if (info->stages[i].blob.data) 
                memFree(info->stages[i].blob.data, info->alloc);
        }
        memFree(info, info->alloc);
    }
}

const ShaderStageInfo* shaderGetStage(const Shader* info, ShaderStage stage)
{
    for (uint32 i = 0; i < info->numStages; i++) {
        if (info->stages[i].stage == stage) {
            return &info->stages[i];
        }
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
    Shader* newShader = reinterpret_cast<Shader*>(_private::assetGetDataUnsafe(handle));

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

    if (memcmp(oldShader->vertexAttributes, newShader->vertexAttributes, 
        newShader->numVertexAttributes*sizeof(ShaderVertexAttributeInfo)) != 0)
    {
        return false;
    }

    if (memcmp(oldShader->params, newShader->params, 
        newShader->numParams*sizeof(ShaderParameterInfo)) != 0)
    {
        return false;
    }

    _private::gfxRecreatePipelinesWithNewShader(newShader->hash, newShader);
    return true;
}

void ShaderLoader::Release(void* data, Allocator*)
{
    ASSERT(data);

    shaderDestroy(reinterpret_cast<Shader*>(data));
}




