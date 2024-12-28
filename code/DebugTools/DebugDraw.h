#pragma once

#include "../Core/MathTypes.h"

#include "../Graphics/GfxBackend.h"

struct Camera;

struct DebugDrawGridProperties
{
    float spacing = 1.0f;
    float boldSpacing = 5.0f;
    float distance = 20.0f;
    Color lineColor = COLOR_WHITE;
    Color boldLineColor = COLOR_WHITE;
};

namespace DebugDraw
{
    bool Initialize();
    void Release();

    API void BeginDraw(GfxBackendCommandBuffer cmd, uint16 viewWidth, uint16 viewHeight);
    API void DrawGroundGrid(const Camera& cam, const DebugDrawGridProperties& props);
    API void EndDraw(GfxBackendCommandBuffer cmd, const Camera& cam, GfxImageHandle depthImage);
}
