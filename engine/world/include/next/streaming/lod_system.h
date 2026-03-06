#pragma once

#include "next/renderer/math/math.h"
#include "next/streaming/world_partition.h"
#include <vector>
#include <unordered_map>
#include <memory>

namespace Next {
namespace Streaming {

// ===== LOD Level =====

struct LODLevel {
    uint32_t level;              // 0 = highest detail
    float distance;              // Distance at which this LOD is used
    float screenSize;            // Minimum screen size (percentage)
    uint64_t triangleCount;
    uint64_t vertexCount;

    // Mesh data (can be simplified or different mesh)
    uint64_t meshHandle;

    // Material overrides (simplified materials for lower LOD)
    std::vector<uint64_t> materialHandles;

    // HLOD reference (if this is HLOD)
    bool isHLOD;
    uint64_t hlodClusterId;      // HLOD cluster this belongs to
};

// ===== LOD Cluster (for HLOD) =====

struct LODCluster {
    uint64_t clusterId;
    CellCoord cell;              // Cell this cluster belongs to
    Vec3 center;
    float radius;

    // HLOD mesh (merged geometry)
    uint64_t hlodMeshHandle;

    // Original objects in this cluster
    std::vector<uint64_t> originalObjectIds;

    // LOD levels for HLOD
    std::vector<LODLevel> hlodLevels;

    // Impostor data (billboard)
    bool hasImpostor;
    uint64_t impostorTextureHandle;

    LODCluster()
        : clusterId(0)
        , center(0.0f, 0.0f, 0.0f)
        , radius(0.0f)
        , hlodMeshHandle(0)
        , hasImpostor(false)
        , impostorTextureHandle(0)
    {}
};

// ===== Impostor Data =====

struct ImpostorData {
    uint64_t objectId;
    CellCoord cell;

    // Billboard textures (multiple views)
    std::vector<uint64_t> billboardTextures;  // 8 views (45° increments)

    // Extents
    Vec3 boundsMin;
    Vec3 boundsMax;

    // Screen size threshold
    float maxScreenSize;  // Switch to impostor below this size

    ImpostorData()
        : objectId(0)
        , boundsMin(0.0f, 0.0f, 0.0f)
        , boundsMax(0.0f, 0.0f, 0.0f)
        , maxScreenSize(0.01f)  // 1% of screen height
    {}
};

// ===== LOD System Configuration =====

struct LODSystemConfig {
    // LOD settings
    uint32_t maxLODLevels = 5;
    float lodTransitionDistance = 64.0f;
    float lodTransitionSpeed = 2.0f;  // Speed of LOD blending

    // Distance multipliers
    float lodDistanceMultiplier = 1.0f;
    float qualityScale = 1.0f;         // Global quality setting (0.5 - 2.0)

    // HLOD settings
    bool enableHLOD = true;
    float hlodDistance = 256.0f;       // Use HLOD beyond this distance
    uint32_t hlodClusterSize = 8;      // Cells per HLOD cluster

    // Impostor settings
    bool enableImpostors = true;
    float impostorDistance = 512.0f;   // Use impostors beyond this distance
    float impostorScreenSizeThreshold = 0.02f;  // 2% of screen height

    // Performance settings
    bool enableAutoLOD = true;         // Automatically adjust LOD based on performance
    float targetFrameTime = 0.016f;    // 60 FPS target
    float minQualityScale = 0.5f;
    float maxQualityScale = 2.0f;

    // Dithering
    bool enableDithering = true;       // Dither LOD transitions
    float ditherDistance = 4.0f;       // Distance over which to dither

    LODSystemConfig() = default;
};

// ===== LOD System =====

class LODSystem {
public:
    LODSystem();
    ~LODSystem();

    // Initialize with configuration
    bool Initialize(const LODSystemConfig& config);

    // Update (called every frame)
    void Update(float deltaTime, const Vec3& cameraPosition, const Mat4& viewProjectionMatrix);

    // LOD management
    void RegisterLODLevels(uint64_t objectId, const std::vector<LODLevel>& levels);
    void UnregisterLODLevels(uint64_t objectId);

    // HLOD management
    uint64_t CreateHLODCluster(const CellCoord& cell, const std::vector<uint64_t>& objectIds);
    void DestroyHLODCluster(uint64_t clusterId);
    LODCluster* GetHLODCluster(uint64_t clusterId);
    const LODCluster* GetHLODCluster(uint64_t clusterId) const;

    // Impostor management
    void CreateImpostor(uint64_t objectId, const CellCoord& cell, const Vec3& boundsMin, const Vec3& boundsMax);
    void DestroyImpostor(uint64_t objectId);
    ImpostorData* GetImpostor(uint64_t objectId);
    const ImpostorData* GetImpostor(uint64_t objectId) const;

    // LOD calculation
    uint32_t CalculateLODLevel(uint64_t objectId, const Vec3& objectPosition, const Vec3& cameraPosition) const;
    float CalculateLODFactor(const Vec3& objectPosition, const Vec3& cameraPosition) const;  // 0.0 (far) - 1.0 (near)

    // Screen size calculation
    float CalculateScreenSize(const Vec3& objectPosition, float objectRadius, const Mat4& viewProjectionMatrix) const;

    // HLOD selection
    bool ShouldUseHLOD(const CellCoord& cell, float distance) const;
    uint64_t GetHLODMesh(const CellCoord& cell) const;

    // Impostor selection
    bool ShouldUseImpostor(float screenSize) const;
    uint64_t GetImpostorTexture(uint64_t objectId) const;

    // Quality control
    void SetQualityScale(float scale);
    float GetQualityScale() const { return qualityScale_; }

    // Auto-LOD
    void EnableAutoLOD(bool enable) { config_.enableAutoLOD = enable; }
    void UpdateAutoLOD(float frameTime);

    // Configuration
    void SetConfig(const LODSystemConfig& config) { config_ = config; }
    const LODSystemConfig& GetConfig() const { return config_; }

    // Statistics
    struct Statistics {
        uint32_t highDetailObjects;     // LOD 0
        uint32_t mediumDetailObjects;   // LOD 1-2
        uint32_t lowDetailObjects;      // LOD 3+
        uint32_t hlodObjects;
        uint32_t impostorObjects;
        float averageLODLevel;
        float currentQualityScale;
    };

    Statistics GetStatistics() const;

    // Cleanup
    void Shutdown();

    // State check
    bool IsInitialized() const { return initialized_; }

private:
    // LOD level calculation
    uint32_t SelectLODLevel(const std::vector<LODLevel>& levels, float distance, float screenSize) const;
    float CalculateDistance(const Vec3& objectPosition, const Vec3& cameraPosition) const;

    // HLOD management
    void BuildHLODCluster(LODCluster* cluster, const std::vector<uint64_t>& objectIds);
    void GenerateHLODMesh(LODCluster* cluster);

    // Impostor generation
    void GenerateImpostorTextures(ImpostorData* impostor, uint64_t objectId);
    void RenderImpostorView(uint64_t objectId, const Vec3& cameraPosition, const Vec3& cameraDirection, void* textureOutput);

    // Dithering
    float CalculateDitherFactor(const Vec3& objectPosition, const Vec3& cameraPosition, uint32_t currentLOD, uint32_t targetLOD) const;

    // Performance monitoring
    void UpdatePerformanceMetrics(float frameTime);
    bool ShouldReduceQuality() const;
    bool ShouldIncreaseQuality() const;

    // Configuration
    LODSystemConfig config_;

    // LOD data
    std::unordered_map<uint64_t, std::vector<LODLevel>> objectLODs_;
    std::unordered_map<uint64_t, LODCluster> hlodClusters_;
    std::unordered_map<uint64_t, ImpostorData> impostors_;

    // Cell to HLOD mapping
    std::unordered_map<CellCoord, uint64_t, CellCoord::Hash> cellToHLOD_;

    // Quality control
    float qualityScale_;
    std::vector<float> recentFrameTimes_;
    size_t maxFrameTimeSamples_;

    // Statistics
    Statistics stats_;

    // State
    bool initialized_;
};

// ===== LOD Transition Manager =====

class LODTransitionManager {
public:
    LODTransitionManager();
    ~LODTransitionManager();

    // Initialize
    bool Initialize(uint32_t maxConcurrentTransitions = 64);

    // Update
    void Update(float deltaTime);

    // Transition management
    void StartTransition(uint64_t objectId, uint32_t fromLOD, uint32_t toLOD, float duration);
    void CancelTransition(uint64_t objectId);
    bool IsTransitioning(uint64_t objectId) const;
    float GetTransitionProgress(uint64_t objectId) const;  // 0.0 - 1.0

    // Current LOD (during transition)
    uint32_t GetCurrentLOD(uint64_t objectId, uint32_t targetLOD) const;

    // Cleanup
    void Shutdown();

private:
    struct Transition {
        uint64_t objectId;
        uint32_t fromLOD;
        uint32_t toLOD;
        float progress;
        float duration;
        float elapsedTime;
    };

    std::unordered_map<uint64_t, Transition> activeTransitions_;
    uint32_t maxConcurrentTransitions_;
    bool initialized_;
};

} // namespace Streaming
} // namespace Next
