#pragma once

#include "Graphics.h"

struct GfxDyanmicUniformBufferRange
{
    uint32 index;
    uint32 count;
};

struct GfxDynamicUniformBuffer
{
    void* Data(uint32 index);
    bool IsValid() const;
    void Flush(const GfxDyanmicUniformBufferRange* ranges, uint32 numRanges);
    void Flush(uint32 index, uint32 _count);

    GfxBuffer buffer;
    uint8* bufferPtr;
    uint32 stride;
    uint32 count;
};

GfxDynamicUniformBuffer gfxCreateDynamicUniformBuffer(uint32 count, uint32 stride);
void gfxDestroyDynamicUniformBuffer(GfxDynamicUniformBuffer& buffer);

//----------------------------------------------------------------------------------------------------------------------
// @impl GfxDynamicUniforBuffer
inline void* GfxDynamicUniformBuffer::Data(uint32 index)
{
    #ifdef CONFIG_CHECK_OUTOFBOUNDS    
        ASSERT_MSG(index < count, "Out of bounds access for dynamic buffer");
    #endif
    
    return bufferPtr + stride*index;
}

inline void GfxDynamicUniformBuffer::Flush(uint32 index, uint32 _count)
{
    GfxDyanmicUniformBufferRange range { index, _count };
    Flush(&range, 1);
}



