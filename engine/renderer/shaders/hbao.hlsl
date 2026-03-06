//=============================================================================
// HBAO (Horizon-Based Ambient Occlusion)
// Based on NVIDIA's HBAO+ algorithm
//=============================================================================

// Constants
cbuffer HBAOConstants : register(b0)
{
    float4x4 Projection;
    float4x4 InvProjection;
    float2 OutputSize;
    float2 InvOutputSize;

    // HBAO Parameters
    float Radius;           // Sampling radius
    float Bias;             // Bias to reduce self-occlusion
    int Steps;             // Steps per direction
    float Power;            // Occlusion power

    float BlurEnabled;
    float3 Padding;
}

// Textures
Texture2D<float> DepthTexture : register(t0);
Texture2D<float3> NormalTexture : register(t1);
SamplerState SamplerPoint : register(s0);

// Output
RWTexture2D<float> OutputAO : register(u0);

//=============================================================================
// Utility Functions
//=============================================================================

// Convert to view space
float3 UVToViewSpace(float2 uv, float depth)
{
    // NDC coordinates
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    ndc.y = -ndc.y; // Flip for DX

    // Unproject
    float4 view = mul(InvProjection, ndc);
    return view.xyz / view.w;
}

// Get view position at UV
float3 GetViewPos(float2 uv)
{
    float depth = DepthTexture.SampleLevel(SamplerPoint, uv, 0).r;
    return UVToViewSpace(uv, depth);
}

// Calculate horizon angle in a direction
float CalculateHorizonAngle(float2 uv, float3 viewPos, float2 dir, float maxRadius)
{
    float horizonAngle = -1.0;

    // March along the direction
    float stepRadius = maxRadius / Steps;
    float radius = stepRadius;

    for (int i = 0; i < Steps; ++i)
    {
        float2 sampleUV = uv + dir * radius;
        float3 sampleViewPos = GetViewPos(sampleUV);

        // Calculate angle to horizon
        float3 direction = sampleViewPos - viewPos;
        float dist = length(direction.xy);
        float height = direction.z;
        float angle = atan2(height, dist);

        // Update maximum horizon angle
        horizonAngle = max(horizonAngle, angle);

        radius += stepRadius;
    }

    return horizonAngle;
}

//=============================================================================
// HBAO Main Algorithm
//=============================================================================

float ComputeHBAO(float2 uv)
{
    float3 viewPos = GetViewPos(uv);

    // Skip sky
    if (viewPos.z <= 0.001)
        return 1.0;

    // Get normal
    float3 normal = NormalTexture.SampleLevel(SamplerPoint, uv, 0).rgb;

    // Calculate maximum radius in screen space
    float radiusWorld = Radius;
    float radiusScreen = radiusWorld / viewPos.z;

    // Sample in 4 or 8 directions
    int numDirections = 4;
    float totalAO = 0.0;

    for (int i = 0; i < numDirections; ++i)
    {
        float angle = (float)i / numDirections * 3.14159 * 2.0;

        float2 dir = float2(cos(angle), sin(angle));
        dir *= InvOutputSize.y; // Scale by height

        // Calculate horizon angle
        float h1 = CalculateHorizonAngle(uv, viewPos, dir, radiusScreen);

        // Opposite direction
        float h2 = CalculateHorizonAngle(uv, viewPos, -dir, radiusScreen);

        // Compute occlusion
        float ao = acos(min(h1, h2)) - acos(max(h1, h2));

        // Apply bias and power
        ao = max(0.0, ao + Bias);
        ao = pow(ao, Power);

        totalAO += ao;
    }

    // Normalize
    totalAO /= numDirections;

    // Convert to ambient occlusion
    float occlusion = 1.0 - totalAO * 0.5;
    occlusion = saturate(occlusion);

    return occlusion;
}

//=============================================================================
// Pixel Shader Entry Point
//=============================================================================

float4 PSHBAO(float4 svPos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float ao = ComputeHBAO(uv);

    OutputAO[svPos.xy] = ao;
    return ao;
}

//=============================================================================
// Blur Shader for HBAO
//=============================================================================

Texture2D<float> InputAO : register(t2);

float4 PSBlur(float4 svPos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    // Separable blur (horizontal first)
    float blurRadius = 2.0;
    float result = 0.0;
    float totalWeight = 0.0;

    for (int x = -2; x <= 2; ++x)
    {
        float weight = 1.0 - abs(x) / 3.0;
        float2 offset = float2(x, 0) * InvOutputSize;
        float sample = InputAO.SampleLevel(SamplerPoint, uv + offset, 0).r;

        result += sample * weight;
        totalWeight += weight;
    }

    result /= totalWeight;

    OutputAO[svPos.xy] = result;
    return result;
}
