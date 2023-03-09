#ifndef __GFX_TOOLS_CPP__
#define __GFX_TOOLS_CPP__

// Check for dependencies
#ifndef __GRAPHICS_VK_CPP__
    #error "This file depends on Graphics.cpp for compilation"
#endif

#include "GfxTools.h"

GfxDynamicUniformBuffer gfxCreateDynamicUniformBuffer(uint32 count, uint32 stride)
{
    ASSERT_MSG(count > 1, "Why not just use a regular uniform buffer ?");
    ASSERT(stride);
    ASSERT(gVk.deviceProps.limits.minUniformBufferOffsetAlignment);

    stride = AlignValue(stride, uint32(gVk.deviceProps.limits.minUniformBufferOffsetAlignment));

    GfxBuffer buffer = gfxCreateBuffer(GfxBufferDesc {
        .size = stride * count,
        .type = GfxBufferType::Uniform,
        .usage = GfxBufferUsage::Stream
    });

    if (!buffer.IsValid())
        return GfxDynamicUniformBuffer {};

    AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::BUFFERS]);
    GfxBufferData& bufferData = gVk.pools.buffers.Data(buffer);

    return GfxDynamicUniformBuffer {
        .buffer = buffer,
        .bufferPtr = (uint8*)bufferData.mappedBuffer,
        .stride = stride,
        .count = count
    };
}

void gfxDestroyDynamicUniformBuffer(GfxDynamicUniformBuffer& buffer)
{
    gfxDestroyBuffer(buffer.buffer);
    memset(&buffer, 0x0, sizeof(buffer));
}

bool GfxDynamicUniformBuffer::IsValid() const
{
    return buffer.IsValid() && gVk.pools.buffers.IsValid(buffer);
}

void GfxDynamicUniformBuffer::Flush(const GfxDyanmicUniformBufferRange* ranges, uint32 numRanges)
{
    VmaAllocation allocation;
    {
        AtomicLockScope lk(gVk.pools.locks[GfxObjectPools::BUFFERS]);
        GfxBufferData& bufferData = gVk.pools.buffers.Data(this->buffer);
        allocation = bufferData.allocation;
    }

    MemTempAllocator tmpAlloc;
    size_t* offsets = tmpAlloc.MallocTyped<size_t>(numRanges);
    size_t* sizes = tmpAlloc.MallocTyped<size_t>(numRanges);

    for (uint32 i = 0; i < numRanges; i++) {
        offsets[i] = ranges[i].index * this->stride;
        sizes[i] = ranges[i].count * this->stride;
    }
    
    [[maybe_unused]] VkResult r = vmaFlushAllocations(gVk.vma, 1, &allocation, offsets, sizes);
    ASSERT(r == VK_SUCCESS);
}

#endif // __GFX_TOOLS_CPP__

