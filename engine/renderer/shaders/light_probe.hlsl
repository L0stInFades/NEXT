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

//=============================================================================
// Probe Baking Compute Shader
//=============================================================================

[numthreads(64, 1, 1)]
void CSBakeProbes(uint3 DTid : SV_DispatchThreadID, uint3 Gid : SV_GroupID)
{
    int probeIndex = DTid.x;
    int rayIndex = DTid.y;

    // Generate ray direction (Hammersley sequence for good distribution)
    float i = (float)rayIndex;
    float numRays = (float)RaysPerProbe;
    float2 hammersley = float2(i / numRays, fract(i * 0.5 + 0.5));

    float phi = 2.0 * 3.14159 * hammersley.y;
    float cosTheta = 1.0 - hammersley.x;
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 rayDir = float3(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta
    );

    // Trace ray and get radiance
    // TODO: Implement ray tracing or screen-space sampling
    float3 radiance = float3(0.0, 0.0, 0.0);

    // For now, sample cubemap
    // TODO: Proper cubemap sampling
    radiance = float3(0.5, 0.5, 0.6); // Placeholder sky color

    // Project onto SH
    float3 shContribution = ProjectSH(rayDir, radiance);

    // Accumulate into shared memory
    // TODO: Implement proper accumulation
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
    // TODO: Reconstruct world position from depth

    float3 worldPos = CameraPos; // Placeholder
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
    // Visualize probe positions
    // TODO: Implement sphere rendering for each probe
    return float4(0.0, 1.0, 0.0, 1.0);
}
