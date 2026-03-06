#pragma once

#include "next/renderer/math/math.h"
#include <d3d12.h>
#include <vector>

namespace Next {

// Light Types
enum class LightType : uint32_t {
    Directional = 0,
    Point = 1,
    Spot = 2
};

// PBR Material Structure
// Matches shader layout for easy binding
// IMPORTANT: HLSL uses 16-byte alignment for structure members!
struct PBRMaterial {
    Vec3 albedo;          // 12 bytes
    float metallic;       // 4 bytes  = 16 bytes (aligned)

    Vec3 roughnessAndAO;  // 12 bytes: [roughness, ao, padding]
    float padding1;       // 4 bytes  = 16 bytes (aligned)

    // Texture flags (packed into single uint32 for experimental features)
    uint32_t textureFlags;  // bitfield: useAlbedoMap | useNormalMap | useMetallicMap | useRoughnessMap | useAOMap
    uint32_t padding2[3];   // 12 bytes = 16 bytes (aligned)

    // Total: 48 bytes, fully 16-byte aligned

    // Flag bit positions
    static constexpr uint32_t ALBEDO_FLAG_BIT = 0;
    static constexpr uint32_t NORMAL_FLAG_BIT = 1;
    static constexpr uint32_t METALLIC_FLAG_BIT = 2;
    static constexpr uint32_t ROUGHNESS_FLAG_BIT = 3;
    static constexpr uint32_t AO_FLAG_BIT = 4;

    PBRMaterial()
        : albedo(1.0f, 1.0f, 1.0f)
        , metallic(0.0f)
        , roughnessAndAO(0.5f, 1.0f, 0.0f)  // roughness=0.5, ao=1.0, padding=0
        , padding1(0.0f)
        , textureFlags(0)
        , padding2{0, 0, 0} {}

    // Helper getters for individual flags (for convenience)
    bool useAlbedoMap() const { return (textureFlags >> ALBEDO_FLAG_BIT) & 1; }
    bool useNormalMap() const { return (textureFlags >> NORMAL_FLAG_BIT) & 1; }
    bool useMetallicMap() const { return (textureFlags >> METALLIC_FLAG_BIT) & 1; }
    bool useRoughnessMap() const { return (textureFlags >> ROUGHNESS_FLAG_BIT) & 1; }
    bool useAOMap() const { return (textureFlags >> AO_FLAG_BIT) & 1; }

    // Helper setters
    void setUseAlbedoMap(bool enable) {
        if (enable) textureFlags |= (1 << ALBEDO_FLAG_BIT);
        else textureFlags &= ~(1 << ALBEDO_FLAG_BIT);
    }
    void setUseNormalMap(bool enable) {
        if (enable) textureFlags |= (1 << NORMAL_FLAG_BIT);
        else textureFlags &= ~(1 << NORMAL_FLAG_BIT);
    }
    void setUseMetallicMap(bool enable) {
        if (enable) textureFlags |= (1 << METALLIC_FLAG_BIT);
        else textureFlags &= ~(1 << METALLIC_FLAG_BIT);
    }
    void setUseRoughnessMap(bool enable) {
        if (enable) textureFlags |= (1 << ROUGHNESS_FLAG_BIT);
        else textureFlags &= ~(1 << ROUGHNESS_FLAG_BIT);
    }
    void setUseAOMap(bool enable) {
        if (enable) textureFlags |= (1 << AO_FLAG_BIT);
        else textureFlags &= ~(1 << AO_FLAG_BIT);
    }

    // Get roughness from the packed vector
    float roughness() const { return roughnessAndAO.x; }
    void setRoughness(float r) { roughnessAndAO.x = r; }

    // Get AO from the packed vector
    float ao() const { return roughnessAndAO.y; }
    void setAO(float a) { roughnessAndAO.y = a; }
};

// Directional Light (Sun)
struct DirectionalLight {
    Vec3 direction;      // Light direction
    float intensity;     // Light intensity

    Vec3 color;          // Light color (RGB)
    int enabled;         // On/off

    DirectionalLight()
        : direction(0.0f, -1.0f, 0.0f)
        , intensity(1.0f)
        , color(1.0f, 1.0f, 1.0f)
        , enabled(1) {}
};

// Point Light (Bulb)
struct PointLight {
    Vec3 position;       // Light position
    float intensity;     // Light intensity

    Vec3 color;          // Light color
    float radius;        // Light radius (for attenuation)

    // Attenuation parameters (experimental)
    float constant;      // Constant attenuation
    float linear;        // Linear attenuation
    float quadratic;     // Quadratic attenuation
    int enabled;         // On/off

    PointLight()
        : position(0.0f, 2.0f, 0.0f)
        , intensity(1.0f)
        , color(1.0f, 1.0f, 1.0f)
        , radius(10.0f)
        , constant(1.0f)
        , linear(0.09f)
        , quadratic(0.032f)
        , enabled(1) {}
};

// Spot Light (Flashlight)
struct SpotLight {
    Vec3 position;       // Light position
    float intensity;     // Light intensity

    Vec3 direction;      // Light direction
    float radius;        // Light radius

    Vec3 color;          // Light color
    float cutoff;        // Inner cutoff angle (radians)

    float outerCutoff;   // Outer cutoff angle (radians)
    int enabled;         // On/off
    float padding[2];

    SpotLight()
        : position(0.0f, 2.0f, 0.0f)
        , intensity(1.0f)
        , direction(0.0f, -1.0f, 0.0f)
        , radius(10.0f)
        , color(1.0f, 1.0f, 1.0f)
        , cutoff(0.35f)        // ~20 degrees
        , outerCutoff(0.5f)    // ~30 degrees
        , enabled(1) {}
};

// Camera data for lighting calculations
struct CameraData {
    Vec3 position;       // Camera position (for specular)
    float padding;

    CameraData() : position(0.0f, 0.0f, -5.0f), padding(0.0f) {}
};

// Global lighting settings
struct LightingSettings {
    Vec3 ambientColor;   // Ambient light color
    float ambientIntensity;  // Ambient intensity

    // HDR/Tone mapping settings (experimental)
    float exposure;      // HDR exposure
    float gamma;         // Gamma correction
    int toneMapMode;     // 0 = None, 1 = ACES, 2 = Reinhard

    LightingSettings()
        : ambientColor(0.1f, 0.1f, 0.1f)
        , ambientIntensity(0.1f)
        , exposure(1.0f)
        , gamma(2.2f)
        , toneMapMode(1) {}  // Default to ACES
};

// Complete Lighting Scene
// All lights in one place for easy management
struct LightingScene {
    CameraData camera;
    LightingSettings settings;

    DirectionalLight directionalLight;
    std::vector<PointLight> pointLights;
    std::vector<SpotLight> spotLights;

    // Max lights (configurable for performance tuning)
    static const uint32_t MAX_POINT_LIGHTS = 4;
    static const uint32_t MAX_SPOT_LIGHTS = 4;

    // Add point light
    void AddPointLight(const PointLight& light) {
        if (pointLights.size() < MAX_POINT_LIGHTS) {
            pointLights.push_back(light);
        }
    }

    // Add spot light
    void AddSpotLight(const SpotLight& light) {
        if (spotLights.size() < MAX_SPOT_LIGHTS) {
            spotLights.push_back(light);
        }
    }

    // Clear all lights
    void ClearLights() {
        pointLights.clear();
        spotLights.clear();
    }

    LightingScene() {
        // Default scene: one directional light
        directionalLight.enabled = 1;
        directionalLight.direction = Vec3(0.3f, -1.0f, 0.5f).Normalize();
        directionalLight.intensity = 1.0f;
        directionalLight.color = Vec3(1.0f, 0.95f, 0.8f);  // Warm sunlight
    }
};

} // namespace Next
