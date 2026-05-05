Texture2D SceneColor : register(t0, space0);
Texture2D GlobalIllumination : register(t1, space0);
SamplerState LinearClampSampler : register(s0, space0);

cbuffer PostProcessConstants : register(b0, space0) {
    float bloomIntensity;
    float bloomThreshold;
    float bloomSoftKnee;
    float bloomRadius;

    float minLuminance;
    float maxLuminance;
    float preExposure;
    float exposureBias;

    float contrast;
    float saturation;
    float gamma;
    float temperature;

    float tint;
    float vibrance;
    float adaptationSpeedUp;
    float adaptationSpeedDown;

    float timeSeconds;
    float globalIlluminationIntensity;
    float globalIlluminationAvailable;
    float invFrameWidth;

    float invFrameHeight;
    float passMode;
    float bloomIterations;
    float padding0;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float3 ToneMapACES(float3 color) {
    return saturate((color * (2.51f * color + 0.03f)) / (color * (2.43f * color + 0.59f) + 0.14f));
}

float3 ApplyColorTemperature(float3 color, float temperatureValue, float tintValue) {
    float3 balance = float3(
        1.0f + temperatureValue * 0.08f - tintValue * 0.02f,
        1.0f + tintValue * 0.06f,
        1.0f - temperatureValue * 0.08f - tintValue * 0.02f
    );
    return max(color * balance, 0.0f);
}

float3 ApplyColorGrading(float3 color) {
    float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    float3 grayscale = luminance.xxx;
    float saturationBoost = saturation + vibrance * saturate(1.0f - abs(luminance - 0.5f) * 2.0f);
    color = lerp(grayscale, color, max(0.0f, saturationBoost));
    color = (color - 0.5f) * max(0.0f, contrast) + 0.5f;
    color = ApplyColorTemperature(color, temperature, tint);
    return max(color, 0.0f);
}

float ComputeAdaptedExposure(float sceneLuminance) {
    float targetExposure = clamp(0.18f / max(sceneLuminance, 0.0001f), minLuminance, maxLuminance);
    float currentExposure = clamp(preExposure, minLuminance, maxLuminance);
    float adaptationRate = targetExposure > currentExposure ? adaptationSpeedUp : adaptationSpeedDown;
    float adaptedExposure = lerp(currentExposure, targetExposure, saturate(adaptationRate * 0.016f));
    return adaptedExposure * exp2(exposureBias);
}

float3 SampleSceneWithGI(float2 uv) {
    float3 color = SceneColor.Sample(LinearClampSampler, uv).rgb;
    float3 indirectLighting = GlobalIllumination.Sample(LinearClampSampler, uv).rgb;
    return max(color + indirectLighting * globalIlluminationIntensity * globalIlluminationAvailable, 0.0f);
}

float3 ApplyBloomThreshold(float3 color) {
    float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    float threshold = max(0.0001f, bloomThreshold);
    float knee = max(0.0001f, bloomSoftKnee * threshold);
    float soft = saturate((luminance - threshold + knee) / (2.0f * knee));
    soft = soft * soft * (3.0f - 2.0f * soft);
    float hard = step(threshold, luminance);
    float contribution = max(hard, soft);
    return color * contribution;
}

float3 GatherBloom(float2 uv) {
    float2 texel = float2(invFrameWidth, invFrameHeight);
    float radiusScale = max(1.0f, bloomRadius * max(1.0f, bloomIterations) * 0.75f);
    float2 r1 = texel * radiusScale;
    float2 r2 = texel * radiusScale * 2.0f;
    float2 r3 = texel * radiusScale * 3.5f;

    float3 bloom = ApplyBloomThreshold(SceneColor.Sample(LinearClampSampler, uv).rgb) * 0.22f;
    bloom += ApplyBloomThreshold(SceneColor.Sample(LinearClampSampler, uv + float2( r1.x, 0.0f)).rgb) * 0.11f;
    bloom += ApplyBloomThreshold(SceneColor.Sample(LinearClampSampler, uv + float2(-r1.x, 0.0f)).rgb) * 0.11f;
    bloom += ApplyBloomThreshold(SceneColor.Sample(LinearClampSampler, uv + float2(0.0f,  r1.y)).rgb) * 0.11f;
    bloom += ApplyBloomThreshold(SceneColor.Sample(LinearClampSampler, uv + float2(0.0f, -r1.y)).rgb) * 0.11f;
    bloom += ApplyBloomThreshold(SceneColor.Sample(LinearClampSampler, uv + float2( r2.x,  r2.y)).rgb) * 0.07f;
    bloom += ApplyBloomThreshold(SceneColor.Sample(LinearClampSampler, uv + float2(-r2.x,  r2.y)).rgb) * 0.07f;
    bloom += ApplyBloomThreshold(SceneColor.Sample(LinearClampSampler, uv + float2( r2.x, -r2.y)).rgb) * 0.07f;
    bloom += ApplyBloomThreshold(SceneColor.Sample(LinearClampSampler, uv + float2(-r2.x, -r2.y)).rgb) * 0.07f;
    bloom += ApplyBloomThreshold(SceneColor.Sample(LinearClampSampler, uv + float2( r3.x, 0.0f)).rgb) * 0.03f;
    bloom += ApplyBloomThreshold(SceneColor.Sample(LinearClampSampler, uv + float2(-r3.x, 0.0f)).rgb) * 0.03f;
    return bloom;
}

float EstimateLocalSceneLuminance(float2 uv) {
    float2 texel = float2(invFrameWidth, invFrameHeight);
    float2 radius = texel * max(8.0f, bloomRadius * 24.0f);

    float luminance = 0.0f;
    luminance += dot(SceneColor.Sample(LinearClampSampler, uv).rgb, float3(0.2126f, 0.7152f, 0.0722f)) * 0.30f;
    luminance += dot(SceneColor.Sample(LinearClampSampler, uv + float2( radius.x, 0.0f)).rgb, float3(0.2126f, 0.7152f, 0.0722f)) * 0.12f;
    luminance += dot(SceneColor.Sample(LinearClampSampler, uv + float2(-radius.x, 0.0f)).rgb, float3(0.2126f, 0.7152f, 0.0722f)) * 0.12f;
    luminance += dot(SceneColor.Sample(LinearClampSampler, uv + float2(0.0f,  radius.y)).rgb, float3(0.2126f, 0.7152f, 0.0722f)) * 0.12f;
    luminance += dot(SceneColor.Sample(LinearClampSampler, uv + float2(0.0f, -radius.y)).rgb, float3(0.2126f, 0.7152f, 0.0722f)) * 0.12f;
    luminance += dot(SceneColor.Sample(LinearClampSampler, uv + float2( radius.x,  radius.y)).rgb, float3(0.2126f, 0.7152f, 0.0722f)) * 0.055f;
    luminance += dot(SceneColor.Sample(LinearClampSampler, uv + float2(-radius.x,  radius.y)).rgb, float3(0.2126f, 0.7152f, 0.0722f)) * 0.055f;
    luminance += dot(SceneColor.Sample(LinearClampSampler, uv + float2( radius.x, -radius.y)).rgb, float3(0.2126f, 0.7152f, 0.0722f)) * 0.055f;
    luminance += dot(SceneColor.Sample(LinearClampSampler, uv + float2(-radius.x, -radius.y)).rgb, float3(0.2126f, 0.7152f, 0.0722f)) * 0.055f;
    return max(luminance, 0.0001f);
}

float4 main(PSInput input) : SV_TARGET {
    if (passMode < 0.5f) {
        return float4(SampleSceneWithGI(input.texcoord), 1.0f);
    }

    if (passMode < 1.5f) {
        return float4(GatherBloom(input.texcoord), 1.0f);
    }

    float3 color = SceneColor.Sample(LinearClampSampler, input.texcoord).rgb;
    float3 bloom = GlobalIllumination.Sample(LinearClampSampler, input.texcoord).rgb;
    float exposure = ComputeAdaptedExposure(EstimateLocalSceneLuminance(input.texcoord));
    color *= exposure;

    float radiusWeight = saturate(bloomRadius * 0.5f + 0.5f);
    color += bloom * bloomIntensity * radiusWeight;

    color = ApplyColorGrading(color);
    color = ToneMapACES(color);
    color = pow(max(color, 0.0f), 1.0f / max(0.01f, gamma));

    return float4(saturate(color), 1.0f);
}
