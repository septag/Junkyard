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
cbuffer Transform
{
    float4x4 ViewProjMat;
};

[shader("vertex")]
PsInput VsMain(VsInput input)
{
    PsInput output;

    output.position = mul(ViewProjMat, float4(input.position, 1.0f));
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 PsMain(PsInput input)
{
    return input.color;
}

