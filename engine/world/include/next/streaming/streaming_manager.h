#pragma once

#include "next/streaming/world_partition.h"
#include "next/streaming/async_io.h"
#include "next/streaming/interest_manager.h"
#include "next/streaming/lod_system.h"
#include "next/streaming/eviction_policy.h"
#include "next/jobsystem/job.h"
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <string>
#include <mutex>

namespace Next {
namespace Streaming {

// ===== Streaming Handle =====

struct StreamingHandle {
    uint64_t id;

    operator bool() const { return id != 0; }
    bool operator==(const StreamingHandle& other) const { return id == other.id; }
};

// ===== Asset Bundle =====

struct AssetBundle {
    uint64_t bundleId;
    std::wstring bundlePath;
    std::vector<CellCoord> cells;  // Cells contained in this bundle
    uint64_t totalSize;
    uint64_t compressedSize;

    // Dependencies
    std::vector<uint64_t> dependencyBundles;
};

// ===== Streaming Statistics =====

struct StreamingStatistics {
    // Cell counts
    uint32_t loadedCells;
    uint32_t loadingCells;
    uint32_t queuedCells;
    uint32_t unloadedCells;

    // Memory usage
    uint64_t memoryUsed;
    uint64_t memoryBudget;
    float memoryUtilization;  // 0.0 - 1.0

    // Performance
    float averageLoadTime;
    float averageUnloadTime;
    uint32_t cellsLoadedPerSecond;
    uint32_t cellsUnloadedPerSecond;

    // Quality
    uint32_t visibleCells;
    uint32_t highDetailCells;
    uint32_t lowDetailCells;
    uint32_t hlodCells;

    // Errors
    uint32_t failedLoads;
    uint32_t timeoutErrors;
};

// ===== Streaming Manager Configuration =====

struct StreamingManagerConfig {
    // Memory budget
    size_t memoryBudgetMB = 2048;        // Total memory budget for streaming
    size_t vertexDataBudgetMB = 512;     // Budget for vertex/index data
    size_t textureBudgetMB = 1024;       // Budget for textures

    // Performance targets
    float targetLoadTime = 0.016f;       // Target load time per cell (60fps)
    float maxStallTime = 0.033f;         // Maximum time to block on streaming (30fps)

    // Streaming parameters
    float loadRadius = 256.0f;
    float unloadRadius = 384.0f;
    float prefetchRadius = 128.0f;

    // Quality settings
    bool enableHLOD = true;
    bool enableImpostors = true;
    uint32_t maxLODLevel = 4;
    float lodTransitionDistance = 64.0f;

    // Prediction settings
    bool enablePrediction = true;
    float predictionTime = 2.0f;         // Predict N seconds ahead
    uint32_t predictionSamples = 8;      // Number of prediction samples

    // Eviction policy
    EvictionStrategy evictionStrategy = EvictionStrategy::LRU;
    float evictionThreshold = 0.9f;      // Evict when 90% of budget used

    // IO settings
    uint32_t maxConcurrentLoads = 16;
    uint32_t maxConcurrentUnloads = 8;
    bool prioritizeVisibleCells = true;

    // Debug settings
    bool enableProfiling = true;
    bool enableVisualization = false;
    bool logStreamingEvents = false;

    // Cell data loading (framework -> real IO integration).
    // If a cell file is missing and allowPlaceholderCellLoad==true, the cell will be marked loaded
    // with placeholder memory usage (keeps demos running without authoring data).
    std::wstring cellDataDirectory = L"data/world/cells";
    std::wstring cellFileExtension = L".ncell"; // also supports ".npkg"
    bool allowPlaceholderCellLoad = true;
    uint64_t placeholderCellSizeBytes = 256 * 1024;
};

// ===== Streaming Manager =====

class StreamingManager {
public:
    StreamingManager();
    ~StreamingManager();

    // Initialize with configuration
    bool Initialize(const StreamingManagerConfig& config);

    // Update (called every frame)
    void Update(float deltaTime, const Vec3& cameraPosition, const Vec3& cameraDirection, const Vec3& cameraVelocity);

    // Manual control
    void LoadCell(const CellCoord& coord, float priority = 0.0f);
    void UnloadCell(const CellCoord& coord);
    void ReloadCell(const CellCoord& coord);

    // Cell queries
    bool IsCellLoaded(const CellCoord& coord) const;
    CellData* GetCell(const CellCoord& coord);
    std::vector<CellCoord> GetLoadedCells() const;
    // Returns cell coordinates whose centers are within `radius` of `position`.
    // Note: this includes cells that are not currently loaded.
    std::vector<CellCoord> GetCellsInRange(const Vec3& position, float radius) const;

    // Asset bundles
    StreamingHandle LoadAssetBundle(const std::wstring& bundlePath);
    void UnloadAssetBundle(StreamingHandle handle);
    bool IsAssetBundleLoaded(StreamingHandle handle) const;

    // Layer management
    void LoadCellLayer(const CellCoord& coord, CellLayer layer, float priority = 0.0f);
    void UnloadCellLayer(const CellCoord& coord, CellLayer layer);
    bool IsCellLayerLoaded(const CellCoord& coord, CellLayer layer) const;

    // Priority control
    void SetCellPriority(const CellCoord& coord, float priority);
    void BoostCellPriority(const CellCoord& coord, float boost);
    void SetGlobalPriorityOverride(std::function<float(const CellCoord&, float)> priorityFunc);

    // Configuration
    void SetConfig(const StreamingManagerConfig& config);
    const StreamingManagerConfig& GetConfig() const { return config_; }

    // Statistics
    StreamingStatistics GetStatistics() const;
    void ResetStatistics();

    // Sub-systems access
    WorldPartition* GetWorldPartition() { return worldPartition_.get(); }
    AsyncIOSystem* GetAsyncIO() { return asyncIO_.get(); }
    InterestManager* GetInterestManager() { return interestManager_.get(); }
    LODSystem* GetLODSystem() { return lodSystem_.get(); }
    EvictionPolicy* GetEvictionPolicy() { return evictionPolicy_.get(); }

    // Memory management
    size_t GetMemoryUsage() const;
    size_t GetMemoryBudget() const;
    float GetMemoryUtilization() const;

    // Force unload all (for level changes)
    void UnloadAll();

    // Cleanup
    void Shutdown();

    // State check
    bool IsInitialized() const { return initialized_; }

private:
    // World partition index (available cells on disk). If empty, the system can fall back to "infinite grid"
    // behavior (useful for prototype/placeholder loads).
    void ScanAvailableCells();

    // Core update logic
    void UpdateStreaming(float deltaTime, const Vec3& cameraPosition, const Vec3& cameraDirection);
    void UpdatePredictiveStreaming(const Vec3& cameraPosition, const Vec3& cameraDirection, const Vec3& cameraVelocity);
    void UpdatePriority(const Vec3& cameraPosition, const Vec3& cameraDirection);
    void ProcessLoadQueue();
    void ProcessUnloadQueue();
    void ProcessCellOpCompletions();

    // Cell lifecycle
    struct CellLoadRequest {
        CellCoord coord;
        float priority;
        uint32_t frameIndex;
        std::vector<CellLayer> layers;  // Layers to load (empty = all layers)
    };

    struct CellUnloadRequest {
        CellCoord coord;
        uint32_t frameIndex;
    };

    void QueueCellLoad(const CellLoadRequest& request);
    void QueueCellUnload(const CellUnloadRequest& request);
    void ProcessCellLoad(const CellLoadRequest& request);
    void ProcessCellUnload(const CellUnloadRequest& request);
    std::wstring GetCellFilePath(const CellCoord& coord) const;

    // IO callbacks
    void OnCellLoadComplete(const CellCoord& coord, bool success, uint64_t bytesProcessed);
    void OnCellLoadFailed(const CellCoord& coord, const std::string& error);

    // Layer loading
    void LoadCellLayers(CellData* cell, const std::vector<CellLayer>& layers);
    void UnloadCellLayers(CellData* cell, const std::vector<CellLayer>& layers);

    // Memory management
    bool CheckMemoryBudget() const;
    void EnforceMemoryBudget();
    void UpdateMemoryStatistics();

    // Priority calculation
    float CalculateCellPriority(const CellCoord& coord, const Vec3& cameraPosition, const Vec3& cameraDirection) const;
    float CalculateLayerPriority(CellLayer layer) const;

    // Statistics tracking
    void UpdateStatistics(float deltaTime);

    // Configuration
    StreamingManagerConfig config_;

    // Sub-systems
    std::unique_ptr<WorldPartition> worldPartition_;
    std::unique_ptr<AsyncIOSystem> asyncIO_;
    std::unique_ptr<InterestManager> interestManager_;
    std::unique_ptr<LODSystem> lodSystem_;
    std::unique_ptr<EvictionPolicy> evictionPolicy_;
    std::unique_ptr<StreamingMemoryPool> memoryPool_;

    // Load/unload queues
    std::vector<CellLoadRequest> loadQueue_;
    std::vector<CellUnloadRequest> unloadQueue_;

    // Active operations
    struct ActiveCellOp {
        Next::JobHandle job;
        std::wstring filePath;     // cell package path
        std::string packageName;   // derived from file stem
        uint64_t fileBytes = 0;
    };
    std::unordered_map<CellCoord, ActiveCellOp, CellCoord::Hash> activeLoadOperations_;
    std::unordered_map<CellCoord, Next::JobHandle, CellCoord::Hash> activeUnloadOperations_;

    struct CellOpCompletion {
        CellCoord coord;
        bool isLoad = true;
        bool success = false;
        std::string packageName;
        uint64_t bytes = 0;
        std::string error;
    };
    mutable std::mutex completionMutex_;
    std::vector<CellOpCompletion> completions_;

    // Asset bundles
    std::unordered_map<uint64_t, AssetBundle> assetBundles_;
    std::unordered_map<CellCoord, uint64_t, CellCoord::Hash> cellToBundle_;
    std::unordered_map<CellCoord, std::string, CellCoord::Hash> cellToPackageName_;
    std::unordered_set<CellCoord, CellCoord::Hash> availableCells_;

    // Statistics
    StreamingStatistics stats_;
    uint64_t currentFrame_;
    float elapsedTime_;

    // Cached camera state (for eviction/policy decisions that happen outside Update())
    Vec3 lastCameraPosition_{0.0f, 0.0f, 0.0f};
    Vec3 lastCameraDirection_{0.0f, 0.0f, -1.0f};
    Vec3 lastCameraVelocity_{0.0f, 0.0f, 0.0f};

    // Priority override
    std::function<float(const CellCoord&, float)> priorityOverride_;

    // State
    bool initialized_;
};

} // namespace Streaming
} // namespace Next
