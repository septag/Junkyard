#pragma once

#include "../Core/Base.h"

using DebugHudMemoryStatsCallback = void(*)(void* userData);

namespace DebugHud
{
    API void DrawDebugHud(float dt, float yOffset = 0);
    API void DrawStatusBar(float dt);
    API void RegisterMemoryStats(const char* name, DebugHudMemoryStatsCallback callback, void* userData = nullptr);

    API void Initialize();
    API void Release();
}

