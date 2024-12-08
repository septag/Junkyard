#pragma once

#include "../Core/Base.h"

struct GfxBackendCommandBuffer
{
    uint32 mGeneration;
    uint16 mCmdBufferIndex;
    uint16 mQueueIndex;

    void Begin();
    void End();
};

enum class GfxBackendQueueType : uint32
{
    None = 0,
    Graphics = 0x1,
    Compute = 0x2,
    Transfer = 0x4,
    Present = 0x8
};
ENABLE_BITMASK(GfxBackendQueueType);

namespace GfxBackend
{
    bool Initialize();
    void Release();

    void Begin();
    void End();

    bool SubmitQueue(GfxBackendQueueType queueType);

    [[nodiscard]] GfxBackendCommandBuffer NewCommandBuffer(GfxBackendQueueType queueType);
} // Gfx
