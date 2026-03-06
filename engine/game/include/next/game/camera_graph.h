#pragma once

#include "next/renderer/math/math.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

namespace Next {

// Forward declarations (to avoid circular dependency)
struct CharacterState;
struct CollisionData;

// Camera Modes (支持所有相机类型)
enum class CameraMode {
    FirstPerson,        // 第一人称
    ThirdPerson,        // 第三人称
    Locked,             // 锁定镜头
    Orbit,              // 轨道镜头
    Cinematic,          // 电影镜头
    Debug               // 调试视角
};

// Camera Pose (相机姿态)
struct CameraPose {
    Vec3 position;           // 位置
    Vec3 target;             // 目标点
    Vec3 up;                 // 上方向
    float fov;               // 视野角

    CameraPose()
        : position(0.0f, 0.0f, 0.0f)
        , target(0.0f, 0.0f, -1.0f)
        , up(0.0f, 1.0f, 0.0f)
        , fov(60.0f) {}
};

// Camera Parameters (可配置参数)
struct CameraParameters {
    // Third Person Parameters
    float thirdPersonDistance = 5.0f;
    float thirdPersonHeight = 2.0f;
    float thirdPersonAngle = 0.0f;

    // First Person Parameters
    float firstPersonHeight = 1.7f;
    float firstPersonSensitivity = 0.1f;

    // Orbit Parameters
    float orbitDistance = 10.0f;
    float orbitMinDistance = 2.0f;
    float orbitMaxDistance = 20.0f;
    float orbitSpeed = 1.0f;

    // Collision
    float collisionRadius = 0.5f;
    bool collisionEnabled = true;

    // Smoothing
    float smoothingFactor = 0.1f;
    bool enableSmoothing = true;
};

// Camera Graph System (统一的相机图系统)
// Design principles:
// - Sustainable Experimental: Easy to add new camera types
// - Advanced: Seamless transitions between camera modes
// - Refactor Friendly: Graph-based architecture
class CameraGraph {
public:
    CameraGraph();
    ~CameraGraph();

    // Initialize camera system
    bool Initialize();

    // Update camera (called every frame)
    void Update(float deltaTime, const CharacterState& character, const CollisionData& collision);

    // Camera mode control
    void SetCameraMode(CameraMode mode);
    CameraMode GetCameraMode() const { return currentMode_; }
    void TransitionToMode(CameraMode targetMode, float duration);

    // Input handling
    void SetRotationInput(float x, float y);
    void SetZoomInput(float delta);

    // Output
    CameraPose GetCameraPose() const { return currentPose_; }
    Mat4 GetViewMatrix() const;
    Mat4 GetProjectionMatrix(float aspect, float nearZ, float farZ) const;

    // Parameters
    void SetParameters(const CameraParameters& params);
    const CameraParameters& GetParameters() const { return parameters_; }

    // Preset system (designer-friendly)
    bool LoadPreset(const std::string& presetName);
    bool SavePreset(const std::string& presetName);
    std::vector<std::string> GetAvailablePresets() const;

    // Target control (for locked/cinematic cameras)
    void SetTarget(const Vec3& target);
    void ClearTarget();

    // Cleanup
    void Shutdown();

    bool IsInitialized() const { return initialized_; }

private:
    // Camera mode implementations
    void UpdateFirstPerson(float deltaTime, const CharacterState& character);
    void UpdateThirdPerson(float deltaTime, const CharacterState& character, const CollisionData& collision);
    void UpdateLockedCamera(float deltaTime);
    void UpdateOrbitCamera(float deltaTime);
    void UpdateCinematicCamera(float deltaTime);

    // Collision handling
    void HandleCollision(Vec3& desiredPosition, const CollisionData& collision);

    // Smoothing
    void ApplySmoothing(const Vec3& targetPosition, float deltaTime);

    // Transition system
    void UpdateTransition(float deltaTime);

    // Current state
    CameraMode currentMode_;
    CameraMode previousMode_;
    CameraPose currentPose_;
    CameraPose previousPose_;

    // Parameters
    CameraParameters parameters_;

    // Input
    Vec2 rotationInput_;
    float zoomInput_;

    // Transition
    bool inTransition_;
    float transitionProgress_;
    float transitionDuration_;
    CameraPose transitionStart_;
    CameraPose transitionEnd_;

    // Target
    bool hasTarget_;
    Vec3 target_;

    // Presets
    std::unordered_map<std::string, CameraParameters> presets_;

    bool initialized_;
};

} // namespace Next
