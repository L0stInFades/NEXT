#include "next/game/camera_graph.h"
#include "next/foundation/logger.h"
#include <cmath>

namespace Next {

// Character State definition
struct CharacterState {
    Vec3 position;
    Vec3 velocity;
    Vec3 forward;
    Vec3 right;
    Vec3 up;
    bool isGrounded;
    float rotation;  // Y-axis rotation
};

// Collision Data definition
struct CollisionData {
    bool hasCollision;
    Vec3 collisionPoint;
    Vec3 collisionNormal;
    float collisionDistance;
};

CameraGraph::CameraGraph()
    : currentMode_(CameraMode::ThirdPerson)
    , previousMode_(CameraMode::ThirdPerson)
    , rotationInput_(0.0f, 0.0f)
    , zoomInput_(0.0f)
    , inTransition_(false)
    , transitionProgress_(0.0f)
    , transitionDuration_(0.0f)
    , hasTarget_(false)
    , target_(0.0f, 0.0f, 0.0f)
    , initialized_(false) {
}

CameraGraph::~CameraGraph() {
    Shutdown();
}

bool CameraGraph::Initialize() {
    // Initialize default parameters
    parameters_ = CameraParameters();

    // Create default presets
    CameraParameters defaultParams;
    presets_["default"] = defaultParams;

    CameraParameters actionParams;
    actionParams.thirdPersonDistance = 6.0f;
    actionParams.thirdPersonHeight = 2.5f;
    actionParams.smoothingFactor = 0.15f;
    presets_["action"] = actionParams;

    CameraParameters explorationParams;
    explorationParams.thirdPersonDistance = 8.0f;
    explorationParams.thirdPersonHeight = 3.0f;
    explorationParams.smoothingFactor = 0.05f;
    presets_["exploration"] = explorationParams;

    initialized_ = true;
    NEXT_LOG_INFO("Camera graph initialized (CP6: Game Feel & Camera System)");
    return true;
}

void CameraGraph::Update(float deltaTime, const CharacterState& character, const CollisionData& collision) {
    if (!initialized_) {
        return;
    }

    // Handle transition if active
    if (inTransition_) {
        UpdateTransition(deltaTime);
    }

    // Store previous pose for smoothing
    previousPose_ = currentPose_;

    // Update based on current mode
    switch (currentMode_) {
        case CameraMode::FirstPerson:
            UpdateFirstPerson(deltaTime, character);
            break;
        case CameraMode::ThirdPerson:
            UpdateThirdPerson(deltaTime, character, collision);
            break;
        case CameraMode::Locked:
            UpdateLockedCamera(deltaTime);
            break;
        case CameraMode::Orbit:
            UpdateOrbitCamera(deltaTime);
            break;
        case CameraMode::Cinematic:
            UpdateCinematicCamera(deltaTime);
            break;
        default:
            break;
    }

    // Apply smoothing if enabled
    if (parameters_.enableSmoothing && !inTransition_) {
        ApplySmoothing(currentPose_.position, deltaTime);
    }

    // Reset input
    rotationInput_ = Vec2(0.0f, 0.0f);
    zoomInput_ = 0.0f;
}

void CameraGraph::UpdateFirstPerson(float deltaTime, const CharacterState& character) {
    // First person camera (character's eyes)
    currentPose_.position = character.position + Vec3(0.0f, parameters_.firstPersonHeight, 0.0f);

    // Rotation input controls camera direction
    parameters_.firstPersonSensitivity = 0.1f;
    float pitch = rotationInput_.y * parameters_.firstPersonSensitivity;
    float yaw = rotationInput_.x * parameters_.firstPersonSensitivity;

    // Calculate camera direction using Vec4 (homogeneous coordinates)
    float sinPitch = std::sin(pitch);
    float cosPitch = std::cos(pitch);
    float sinYaw = std::sin(yaw);
    float cosYaw = std::cos(yaw);

    // Forward vector
    Vec3 forward;
    forward.x = -sinYaw * cosPitch;
    forward.y = sinPitch;
    forward.z = -cosYaw * cosPitch;

    currentPose_.target = currentPose_.position + forward;
    currentPose_.up = Vec3(0.0f, 1.0f, 0.0f);
}

void CameraGraph::UpdateThirdPerson(float deltaTime, const CharacterState& character, const CollisionData& collision) {
    // Third person camera (behind character)
    float angle = parameters_.thirdPersonAngle;

    // Calculate camera position behind character
    float cosAngle = std::cos(angle);
    float sinAngle = std::sin(angle);

    Vec3 desiredPosition;
    desiredPosition.x = character.position.x - sinAngle * parameters_.thirdPersonDistance;
    desiredPosition.y = character.position.y + parameters_.thirdPersonHeight;
    desiredPosition.z = character.position.z - cosAngle * parameters_.thirdPersonDistance;

    // Handle collision
    if (parameters_.collisionEnabled) {
        HandleCollision(desiredPosition, collision);
    }

    currentPose_.position = desiredPosition;
    currentPose_.target = character.position + Vec3(0.0f, 1.5f, 0.0f);
    currentPose_.up = Vec3(0.0f, 1.0f, 0.0f);
}

void CameraGraph::UpdateLockedCamera(float deltaTime) {
    // Locked camera (looks at target)
    if (!hasTarget_) {
        return;
    }

    // Camera follows target from fixed position
    currentPose_.target = target_;
    // Position remains fixed (set externally)
}

void CameraGraph::UpdateOrbitCamera(float deltaTime) {
    // Orbit camera (rotates around target)
    if (!hasTarget_) {
        return;
    }

    // Update angle based on input
    parameters_.thirdPersonAngle += rotationInput_.x * parameters_.orbitSpeed * deltaTime;

    // Update distance based on zoom
    parameters_.orbitDistance += zoomInput_ * deltaTime * 2.0f;
    parameters_.orbitDistance = std::max(parameters_.orbitMinDistance,
                                          std::min(parameters_.orbitMaxDistance, parameters_.orbitDistance));

    // Calculate camera position
    float angle = parameters_.thirdPersonAngle;
    float cosAngle = std::cos(angle);
    float sinAngle = std::sin(angle);

    currentPose_.position.x = target_.x - sinAngle * parameters_.orbitDistance;
    currentPose_.position.y = target_.y + parameters_.orbitDistance * 0.5f;
    currentPose_.position.z = target_.z - cosAngle * parameters_.orbitDistance;

    currentPose_.target = target_;
    currentPose_.up = Vec3(0.0f, 1.0f, 0.0f);
}

void CameraGraph::UpdateCinematicCamera(float deltaTime) {
    // Path-based cinematic camera requires:
    // - Curve/spline interpolation system (Catmull-Rom, Bezier)
    // - Keyframe animation system
    // - Camera path editor tool
    // - Auto-focus and look-at target tracking
    // For now, cinematic camera behaves like default camera
}

void CameraGraph::HandleCollision(Vec3& desiredPosition, const CollisionData& collision) {
    if (!collision.hasCollision) {
        return;
    }

    // Simple collision avoidance: push camera away from collision
    Vec3 toCamera = desiredPosition - collision.collisionPoint;
    float distance = toCamera.Length();

    if (distance < parameters_.collisionRadius) {
        Vec3 pushDirection = toCamera.Normalize();
        desiredPosition = collision.collisionPoint + pushDirection * parameters_.collisionRadius;
    }
}

void CameraGraph::ApplySmoothing(const Vec3& targetPosition, float deltaTime) {
    // LERP smoothing (damped spring)
    float t = 1.0f - std::pow(1.0f - parameters_.smoothingFactor, deltaTime * 60.0f);
    currentPose_.position = previousPose_.position + (targetPosition - previousPose_.position) * t;
}

void CameraGraph::UpdateTransition(float deltaTime) {
    transitionProgress_ += deltaTime / transitionDuration_;

    if (transitionProgress_ >= 1.0f) {
        // Transition complete
        currentMode_ = previousMode_;
        inTransition_ = false;
        transitionProgress_ = 0.0f;
        NEXT_LOG_INFO("Camera transition complete: mode=%d", static_cast<int>(currentMode_));
        return;
    }

    // Smooth interpolation (ease-in-ease-out)
    float t = transitionProgress_;
    float smoothT = t * t * (3.0f - 2.0f * t);  // Smoothstep

    // Interpolate between start and end poses
    currentPose_.position = transitionStart_.position + (transitionEnd_.position - transitionStart_.position) * smoothT;
    currentPose_.target = transitionStart_.target + (transitionEnd_.target - transitionStart_.target) * smoothT;
    currentPose_.fov = transitionStart_.fov + (transitionEnd_.fov - transitionStart_.fov) * smoothT;
}

void CameraGraph::SetCameraMode(CameraMode mode) {
    if (mode == currentMode_) {
        return;
    }

    // Instant mode switch (no transition)
    previousMode_ = currentMode_;
    currentMode_ = mode;

    NEXT_LOG_INFO("Camera mode switched: %d -> %d", static_cast<int>(previousMode_), static_cast<int>(currentMode_));
}

void CameraGraph::TransitionToMode(CameraMode targetMode, float duration) {
    if (targetMode == currentMode_ || duration <= 0.0f) {
        SetCameraMode(targetMode);
        return;
    }

    // Setup transition
    previousMode_ = currentMode_;
    transitionStart_ = currentPose_;

    // Calculate target pose
    CameraMode tempMode = currentMode_;
    currentMode_ = targetMode;
    // Note: We'd need to update once to get the target pose
    // For now, we'll approximate it
    transitionEnd_ = currentPose_;
    currentMode_ = tempMode;

    inTransition_ = true;
    transitionProgress_ = 0.0f;
    transitionDuration_ = duration;

    NEXT_LOG_INFO("Camera transition started: %d -> %d (duration=%.2f)",
                  static_cast<int>(currentMode_), static_cast<int>(targetMode), duration);
}

void CameraGraph::SetRotationInput(float x, float y) {
    rotationInput_.x += x;
    rotationInput_.y += y;
}

void CameraGraph::SetZoomInput(float delta) {
    zoomInput_ += delta;
}

Mat4 CameraGraph::GetViewMatrix() const {
    return Mat4::LookAt(currentPose_.position, currentPose_.target, currentPose_.up);
}

Mat4 CameraGraph::GetProjectionMatrix(float aspect, float nearZ, float farZ) const {
    float fovRadians = currentPose_.fov * 3.14159f / 180.0f;
    return Mat4::Perspective(fovRadians, aspect, nearZ, farZ);
}

void CameraGraph::SetParameters(const CameraParameters& params) {
    parameters_ = params;
}

bool CameraGraph::LoadPreset(const std::string& presetName) {
    auto it = presets_.find(presetName);
    if (it != presets_.end()) {
        parameters_ = it->second;
        NEXT_LOG_INFO("Loaded camera preset: %s", presetName.c_str());
        return true;
    }

    NEXT_LOG_ERROR("Camera preset not found: %s", presetName.c_str());
    return false;
}

bool CameraGraph::SavePreset(const std::string& presetName) {
    presets_[presetName] = parameters_;
    NEXT_LOG_INFO("Saved camera preset: %s", presetName.c_str());
    return true;
}

std::vector<std::string> CameraGraph::GetAvailablePresets() const {
    std::vector<std::string> names;
    for (const auto& pair : presets_) {
        names.push_back(pair.first);
    }
    return names;
}

void CameraGraph::SetTarget(const Vec3& target) {
    hasTarget_ = true;
    target_ = target;
}

void CameraGraph::ClearTarget() {
    hasTarget_ = false;
}

void CameraGraph::Shutdown() {
    presets_.clear();
    initialized_ = false;

    NEXT_LOG_INFO("Camera graph shutdown complete");
}

} // namespace Next
