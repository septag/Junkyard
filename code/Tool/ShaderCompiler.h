#pragma once

#include "../Graphics/Shader.h"

#if CONFIG_TOOLMODE
// Note: `alloc` should not be tmpAlloc
API Pair<Shader*, uint32> shaderCompile(const Blob& blob, const char* filepath, const ShaderCompileDesc& desc, 
                                        char* errorDiag, uint32 errorDiagSize, Allocator* alloc);

namespace _private
{
    bool shaderInitializeCompiler();
    void shaderReleaseCompiler();
}
#endif
