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

cbuffer FrameTransform
{
    float4x4 ViewMat;
    float4x4 ProjMat;
};

Sampler2D BaseColorTexture;

[shader("vertex")]
Psinput VsMain(VsInput input)
{
    Psinput output;
    float4x4 modelViewProj = mul(mul(ProjMat, ViewMat), ModelMat);
    output.position = mul(modelViewProj, float4(input.position, 1.0f));
    output.normal = mul((float3x3)ModelMat, input.normal);
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 PsMain(Psinput input) : SV_Target
{
    const half3 lightDir = half3(-0.5h, 0.5h, -1.0h);
    const half3 ambient = half3(0.05h, 0.04h, 0.065h);

    half3 lv = -normalize(lightDir);
    half3 n = normalize((half3)input.normal);
    half NdotL = max(0.0h, dot(n, lv));

    half4 albedo = (half4)BaseColorTexture.Sample(input.uv);
    return float4(half3(NdotL)*albedo.xyz + ambient, 1.0f);
}