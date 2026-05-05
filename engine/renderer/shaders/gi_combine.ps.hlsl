Texture2D AOTexture : register(t0, space0);
Texture2D ProbeTexture : register(t1, space0);
Texture2D ScreenSpaceGITexture : register(t2, space0);
SamplerState LinearClampSampler : register(s0, space0);

cbuffer GICombineConstants : register(b0, space0) {
    float giIntensity;
    float indirectIntensity;
    float aoStrength;
    float probeStrength;

    float screenSpaceStrength;
    float padding0;
    float padding1;
    float padding2;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target {
    const float2 uv = saturate(input.texcoord);
    const float ao = saturate(AOTexture.Sample(LinearClampSampler, uv).r);
    const float3 probes = ProbeTexture.Sample(LinearClampSampler, uv).rgb;
    const float3 screenSpaceGI = ScreenSpaceGITexture.Sample(LinearClampSampler, uv).rgb;

    const float aoVisibility = lerp(1.0f, ao, saturate(aoStrength));
    const float3 ambientBase = float3(0.02f, 0.024f, 0.032f) * indirectIntensity;
    const float3 combined =
        ambientBase * aoVisibility +
        probes * probeStrength +
        screenSpaceGI * screenSpaceStrength;

    return float4(max(combined * giIntensity, 0.0f), 1.0f);
}
