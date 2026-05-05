Texture2D<float4> DepthTexture : register(t0, space0);
SamplerState LinearClampSampler : register(s0, space0);

cbuffer HBAOConstants : register(b0, space0) {
    float radiusPixels;
    float bias;
    float stepCount;
    float power;
    float invWidth;
    float invHeight;
    float depthScale;
    float padding0;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float HorizonContribution(float centerDepth, float sampleDepth, float stepRatio) {
    const float depthDelta = saturate((centerDepth - sampleDepth - bias) * depthScale);
    const float rangeWeight = saturate(1.0f - abs(centerDepth - sampleDepth) * depthScale * stepRatio);
    return depthDelta * rangeWeight;
}

float4 PSHBAO(PSInput input) : SV_Target {
    const float2 uv = saturate(input.texcoord);
    const float centerDepth = DepthTexture.Sample(LinearClampSampler, uv).r;

    if (centerDepth >= 0.9999f) {
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    const int steps = clamp((int)stepCount, 2, 8);
    const float2 texel = float2(invWidth, invHeight);
    const float2 directions[8] = {
        float2( 1.0f,  0.0f),
        float2(-1.0f,  0.0f),
        float2( 0.0f,  1.0f),
        float2( 0.0f, -1.0f),
        normalize(float2( 1.0f,  1.0f)),
        normalize(float2(-1.0f,  1.0f)),
        normalize(float2( 1.0f, -1.0f)),
        normalize(float2(-1.0f, -1.0f))
    };

    float occlusion = 0.0f;
    float totalWeight = 0.0f;

    [unroll]
    for (int d = 0; d < 8; ++d) {
        float horizon = 0.0f;

        [loop]
        for (int s = 1; s <= steps; ++s) {
            const float stepRatio = (float)s / (float)steps;
            const float2 sampleUV = saturate(uv + directions[d] * radiusPixels * stepRatio * texel);
            const float sampleDepth = DepthTexture.Sample(LinearClampSampler, sampleUV).r;
            horizon = max(horizon, HorizonContribution(centerDepth, sampleDepth, stepRatio));
        }

        const float directionWeight = 1.0f;
        occlusion += horizon * directionWeight;
        totalWeight += directionWeight;
    }

    occlusion = totalWeight > 0.0f ? occlusion / totalWeight : 0.0f;
    const float ao = pow(saturate(1.0f - occlusion), max(0.001f, power));
    return float4(ao, ao, ao, 1.0f);
}

float4 PSBlur(PSInput input) : SV_Target {
    const float2 uv = saturate(input.texcoord);
    const float2 texel = float2(invWidth, invHeight);
    const float blurRadius = max(1.0f, radiusPixels * 0.125f);
    const float2 r = texel * blurRadius;

    float ao = DepthTexture.Sample(LinearClampSampler, uv).r * 0.24f;
    ao += DepthTexture.Sample(LinearClampSampler, uv + float2( r.x, 0.0f)).r * 0.12f;
    ao += DepthTexture.Sample(LinearClampSampler, uv + float2(-r.x, 0.0f)).r * 0.12f;
    ao += DepthTexture.Sample(LinearClampSampler, uv + float2(0.0f,  r.y)).r * 0.12f;
    ao += DepthTexture.Sample(LinearClampSampler, uv + float2(0.0f, -r.y)).r * 0.12f;
    ao += DepthTexture.Sample(LinearClampSampler, uv + float2( r.x,  r.y)).r * 0.07f;
    ao += DepthTexture.Sample(LinearClampSampler, uv + float2(-r.x,  r.y)).r * 0.07f;
    ao += DepthTexture.Sample(LinearClampSampler, uv + float2( r.x, -r.y)).r * 0.07f;
    ao += DepthTexture.Sample(LinearClampSampler, uv + float2(-r.x, -r.y)).r * 0.07f;
    return float4(ao, ao, ao, 1.0f);
}
