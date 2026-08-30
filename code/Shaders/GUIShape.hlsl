Texture2D<float4> ShapeTexture;
SamplerState ShapeSampler;

[[vk_push_constant]]
cbuffer PerFrameData
{
    float4x4 WorldToClipMat;
};

struct VsInput
{
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

struct PsInput
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
};

[shader("vertex")]
PsInput VsMain(VsInput input)
{
    PsInput output;

    output.pos = mul(WorldToClipMat, float4(input.pos, 0, 1));
    output.uv = input.uv;
    output.color = input.color;

    return output;
}

[shader("fragment")]
float4 PsMain(PsInput input) : SV_Target
{
    float4 imgColor = ShapeTexture.Sample(ShapeSampler, input.uv);
    return imgColor * input.color;    
}
