#pragma once

#include "../Core/Base.h"
#include "../Core/System.h"

#include "../Common/CommonTypes.h"

#include "../Tool/ShaderCompiler.h" // ShaderCompileDesc

struct GfxShader;
struct AssetGroup;

struct ShaderLoadParams
{
    ShaderCompileDesc compileDesc;  
};

API AssetHandleShader assetLoadShader(const char* path, const ShaderLoadParams& desc, AssetBarrier barrier = AssetBarrier());
API GfxShader* assetGetShader(AssetHandleShader shaderHandle);

namespace _private 
{
    bool assetInitializeShaderManager();
    void assetReleaseShaderManager();
}

namespace Asset
{
    API AssetHandleShader LoadShader(const char* path, const ShaderLoadParams& params, const AssetGroup& group);
    API GfxShader* GetShader(AssetHandleShader shaderHandle);
}



