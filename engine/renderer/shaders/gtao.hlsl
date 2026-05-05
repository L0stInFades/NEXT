Texture2D<float> DepthTexture : register(t0, space0);
SamplerState LinearClampSampler : register(s0, space0);

cbuffer GTAOConstants : register(b0, space0) {
    float radiusPixels;
    float power;
    float sampleCount;
    float temporalStability;
    float invWidth;
    float invHeight;
    float depthScale;
    float padding0;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float Hash12(float2 p) {
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float4 PSGTAO(PSInput input) : SV_Target {
    const float2 uv = saturate(input.texcoord);
    const float centerDepth = DepthTexture.Sample(LinearClampSampler, uv).r;

    if (centerDepth >= 0.9999f) {
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    const int count = clamp((int)sampleCount, 4, 16);
    const float2 texel = float2(invWidth, invHeight);
    const float angleOffset = Hash12(input.position.xy) * 6.2831853f;

    float occlusion = 0.0f;
    float totalWeight = 0.0f;

    [loop]
    for (int i = 0; i < count; ++i) {
        const float t = ((float)i + 0.5f) / (float)count;
        const float angle = angleOffset + (float)i * 2.3999632f;
        const float sampleRadius = radiusPixels * sqrt(t);
        const float2 sampleUV = saturate(uv + float2(cos(angle), sin(angle)) * sampleRadius * texel);
        const float sampleDepth = DepthTexture.Sample(LinearClampSampler, sampleUV).r;

        const float depthDelta = saturate((centerDepth - sampleDepth) * depthScale);
        const float rangeWeight = saturate(1.0f - abs(centerDepth - sampleDepth) * depthScale * 0.25f);
        occlusion += depthDelta * rangeWeight;
        totalWeight += rangeWeight;
    }

    occlusion = totalWeight > 0.0f ? occlusion / totalWeight : 0.0f;
    const float ao = pow(saturate(1.0f - occlusion), max(0.001f, power));
    return float4(ao, ao, ao, 1.0f);
}
