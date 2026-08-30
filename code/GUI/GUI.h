#pragma once

#include "../Graphics/GfxBackend.h"
#include "../External/clay/clay.h"

namespace GUI
{
    bool Initialize();
    void Release();

    void Begin();
    void End(GfxCommandBuffer cmd);

    bool IsEnabled();
} // namespace GUI