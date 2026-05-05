//=============================================================================
// Light Probe Baking Shader
// Projects environment lighting into spherical harmonics
//=============================================================================

// Constants
cbuffer ProbeConstants : register(b0)
{
    float4x4 WorldToProbe;
    float4x4 ProbeToWorld;
    float3 ProbePosition;
    float ProbeRadius;
    int RaysPerProbe;
    int SHOrder;
    float2 Padding;
}

// Textures
Texture2D<float4> InputCubemap : register(t0); // 6 faces in texture array
Texture2D<float> DepthBuffer : register(t1);
Texture2D<float3> NormalBuffer : register(t2);
SamplerState SamplerLinear : register(s0);

// Output
RWStructuredBuffer<float4> SHCoefficients : register(u0); // 9 SH coefficients (RGB + padding)

//=============================================================================
// Spherical Harmonics Constants
//=============================================================================

static const float SH_C0 = 0.282095f;
static const float SH_C1 = 0.488603f;
static const float SH_C2_0 = 1.092548f;
static const float SH_C2_1 = 0.315392f;
static const float SH_C2_2 = 0.746655f;

//=============================================================================
// SH Projection Functions
//=============================================================================

// Project a direction onto SH basis
float3 ProjectSH(float3 dir, float3 radiance)
{
    float3 sh[9];

    float x = dir.x;
    float y = dir.y;
    float z = dir.z;

    // Band 0
    sh[0] = radiance * SH_C0;

    // Band 1
    sh[1] = radiance * (SH_C1 * y);
    sh[2] = radiance * (SH_C1 * z);
    sh[3] = radiance * (SH_C1 * x);

    // Band 2
    sh[4] = radiance * (SH_C2_0 * x * y);
    sh[5] = radiance * (SH_C2_1 * (3.0 * z * z - 1.0));
    sh[6] = radiance * (SH_C2_0 * y * z);
    sh[7] = radiance * (SH_C2_2 * (x * x - y * y));
    sh[8] = radiance * (SH_C2_2 * x * z);

    // Accumulate
    float3 result = 0.0;
    for (int i = 0; i < 9; ++i)
    {
        result += sh[i];
    }

    return result;
}

// Evaluate SH at a direction
float3 EvaluateSH(float3 sh[9], float3 dir)
{
    float x = dir.x;
    float y = dir.y;
    float z = dir.z;

    float3 result = sh[0] * SH_C0;
    result += sh[1] * (SH_C1 * y);
    result += sh[2] * (SH_C1 * z);
    result += sh[3] * (SH_C1 * x);
    result += sh[4] * (SH_C2_0 * x * y);
    result += sh[5] * (SH_C2_1 * (3.0 * z * z - 1.0));
    result += sh[6] * (SH_C2_0 * y * z);
    result += sh[7] * (SH_C2_2 * (x * x - y * y));
    result += sh[8] * (SH_C2_2 * x * z);

    return result;
}

float3 AnalyticProbeRadiance(float3 dir)
{
    float up = saturate(dir.z * 0.5 + 0.5);
    float horizon = pow(1.0 - up, 2.0);
    float3 sky = lerp(float3(0.08, 0.10, 0.13), float3(0.48, 0.58, 0.78), up);
    float3 groundBounce = float3(0.18, 0.14, 0.10) * horizon;
    return sky + groundBounce;
}

//=============================================================================
// Probe Baking Compute Shader
//=============================================================================

[numthreads(64, 1, 1)]
void CSBakeProbes(uint3 DTid : SV_DispatchThreadID, uint3 Gid : SV_GroupID)
{
    int probeIndex = DTid.x;
    if (RaysPerProbe <= 0)
    {
        return;
    }

    float3 shAccum[9];
    [unroll]
    for (int coeff = 0; coeff < 9; ++coeff)
    {
        shAccum[coeff] = 0.0;
    }

    for (int rayIndex = 0; rayIndex < RaysPerProbe; ++rayIndex)
    {
        float i = (float)rayIndex;
        float numRays = (float)RaysPerProbe;
        float2 hammersley = float2(i / numRays, frac(i * 0.5 + 0.5));

        float phi = 2.0 * 3.14159 * hammersley.y;
        float cosTheta = 1.0 - hammersley.x;
        float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

        float3 rayDir = normalize(float3(
            cos(phi) * sinTheta,
            sin(phi) * sinTheta,
            cosTheta
        ));
        float3 radiance = AnalyticProbeRadiance(rayDir);

        float x = rayDir.x;
        float y = rayDir.y;
        float z = rayDir.z;
        shAccum[0] += radiance * SH_C0;
        shAccum[1] += radiance * (SH_C1 * y);
        shAccum[2] += radiance * (SH_C1 * z);
        shAccum[3] += radiance * (SH_C1 * x);
        shAccum[4] += radiance * (SH_C2_0 * x * y);
        shAccum[5] += radiance * (SH_C2_1 * (3.0 * z * z - 1.0));
        shAccum[6] += radiance * (SH_C2_0 * y * z);
        shAccum[7] += radiance * (SH_C2_2 * (x * x - y * y));
        shAccum[8] += radiance * (SH_C2_2 * x * z);
    }

    float weight = 4.0 * 3.14159 / (float)RaysPerProbe;
    [unroll]
    for (int coeff = 0; coeff < 9; ++coeff)
    {
        SHCoefficients[probeIndex * 9 + coeff] = float4(shAccum[coeff] * weight, 1.0);
    }
}

//=============================================================================
// Probe Evaluation Pixel Shader
//=============================================================================

cbuffer EvalConstants : register(b1)
{
    float3 CameraPos;
    float MaxBlendDistance;
    int NumProbes;
    float3 Padding2;
}

StructuredBuffer<float3> ProbePositions : register(t3);
StructuredBuffer<float3> ProbeSH : register(t4); // 9 SH coefficients per probe

float4 PSEvaluateProbes(float4 svPos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    // Get surface normal
    float3 normal = normalize(NormalBuffer.Sample(SamplerLinear, uv).rgb);

    // Get world position
    float depth = DepthBuffer.Sample(SamplerLinear, uv).r;
    float2 ndc = uv * 2.0 - 1.0;
    float linearDepth = max(depth * MaxBlendDistance, 0.001);
    float3 worldPos = CameraPos + float3(ndc.x * linearDepth, -ndc.y * linearDepth, linearDepth);
    float3 irradiance = float3(0.0, 0.0, 0.0);
    float totalWeight = 0.0;

    // Find nearby probes and interpolate
    for (int i = 0; i < NumProbes; ++i)
    {
        float3 probePos = ProbePositions[i];
        float dist = length(worldPos - probePos);

        if (dist < MaxBlendDistance)
        {
            // Get SH for this probe
            int shOffset = i * 9;
            float3 sh[9];
            [unroll]
            for (int j = 0; j < 9; ++j)
            {
                sh[j] = ProbeSH[shOffset + j];
            }

            // Evaluate SH in normal direction
            float3 probeIrradiance = EvaluateSH(sh, normal);

            // Weight by distance
            float weight = 1.0 - smoothstep(0.0, MaxBlendDistance, dist);
            irradiance += probeIrradiance * weight;
            totalWeight += weight;
        }
    }

    // Normalize
    if (totalWeight > 0.0)
    {
        irradiance /= totalWeight;
    }

    return float4(irradiance, 1.0);
}

//=============================================================================
// Probe Visualization (debug)
//=============================================================================

float4 PSVisualizeProbes(float4 svPos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float2 p = uv * 2.0 - 1.0;
    float r = length(p);
    float sphere = smoothstep(0.55, 0.42, r);
    float rim = smoothstep(0.62, 0.54, r) * (1.0 - sphere);
    float3 color = float3(0.08, 0.95, 0.42) * sphere + float3(0.9, 1.0, 0.55) * rim;
    return float4(color, max(sphere, rim));
}
