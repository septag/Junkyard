[SpecializationConstant]
bool LinearizeDepthTexture = false;

#ifdef IS_DEPTH_TEXTURE
Texture2D<float> DepthTexture;
#else
Texture2D<float4> ColorTexture;
#endif

SamplerState TextureSampler;

[[vk_push_constant]]
cbuffer PerFrameData
{
    float CameraNear : packoffset(c0);
    float CameraFar : packoffset(c0.y);
};

// Single fullscreen triangle (No input vertex buffer needed)
// Reference: https : // www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
struct PsInput
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
PsInput VsMain(uint vertexId : SV_VertexID)
{
    PsInput o;

    const float2 pos[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 3.0f),
        float2( 3.0, -1.0f)
    };

    o.pos = float4(pos[vertexId], 0, 1.0f);
    o.uv = 0.5f * (pos[vertexId] + 1.0f); // map from [-1, 1] -> [0, 1]
    return o;
}

float ComputeViewDepth(float ndcZ, float near, float far)
{
    return near * far / (far - ndcZ * (far - near));
}

[shader("fragment")]
float4 PsMain(PsInput i) : SV_Target
{
#ifdef IS_DEPTH_TEXTURE
    float depth = DepthTexture.Sample(TextureSampler, i.uv);
    if (LinearizeDepthTexture)
        depth = ComputeViewDepth(depth, CameraNear, CameraFar);
    return float4(depth.xxx, 1);
#else
    return ColorTexture.Sample(TextureSampler, i.uv);
#endif
}