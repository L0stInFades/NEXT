//=============================================================================
// GTAO Spatial Filter
// Edge-aware bilateral filter for high-quality AO
//=============================================================================

// Constants
cbuffer FilterConstants : register(b0)
{
    float2 InvOutputSize;
    float Sharpness;     // Edge preservation
    int KernelRadius;    // Filter radius
    float3 Padding;
}

// Textures
Texture2D<float> AOTexture : register(t0);
Texture2D<float> DepthTexture : register(t1);
Texture2D<float3> NormalTexture : register(t2);
SamplerState SamplerLinear : register(s0);

// Output
RWTexture2D<float> OutputAO : register(u0);

//=============================================================================
// Utility Functions
//=============================================================================

float GetLinearDepth(float2 uv)
{
    return DepthTexture.Sample(SamplerLinear, uv).r;
}

float3 GetNormal(float2 uv)
{
    return NormalTexture.Sample(SamplerLinear, uv).rgb;
}

// Bilateral weight calculation
float CalculateWeight(float2 uv, float2 offset, float centerDepth, float3 centerNormal)
{
    float2 sampleUV = uv + offset;

    // Get sample depth and normal
    float sampleDepth = GetLinearDepth(sampleUV);
    float3 sampleNormal = GetNormal(sampleUV);

    // Depth weight (edge-aware)
    float depthDelta = abs(centerDepth - sampleDepth);
    float depthWeight = exp(-depthDelta * Sharpness);

    // Normal weight
    float normalDelta = 1.0 - dot(centerNormal, sampleNormal);
    float normalWeight = exp(-normalDelta * Sharpness * 10.0);

    // Spatial weight (Gaussian)
    float dist = length(offset);
    float spatialWeight = exp(-(dist * dist) / (2.0));

    return depthWeight * normalWeight * spatialWeight;
}

//=============================================================================
// Pixel Shader Entry Point
//=============================================================================

float4 PSFilter(float4 svPos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    // Get center values
    float centerAO = AOTexture.Sample(SamplerLinear, uv).r;
    float centerDepth = GetLinearDepth(uv);
    float3 centerNormal = GetNormal(uv);

    // Skip filtering for invalid pixels
    if (centerAO >= 0.999)
    {
        OutputAO[svPos.xy] = centerAO;
        return centerAO;
    }

    // Accumulate filtered result
    float totalWeight = 0.0001;
    float filteredAO = 0.0;

    // Filter kernel (separable for performance)
    for (int x = -KernelRadius; x <= KernelRadius; ++x)
    {
        for (int y = -KernelRadius; y <= KernelRadius; ++y)
        {
            if (x == 0 && y == 0)
                continue;

            float2 offset = float2(x, y) * InvOutputSize;
            float weight = CalculateWeight(uv, offset, centerDepth, centerNormal);

            float2 sampleUV = uv + offset;
            float sampleAO = AOTexture.Sample(SamplerLinear, sampleUV).r;

            filteredAO += sampleAO * weight;
            totalWeight += weight;
        }
    }

    // Add center pixel
    filteredAO += centerAO;
    totalWeight += 1.0;

    // Normalize
    filteredAO /= totalWeight;
    filteredAO = saturate(filteredAO);

    OutputAO[svPos.xy] = filteredAO;
    return filteredAO;
}

//=============================================================================
// Temporal Accumulation (optional pass)
//=============================================================================

cbuffer TemporalConstants : register(b1)
{
    float Feedback;  // Temporal blend factor (0-1)
    float3 Padding2;
}

Texture2D<float> HistoryAO : register(t3);
Texture2D<float2> MotionVectors : register(t4); // For reprojection

float4 PSTemporal(float4 svPos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float currentAO = AOTexture.Sample(SamplerLinear, uv).r;

    // Get motion vector for reprojection
    float2 motion = MotionVectors.Sample(SamplerLinear, uv).rg;
    float2 prevUV = uv - motion;

    // Check if previous frame is valid
    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0)
    {
        OutputAO[svPos.xy] = currentAO;
        return currentAO;
    }

    // Get previous frame AO
    float historyAO = HistoryAO.Sample(SamplerLinear, prevUV).r;

    // Temporal blend
    float blendedAO = lerp(historyAO, currentAO, Feedback);

    OutputAO[svPos.xy] = blendedAO;
    return blendedAO;
}
