#pragma once

#include "../Core/MathTypes.h"

union Mat4;
struct Camera;

struct DebugDrawGridProperties
{
    float spacing = 1.0f;
    float boldSpacing = 5.0f;
    float distance = 20.0f;
    Color lineColor = COLOR_WHITE;
    Color boldLineColor = COLOR_WHITE;
};

API void ddDrawGrid_XYAxis(const Camera& cam, float viewWidth, float viewHeight, const DebugDrawGridProperties& props);

namespace _private
{
    bool ddInitialize();
    void ddRelease();
}
