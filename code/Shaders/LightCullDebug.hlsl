#ifndef TILE_SIZE
#define TILE_SIZE 16
#endif

#ifndef MAX_LIGHTS_PER_TILE
#define MAX_LIGHTS_PER_TILE 4
#endif

StructuredBuffer<uint> VisibleLightIndices;

[[vk_push_constant]]
cbuffer PerFrameData
{
    uint TilesCountX : packoffset(c0);
    uint TilesCountY : packoffset(c0.y);
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

[shader("fragment")]
float4 PsMain(PsInput i) : SV_Target
{
    uint2 loc = uint2(i.pos.xy);
    uint2 tileId = loc / uint2(TILE_SIZE, TILE_SIZE);
    uint index = tileId.y * TilesCountX + tileId.x;

    uint startIdx = index * MAX_LIGHTS_PER_TILE;
    uint i;
    for (i = 0; i < MAX_LIGHTS_PER_TILE; i++) {
        if (VisibleLightIndices[startIdx + i] == -1)
            break;
    }

    // Move from blue (less lights) to red (maximum lights)
    float value = pow(float(i) / float(MAX_LIGHTS_PER_TILE), 0.4);
    return i == 0 ? float4(0, 0, 0, 1.0) : float4(value, 0, 1 - value, 1.0);
}
