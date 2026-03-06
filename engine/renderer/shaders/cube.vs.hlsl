// Cube Vertex Shader
// 3D transformation with MVP matrix
// Using space0 for basic rendering

struct VSInput {
    float3 position : POSITION;
    float3 color : COLOR;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

// Constant buffer: MVP matrices (16-byte aligned)
// b0 = register 0, space 0
// Note: avoid naming this "ConstantBuffer" because it collides with HLSL keywords in newer shader models.
cbuffer CubeConstants : register(b0, space0) {
    float4x4 modelMatrix;      // 64 bytes (0-63)
    float4x4 viewMatrix;       // 64 bytes (64-127)
    float4x4 projectionMatrix; // 64 bytes (128-191)
    float time;                // 4 bytes (192)
    float padding[3];          // 12 bytes (193-204) - alignment to 16 bytes
};

VSOutput main(VSInput input) {
    VSOutput output;

    // Apply transformations: MVP
    float4 worldPos = mul(float4(input.position, 1.0f), modelMatrix);
    float4 viewPos = mul(worldPos, viewMatrix);
    float4 clipPos = mul(viewPos, projectionMatrix);

    output.position = clipPos;
    output.color = input.color;

    return output;
}
