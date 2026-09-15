#pragma once

#include "../Graphics/GfxBackend.h"
#include "../External/clay/clay.h"

namespace GUI
{
    bool Initialize();
    void Release();

    void Begin(RectInt viewRect, bool processInput);
    void End(GfxCommandBuffer& cmd);
    void Draw(GfxCommandBuffer& cmd);

    bool IsEnabled();
} // namespace GUI