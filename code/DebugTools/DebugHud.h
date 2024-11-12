#pragma once

#include "../Core/Base.h"

namespace DebugHud
{
    API void DrawDebugHud(float dt, bool *pOpen = nullptr);
    API void DrawStatusBar(float dt);

    API void Initialize();
    API void Release();
}

