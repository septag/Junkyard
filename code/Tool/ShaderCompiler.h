#pragma once

#include "../Core/System.h"

static inline constexpr uint32 kShaderMaxDefines = 4;
static inline constexpr uint32 kShaderMaxIncludeDirs = 2;

struct ShaderDefine
{
    String32 define;
    String32 value;
};

struct ShaderCompileDesc
{
    uint32 numDefines;
    uint32 numIncludeDirs;
    ShaderDefine defines[kShaderMaxDefines];
    Path includeDirs[kShaderMaxIncludeDirs];
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
