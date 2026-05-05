// PBR Vertex Shader
// Transforms vertices and passes data for PBR lighting
// Using space0 to isolate from pixel shader

struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float2 texcoord : TEXCOORD;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float2 texcoord : TEXCOORD;
};

// Constant buffer (register b0, space0)
// Space0 isolates vertex shader constants from pixel shader
cbuffer TransformBuffer : register(b0, space0) {
    float4x4 mvp;
    float4x4 model;
};

VSOutput main(VSInput input) {
    VSOutput output;

    // Clip space position
    output.position = mul(float4(input.position, 1.0f), mvp);

    // World space position (for lighting calculations)
    float4 worldPos = mul(float4(input.position, 1.0f), model);
    output.worldPos = worldPos.xyz;

    // Transform tangent basis to world space
    float3x3 model3 = (float3x3)model;
    output.normal = normalize(mul(input.normal, model3));
    output.tangent = normalize(mul(input.tangent, model3));

    // Pass through texture coordinates
    output.texcoord = input.texcoord;

    return output;
}
