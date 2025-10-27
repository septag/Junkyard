struct VsInput
{
    float3 position : POSITION;
    float4 color : COLOR;
};

struct PsInput
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

[[vk_push_constant]]
cbuffer PerObjectData
{
    float4x4 LocalToWorldMat;
    float4 TintColor;
};

cbuffer PerFrameData
{
    float4x4 WorldToClipMat;
};

[shader("vertex")]
PsInput VsMain(VsInput i)
{
    PsInput o;

    float4 worldPos = mul(LocalToWorldMat, float4(i.position, 1.0f));
    o.position = mul(WorldToClipMat, worldPos);
    o.color = i.color * TintColor;
    return o;
}

[shader("fragment")]
float4 PsMain(PsInput i)
{
    return i.color;
}

