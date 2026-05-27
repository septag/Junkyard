#define TEXT_EFFECT_NONE 0
#define TEXT_EFFECT_DROPSHADOW 1
#define TEXT_EFFECT_OUTLINE 2

[SpecializationConstant]
int TextEffect = TEXT_EFFECT_NONE;

Texture2D<float4> FontTexture;
SamplerState FontSampler;

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

// One Tap SDF: https://libgdx.com/wiki/graphics/2d/fonts/distance-field-fonts
// This implementation uses 4-tap anti-aliased SDF
float SampleSDF(float2 uv, float2 texelSize, float threshold)
{
    const float tap = 0.25f;
    float2 o0 = float2(tap, tap * 0.5f) * texelSize;
    float2 o1 = float2(-tap, tap * 0.5f) * texelSize;
    float2 o2 = float2(tap, -tap * 0.5f) * texelSize;
    float2 o3 = float2(-tap, -tap * 0.5f) * texelSize;

    float d0 = FontTexture.Sample(FontSampler, uv + o0).r;
    float d1 = FontTexture.Sample(FontSampler, uv + o1).r;
    float d2 = FontTexture.Sample(FontSampler, uv + o2).r;
    float d3 = FontTexture.Sample(FontSampler, uv + o3).r;

    float centerDist = (d0 + d1 + d2 + d3) * 0.25f;

    float2 grad = float2(ddx(centerDist), ddy(centerDist));
    float smoothWidth = max(0.5f * length(grad), 1.0f / 16.0f);

    float sdf = (smoothstep(threshold - smoothWidth, threshold + smoothWidth, d0) +
                 smoothstep(threshold - smoothWidth, threshold + smoothWidth, d1) +
                 smoothstep(threshold - smoothWidth, threshold + smoothWidth, d2) +
                 smoothstep(threshold - smoothWidth, threshold + smoothWidth, d3)) * 0.25f;
    return sdf;
}

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
    uint2 textureSize;
    FontTexture.GetDimensions(textureSize.x, textureSize.y);
    const float2 texelSize = rcp(float2(textureSize.x, textureSize.y));
    const float2 uv = input.uv * texelSize;

    float textAlpha = SampleSDF(uv, texelSize, 0.5f);
    float4 text = float4(input.color.xyz, input.color.a * textAlpha);

    if (TextEffect == TEXT_EFFECT_DROPSHADOW) {
        const float2 shadowOffset = float2(2.0f, 2.0f) * texelSize;
        float shadowAlpha = SampleSDF(uv - shadowOffset, texelSize, 0.5f);
        float4 shadow = float4(0, 0, 0, input.color.a * shadowAlpha);
    
        return lerp(shadow, text, text.a);
    }
    else if (TextEffect == TEXT_EFFECT_OUTLINE) {
        const float outlineWidth = 0.3f;
        float outlineAlpha = SampleSDF(uv, texelSize, 0.5f - outlineWidth);
        float4 outline = float4(0, 0, 0, input.color.a * outlineAlpha);

        return lerp(outline, text, text.a);
    }
    else {
        return text;    
    }
    // TODO: 
}
