// Cube Pixel Shader
// Simple per-vertex color rendering

struct PSInput {
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 texcoord : TEXCOORD;
};

Texture2D albedoTexture : register(t0, space0);
SamplerState albedoSampler : register(s0, space0);

float4 main(PSInput input) : SV_TARGET {
    float4 texel = albedoTexture.Sample(albedoSampler, input.texcoord);
    return texel * input.color;
}
