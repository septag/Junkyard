struct VsInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct Psinput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

cbuffer ModelTransform
{
    float4x4 ModelMat;
};

cbuffer FrameTransform
{
    float4x4 ViewMat;
    float4x4 ProjMat;
};

Sampler2D BaseColorTextures[];

[shader("vertex")]
Psinput VsMain(VsInput input)
{
    Psinput output;
    float4x4 modelViewProj = mul(mul(ProjMat, ViewMat), ModelMat);
    output.position = mul(modelViewProj, float4(input.position, 1.0f));
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 PsMain(Psinput input) : SV_Target
{
    float4 albedo = BaseColorTextures[0].Sample(input.uv);
    return float4(albedo.xyz, 1.0f);
}