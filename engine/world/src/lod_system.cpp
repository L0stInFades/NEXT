#include "next/streaming/lod_system.h"
#include "next/foundation/logger.h"

namespace Next {
namespace Streaming {

// ===== LOD System Implementation =====

LODSystem::LODSystem()
    : qualityScale_(1.0f)
    , maxFrameTimeSamples_(60)
    , initialized_(false)
{
}

LODSystem::~LODSystem() {
    Shutdown();
}

bool LODSystem::Initialize(const LODSystemConfig& config) {
    if (initialized_) {
        NEXT_LOG_WARNING("LODSystem already initialized");
        return true;
    }

    config_ = config;
    qualityScale_ = 1.0f;

    recentFrameTimes_.reserve(maxFrameTimeSamples_);


    initialized_ = true;
    return true;
}

void LODSystem::Update(float deltaTime, const Vec3& cameraPosition, const Mat4& viewProjectionMatrix) {
    if (!initialized_) {
        return;
    }

    // Update statistics
    stats_.highDetailObjects = 0;
    stats_.mediumDetailObjects = 0;
    stats_.lowDetailObjects = 0;
    stats_.hlodObjects = 0;
    stats_.impostorObjects = 0;
    stats_.averageLODLevel = 0.0f;
    stats_.currentQualityScale = qualityScale_;
}

void LODSystem::RegisterLODLevels(uint64_t objectId, const std::vector<LODLevel>& levels) {
    objectLODs_[objectId] = levels;
}

void LODSystem::UnregisterLODLevels(uint64_t objectId) {
    objectLODs_.erase(objectId);
}

uint64_t LODSystem::CreateHLODCluster(const CellCoord& cell, const std::vector<uint64_t>& objectIds) {
    // TODO: Implement HLOD cluster creation
    NEXT_LOG_WARNING("CreateHLODCluster not fully implemented");
    return 0;
}

void LODSystem::DestroyHLODCluster(uint64_t clusterId) {
    hlodClusters_.erase(clusterId);
}

LODCluster* LODSystem::GetHLODCluster(uint64_t clusterId) {
    auto it = hlodClusters_.find(clusterId);
    if (it != hlodClusters_.end()) {
        return &it->second;
    }
    return nullptr;
}

const LODCluster* LODSystem::GetHLODCluster(uint64_t clusterId) const {
    auto it = hlodClusters_.find(clusterId);
    if (it != hlodClusters_.end()) {
        return &it->second;
    }
    return nullptr;
}

void LODSystem::CreateImpostor(uint64_t objectId, const CellCoord& cell, const Vec3& boundsMin, const Vec3& boundsMax) {
    ImpostorData impostor;
    impostor.objectId = objectId;
    impostor.cell = cell;
    impostor.boundsMin = boundsMin;
    impostor.boundsMax = boundsMax;

    // Generate impostor textures
    // TODO: Implement impostor generation
    NEXT_LOG_WARNING("Impostor generation not fully implemented");

    impostors_[objectId] = impostor;
}

void LODSystem::DestroyImpostor(uint64_t objectId) {
    impostors_.erase(objectId);
}

ImpostorData* LODSystem::GetImpostor(uint64_t objectId) {
    auto it = impostors_.find(objectId);
    if (it != impostors_.end()) {
        return &it->second;
    }
    return nullptr;
}

const ImpostorData* LODSystem::GetImpostor(uint64_t objectId) const {
    auto it = impostors_.find(objectId);
    if (it != impostors_.end()) {
        return &it->second;
    }
    return nullptr;
}

uint32_t LODSystem::CalculateLODLevel(uint64_t objectId, const Vec3& objectPosition, const Vec3& cameraPosition) const {
    auto it = objectLODs_.find(objectId);
    if (it == objectLODs_.end()) {
        return 0;
    }

    const std::vector<LODLevel>& levels = it->second;
    if (levels.empty()) {
        return 0;
    }

    float distance = CalculateDistance(objectPosition, cameraPosition);
    float screenSize = 1000.0f / (distance + 1.0f);  // Approximate screen size

    return SelectLODLevel(levels, distance, screenSize);
}

float LODSystem::CalculateLODFactor(const Vec3& objectPosition, const Vec3& cameraPosition) const {
    float distance = CalculateDistance(objectPosition, cameraPosition);
    float lodDistance = config_.lodDistanceMultiplier * config_.lodTransitionDistance;

    return 1.0f - std::min(distance / lodDistance, 1.0f);
}

float LODSystem::CalculateScreenSize(const Vec3& objectPosition, float objectRadius, const Mat4& viewProjectionMatrix) const {
    // TODO: Implement accurate screen size calculation
    float distance = objectPosition.Length();
    return objectRadius / (distance + 1.0f);
}

bool LODSystem::ShouldUseHLOD(const CellCoord& cell, float distance) const {
    if (!config_.enableHLOD) {
        return false;
    }
    return distance > config_.hlodDistance;
}

uint64_t LODSystem::GetHLODMesh(const CellCoord& cell) const {
    auto it = cellToHLOD_.find(cell);
    if (it != cellToHLOD_.end()) {
        return it->second;
    }
    return 0;
}

bool LODSystem::ShouldUseImpostor(float screenSize) const {
    if (!config_.enableImpostors) {
        return false;
    }
    return screenSize < config_.impostorScreenSizeThreshold;
}

uint64_t LODSystem::GetImpostorTexture(uint64_t objectId) const {
    auto it = impostors_.find(objectId);
    if (it != impostors_.end() && it->second.billboardTextures.size() > 0) {
        return it->second.billboardTextures[0];
    }
    return 0;
}

void LODSystem::SetQualityScale(float scale) {
    // Manual clamp instead of std::clamp (C++17)
    if (scale < config_.minQualityScale) {
        qualityScale_ = config_.minQualityScale;
    } else if (scale > config_.maxQualityScale) {
        qualityScale_ = config_.maxQualityScale;
    } else {
        qualityScale_ = scale;
    }
}


void LODSystem::UpdateAutoLOD(float frameTime) {
    if (!config_.enableAutoLOD) {
        return;
    }

    // Track frame times
    recentFrameTimes_.push_back(frameTime);
    if (recentFrameTimes_.size() > maxFrameTimeSamples_) {
        recentFrameTimes_.erase(recentFrameTimes_.begin());
    }

    // Adjust quality based on performance
    if (ShouldReduceQuality()) {
        qualityScale_ -= 0.01f;
        qualityScale_ = std::max(qualityScale_, config_.minQualityScale);
    } else if (ShouldIncreaseQuality()) {
        qualityScale_ += 0.01f;
        qualityScale_ = std::min(qualityScale_, config_.maxQualityScale);
    }
}

LODSystem::Statistics LODSystem::GetStatistics() const {
    return stats_;
}

void LODSystem::Shutdown() {
    if (!initialized_) {
        return;
    }

    objectLODs_.clear();
    hlodClusters_.clear();
    impostors_.clear();
    cellToHLOD_.clear();
    recentFrameTimes_.clear();

    initialized_ = false;
    // NEXT_LOG_INFO("LODSystem shutdown complete");
}

// ===== Private Methods =====

uint32_t LODSystem::SelectLODLevel(const std::vector<LODLevel>& levels, float distance, float screenSize) const {
    if (levels.empty()) {
        return 0;
    }

    float adjustedDistance = distance / qualityScale_;

    for (uint32_t i = 0; i < levels.size(); ++i) {
        if (adjustedDistance <= levels[i].distance) {
            return i;
        }
    }

    return static_cast<uint32_t>(levels.size()) - 1;
}

float LODSystem::CalculateDistance(const Vec3& objectPosition, const Vec3& cameraPosition) const {
    Vec3 toObject = objectPosition - cameraPosition;
    return toObject.Length();
}

void LODSystem::BuildHLODCluster(LODCluster* cluster, const std::vector<uint64_t>& objectIds) {
    // TODO: Implement HLOD cluster building
}

void LODSystem::GenerateHLODMesh(LODCluster* cluster) {
    // TODO: Implement HLOD mesh generation
}

void LODSystem::GenerateImpostorTextures(ImpostorData* impostor, uint64_t objectId) {
    // TODO: Implement impostor texture generation
}

void LODSystem::RenderImpostorView(uint64_t objectId, const Vec3& cameraPosition, const Vec3& cameraDirection, void* textureOutput) {
    // TODO: Implement impostor view rendering
}

float LODSystem::CalculateDitherFactor(const Vec3& objectPosition, const Vec3& cameraPosition, uint32_t currentLOD, uint32_t targetLOD) const {
    // TODO: Implement dither factor calculation
    return 0.0f;
}

void LODSystem::UpdatePerformanceMetrics(float frameTime) {
    // Already handled in UpdateAutoLOD
}

bool LODSystem::ShouldReduceQuality() const {
    if (recentFrameTimes_.size() < maxFrameTimeSamples_) {
        return false;
    }

    float averageFrameTime = 0.0f;
    for (float time : recentFrameTimes_) {
        averageFrameTime += time;
    }
    averageFrameTime /= recentFrameTimes_.size();

    return averageFrameTime > config_.targetFrameTime * 1.2f;
}

bool LODSystem::ShouldIncreaseQuality() const {
    if (recentFrameTimes_.size() < maxFrameTimeSamples_) {
        return false;
    }

    float averageFrameTime = 0.0f;
    for (float time : recentFrameTimes_) {
        averageFrameTime += time;
    }
    averageFrameTime /= recentFrameTimes_.size();

    return averageFrameTime < config_.targetFrameTime * 0.8f;
}

} // namespace Streaming
} // namespace Next
