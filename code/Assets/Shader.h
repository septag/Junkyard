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

namespace Asset
{
    API bool InitializeShaderManager();
    API void ReleaseShaderManager();

    API AssetHandleShader LoadShader(const char* path, const ShaderLoadParams& params, const AssetGroup& group);
}



