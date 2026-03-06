// Simplified PBR Pixel Shader - For debugging PSO creation
// This shader just outputs a fixed color to test PSO creation

struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
};

float4 main(PSInput input) : SV_TARGET {
    // Simple red color for testing
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}
