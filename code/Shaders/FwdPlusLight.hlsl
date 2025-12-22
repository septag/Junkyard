#ifndef TILE_SIZE
#define TILE_SIZE 16
#endif

#ifndef MAX_LIGHTS_PER_TILE
#define MAX_LIGHTS_PER_TILE 4
#endif

#include "Samplers.h.hlsl"

struct VsInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct PsInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 posWS : TEXCOORD1;
    float3 normalWS : TEXCOORD2;
};

struct RLightBounds
{
    float3 position;
    float radius;
};

struct RLightProps
{
    float4 color;
};

struct RLightShaderFrameData
{
    float4x4 worldToClipMat;
    float4x4 worldToSunLightClipMat;
    float3 sunLightDir;
    float4 sunLightColorIntensity;
    float4 skyAmbientColor;
    float4 groundAmbientColor;
    uint tilesCountX;
};

// PerFrame
ConstantBuffer<RLightShaderFrameData> PerFrameData;
StructuredBuffer<uint> VisibleLightIndices;
StructuredBuffer<RLightProps> LocalLights;
StructuredBuffer<RLightBounds> LocalLightBounds;
Texture2D<float> ShadowMap;

// PerObject
[vk::binding(0, 1)]
cbuffer PerObjectData
{
    float4x4 LocalToWorldMat;
};

[vk::binding(1, 1)]
Texture2D BaseColorTexture;

[shader("vertex")]
PsInput VsMain(VsInput v)
{
    PsInput o;
    float4 posWS = mul(LocalToWorldMat, float4(v.position, 1.0f));
    o.position = mul(PerFrameData.worldToClipMat, posWS);
    
    o.uv = v.uv;
    o.posWS = posWS.xyz;
    o.normalWS = v.normal;
    o.normalWS = mul((float3x3)LocalToWorldMat, v.normal);

    return o;
}

[shader("fragment")]
float4 PsMain(PsInput i) : SV_Target
{
    uint2 loc = uint2(i.position.xy);
    uint2 tileId = loc / uint2(TILE_SIZE, TILE_SIZE);
    uint index = tileId.y * PerFrameData.tilesCountX + tileId.x;

    float4 albedoColor = BaseColorTexture.Sample(SamplerTrilinear, i.uv);
    float3 N = normalize(i.normalWS);
    float3 finalDiffuse = float3(0, 0, 0);
    float3 finalAmbient = float3(0, 0, 0);

    // Sun light
    {
        // Shadow
        float4 posLS = mul(PerFrameData.worldToSunLightClipMat, float4(i.posWS, 1));
        posLS.xyz /= posLS.w;
        float2 shadowUv = posLS.xy * 0.5f + 0.5f;   // From clip-space to uv-space
        float shadowTerm = ShadowMap.SampleCmpLevelZero(SamplerBilinearLess, shadowUv, posLS.z);

        // Light
        float3 L = -PerFrameData.sunLightDir;
        float nDotL = max(0, dot(L, N));
        float3 diffuse = PerFrameData.sunLightColorIntensity.xyz * PerFrameData.sunLightColorIntensity.w * nDotL;

        // Simple hemisphere ambient term (blend sky/ground color based on the normal angle)
        float nDotUp = dot(N, float3(0, 0, 1));
        float a = 0.5 + 0.5 * nDotUp;
        float3 ambient = lerp(PerFrameData.groundAmbientColor.xyz * PerFrameData.groundAmbientColor.w,
                              PerFrameData.skyAmbientColor.xyz * PerFrameData.skyAmbientColor.w, a);

        finalDiffuse += diffuse * shadowTerm;
        finalAmbient += ambient;
    }

    uint startIdx = index * MAX_LIGHTS_PER_TILE;
    uint offsetIdx = 0;
    while (VisibleLightIndices[startIdx + offsetIdx] != -1) {
        uint lightIdx = VisibleLightIndices[startIdx + offsetIdx];
        float3 lightPos = LocalLightBounds[lightIdx].position;
        float lightRadius = LocalLightBounds[lightIdx].radius;
        float3 lightColor = LocalLights[lightIdx].color.xyz;
        float lightIntensity = LocalLights[lightIdx].color.w;

        float3 L = lightPos - i.posWS;
        float distToLight = length(L);
        L = normalize(L);

        float nDotL = max(0, dot(L, N));

        float x = min(1.0f, distToLight / lightRadius);
        // float falloff = 1.0 - smoothstep(0.0, 1.0, x);   // Simple 
        // float falloff = saturate(-0.05 + 1.05 / (1.0 + 20.0 * x * x));  // smooth
        float falloff = pow(1.0 - saturate(x * x * x * x), 2.0); // Unreal
        // Unity
        // float falloff = 1.0 - (x * x * x * x); // Or other powers
        // falloff = falloff * falloff / (1.0 + distToLight * distToLight);
        float3 diffuse = lightIntensity * lightColor * nDotL * falloff;

        finalDiffuse += diffuse;

        offsetIdx++;
    }

    float3 litColor = albedoColor.xyz * (finalDiffuse + finalAmbient);
    // float alpha = smoothstep(0.5, 0.6, albedoColor.w);
    float alpha = albedoColor.w;
    const float _Cutoff = 0.5f;
    alpha = (alpha - _Cutoff) / max(fwidth(alpha), 0.0001) + 0.5;

    return float4(litColor, alpha);
}