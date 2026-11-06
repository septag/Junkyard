// Screen-space (contact) shadows for the sun
// For each pixel, a short ray is marched in view-space towards the sun. At each step, the ray is projected to the
// screen and compared with the depth buffer. If the scene is in front of the ray (but not more than Thickness), the
// pixel is occluded. Output: 1 = lit, 0 = occluded. Combined with the sun shadow map in the light pass (min)
// Depths are compared as linear view depth (clip.w for perspective projections)

#ifndef MSAA
#define MSAA 1
#endif

cbuffer PerFrameData
{
    float4x4 ViewToClipMat;
    float4x4 ClipToViewMat;
    float3 SunDirVS;            // Towards the sun, normalized
    float MaxDistance;
    float Thickness;
    float Bias;
    float CameraNear;
    float CameraFar;
    uint NumSteps;
    uint Width;
    uint Height;
};

#if MSAA > 1
Texture2DMS<float> DepthTexture;
#else
Texture2D<float> DepthTexture;
#endif

[format("r8")]
RWTexture2D<float> ShadowMask;

float LoadDepth(int2 pixel)
{
#if MSAA > 1
    return DepthTexture.Load(pixel, 0);     // First sample is good enough for a shadow mask
#else
    return DepthTexture.Load(int3(pixel, 0));
#endif
}

float ComputeViewDepth(float ndcZ, float near, float far)
{
    return near * far / (far - ndcZ * (far - near));
}

// Interleaved gradient noise (Jimenez 2014). Jitters the ray start against banding
float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CsMain(uint3 globalPos : SV_DispatchThreadID)
{
    if (globalPos.x >= Width || globalPos.y >= Height)
        return;

    float depth = LoadDepth(int2(globalPos.xy));
    if (depth >= 1.0f) {    // Nothing rendered
        ShadowMask[globalPos.xy] = 1.0f;
        return;
    }

    float2 screenSize = float2(Width, Height);
    float2 ndc = (float2(globalPos.xy) + 0.5f) / screenSize * 2.0f - 1.0f;
    float4 posVS = mul(ClipToViewMat, float4(ndc, depth, 1.0f));
    posVS.xyz /= posVS.w;

    float3 rayStep = SunDirVS * (MaxDistance / float(NumSteps));
    float3 rayPos = posVS.xyz + rayStep * InterleavedGradientNoise(float2(globalPos.xy));

    float occlusion = 0;
    for (uint i = 0; i < NumSteps; i++) {
        rayPos += rayStep;

        float4 posClip = mul(ViewToClipMat, float4(rayPos, 1.0f));
        float2 uv = (posClip.xy / posClip.w) * 0.5f + 0.5f;
        if (any(uv < 0) || any(uv >= 1.0f))
            break;

        float sceneDepth = ComputeViewDepth(LoadDepth(int2(uv * screenSize)), CameraNear, CameraFar);
        float delta = posClip.w - sceneDepth;   // > 0: Scene is in front of the ray
        if (delta > Bias && delta < Thickness) {
            occlusion = 1.0f - float(i) / float(NumSteps);   // Fades out towards the end of the ray
            break;
        }
    }

    ShadowMask[globalPos.xy] = 1.0f - occlusion;
}
