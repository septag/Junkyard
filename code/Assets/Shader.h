#pragma once

#include "../Core/Base.h"
#include "../Core/System.h"

#include "../Common/CommonTypes.h"

#include "../Tool/ShaderCompiler.h" // ShaderCompileDesc

struct AssetGroup;

struct ShaderLoadParams
{
    ShaderCompileDesc compileDesc;  
};

namespace Shader
{
    API bool InitializeManager();
    API void ReleaseManager();

    // DataType: AssetObjPtrScope<GfxShader>
    API AssetHandleShader Load(const char* path, const ShaderLoadParams& params, const AssetGroup& group);
}



