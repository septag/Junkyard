#ifndef HAS_ALPHA_MASK
#define HAS_ALPHA_MASK 0
#endif

struct VsInput
{
    float3 position : POSITION;

#if HAS_ALPHA_MASK
    float2 uv : TEXCOORD0;
#endif
};

struct PsInput
{
    float4 position : SV_POSITION;

#if HAS_ALPHA_MASK
    float2 uv : TEXCOORD0;
#endif
};

[[vk_push_constant]]
cbuffer PerObjectData
{
    float4x4 LocalToWorldMat;
};

cbuffer PerFrameData
{
    float4x4 WorldToClipMat;
}

#if HAS_ALPHA_MASK
Sampler2D ColorTexture;
#endif

[shader("vertex")]
PsInput VsMain(VsInput v)
{
    PsInput o;
    float4x4 localToClipMat = mul(WorldToClipMat, LocalToWorldMat);
    o.position = mul(localToClipMat, float4(v.position, 1));
#if HAS_ALPHA_MASK
    o.uv = v.uv;
#endif
    return o;
}

#if HAS_ALPHA_MASK
[shader("fragment")]
void PsMain(PsInput i)
{
    float alpha = ColorTexture.Sample(i.uv).w;

    const float _Cutoff = 0.9f;
    alpha = (alpha - _Cutoff) / max(fwidth(alpha), 0.0001) + 0.5;
    if (alpha <= 0)
        discard;
}
#endif