#include "next/game/input_curves.h"
#include "next/foundation/logger.h"
#include <cmath>
#include <algorithm>

namespace Next {

// ===== InputResponseCurve Implementation =====

InputResponseCurve::InputResponseCurve()
    : currentSpeed_(0.0f)
    , acceleration_(0.0f) {
}

void InputResponseCurve::Initialize() {
    params_ = CurveParameters();

    // Create default presets
    CurveParameters linear;
    linear.accelerationTime = 0.1f;
    linear.decelerationTime = 0.1f;
    linear.enableInertia = false;
    presets_["linear"] = linear;

    CurveParameters responsive;
    responsive.accelerationTime = 0.15f;
    responsive.decelerationTime = 0.2f;
    responsive.maxSpeed = 6.0f;
    presets_["responsive"] = responsive;

    CurveParameters heavy;
    heavy.accelerationTime = 0.4f;
    heavy.decelerationTime = 0.5f;
    heavy.maxSpeed = 3.0f;
    heavy.enableInertia = true;
    presets_["heavy"] = heavy;

    CurveParameters snappy;
    snappy.accelerationTime = 0.1f;
    snappy.decelerationTime = 0.15f;
    snappy.maxSpeed = 7.0f;
    snappy.deadZone = 0.15f;
    presets_["snappy"] = snappy;
}

float InputResponseCurve::ProcessInput(float rawInput, float deltaTime) {
    // Apply dead zone
    if (std::abs(rawInput) < params_.deadZone) {
        rawInput = 0.0f;
    } else {
        // Remap outside dead zone
        float sign = (rawInput > 0.0f) ? 1.0f : -1.0f;
        rawInput = sign * (std::abs(rawInput) - params_.deadZone) / (1.0f - params_.deadZone);
    }

    // Apply saturation
    rawInput = std::clamp(rawInput, -params_.saturationThreshold, params_.saturationThreshold);

    // Evaluate custom curve or use physics
    if (params_.useCustomCurve) {
        return EvaluateCurve(rawInput);
    }

    if (params_.enableInertia && params_.enableAcceleration) {
        return ApplyPhysics(rawInput, deltaTime);
    }

    // Direct pass-through
    return rawInput * params_.maxSpeed;
}

float InputResponseCurve::EvaluateCurve(float input) {
    // Custom curve support requires Vec2 type for control points
    // For now, using linear response (identity function)
    // Future implementation will support:
    // - Bezier curves
    // - Piecewise linear interpolation
    // - Exponential response curves
    return input;
}

float InputResponseCurve::ApplyPhysics(float rawInput, float deltaTime) {
    // Calculate target speed
    float targetSpeed = rawInput * params_.maxSpeed;

    // Calculate acceleration
    if (std::abs(targetSpeed) > std::abs(currentSpeed_)) {
        // Accelerating
        float accelRate = params_.maxSpeed / params_.accelerationTime;
        acceleration_ = accelRate;
    } else {
        // Decelerating
        float decelRate = params_.maxSpeed / params_.decelerationTime;
        acceleration_ = decelRate;
    }

    // Apply acceleration
    float speedDelta = acceleration_ * deltaTime;
    if (std::abs(targetSpeed - currentSpeed_) < speedDelta) {
        currentSpeed_ = targetSpeed;
    } else {
        if (targetSpeed > currentSpeed_) {
            currentSpeed_ += speedDelta;
        } else {
            currentSpeed_ -= speedDelta;
        }
    }

    return currentSpeed_;
}

void InputResponseCurve::Reset() {
    currentSpeed_ = 0.0f;
    acceleration_ = 0.0f;
}

void InputResponseCurve::SetParameters(const CurveParameters& params) {
    params_ = params;
}

bool InputResponseCurve::LoadPreset(const std::string& presetName) {
    auto it = presets_.find(presetName);
    if (it != presets_.end()) {
        params_ = it->second;
        NEXT_LOG_INFO("Loaded input curve preset: %s", presetName.c_str());
        return true;
    }

    NEXT_LOG_ERROR("Input curve preset not found: %s", presetName.c_str());
    return false;
}

bool InputResponseCurve::SavePreset(const std::string& presetName) {
    presets_[presetName] = params_;
    NEXT_LOG_INFO("Saved input curve preset: %s", presetName.c_str());
    return true;
}

// ===== CharacterController Implementation =====

CharacterController::CharacterController()
    : rawInput_(0.0f, 0.0f, 0.0f)
    , jumpPressed_(false)
    , sprintPressed_(false)
    , collisionEnabled_(true)
    , initialized_(false) {
}

bool CharacterController::Initialize() {
    // Initialize state
    state_.position = Vec3(0.0f, 0.0f, 0.0f);
    state_.velocity = Vec3(0.0f, 0.0f, 0.0f);
    state_.forward = Vec3(0.0f, 0.0f, -1.0f);
    state_.right = Vec3(1.0f, 0.0f, 0.0f);
    state_.up = Vec3(0.0f, 1.0f, 0.0f);
    state_.isGrounded = true;
    state_.rotation = 0.0f;

    // Initialize movement parameters
    movementParams_ = MovementParameters();

    // Initialize input curves
    forwardCurve_.Initialize();
    strafeCurve_.Initialize();

    initialized_ = true;
    NEXT_LOG_INFO("Character controller initialized (CP6: Game Feel & Input)");
    return true;
}

void CharacterController::Update(float deltaTime, const Vec3& input, bool jumpPressed, bool sprintPressed) {
    if (!initialized_) {
        return;
    }

    rawInput_ = input;
    jumpPressed_ = jumpPressed;
    sprintPressed_ = sprintPressed;

    // Process input through curves
    float forwardSpeed = forwardCurve_.ProcessInput(input.z, deltaTime);
    float strafeSpeed = strafeCurve_.ProcessInput(input.x, deltaTime);

    // Calculate movement direction
    Vec3 movementDir = state_.forward * forwardSpeed + state_.right * strafeSpeed;

    // Apply sprint multiplier
    if (sprintPressed_ && state_.isGrounded) {
        movementDir = movementDir * (movementParams_.runSpeed / movementParams_.walkSpeed);
    }

    // Update velocity
    state_.velocity.x = movementDir.x;
    state_.velocity.z = movementDir.z;

    // Jump
    if (jumpPressed_ && state_.isGrounded) {
        state_.velocity.y = movementParams_.jumpForce;
        state_.isGrounded = false;
    }

    // Apply physics
    UpdatePhysics(deltaTime);

    // Update collision
    if (collisionEnabled_) {
        UpdateCollision(state_.position);
    }

    // Update forward/right vectors
    float sinRot = std::sin(state_.rotation);
    float cosRot = std::cos(state_.rotation);
    state_.forward = Vec3(-sinRot, 0.0f, -cosRot).Normalize();
    state_.right = Vec3(cosRot, 0.0f, -sinRot).Normalize();
}

void CharacterController::UpdatePhysics(float deltaTime) {
    // Apply gravity
    if (!state_.isGrounded) {
        state_.velocity.y -= movementParams_.gravity * deltaTime;
    }

    // Update position
    state_.position = state_.position + state_.velocity * deltaTime;

    // Ground check
    if (state_.position.y <= 0.0f) {
        state_.position.y = 0.0f;
        state_.velocity.y = 0.0f;
        state_.isGrounded = true;
    }
}

void CharacterController::UpdateCollision(const Vec3& desiredPosition) {
    // Full collision detection requires:
    // - Physics engine integration (bullet/physx)
    // - Collision mesh system
    // - Spatial partitioning for efficiency
    // For now, simple ground collision is handled in UpdatePhysics
}

void CharacterController::SetMovementParameters(const MovementParameters& params) {
    movementParams_ = params;
}

void CharacterController::Shutdown() {
    initialized_ = false;
    NEXT_LOG_INFO("Character controller shutdown complete");
}

} // namespace Next
