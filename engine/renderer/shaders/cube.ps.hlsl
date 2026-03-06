// Cube Pixel Shader
// Simple per-vertex color rendering

struct PSInput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

float4 main(PSInput input) : SV_TARGET {
    // Output vertex color
    return float4(input.color, 1.0f);
}
