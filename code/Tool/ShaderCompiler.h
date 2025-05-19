#pragma once

#include "../Core/System.h"

static inline constexpr uint32 SHADER_MAX_DEFINES = 4;
static inline constexpr uint32 SHADER_MAX_INCLUDE_DIRS = 2;

struct ShaderDefine
{
    String32 define;
    String32 value;
};

struct ShaderCompileDesc
{
    uint32 numDefines;
    uint32 numIncludeDirs;
    ShaderDefine defines[SHADER_MAX_DEFINES];
    Path includeDirs[SHADER_MAX_INCLUDE_DIRS];
    bool dumpIntermediates;
    bool debug;
    uint8 _padding[2];      // Structure should have no holes because we are hashing it for asset-manager
};

#if CONFIG_TOOLMODE

struct GfxShader;
struct MemAllocator;

namespace ShaderCompiler
{
    // Note: `alloc` should not be tmpAlloc
    API Pair<GfxShader*, uint32> Compile(const Span<uint8>& sourceCode, const char* filepath, const ShaderCompileDesc& desc, 
                                         char* errorDiag, uint32 errorDiagSize, MemAllocator* alloc);
    API void ReleaseLiveSessions();
}

#endif
