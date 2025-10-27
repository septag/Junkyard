// Note: Calculations currently transformed and done in view-space. 
//       This means that light cull is much faster but also much more conservative. 
//       Basically instead of sphere, tests with the AABB bounding box of the light
//       Reasons:
//          1) For tile plane tests, I project a pyramid from camera origin to the edge of the tile at the far plane
//          2) For z tests, I conservatively check min/max Z of the tile to the center of plane +- radius. which is also basically a distance range
//       We can go more precise depending on the light model. But I'm not planning to add too many lights to the scene or implement complicated light models
//
#ifndef TILE_SIZE
#define TILE_SIZE 16
#endif

#ifndef MAX_LIGHTS_PER_TILE
#define MAX_LIGHTS_PER_TILE 4
#endif

#ifndef MSAA
#define MSAA 0
#endif

struct PointLightCull
{
    float3 position;
    float radius;
};

cbuffer PerFrameData
{
    float4x4 WorldToViewMat : packoffset(c0);
    float4x4 ClipToViewMat : packoffset(c4);
    float CameraNear : packoffset(c8);
    float CameraFar : packoffset(c8.y);
    uint NumLights : packoffset(c9);
    uint WindowWidth : packoffset(c9.y);
    uint WindowHeight : packoffset(c9.z);
};

StructuredBuffer<PointLightCull> Lights;
RWStructuredBuffer<uint> VisibleLightIndices;

#if MSAA
Texture2DMS<float> DepthTexture;
#else
Texture2D<float> DepthTexture;
#endif

groupshared uint MinDepthInTile;
groupshared uint MaxDepthInTile;
groupshared uint NumLightsInTile;
groupshared uint LightIndicesInTile[MAX_LIGHTS_PER_TILE];
groupshared float4 TileFrustumPlanes[4];

float ComputeViewDepth(float ndcZ, float near, float far)
{
    return near * far / (far - ndcZ * (far - near));
}

float3 Unproject(float2 ndcPos)
{
    float4 clipPos = float4(ndcPos.xy, 1.0, 1.0);   // z = 1: far view
    float4 viewPos = mul(ClipToViewMat, clipPos);
    return viewPos.xyz / viewPos.w;
}

// Plane::From3Points (C++ code)
// But in here, the first point is the origin (0), so we can simplify it
// d=0, cuz we know that the plane passes the origin (d = -dot(normal, p0))
float4 CreatePlane(float3 p1, float3 p2)
{
    float3 normal = normalize(cross(p1, p2));
    return float4(normal.xyz, 0);
}

[shader("compute")]
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void CsMain(uint3 globalPos : SV_DispatchThreadID, 
            uint3 tileId : SV_GroupID, 
            uint localIndex : SV_GroupIndex)
{
    // Create frustum planes for each tile
    // Frustum is calculated only once per each tile
    if (localIndex == 0) {
        MinDepthInTile = 0x7f7fffff; // FLT_MAX
        MaxDepthInTile = 0;
        NumLightsInTile = 0;

        // Tile points (CW)
        float3 tilePts[4];
        float2 rcpScreenSize = float2(rcp(float(WindowWidth)), rcp(float(WindowHeight)));
    
        float2 tileTopLeft = float2(tileId.x * TILE_SIZE, tileId.y * TILE_SIZE);
        tilePts[0] = Unproject(2.0f * tileTopLeft * rcpScreenSize - 1);     // top-left
        tilePts[1] = Unproject(2.0f * (tileTopLeft + float2(TILE_SIZE, 0)) * rcpScreenSize - 1); // top-right
        tilePts[2] = Unproject(2.0f * (tileTopLeft + float2(TILE_SIZE, TILE_SIZE)) * rcpScreenSize - 1); // bottom-right
        tilePts[3] = Unproject(2.0f * (tileTopLeft + float2(0, TILE_SIZE)) * rcpScreenSize - 1);   // bottom-left
    
        TileFrustumPlanes[0] = CreatePlane(tilePts[0], tilePts[1]); // top-edge
        TileFrustumPlanes[1] = CreatePlane(tilePts[1], tilePts[2]); // right-edge
        TileFrustumPlanes[2] = CreatePlane(tilePts[2], tilePts[3]); // bottom-edge
        TileFrustumPlanes[3] = CreatePlane(tilePts[3], tilePts[0]); // left-edge
    }

    GroupMemoryBarrierWithGroupSync();

    // Min/Max depth calculation for each tile
    {
#if MSAA
        uint depthBufferWidth, depthBufferHeight, depthBufferNumSamples;
        DepthTexture.GetDimensions(depthBufferWidth, depthBufferHeight, depthBufferNumSamples);
        float minZ = 3.402823466e+38F;  // FLT_MAX: IEE-754
        float maxZ = 0;
        float depth0 = DepthTexture.Load(globalPos.xy, 0).x;
        float viewDepth0 = ComputeViewDepth(depth0, CameraNear, CameraFar);
        if (depth0 != 0) {
            maxZ = max(maxZ, viewDepth0);
            minZ = min(minZ, viewDepth0);
        }

        for (uint sampleIdx = 1; sampleIdx < depthBufferNumSamples; sampleIdx++) {
            float depth = DepthTexture.Load(globalPos.xy, sampleIdx).x;
            float viewDepth = ComputeViewDepth(depth, CameraNear, CameraFar);
            if (depth != 0) {
                maxZ = max(maxZ, viewDepth);
                minZ = min(minZ, viewDepth);
            }
        }

        InterlockedMax(MaxDepthInTile, asuint(maxZ));
        InterlockedMin(MinDepthInTile, asuint(minZ));
#else
        float depth = DepthTexture.Load(uint3(globalPos.xy, 0)).x;
        uint viewDepthInt = asuint(ComputeViewDepth(depth, CameraNear, CameraFar));
        if (depth != 0.0f) {
            InterlockedMax(MaxDepthInTile, viewDepthInt);
            InterlockedMin(MinDepthInTile, viewDepthInt);
        }
#endif
    }

    GroupMemoryBarrierWithGroupSync();

    float maxDepth = asfloat(MaxDepthInTile);
    float minDepth = asfloat(MinDepthInTile);

    // Cull lights per-tile
    // Each threads picks up a light from the list and tests it until all lights are tested
    uint numLights = NumLights & 0xffffu;
    const uint numThreadsPerTile = TILE_SIZE * TILE_SIZE;
    for (uint i = localIndex; i < numLights; i += numThreadsPerTile) {
        PointLightCull pointLight = Lights[i];

        float4 lightCenterView = mul(WorldToViewMat, float4(pointLight.position, 1));

        bool topInside = dot(TileFrustumPlanes[0].xyz, lightCenterView.xyz) >= -pointLight.radius;
        bool rightInside = dot(TileFrustumPlanes[1].xyz, lightCenterView.xyz) >= -pointLight.radius;
        bool bottomInside = dot(TileFrustumPlanes[2].xyz, lightCenterView.xyz) >= -pointLight.radius;
        bool leftInside = dot(TileFrustumPlanes[3].xyz, lightCenterView.xyz) >= -pointLight.radius;
        bool inside = topInside & rightInside & bottomInside & leftInside;

        float lightViewDepth = -lightCenterView.z;
        if (inside &&
            (lightViewDepth + pointLight.radius >= minDepth) &&
            (lightViewDepth - pointLight.radius <= maxDepth))
        {
            uint lightIdx = 0;
            InterlockedAdd(NumLightsInTile, 1, lightIdx);
            if (lightIdx < MAX_LIGHTS_PER_TILE)
                LightIndicesInTile[lightIdx] = i;
            else
                InterlockedExchange(NumLightsInTile, MAX_LIGHTS_PER_TILE);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Output results
    if (localIndex == 0) {
        uint numTilesX = (uint)((WindowWidth + TILE_SIZE - 1) / TILE_SIZE);
        uint startIdx = (tileId.x + tileId.y * numTilesX) * MAX_LIGHTS_PER_TILE;

        for (uint i = 0; i < NumLightsInTile; i++) 
            VisibleLightIndices[startIdx + i] = LightIndicesInTile[i];

        // TODO: Fix this. this can go out of bounds
        VisibleLightIndices[startIdx + NumLightsInTile] = -1;
    }
}