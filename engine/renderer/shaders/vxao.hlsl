Texture2D<float> DepthTexture : register(t0, space0);
SamplerState LinearClampSampler : register(s0, space0);

cbuffer VXAOConstants : register(b0, space0) {
    float radiusPixels;
    float coneSampleCount;
    float coneDirectionCount;
    float power;
    float invWidth;
    float invHeight;
    float depthScale;
    float hardness;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float2 ConeDirection(int index) {
    const float angle = ((float)index + 0.5f) * 0.78539816339f;
    return float2(cos(angle), sin(angle));
}

float4 PSVXAO(PSInput input) : SV_Target {
    const float2 uv = saturate(input.texcoord);
    const float centerDepth = DepthTexture.Sample(LinearClampSampler, uv).r;

    if (centerDepth >= 0.9999f) {
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    const int cones = clamp((int)coneDirectionCount, 4, 8);
    const int samples = clamp((int)coneSampleCount, 2, 12);
    const float2 texel = float2(invWidth, invHeight);

    float occlusion = 0.0f;
    float totalWeight = 0.0f;

    [loop]
    for (int c = 0; c < cones; ++c) {
        const float2 direction = ConeDirection(c);
        float coneOcclusion = 0.0f;
        float coneWeight = 0.0f;

        [loop]
        for (int s = 1; s <= samples; ++s) {
            const float sampleT = (float)s / (float)samples;
            const float sampleRadius = radiusPixels * sampleT;
            const float2 sampleUV = saturate(uv + direction * sampleRadius * texel);
            const float sampleDepth = DepthTexture.Sample(LinearClampSampler, sampleUV).r;
            const float depthDelta = saturate((centerDepth - sampleDepth) * depthScale);
            const float falloff = saturate(1.0f - sampleT);
            const float rangeWeight = falloff * falloff;

            coneOcclusion += depthDelta * rangeWeight;
            coneWeight += rangeWeight;
        }

        occlusion += coneWeight > 0.0f ? coneOcclusion / coneWeight : 0.0f;
        totalWeight += 1.0f;
    }

    occlusion = totalWeight > 0.0f ? occlusion / totalWeight : 0.0f;
    occlusion = saturate(occlusion * max(0.001f, hardness));
    const float ao = pow(saturate(1.0f - occlusion), max(0.001f, power));
    return float4(ao, ao, ao, 1.0f);
}
