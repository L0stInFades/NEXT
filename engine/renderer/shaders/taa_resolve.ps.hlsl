Texture2D CurrentColor : register(t0, space0);
Texture2D HistoryColor : register(t1, space0);
Texture2D MotionVectors : register(t2, space0);
SamplerState LinearClampSampler : register(s0, space0);

cbuffer TAAConstants : register(b0, space0) {
    float blendFactor;
    float sharpening;
    float antiGhosting;
    float velocityScale;

    float rectificationBias;
    float historyValid;
    float velocityAvailable;
    float texelSizeX;

    float texelSizeY;
    float padding0;
    float padding1;
    float padding2;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float Luminance(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 SampleCurrent(float2 uv) {
    return CurrentColor.Sample(LinearClampSampler, uv).rgb;
}

float4 main(PSInput input) : SV_TARGET {
    const float2 uv = saturate(input.texcoord);
    const float2 texelSize = float2(texelSizeX, texelSizeY);

    float3 current = SampleCurrent(uv);

    float2 motion = float2(0.0f, 0.0f);
    if (velocityAvailable > 0.5f) {
        motion = MotionVectors.Sample(LinearClampSampler, uv).rg * velocityScale;
    }

    const float2 historyUV = uv - motion;
    const bool insideHistory =
        all(historyUV >= float2(0.0f, 0.0f)) &&
        all(historyUV <= float2(1.0f, 1.0f));

    float3 history = HistoryColor.Sample(LinearClampSampler, saturate(historyUV)).rgb;

    float3 neighborhoodMin = current;
    float3 neighborhoodMax = current;
    float3 neighborhoodSum = float3(0.0f, 0.0f, 0.0f);

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float2 sampleUV = saturate(uv + float2(x, y) * texelSize);
            const float3 sampleColor = SampleCurrent(sampleUV);
            neighborhoodMin = min(neighborhoodMin, sampleColor);
            neighborhoodMax = max(neighborhoodMax, sampleColor);
            neighborhoodSum += sampleColor;
        }
    }

    const float3 bias = float3(rectificationBias, rectificationBias, rectificationBias);
    history = clamp(history, neighborhoodMin - bias, neighborhoodMax + bias);

    const float luminanceDelta = abs(Luminance(current) - Luminance(history));
    float historyWeight = saturate(blendFactor) * historyValid;
    historyWeight *= insideHistory ? 1.0f : 0.0f;
    historyWeight *= saturate(1.0f - luminanceDelta * max(0.0f, antiGhosting));

    float3 resolved = lerp(current, history, historyWeight);

    if (sharpening > 0.0f) {
        const float3 neighborhoodAverage = neighborhoodSum / 9.0f;
        resolved += (resolved - neighborhoodAverage) * sharpening;
    }

    return float4(max(resolved, 0.0f), 1.0f);
}
