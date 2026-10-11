#ifndef TILE_SIZE
#define TILE_SIZE 16
#endif

#ifndef MAX_LIGHTS_PER_TILE
#define MAX_LIGHTS_PER_TILE 4
#endif

StructuredBuffer<uint> VisibleLightIndices;

[vk_push_constant]
cbuffer PerFrameData
{
    uint TilesCountX;
    uint TilesCountY;
};

// Single fullscreen triangle (No input vertex buffer needed)
// Reference: https : // www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
struct PsInput
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
PsInput VsMain(uint vertexId : SV_VertexID)
{
    PsInput o;

    const float2 pos[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 3.0f),
        float2( 3.0, -1.0f)
    };

    o.pos = float4(pos[vertexId], 0, 1.0f);
    o.uv = 0.5f * (pos[vertexId] + 1.0f); // map from [-1, 1] -> [0, 1]
    return o;
}

float3 HSV_To_RGB(float3 hsv)
{
    float4 K = float4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
    float3 p = abs(frac(hsv.xxx + K.xyz) * 6.0f - K.www);
    return hsv.x * lerp(K.xxx, saturate(p - K.xxx), hsv.y);
}

[shader("fragment")]
float4 PsMain(PsInput i) : SV_Target
{
    uint2 loc = uint2(i.pos.xy);
    uint2 tileId = loc / uint2(TILE_SIZE, TILE_SIZE);
    if (tileId.x >= TilesCountX || tileId.y >= TilesCountY)
        return float4(0, 0, 0, 1);
    uint index = tileId.y * TilesCountX + tileId.x;

    // Bounded loop: Out of range or uninitialized data must not hang the GPU
    // Note that a full tile has no sentinel (See LightCull.hlsl)
    uint startIdx = index * MAX_LIGHTS_PER_TILE;
    uint visibleLightCount = 0;
    while (visibleLightCount < MAX_LIGHTS_PER_TILE && VisibleLightIndices[startIdx + visibleLightCount] != -1)
        visibleLightCount++;

    // Move from blue (less lights) to red (maximum lights)
    float value = pow(float(visibleLightCount) / float(MAX_LIGHTS_PER_TILE), 0.4);
    float hue = lerp(0.66, 0, saturate(value)); // 0.66=blue, 0=red
    return float4(HSV_To_RGB(float3(hue, 1.0f, 1.0)), 1.0f);
}
