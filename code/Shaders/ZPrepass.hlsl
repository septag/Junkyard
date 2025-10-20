struct VsInput
{
    float3 position : POSITION;
};

struct PsInput
{
    float4 position : SV_POSITION;
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

[shader("vertex")]
PsInput VsMain(VsInput v)
{
    PsInput o;
    float4x4 localToClipMat = mul(WorldToClipMat, LocalToWorldMat);
    o.position = mul(localToClipMat, float4(v.position, 1));
    return o;
}