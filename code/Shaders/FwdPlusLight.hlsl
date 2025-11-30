#ifndef TILE_SIZE
#define TILE_SIZE 16
#endif

#ifndef MAX_LIGHTS_PER_TILE
#define MAX_LIGHTS_PER_TILE 4
#endif

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

cbuffer PerFrameData
{
    float4x4 WorldToClipMat : packoffset(c0);
    float4x4 WorldToSunLightClipMat : packoffset(c4);
    float3 SunLightDir : packoffset(c8);
    float4 SunLightColorIntensity : packoffset(c9);
    float4 SkyAmbientColor : packoffset(c10);
    float4 GroundAmbientColor : packoffset(c11);
    uint TilesCountX : packoffset(c12);
};

[[vk_push_constant]]
cbuffer PerObjectData
{
    float4x4 LocalToWorldMat;
};

struct PointLightCull
{
    float3 position;
    float radius;
};

struct LocalLight
{
    float4 color;
};


StructuredBuffer<uint> VisibleLightIndices;
StructuredBuffer<LocalLight> LocalLights;
StructuredBuffer<PointLightCull> LocalLightBounds;

Texture2D<float> ShadowMap;
SamplerComparisonState ShadowSampler;

Sampler2D BaseColorTexture;

[shader("vertex")]
PsInput VsMain(VsInput i)
{
    PsInput o;
    float4 posWS = mul(LocalToWorldMat, float4(i.position, 1.0f));
    o.position = mul(WorldToClipMat, posWS);
    o.uv = i.uv;
    o.posWS = posWS.xyz;
    o.normalWS = mul((float3x3)LocalToWorldMat, i.normal);

    return o;
}

[shader("fragment")]
float4 PsMain(PsInput i) : SV_Target
{
    uint2 loc = uint2(i.position.xy);
    uint2 tileId = loc / uint2(TILE_SIZE, TILE_SIZE);
    uint index = tileId.y * TilesCountX + tileId.x;

    float4 albedoColor = BaseColorTexture.Sample(i.uv);
    float3 N = normalize(i.normalWS);
    float3 finalDiffuse = float3(0, 0, 0);
    float3 finalAmbient = float3(0, 0, 0);

    // Sun light
    {
        // Shadow
        float4 shadowPos = mul(WorldToSunLightClipMat, float4(i.posWS, 1));
        shadowPos.xyz /= shadowPos.w;
        float2 shadowUv = shadowPos.xy * 0.5f + 0.5f;
        // float shadowDepth = shadowPos.z * 0.5f + 0.5f;
        float shadowDepth = shadowPos.z;
        // shadowUv.y = 1 - shadowUv.y;
        float shadowTerm = ShadowMap.SampleCmpLevelZero(ShadowSampler, shadowUv, shadowDepth);

        // Light
        float3 L = -SunLightDir;
        float nDotL = max(0, dot(L, N));
        float3 diffuse = SunLightColorIntensity.xyz * SunLightColorIntensity.w * nDotL;

        // Simple hemisphere ambient term (blend sky/ground color based on the normal angle)
        float nDotUp = dot(N, float3(0, 0, 1));
        float a = 0.5 + 0.5 * nDotUp;
        float3 ambient = lerp(GroundAmbientColor.xyz * GroundAmbientColor.w, SkyAmbientColor.xyz * SkyAmbientColor.w, a);

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