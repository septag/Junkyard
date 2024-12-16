RWTexture2D<float4> MainImage;

[shader("compute")]
[numthreads(16, 16, 1)]
void CsMain(
    uint3 dispatchThreadID : SV_DispatchThreadID, // Global invocation ID
    uint3 groupThreadID : SV_GroupThreadID        // Local invocation ID
)
{
    uint2 texelCoord = dispatchThreadID.xy; // Replace `gl_GlobalInvocationID`
    uint2 size;
    MainImage.GetDimensions(size.x, size.y);   // Replace `imageSize`

    if (texelCoord.x < size.x && texelCoord.y < size.y)
    {
        float4 color = float4(0.0, 0.0, 0.0, 1.0);

        if (groupThreadID.x != 0 && groupThreadID.y != 0)
        {
            color.x = float(texelCoord.x) / size.x;
            color.y = float(texelCoord.y) / size.y;
        }

        MainImage[texelCoord] = color; // Replace `imageStore`
    }
}
