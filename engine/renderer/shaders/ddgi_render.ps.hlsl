struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target {
    const float2 uv = saturate(input.texcoord);
    const float probeBand = smoothstep(0.15f, 0.85f, uv.y);
    const float floorBounce = 1.0f - smoothstep(0.15f, 0.95f, uv.y);
    const float sideWall = abs(uv.x * 2.0f - 1.0f);

    const float3 irradiance =
        float3(0.030f, 0.036f, 0.046f) * probeBand +
        float3(0.046f, 0.036f, 0.025f) * floorBounce +
        float3(0.006f, 0.007f, 0.008f) * sideWall;

    return float4(irradiance, 1.0f);
}
