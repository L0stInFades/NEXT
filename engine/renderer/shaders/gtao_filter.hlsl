//=============================================================================
// GTAO Spatial and Temporal Filters
//=============================================================================

Texture2D<float4> AOTexture : register(t0, space0);
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

float SampleAO(float2 uv) {
    return AOTexture.Sample(LinearClampSampler, saturate(uv)).r;
}

float4 PSFilter(PSInput input) : SV_Target {
    const float2 uv = saturate(input.texcoord);
    const float2 texel = float2(invWidth, invHeight);
    const float radius = max(1.0f, radiusPixels * 0.10f);
    const float2 r1 = texel * radius;
    const float2 r2 = texel * radius * 2.0f;

    float ao = SampleAO(uv) * 0.28f;
    ao += SampleAO(uv + float2( r1.x, 0.0f)) * 0.12f;
    ao += SampleAO(uv + float2(-r1.x, 0.0f)) * 0.12f;
    ao += SampleAO(uv + float2(0.0f,  r1.y)) * 0.12f;
    ao += SampleAO(uv + float2(0.0f, -r1.y)) * 0.12f;
    ao += SampleAO(uv + float2( r2.x,  r2.y)) * 0.06f;
    ao += SampleAO(uv + float2(-r2.x,  r2.y)) * 0.06f;
    ao += SampleAO(uv + float2( r2.x, -r2.y)) * 0.06f;
    ao += SampleAO(uv + float2(-r2.x, -r2.y)) * 0.06f;
    ao = pow(saturate(ao), max(0.001f, power * 0.5f));
    return float4(ao, ao, ao, 1.0f);
}

cbuffer TemporalConstants : register(b1, space0) {
    float Feedback;
    float3 Padding2;
};

Texture2D<float4> HistoryAO : register(t3, space0);
Texture2D<float2> MotionVectors : register(t4, space0);

float4 PSTemporal(PSInput input) : SV_Target {
    const float2 uv = saturate(input.texcoord);
    const float currentAO = SampleAO(uv);
    const float2 motion = MotionVectors.Sample(LinearClampSampler, uv).rg;
    const float2 prevUV = uv - motion;

    if (prevUV.x < 0.0f || prevUV.x > 1.0f || prevUV.y < 0.0f || prevUV.y > 1.0f) {
        return float4(currentAO, currentAO, currentAO, 1.0f);
    }

    const float historyAO = HistoryAO.Sample(LinearClampSampler, prevUV).r;
    const float feedback = saturate(Feedback);
    const float blendedAO = lerp(historyAO, currentAO, feedback);
    return float4(blendedAO, blendedAO, blendedAO, 1.0f);
}
