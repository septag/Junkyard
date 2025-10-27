struct VsInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct Psinput
{
    float4 position : SV_POSITION;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD0;
};

// changes DescriptorTableSlot to PushConstant
[[vk_push_constant]]
cbuffer ModelTransform
{
    float4x4 ModelMat;
};

cbuffer FrameInfo
{
    float4x4 WorldToClipMat;
    float3 LightDir;
    float LightFactor;
};

Sampler2D BaseColorTexture;

[shader("vertex")]
Psinput VsMain(VsInput input)
{
    Psinput output;
    float4x4 modelViewProj = mul(WorldToClipMat, ModelMat);
    output.position = mul(modelViewProj, float4(input.position, 1.0f));
    output.normal = mul((float3x3)ModelMat, input.normal);
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 PsMain(Psinput input) : SV_Target
{
    const float3 ambient = float3(0.05, 0.04, 0.065);

    float3 lv = -normalize(LightDir);
    float3 n = normalize((float3)input.normal);
    float NdotL = max(0.0, dot(n, lv));
    NdotL = max(LightFactor, NdotL);

    float4 albedo = BaseColorTexture.Sample(input.uv);
    return float4(NdotL*albedo.xyz + ambient*albedo.xyz, 1.0f);
}