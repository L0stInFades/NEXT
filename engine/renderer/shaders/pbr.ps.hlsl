// PBR Pixel Shader
// Physically Based Rendering using Cook-Torrance BRDF

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
struct DirectionalLight {
    float3 direction;
    float intensity;
    float3 color;
    int enabled;
};

struct PointLight {
    float3 position;
    float intensity;
    float3 color;
    float radius;
    float constant;
    float linearAttenuation;
    float quadratic;
    int enabled;
};

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

// ====== Constant Buffers ======
// Using space1 to isolate from vertex shader (space0)

// Material parameters - space1, register 0
cbuffer MaterialBuffer : register(b0, space1) {
    PBRMaterial material;
};

// Lighting parameters - space1, register 1
cbuffer LightingBuffer : register(b1, space1) {
    CameraData camera;
    LightingSettings settings;
    DirectionalLight directionalLight;
    PointLight pointLights[4];
    int numPointLights;
    int numSpotLights;
};

// ====== Textures ======
Texture2D albedoMap : register(t0, space1);
Texture2D normalMap : register(t1, space1);
Texture2D metallicMap : register(t2, space1);
Texture2D roughnessMap : register(t3, space1);
Texture2D aoMap : register(t4, space1);

SamplerState sampler0 : register(s0, space1);

// Texture flag bits (must match C++ PBRMaterial)
static const uint ALBEDO_FLAG_BIT   = 0;
static const uint NORMAL_FLAG_BIT   = 1;
static const uint METALLIC_FLAG_BIT = 2;
static const uint ROUGHNESS_FLAG_BIT = 3;
static const uint AO_FLAG_BIT       = 4;

bool HasTexture(uint bit) {
    return (material.textureFlags & (1u << bit)) != 0;
}

// ====== PBR Helper Functions ======

// Simplified version - no textures for initial testing
// We'll use material parameters directly

// Normal Distribution Function (Trowbridge-Reitz GGX)
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = 3.14159 * denom * denom;

    return num / max(denom, 0.0001);
}

// Geometry Function (Smith)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / max(denom, 0.0001);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Fresnel Function (Schlick approximation)
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ====== Main Lighting Calculation ======
float3 CalculatePBR(float3 N, float3 V, float3 L, float3 radiance, float3 albedo, float metallic, float roughness) {
    float3 H = normalize(V + L);

    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), lerp(float3(0.04, 0.04, 0.04), albedo, metallic));

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    float3 specular = numerator / max(denominator, 0.0001);

    // kS is the specular component
    float3 kS = F;

    // kD is the diffuse component
    float3 kD = float3(1.0, 1.0, 1.0) - kS;
    kD *= 1.0 - metallic;

    // Lambertian diffuse
    float NdotL = max(dot(N, L), 0.0);
    float3 diffuse = kD * albedo / 3.14159;

    // Final lighting
    return (diffuse + specular) * radiance * NdotL;
}

// ====== Tone Mapping =====
float3 ToneMapACES(float3 color) {
    const float A = 2.51;
    const float B = 0.03;
    const float C = 2.43;
    const float D = 0.59;
    const float E = 0.14;

    return clamp((color * (A * color + B)) / (color * (C * color + D) + E), 0.0, 1.0);
}

float3 ToneMapReinhard(float3 color) {
    return color / (color + float3(1.0, 1.0, 1.0));
}

float3 ToneMapNone(float3 color) {
    return clamp(color, 0.0, 1.0);
}

// ====== Main Pixel Shader ======
float4 main(PSInput input) : SV_TARGET {
    // Get material parameters (using direct values, no textures for now)
    float3 albedo = material.albedo;
    float metallic = material.metallic;
    float roughness = material.roughnessAndAO.x;  // roughness is in x component
    float ao = material.roughnessAndAO.y;         // ao is in y component

    if (HasTexture(ALBEDO_FLAG_BIT)) {
        float4 texColor = albedoMap.Sample(sampler0, input.texcoord);
        albedo = texColor.rgb;
    }

    // Normal map sampling is intentionally omitted for now (tangent space not wired).

    if (HasTexture(METALLIC_FLAG_BIT)) {
        metallic = metallicMap.Sample(sampler0, input.texcoord).r;
    }

    if (HasTexture(ROUGHNESS_FLAG_BIT)) {
        roughness = roughnessMap.Sample(sampler0, input.texcoord).r;
    }

    if (HasTexture(AO_FLAG_BIT)) {
        ao = aoMap.Sample(sampler0, input.texcoord).r;
    }

    // Normal mapping (disabled for now - requires tangent/bitangent)
    float3 N = normalize(input.normal);

    // View direction
    float3 V = normalize(camera.position - input.worldPos);

    // Reflectance
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    // Accumulate lighting
    float3 Lo = float3(0.0, 0.0, 0.0);

    // Directional Light
    if (directionalLight.enabled) {
        float3 L = normalize(-directionalLight.direction);
        float3 radiance = directionalLight.color * directionalLight.intensity;
        Lo += CalculatePBR(N, V, L, radiance, albedo, metallic, roughness);
    }

    // Point Lights
    for (int i = 0; i < numPointLights; i++) {
        if (!pointLights[i].enabled) continue;

        float3 L = pointLights[i].position - input.worldPos;
        float distance = length(L);
        L = normalize(L);

        // Attenuation
        float attenuation = 1.0 / (pointLights[i].constant +
                                   pointLights[i].linearAttenuation * distance +
                                   pointLights[i].quadratic * distance * distance);

        float3 radiance = pointLights[i].color * pointLights[i].intensity * attenuation;
        Lo += CalculatePBR(N, V, L, radiance, albedo, metallic, roughness);
    }

    // Ambient
    float3 ambient = settings.ambientColor * settings.ambientIntensity * albedo * ao;

    // HDR final color
    float3 color = ambient + Lo;

    // Tone mapping
    if (settings.toneMapMode == 0) {
        color = ToneMapNone(color);
    } else if (settings.toneMapMode == 1) {
        color = ToneMapACES(color);
    } else if (settings.toneMapMode == 2) {
        color = ToneMapReinhard(color);
    }

    // Gamma correction
    color = pow(color, float3(1.0 / settings.gamma, 1.0 / settings.gamma, 1.0 / settings.gamma));

    return float4(color, 1.0);
}
