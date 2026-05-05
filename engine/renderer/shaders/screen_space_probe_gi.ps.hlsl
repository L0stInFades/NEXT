struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float Hash12(float2 p) {
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float4 main(PSInput input) : SV_Target {
    const float2 uv = saturate(input.texcoord);
    const float2 probeGrid = floor(uv * 48.0f);
    const float probeJitter = Hash12(probeGrid);
    const float skyTerm = smoothstep(0.0f, 1.0f, uv.y);
    const float horizonTerm = 1.0f - abs(uv.y * 2.0f - 1.0f);

    const float3 coolBounce = float3(0.030f, 0.040f, 0.055f) * skyTerm;
    const float3 warmBounce = float3(0.055f, 0.044f, 0.030f) * horizonTerm;
    const float3 probeVariation = lerp(float3(0.92f, 0.96f, 1.0f), float3(1.0f, 0.94f, 0.88f), probeJitter) * 0.012f;

    return float4(coolBounce + warmBounce + probeVariation, 1.0f);
}
