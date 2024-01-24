#pragma once

#include "../Core/Base.h"
#include "../Core/Allocators.h"
#include "../Core/System.h"

#include "../CommonTypes.h"

#include "../Tool/ShaderCompiler.h" // ShaderCompileDesc

struct GfxShader;

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



