#pragma once

#include "../Math/MathTypes.h"

union Mat4;
struct Camera;

struct DebugDrawGridProperties
{
    float spacing = 1.0f;
    float boldSpacing = 5.0f;
    float distance = 20.0f;
    Color lineColor = kColorWhite;
    Color boldLineColor = kColorWhite;
};

API void ddDrawGrid_XYAxis(const Camera& cam, float viewWidth, float viewHeight, const DebugDrawGridProperties& props);

namespace _private
{
    bool ddInitialize();
    void ddRelease();
}
