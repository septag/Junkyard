#pragma once

#include "../Core/MathTypes.h"

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

    API void DrawGroundGrid(const Camera& cam, float viewWidth, float viewHeight, const DebugDrawGridProperties& props);
}
