//=============================================================================
// GTAO (Ground Truth Ambient Occlusion) Pixel Shader
// Based on "Ground Truth Ambient Occlusion" (HOT3D 2016)
//=============================================================================

// Constants
cbuffer GTAOConstants : register(b0)
{
    float4x4 ViewToClip;
    float4x4 ClipToView;
    float4x4 WorldToView;
    float2 OutputSize;
    float2 InvOutputSize;

    // GTAO Parameters
    float Radius;           // Sampling radius in world units
    float Power;            // Occlusion power (contrast)
    int Samples;            // Number of samples
    float TemporalStability; // Temporal accumulation

    // Padding
    float3 Padding;
};

// Textures
Texture2D<float> DepthTexture : register(t0);
Texture2D<float3> NormalTexture : register(t1);
SamplerState SamplerLinear : register(s0);

// Output
RWTexture2D<float> OutputAO : register(u0);

//=============================================================================
// Utility Functions
//=============================================================================

// Convert depth to linear depth
float LinearDepth(float2 uv)
{
    float depth = DepthTexture.Sample(SamplerLinear, uv).r;

    // Reconstruct view-space position
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(ClipToView, clipPos);
    return viewPos.z / viewPos.w;
}

// Reconstruct view-space position from depth
float3 ReconstructViewPos(float2 uv, float depth)
{
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(ClipToView, clipPos);
    return viewPos.xyz / viewPos.w;
}

// Generate noise for sampling
float2 GenerateNoise(float2 uv, int index)
{
    float2 rand = float2(
        frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453),
        frac(sin(dot(uv, float2(63.1347, 31.4162))) * 12543.7121)
    );
    return rand * 2.0 - 1.0;
}

// Get view-space normal
float3 GetViewNormal(float2 uv)
{
    float3 normalWS = NormalTexture.Sample(SamplerLinear, uv).rgb;
    float3 normalVS = mul((float3x3)WorldToView, normalWS);
    return normalize(normalVS);
}

//=============================================================================
// GTAO Integration
//=============================================================================

// GTAO main integration
float IntegrateAO(float2 uv, float3 viewPos, float3 viewNormal, float radius, int samples)
{
    float occlusion = 0.0;

    // Generate noise pattern
    float2 noise = GenerateNoise(uv, 0);

    // Create orthonormal basis around normal
    float3 tangent = normalize(abs(viewNormal.x) > 0.99 ? float3(0, 1, 0) : float3(1, 0, 0));
    float3 bitangent = cross(viewNormal, tangent);
    tangent = cross(bitangent, viewNormal);

    // Sample in a hemisphere
    float stepSize = radius / samples;

    for (int i = 0; i < samples; ++i)
    {
        // Golden angle spiral sampling
        float angle = i * 2.3999632 + noise.x * 6.28318;
        float distance = (float)i / samples * radius + noise.y * stepSize;

        // Sample direction in tangent space
        float2 offset = float2(cos(angle), sin(angle)) * distance;
        float3 sampleDir = normalize(viewNormal + tangent * offset.x + bitangent * offset.y);

        // Get sample position
        float3 samplePos = viewPos + sampleDir * distance;

        // Project sample to screen space
        float4 sampleClip = mul(ViewToClip, float4(samplePos, 1.0));
        float2 sampleUV = sampleClip.xy / sampleClip.w * 0.5 + 0.5;
        sampleUV.y = 1.0 - sampleUV.y; // Flip Y for DirectX

        // Boundary check
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            continue;

        // Get sample depth
        float sampleDepth = LinearDepth(sampleUV);

        // Calculate horizon angle
        float3 sampleViewPos = ReconstructViewPos(sampleUV, DepthTexture.Sample(SamplerLinear, sampleUV).r);
        float3 directionToSample = sampleViewPos - viewPos;
        float distToSample = length(directionToSample);

        // Occlusion test
        float horizonAngle = asin(dot(viewNormal, normalize(directionToSample)));

        // Accumulate occlusion
        occlusion += horizonAngle;
    }

    // Normalize and apply power
    occlusion = occlusion / samples;
    occlusion = pow(occlusion, Power);

    return occlusion;
}

//=============================================================================
// Pixel Shader Entry Point
//=============================================================================

[RootSignature("CBV(b0), DescriptorTable(SRV(t0), SRV(t1), Sampler(s0)), UAV(u0)")]
float4 PSGTAO(float4 svPos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    // Get current pixel depth
    float depth = DepthTexture.Sample(SamplerLinear, uv).r;

    // Skip sky/infinite depth
    if (depth >= 0.9999)
    {
        OutputAO[svPos.xy] = 1.0;
        return 1.0;
    }

    // Reconstruct view-space position
    float3 viewPos = ReconstructViewPos(uv, depth);

    // Get view-space normal
    float3 viewNormal = GetViewNormal(uv);

    // Integrate AO
    float ao = IntegrateAO(uv, viewPos, viewNormal, Radius, Samples);

    // Output occlusion (1 = no occlusion, 0 = full occlusion)
    float occlusion = 1.0 - ao;
    occlusion = saturate(occlusion);

    OutputAO[svPos.xy] = occlusion;
    return occlusion;
}
