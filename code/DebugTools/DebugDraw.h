#pragma once

#include "../Core/MathTypes.h"

#include "../Graphics/GfxBackend.h"

struct Camera;

struct DebugDrawGridProperties
{
    float spacing = 1.0f;
    float boldSpacing = 5.0f;
    float distance = 20.0f;
    Color4u lineColor = COLOR4U_WHITE;
    Color4u boldLineColor = COLOR4U_WHITE;
};

namespace DebugDraw
{
    bool Initialize();
    void Release();

    API void BeginDraw(GfxCommandBuffer cmd, uint16 viewWidth, uint16 viewHeight);
    API void DrawGroundGrid(const Camera& cam, const DebugDrawGridProperties& props);
    API void EndDraw(GfxCommandBuffer cmd, const Camera& cam, GfxImageHandle depthImage);
}
