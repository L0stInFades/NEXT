// PBR Pixel Shader - Step 1: Add constant buffer reading

struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD;
};

// ====== PBR Material ======
// IMPORTANT: Must match C++ structure layout with 16-byte alignment!
struct PBRMaterial {
    float3 albedo;          // 12 bytes
    float metallic;         // 4 bytes  = 16 bytes

    float3 roughnessAndAO;  // 12 bytes: [roughness, ao, padding]
    float padding1;         // 4 bytes  = 16 bytes

    uint textureFlags;      // 4 bytes: bitfield for texture use flags
    uint padding2;          // 4 bytes
    uint padding3;          // 4 bytes
    uint padding4;          // 4 bytes  = 16 bytes
};

// ====== Lights ======
struct CameraData {
    float3 position;
    float padding;
};

struct LightingSettings {
    float3 ambientColor;
    float ambientIntensity;
    float exposure;
    float gamma;
    int toneMapMode;
};

// Directional Light
struct DirectionalLight {
    float3 direction;
    float intensity;
    float3 color;
    int enabled;
};

// ====== Constant Buffers ======
// Using space1 to isolate from vertex shader (space0)
cbuffer MaterialBuffer : register(b0, space1) {
    PBRMaterial material;
};

cbuffer LightingBuffer : register(b1, space1) {
    CameraData camera;
    LightingSettings settings;
    DirectionalLight directionalLight;
};

float4 main(PSInput input) : SV_TARGET {
    // Test: Read material data (using new packed structure)
    float3 albedo = material.albedo;
    float metallic = material.metallic;
    float roughness = material.roughnessAndAO.x;  // roughness is in x component

    // Simple test: color based on albedo
    return float4(albedo, 1.0f);
}
