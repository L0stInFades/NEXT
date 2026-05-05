Texture3D<float4> VoxelAlbedo : register(t0, space0);
Texture3D<float4> VoxelNormal : register(t1, space0);
Texture3D<float4> VoxelEmission : register(t2, space0);
SamplerState LinearClampSampler : register(s0, space0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target {
    const float2 uv = saturate(input.texcoord);
    const float3 origin = float3(uv.x, saturate(0.15f + uv.y * 0.70f), uv.y);
    const float3 coneDirection = normalize(float3(uv.x - 0.5f, 0.42f, uv.y - 0.5f));

    float3 accumulated = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int i = 0; i < 6; ++i) {
        const float t = ((float)i + 0.5f) / 6.0f;
        const float3 sampleUVW = saturate(origin + coneDirection * (t * 0.42f));
        const float3 albedo = VoxelAlbedo.SampleLevel(LinearClampSampler, sampleUVW, 0.0f).rgb;
        const float3 normal = normalize(VoxelNormal.SampleLevel(LinearClampSampler, sampleUVW, 0.0f).rgb * 2.0f - 1.0f);
        const float3 emission = VoxelEmission.SampleLevel(LinearClampSampler, sampleUVW, 0.0f).rgb;
        const float directional = saturate(dot(normal, -coneDirection) * 0.5f + 0.5f);
        const float weight = (1.0f - t) * (1.0f - t);

        accumulated += (emission + albedo * 0.018f * directional) * weight;
        weightSum += weight;
    }

    const float3 indirect = weightSum > 0.0f ? accumulated / weightSum : 0.0f;
    return float4(indirect, 0.0f);
}
