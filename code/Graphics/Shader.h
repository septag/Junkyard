#pragma once

#include "../Core/Base.h"
#include "../Core/Memory.h"
#include "../Core/System.h"

#include "../CommonTypes.h"

#include "Graphics.h"

static inline constexpr uint32 kShaderMaxDefines = 4;
static inline constexpr uint32 kShaderMaxIncludeDirs = 2;

struct Blob;

#pragma pack(push, 8)
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

struct ShaderBlob
{
    void* data;
    int64 size;
};

struct ShaderStageInfo
{
    ShaderStage     stage;
    char            entryName[32];
    ShaderBlob      blob;
};

enum class ShaderParameterType : uint32
{
    Uniformbuffer,
    Samplerstate,
    Resource
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
    char                            name[32];
    uint32                          hash;           // This is actually the AssetId of the shader
    uint32                          numStages;
    uint32                          numParams;
    uint32                          numVertexAttributes;
    Allocator*                      alloc;
    ShaderStageInfo*                stages;
    ShaderParameterInfo*            params;
    ShaderVertexAttributeInfo*      vertexAttributes;
};
#pragma pack(pop)

API const ShaderStageInfo* shaderGetStage(const Shader* info, ShaderStage stage);

API AssetHandleShader assetLoadShader(const char* path, const ShaderCompileDesc& desc, AssetBarrier barrier = AssetBarrier());
API Shader* assetGetShader(AssetHandleShader shaderHandle);

namespace _private 
{
    bool shaderInitialize();
    void shaderRelease();
}



