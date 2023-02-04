struct VS_INPUT
{
    float2 pos : POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
};

cbuffer TransformUbo
{
    float4x4 ProjMat; 
};

[shader("vertex")]
PS_INPUT VsMain(VS_INPUT input)
{
    PS_INPUT output;
    output.pos = mul(ProjMat, float4(input.pos.xy, 0.f, 1.f));
    output.uv  = input.uv;
    output.col = input.col;
    return output;
}

SamplerState Sampler0;
Texture2D Texture0;

[shader("fragment")]
float4 PsMain(PS_INPUT input) : SV_Target
{
    return input.col * Texture0.Sample(Sampler0, input.uv); 
}