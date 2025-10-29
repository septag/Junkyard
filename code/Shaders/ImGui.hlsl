#ifndef SRGB_TARGET
#define SRGB_TARGET 0
#endif

struct VsInput
{
    float2 pos : POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};

struct PsInput
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
};

[[vk_push_constant]]
cbuffer Transform
{
    float4x4 ProjMat; 
};

Sampler2D MainTexture;

float3 SRGBToLinear(float3 c)
{
    return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4));
}

[shader("vertex")]
PsInput VsMain(VsInput input)
{
    PsInput output;
    output.pos = mul(ProjMat, float4(input.pos.xy, 0.f, 1.f));
    output.uv  = input.uv;

#if SRGB_TARGET
    output.col = float4(SRGBToLinear(input.col.xyz), input.col.w);
#else
    output.col = input.col;
#endif

    return output;
}

[shader("fragment")]
float4 PsMain(PsInput input) : SV_Target
{
    return input.col * MainTexture.Sample(input.uv); 
}