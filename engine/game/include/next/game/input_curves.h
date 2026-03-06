#pragma once

#include "next/renderer/math/math.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace Next {

// Input Response Curve (输入响应曲线)
// Design principles:
// - Sustainable Experimental: Easy to tune input feel
// - Advanced: Physics-based input with acceleration/deceleration
// - Refactor Friendly: Preset system for designers
class InputResponseCurve {
public:
    InputResponseCurve();
    ~InputResponseCurve() = default;

    // Initialize with default parameters
    void Initialize();

    // Process input (apply response curve)
    float ProcessInput(float rawInput, float deltaTime);

    // Reset state (when character stops)
    void Reset();

    // Curve parameters (designer-friendly)
    struct CurveParameters {
        float accelerationTime = 0.2f;      // 加速时间（秒）
        float decelerationTime = 0.3f;      // 减速时间（秒）
        float maxSpeed = 5.0f;              // 最大速度
        float turnSpeed = 2.0f;             // 转向速度
        bool enableInertia = true;          // 启用惯性
        bool enableAcceleration = true;     // 启用加速
        float deadZone = 0.1f;              // 死区（0-1）
        float saturationThreshold = 0.9f;   // 饱和阈值（0-1）

        // Custom curve support requires Vec2 type for control point definition
        // Future enhancement: Add custom curve support when Vec2 is available
        bool useCustomCurve = false;
    };

    void SetParameters(const CurveParameters& params);
    const CurveParameters& GetParameters() const { return params_; }

    // Get current state
    float GetCurrentSpeed() const { return currentSpeed_; }
    float GetAcceleration() const { return acceleration_; }

    // Preset system
    bool LoadPreset(const std::string& presetName);
    bool SavePreset(const std::string& presetName);

private:
    // Calculate response curve value
    float EvaluateCurve(float input);

    // Physics-based acceleration/deceleration
    float ApplyPhysics(float rawInput, float deltaTime);

    // Parameters
    CurveParameters params_;

    // State
    float currentSpeed_;
    float acceleration_;

    // Presets
    std::unordered_map<std::string, CurveParameters> presets_;
};

// Character Controller (角色控制器)
// Design principles:
// - Sustainable Experimental: Easy to adjust movement feel
// - Advanced: Physics-based movement with collision
// - Refactor Friendly: Clear separation of input and physics
class CharacterController {
public:
    CharacterController();
    ~CharacterController() = default;

    // Initialize
    bool Initialize();

    // Update character (called every frame)
    void Update(float deltaTime, const Vec3& input, bool jumpPressed, bool sprintPressed);

    // Get character state
    struct State {
        Vec3 position;
        Vec3 velocity;
        Vec3 forward;
        Vec3 right;
        Vec3 up;
        bool isGrounded;
        float rotation;
    };

    const State& GetState() const { return state_; }

    // Movement parameters
    struct MovementParameters {
        float walkSpeed = 2.0f;             // 行走速度
        float runSpeed = 5.0f;              // 跑步速度
        float jumpForce = 5.0f;             // 跳跃力度
        float gravity = 9.8f;               // 重力
        float friction = 5.0f;              // 摩擦力
        float airControl = 0.1f;            // 空中控制
        float mass = 70.0f;                 // 质量（kg）
        float stepHeight = 0.5f;            // 台阶高度
        float slopeLimit = 45.0f;           // 坡度限制（度）
    };

    void SetMovementParameters(const MovementParameters& params);
    const MovementParameters& GetMovementParameters() const { return movementParams_; }

    // Input curves
    InputResponseCurve& GetForwardCurve() { return forwardCurve_; }
    InputResponseCurve& GetStrafeCurve() { return strafeCurve_; }

    // Collision
    void SetCollisionEnabled(bool enabled) { collisionEnabled_ = enabled; }
    bool IsCollisionEnabled() const { return collisionEnabled_; }

    // Cleanup
    void Shutdown();

    bool IsInitialized() const { return initialized_; }

private:
    // Physics update
    void UpdatePhysics(float deltaTime);
    void UpdateCollision(const Vec3& desiredPosition);

    // State
    State state_;

    // Parameters
    MovementParameters movementParams_;

    // Input curves
    InputResponseCurve forwardCurve_;
    InputResponseCurve strafeCurve_;

    // Input
    Vec3 rawInput_;
    bool jumpPressed_;
    bool sprintPressed_;

    // Collision
    bool collisionEnabled_;

    bool initialized_;
};

} // namespace Next
