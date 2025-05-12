#include "Shader.h"
#include "AssetManager.h"

#include "../Common/VirtualFS.h"
#include "../Common/JunkyardSettings.h"

#include "../Tool/ShaderCompiler.h"

#include "../Graphics/GfxBackend.h"

static constexpr uint32 SHADER_ASSET_TYPE = MakeFourCC('S', 'H', 'A', 'D');

struct AssetShaderImpl final : AssetTypeImplBase
{
    bool Bake(const AssetParams& params, AssetData* data, const Span<uint8>& srcData, String<256>* outErrorDesc) override;
    bool Reload(void* newData, void* oldData) override;
};

static AssetShaderImpl gShaderImpl;

bool Shader::InitializeManager()
{
    // Register asset loader
    AssetTypeDesc shaderTypeDesc {
        .fourcc = SHADER_ASSET_TYPE,
        .name = "Shader",
        .impl = &gShaderImpl,
        .extraParamTypeName = "ShaderCompileDesc",
        .extraParamTypeSize = sizeof(ShaderCompileDesc),
        .failedObj = nullptr,
        .asyncObj = nullptr
    };

    Asset::RegisterType(shaderTypeDesc);

    return true;
}

void Shader::ReleaseManager()
{
    #if CONFIG_TOOLMODE
    ShaderCompiler::ReleaseLiveSessions();
    #endif

    Asset::UnregisterType(SHADER_ASSET_TYPE);
}

bool AssetShaderImpl::Bake(const AssetParams& params, AssetData* data, const Span<uint8>& srcData, String<256>* outErrorDesc)
{
    #if CONFIG_TOOLMODE
        MemTempAllocator tmpAlloc;
        ShaderCompileDesc compileDesc = *((ShaderCompileDesc*)params.extraParams);

        compileDesc.dumpIntermediates |= data->GetMetaValue("dumpIntermediates", false);
        compileDesc.debug |= data->GetMetaValue("debug", false);

        const SettingsGraphics& graphicsSettings = SettingsJunkyard::Get().graphics;
        compileDesc.dumpIntermediates |= graphicsSettings.shaderDumpIntermediates;
        compileDesc.debug |= graphicsSettings.shaderDebug;

        Path shaderAbsolutePath = Vfs::ResolveFilepath(params.path.CStr());
        if constexpr (PLATFORM_WINDOWS)
            shaderAbsolutePath.ConvertToWin();
            
        char errorDiag[256];
        Pair<GfxShader*, uint32> shader = ShaderCompiler::Compile(srcData, shaderAbsolutePath.CStr(), compileDesc, errorDiag, sizeof(errorDiag), &tmpAlloc);
        if (!shader.first) {
            outErrorDesc->FormatSelf("Compiling shader failed: %s", errorDiag);
            return false;
        }

        // Used for reloading pipelines in Graphics subsystem
        shader.first->hash = data->mParamsHash;

        data->SetObjData(shader.first, shader.second);
        return true;
    #else
        UNUSED(params);
        UNUSED(data);
        UNUSED(srcData);
        UNUSED(outErrorDesc);
        ASSERT_MSG(0, "None ToolMode builds does not support shader compilation");
        return false;
    #endif
}

bool AssetShaderImpl::Reload(void* newData, void* oldData) 
{
    GfxShader* oldShader = reinterpret_cast<GfxShader*>(oldData);
    GfxShader* newShader = reinterpret_cast<GfxShader*>(newData);

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

    GfxBackend::ReloadShaderPipelines(*newShader);
    return true;
    
}

AssetHandleShader Shader::Load(const char* path, const ShaderLoadParams& params, const AssetGroup& group)
{
    AssetParams assetParams {
        .typeId = SHADER_ASSET_TYPE,
        .path = path,
        .extraParams = const_cast<ShaderLoadParams*>(&params)
    };

    return group.AddToLoadQueue(assetParams);
}


