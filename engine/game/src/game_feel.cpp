#include "next/game/game_feel.h"
#include "next/foundation/logger.h"
#include <cmath>

namespace Next {

// ===== CameraShake Implementation =====

CameraShake::CameraShake()
    : isShaking_(false)
    , shakeTime_(0.0f)
    , shakeDuration_(0.0f)
    , shakeIntensity_(1.0f)
    , currentShake_(0.0f, 0.0f, 0.0f)
    , positionSeed_(42)
    , rotationSeed_(123)
    , initialized_(false) {
}

bool CameraShake::Initialize() {
    // Create default shake presets
    ShakeParameters subtle;
    subtle.positionAmplitude = 0.05f;
    subtle.rotationAmplitude = 1.0f;
    subtle.noiseFrequency = 2.0f;
    presets_["subtle"] = subtle;

    ShakeParameters moderate;
    moderate.positionAmplitude = 0.15f;
    moderate.rotationAmplitude = 3.0f;
    moderate.noiseFrequency = 1.5f;
    presets_["moderate"] = moderate;

    ShakeParameters heavy;
    heavy.positionAmplitude = 0.3f;
    heavy.rotationAmplitude = 5.0f;
    heavy.noiseFrequency = 1.0f;
    heavy.enableFOV = true;
    presets_["heavy"] = heavy;

    ShakeParameters explosion;
    explosion.positionAmplitude = 0.5f;
    explosion.rotationAmplitude = 10.0f;
    explosion.noiseFrequency = 0.8f;
    explosion.enableFOV = true;
    explosion.fovAmplitude = 2.0f;
    presets_["explosion"] = explosion;

    ShakeParameters impact;
    impact.positionAmplitude = 0.2f;
    impact.rotationAmplitude = 5.0f;
    impact.noiseFrequency = 3.0f;
    presets_["impact"] = impact;

    ShakeParameters earthquake;
    earthquake.positionAmplitude = 0.8f;
    earthquake.rotationAmplitude = 2.0f;
    earthquake.noiseFrequency = 0.3f;
    earthquake.noiseSpeed = 0.5f;
    presets_["earthquake"] = earthquake;

    params_ = moderate;  // Default

    initialized_ = true;
    NEXT_LOG_INFO("Camera shake initialized (CP6: Game Feel)");
    return true;
}

void CameraShake::StartShake(ShakeType type, float duration, float intensity) {
    // Load preset based on type
    switch (type) {
        case ShakeType::Subtle:
            LoadPreset("subtle");
            break;
        case ShakeType::Moderate:
            LoadPreset("moderate");
            break;
        case ShakeType::Heavy:
            LoadPreset("heavy");
            break;
        case ShakeType::Explosion:
            LoadPreset("explosion");
            break;
        case ShakeType::Impact:
            LoadPreset("impact");
            break;
        case ShakeType::Earthquake:
            LoadPreset("earthquake");
            break;
        default:
            break;
    }

    shakeDuration_ = duration;
    shakeIntensity_ = intensity;
    shakeTime_ = 0.0f;
    isShaking_ = true;

    NEXT_LOG_DEBUG("Camera shake started: type=%d, duration=%.2f, intensity=%.2f",
                   static_cast<int>(type), duration, intensity);
}

void CameraShake::StartCustomShake(const std::string& presetName, float duration, float intensity) {
    if (!LoadPreset(presetName)) {
        NEXT_LOG_ERROR("Failed to load shake preset: %s", presetName.c_str());
        return;
    }

    shakeDuration_ = duration;
    shakeIntensity_ = intensity;
    shakeTime_ = 0.0f;
    isShaking_ = true;

    NEXT_LOG_DEBUG("Custom camera shake started: preset=%s, duration=%.2f, intensity=%.2f",
                   presetName.c_str(), duration, intensity);
}

void CameraShake::Update(float deltaTime) {
    if (!isShaking_) {
        currentShake_ = Vec3(0.0f, 0.0f, 0.0f);
        return;
    }

    shakeTime_ += deltaTime;

    if (shakeTime_ >= shakeDuration_) {
        isShaking_ = false;
        currentShake_ = Vec3(0.0f, 0.0f, 0.0f);
        return;
    }

    // Calculate shake intensity falloff
    float progress = shakeTime_ / shakeDuration_;
    float intensity = shakeIntensity_ * (1.0f - progress);  // Linear falloff

    // Generate noise-based shake
    float time = shakeTime_ * params_.noiseSpeed;

    float noiseX = GenerateNoise(time, positionSeed_);
    float noiseY = GenerateNoise(time + 100.0f, positionSeed_ + 1);
    float noiseZ = GenerateNoise(time + 200.0f, positionSeed_ + 2);

    Vec3 shakeOffset;
    if (params_.enablePosition) {
        shakeOffset.x = noiseX * params_.positionAmplitude * intensity;
        shakeOffset.y = noiseY * params_.positionAmplitude * intensity;
        shakeOffset.z = noiseZ * params_.positionAmplitude * intensity;
    }

    // Add rotation shake
    if (params_.enableRotation) {
        float rotNoise = GenerateNoise(time + 300.0f, rotationSeed_);
        // Rotation shake not implemented in offset (would need quaternion)
    }

    currentShake_ = shakeOffset;
}

float CameraShake::GenerateNoise(float time, int seed) {
    // Simple pseudo-random noise
    // In production, use Perlin noise or similar
    float x = time + seed * 0.1f;
    return std::sin(x) * 0.5f + std::sin(x * 2.1f) * 0.25f + std::sin(x * 4.3f) * 0.125f;
}

void CameraShake::StopShake() {
    isShaking_ = false;
    currentShake_ = Vec3(0.0f, 0.0f, 0.0f);
}

void CameraShake::SetShakeParameters(const ShakeParameters& params) {
    params_ = params;
}

bool CameraShake::LoadPreset(const std::string& presetName) {
    auto it = presets_.find(presetName);
    if (it != presets_.end()) {
        params_ = it->second;
        NEXT_LOG_DEBUG("Loaded shake preset: %s", presetName.c_str());
        return true;
    }

    NEXT_LOG_ERROR("Shake preset not found: %s", presetName.c_str());
    return false;
}

bool CameraShake::SavePreset(const std::string& presetName) {
    presets_[presetName] = params_;
    NEXT_LOG_INFO("Saved shake preset: %s", presetName.c_str());
    return true;
}

// ===== ScreenEffect Implementation =====

ScreenEffect::ScreenEffect()
    : isEffectActive_(false)
    , effectTime_(0.0f)
    , effectDuration_(0.0f)
    , initialized_(false) {
}

bool ScreenEffect::Initialize() {
    params_ = EffectParameters();
    initialized_ = true;
    NEXT_LOG_INFO("Screen effect initialized (CP6: Game Feel)");
    return true;
}

void ScreenEffect::StartEffect(ScreenEffectType type, float duration) {
    // Configure effect based on type
    switch (type) {
        case ScreenEffectType::Flash:
            params_.color[0] = 1.0f;  // White flash
            params_.color[1] = 1.0f;
            params_.color[2] = 1.0f;
            params_.color[3] = 0.8f;
            params_.intensity = 1.0f;
            break;
        case ScreenEffectType::Fade:
            params_.color[0] = 0.0f;  // Black fade
            params_.color[1] = 0.0f;
            params_.color[2] = 0.0f;
            params_.color[3] = 1.0f;
            params_.intensity = 1.0f;
            break;
        default:
            break;
    }

    effectDuration_ = duration;
    effectTime_ = 0.0f;
    isEffectActive_ = true;

    NEXT_LOG_DEBUG("Screen effect started: type=%d, duration=%.2f", static_cast<int>(type), duration);
}

void ScreenEffect::StartCustomEffect(const std::string& presetName, float duration) {
    // Preset system requires:
    // - Data-driven configuration file format (JSON/XML)
    // - Asset manager integration for preset loading
    // - Preset validation and error handling
    // For now, using default settings
    effectDuration_ = duration;
    effectTime_ = 0.0f;
    isEffectActive_ = true;

    NEXT_LOG_DEBUG("Custom screen effect started: preset=%s, duration=%.2f", presetName.c_str(), duration);
}

void ScreenEffect::Update(float deltaTime) {
    if (!isEffectActive_) {
        return;
    }

    effectTime_ += deltaTime;

    if (effectTime_ >= effectDuration_) {
        isEffectActive_ = false;
        return;
    }

    // Update effect intensity based on fade in/out
    float progress = effectTime_ / effectDuration_;

    if (params_.fadeIn && progress < 0.1f) {
        params_.intensity = progress / 0.1f;
    } else if (params_.fadeOut && progress > 0.9f) {
        params_.intensity = (1.0f - progress) / 0.1f;
    }
}

void ScreenEffect::StopEffect() {
    isEffectActive_ = false;
}

void ScreenEffect::Shutdown() {
    initialized_ = false;
}

// ===== GameFeelSystem Implementation =====

GameFeelSystem::GameFeelSystem()
    : initialized_(false) {
}

GameFeelSystem::~GameFeelSystem() {
    Shutdown();
}

bool GameFeelSystem::Initialize() {
    // Initialize sub-systems
    if (!cameraShake_.Initialize()) {
        NEXT_LOG_ERROR("Failed to initialize camera shake");
        return false;
    }

    if (!screenEffect_.Initialize()) {
        NEXT_LOG_ERROR("Failed to initialize screen effect");
        return false;
    }

    initialized_ = true;
    NEXT_LOG_INFO("Game feel system initialized (CP6: Game Feel & Camera System)");
    return true;
}

void GameFeelSystem::Update(float deltaTime) {
    if (!initialized_) {
        return;
    }

    // Update sub-systems
    cameraShake_.Update(deltaTime);
    screenEffect_.Update(deltaTime);
}

void GameFeelSystem::ShakeCamera(ShakeType type, float duration, float intensity) {
    cameraShake_.StartShake(type, duration, intensity);
}

void GameFeelSystem::FlashScreen(float duration, float intensity) {
    screenEffect_.StartEffect(ScreenEffectType::Flash, duration);
    // Note: params_ is private, intensity would be set in StartEffect
    // For now, we'll document that intensity should be passed to StartEffect
}

void GameFeelSystem::FadeScreen(float duration, bool fadeIn) {
    // Fade direction parameter is currently stored but not used in rendering
    // Future implementation should pass fadeIn to ScreenEffect::StartEffect
    // or use separate ScreenEffectType values for FadeIn/FadeOut
    screenEffect_.StartEffect(ScreenEffectType::Fade, duration);
}

void GameFeelSystem::ApplyRadialBlur(float duration, float amount) {
    screenEffect_.StartEffect(ScreenEffectType::RadialBlur, duration);
    // Note: blurAmount would be set in StartEffect
}

void GameFeelSystem::OnImpact(const Vec3& position, float force) {
    // Trigger impact shake
    float duration = 0.3f + force * 0.1f;
    float intensity = std::min(force * 0.5f, 1.0f);

    if (force > 5.0f) {
        ShakeCamera(ShakeType::Heavy, duration, intensity);
    } else if (force > 2.0f) {
        ShakeCamera(ShakeType::Moderate, duration, intensity);
    } else {
        ShakeCamera(ShakeType::Subtle, duration, intensity);
    }
}

void GameFeelSystem::OnDamageTaken(float damage, const Vec3& direction) {
    // Screen flash on damage
    FlashScreen(0.3f, 0.5f);

    // Camera shake based on damage
    float intensity = std::min(damage * 0.1f, 1.0f);
    ShakeCamera(ShakeType::Impact, 0.4f, intensity);
}

void GameFeelSystem::OnDeath() {
    // Heavy screen effects on death
    FadeScreen(2.0f, false);
    ShakeCamera(ShakeType::Explosion, 1.0f, 0.8f);
}

void GameFeelSystem::Shutdown() {
    initialized_ = false;
    NEXT_LOG_INFO("Game feel system shutdown complete");
}

} // namespace Next
