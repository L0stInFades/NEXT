#pragma once

#include "next/renderer/math/math.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace Next {

// Camera Shake Types (相机震动类型)
enum class ShakeType {
    None,
    Subtle,           // 轻微震动
    Moderate,         // 中等震动
    Heavy,            // 强烈震动
    Explosion,        // 爆炸震动
    Impact,           // 冲击震动
    Earthquake,       // 地震震动
    Custom            // 自定义震动
};

// Screen Effect Types (屏幕效果类型)
enum class ScreenEffectType {
    None,
    Flash,            // 闪光
    Fade,             // 淡入淡出
    Chromatic,        // 色差
    Vignette,         // 晕影
    RadialBlur,       // 径向模糊
    FilmGrain,        // 胶片颗粒
    Custom            // 自定义效果
};

// Camera Shake (相机震动)
// Design principles:
// - Sustainable Experimental: Easy to create new shake effects
// - Advanced: Multi-layered noise-based shaking
// - Refactor Friendly: Preset system
class CameraShake {
public:
    CameraShake();
    ~CameraShake() = default;

    // Initialize
    bool Initialize();

    // Start shake
    void StartShake(ShakeType type, float duration, float intensity = 1.0f);
    void StartCustomShake(const std::string& presetName, float duration, float intensity = 1.0f);

    // Update shake (called every frame)
    void Update(float deltaTime);

    // Get shake offset
    Vec3 GetShakeOffset() const { return currentShake_; }

    // Stop shake immediately
    void StopShake();

    // Shake parameters
    struct ShakeParameters {
        float positionAmplitude = 0.1f;     // 位置震动幅度
        float rotationAmplitude = 2.0f;     // 旋转震动幅度
        float fovAmplitude = 1.0f;          // FOV 震动幅度
        float noiseFrequency = 1.0f;        // 噪声频率
        float noiseSpeed = 1.0f;            // 噪声速度
        bool perlinNoise = true;            // 使用 Perlin 噪声
        bool enablePosition = true;         // 启用位置震动
        bool enableRotation = true;         // 启用旋转震动
        bool enableFOV = false;             // 启用 FOV 震动
    };

    void SetShakeParameters(const ShakeParameters& params);
    const ShakeParameters& GetShakeParameters() const { return params_; }

    // Preset system
    bool LoadPreset(const std::string& presetName);
    bool SavePreset(const std::string& presetName);

    bool IsShaking() const { return isShaking_; }

private:
    // Generate noise value
    float GenerateNoise(float time, int seed);

    // Parameters
    ShakeParameters params_;

    // State
    bool isShaking_;
    float shakeTime_;
    float shakeDuration_;
    float shakeIntensity_;
    Vec3 currentShake_;

    // Noise seed
    int positionSeed_;
    int rotationSeed_;

    // Presets
    std::unordered_map<std::string, ShakeParameters> presets_;

    bool initialized_;
};

// Screen Effect (屏幕效果)
// Design principles:
// - Sustainable Experimental: Easy to add new effects
// - Advanced: Layered compositing
// - Refactor Friendly: Effect chaining
class ScreenEffect {
public:
    ScreenEffect();
    ~ScreenEffect() = default;

    // Initialize
    bool Initialize();

    // Start effect
    void StartEffect(ScreenEffectType type, float duration);
    void StartCustomEffect(const std::string& presetName, float duration);

    // Update effect (called every frame)
    void Update(float deltaTime);

    // Get effect parameters
    struct EffectParameters {
        float intensity = 1.0f;
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float radius = 0.5f;
        float blurAmount = 1.0f;
        float grainStrength = 0.1f;
        bool fadeIn = true;
        bool fadeOut = true;
        float fadeDuration = 0.5f;
    };

    const EffectParameters& GetEffectParameters() const { return params_; }

    // Is effect active
    bool IsEffectActive() const { return isEffectActive_; }

    // Stop effect
    void StopEffect();

    // Cleanup
    void Shutdown();

    bool IsInitialized() const { return initialized_; }

private:
    // Parameters
    EffectParameters params_;

    // State
    bool isEffectActive_;
    float effectTime_;
    float effectDuration_;

    bool initialized_;
};

// Game Feel System (手感系统)
// Design principles:
// - Sustainable Experimental: Easy to experiment with feel
// - Advanced: Multi-modal feedback (visual, audio, haptic)
// - Refactor Friendly: Modular feedback system
class GameFeelSystem {
public:
    GameFeelSystem();
    ~GameFeelSystem();

    // Initialize game feel system
    bool Initialize();

    // Update (called every frame)
    void Update(float deltaTime);

    // Camera shake
    void ShakeCamera(ShakeType type, float duration, float intensity = 1.0f);

    // Screen effects
    void FlashScreen(float duration, float intensity = 1.0f);
    void FadeScreen(float duration, bool fadeIn);
    void ApplyRadialBlur(float duration, float amount = 1.0f);

    // Impact feedback
    void OnImpact(const Vec3& position, float force);

    // Damage feedback
    void OnDamageTaken(float damage, const Vec3& direction);

    // Death feedback
    void OnDeath();

    // Cleanup
    void Shutdown();

    bool IsInitialized() const { return initialized_; }

private:
    // Sub-systems
    CameraShake cameraShake_;
    ScreenEffect screenEffect_;

    bool initialized_;
};

} // namespace Next
