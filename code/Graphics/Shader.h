#pragma once

#include "../Core/Base.h"
#include "../Core/Memory.h"
#include "../Core/System.h"

#include "../CommonTypes.h"

#include "Graphics.h"

static inline constexpr uint32 kShaderMaxDefines = 4;
static inline constexpr uint32 kShaderMaxIncludeDirs = 2;

struct Blob;

struct ShaderDefine
{
    String32 define;
    String32 value;
};

struct ShaderIncludeDir
{
    Path includeDir;
};

struct ShaderCompileDesc
{
    uint32            numDefines;
    uint32            numIncludeDirs;
    ShaderDefine      defines[kShaderMaxDefines];
    ShaderIncludeDir  includeDirs[kShaderMaxIncludeDirs];
    bool dumpIntermediates;
    bool debug;
};

enum class ShaderStage : uint32
{
    Unknown = 0,
    Vertex,
    Fragment,
    Compute,
};

struct ShaderStageInfo
{
    ShaderStage stage;
    char entryName[32];
    uint32 dataSize;
    RelativePtr<uint8> data;
};

enum class ShaderParameterType : uint32
{
    Uniformbuffer,
    Samplerstate,
    Resource,
    Array
};

struct ShaderParameterInfo
{
    char                name[32];
    ShaderParameterType type;
    ShaderStage         stage;
    uint32              bindingIdx;
    bool                isPushConstant;
};

struct ShaderVertexAttributeInfo 
{
    char        name[32];
    char        semantic[16];
    uint32      semanticIdx;
    uint32      location;
    GfxFormat   format;
};

struct Shader
{
    char   name[32];
    uint32 hash;           // This is actually the AssetId of the shader
    uint32 numStages;
    uint32 numParams;
    uint32 numVertexAttributes;
    RelativePtr<ShaderStageInfo> stages;
    RelativePtr<ShaderParameterInfo> params;
    RelativePtr<ShaderVertexAttributeInfo> vertexAttributes;
};

API const ShaderStageInfo* shaderGetStage(const Shader& info, ShaderStage stage);
API const ShaderParameterInfo* shaderGetParam(const Shader& info, const char* name);

API AssetHandleShader assetLoadShader(const char* path, const ShaderCompileDesc& desc, AssetBarrier barrier = AssetBarrier());
API Shader* assetGetShader(AssetHandleShader shaderHandle);

namespace _private 
{
    bool shaderInitialize();
    void shaderRelease();
}



