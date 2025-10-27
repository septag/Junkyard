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

// Note: API is not thread-safe, all calls should happen in a single thread
namespace DebugDraw
{
    bool Initialize();
    void Release();

    API void BeginDraw(GfxCommandBuffer cmd, const Camera& cam, uint16 viewWidth, uint16 viewHeight);
    API void EndDraw(GfxCommandBuffer cmd, GfxImageHandle depthImage);

    API void DrawGroundGrid(const Camera& cam, const DebugDrawGridProperties& props);
    API void DrawBoundingSphere(Float4 sphere, Color4u color, uint32 numRings = 8, uint32 numSectors= 12);
}
