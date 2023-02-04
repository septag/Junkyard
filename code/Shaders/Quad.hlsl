struct VS_INPUT
{
    float3 position : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

// changes DescriptorTableSlot to PushConstant
[[vk::push_constant]]
cbuffer ModelTransform
{
    float4x4 modelMat;
};

cbuffer FrameTransform
{
    float4x4 viewMat;
    float4x4 projMat;
};

Texture2D    uTexture;
SamplerState uTextureSampler;

[shader("vertex")]
PS_INPUT VsMain(VS_INPUT input)
{
    PS_INPUT output;
    float4x4 modelViewProj = mul(mul(projMat, viewMat), modelMat);
    output.position = mul(modelViewProj, float4(input.position, 1.0f));
    output.color = float4(1.0f, 1.0f, 1.0f, 1.0f); //input.color;
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 PsMain(PS_INPUT input) : SV_Target
{
    return uTexture.Sample(uTextureSampler, input.uv) * input.color;
}