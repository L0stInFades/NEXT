#include "next/streaming/streaming_manager.h"
#include "next/foundation/logger.h"
#include "next/runtime/asset/asset_manager.h"
#include "next/jobsystem/job_system.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <filesystem>

namespace Next {
namespace Streaming {

// ===== Streaming Manager Implementation =====

StreamingManager::StreamingManager()
    : currentFrame_(0)
    , elapsedTime_(0.0f)
    , initialized_(false)
{
}

StreamingManager::~StreamingManager() {
    Shutdown();
}

bool StreamingManager::Initialize(const StreamingManagerConfig& config) {
    if (initialized_) {
        NEXT_LOG_WARNING("StreamingManager already initialized");
        return true;
    }

    config_ = config;

    // Initialize sub-systems
    worldPartition_ = std::make_unique<WorldPartition>();
    WorldPartitionConfig partitionConfig;
    partitionConfig.cellSize = 64.0f;  // Default cell size
    partitionConfig.loadRadius = config.loadRadius;
    partitionConfig.unloadRadius = config.unloadRadius;
    partitionConfig.maxLoadedCells = 256;  // Default
    partitionConfig.enableHLOD = config.enableHLOD;
    if (!worldPartition_->Initialize(partitionConfig)) {
        NEXT_LOG_ERROR("Failed to initialize WorldPartition");
        return false;
    }

    asyncIO_ = std::make_unique<AsyncIOSystem>();
    if (!asyncIO_->Initialize(AsyncIOConfig())) {
        NEXT_LOG_ERROR("Failed to initialize AsyncIOSystem");
        return false;
    }

    interestManager_ = std::make_unique<InterestManager>();
    if (!interestManager_->Initialize(InterestManagerConfig())) {
        NEXT_LOG_ERROR("Failed to initialize InterestManager");
        return false;
    }
    interestManager_->SetWorldPartition(worldPartition_.get());

    lodSystem_ = std::make_unique<LODSystem>();
    if (!lodSystem_->Initialize(LODSystemConfig())) {
        NEXT_LOG_ERROR("Failed to initialize LODSystem");
        return false;
    }

    evictionPolicy_ = std::make_unique<EvictionPolicy>();
    if (!evictionPolicy_->Initialize(EvictionPolicyConfig())) {
        NEXT_LOG_ERROR("Failed to initialize EvictionPolicy");
        return false;
    }
    evictionPolicy_->SetMemoryBudget(config.memoryBudgetMB * 1024 * 1024);
    evictionPolicy_->SetMaxCellCount(static_cast<uint32_t>(partitionConfig.maxLoadedCells));

    memoryPool_ = std::make_unique<StreamingMemoryPool>();
    if (!memoryPool_->Initialize(config.memoryBudgetMB * 1024 * 1024)) {
        NEXT_LOG_ERROR("Failed to initialize StreamingMemoryPool");
        return false;
    }

    // Reserve space for queues
    loadQueue_.reserve(config.maxConcurrentLoads);
    unloadQueue_.reserve(config.maxConcurrentUnloads);

    // Index existing authored cells from disk (prevents trying to stream infinite empty grid).
    ScanAvailableCells();

    // Reset statistics
    stats_ = StreamingStatistics();
    currentFrame_ = 0;
    elapsedTime_ = 0.0f;

    NEXT_LOG_INFO("StreamingManager initialized (CP7: World Streaming)");
    NEXT_LOG_INFO("  Memory budget: %zu MB", config.memoryBudgetMB);
    NEXT_LOG_INFO("  Load radius: %.1f meters", config.loadRadius);
    NEXT_LOG_INFO("  Max LOD level: %u", config.maxLODLevel);
    NEXT_LOG_INFO("  Available cells: %zu", availableCells_.size());

    initialized_ = true;
    return true;
}

void StreamingManager::ScanAvailableCells() {
    availableCells_.clear();

    const std::filesystem::path root(config_.cellDataDirectory);
    if (root.empty() || !std::filesystem::exists(root)) {
        return;
    }

    const std::wstring want = config_.cellFileExtension.empty() ? L".ncell" : config_.cellFileExtension;

    auto tryParseCell = [](const std::filesystem::path& file, CellCoord& out) -> bool {
        const std::wstring stem = file.stem().wstring();  // without extension
        if (stem.rfind(L"cell_", 0) != 0) {
            return false;
        }
        const std::wstring rest = stem.substr(5);
        const size_t us1 = rest.find(L'_');
        if (us1 == std::wstring::npos) {
            return false;
        }
        const size_t us2 = rest.find(L'_', us1 + 1);
        const std::wstring xStr = rest.substr(0, us1);
        const std::wstring zStr = (us2 == std::wstring::npos) ? rest.substr(us1 + 1) : rest.substr(us1 + 1, us2 - (us1 + 1));
        try {
            out.x = std::stoi(xStr);
            out.z = std::stoi(zStr);
        } catch (...) {
            return false;
        }
        return true;
    };

    // Flat scan for bring-up. Production can switch to a prebuilt index file for faster startup.
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::filesystem::path p = entry.path();
        const std::wstring ext = p.extension().wstring();
        if (ext != want && ext != L".ncell" && ext != L".npkg") {
            continue;
        }

        CellCoord c;
        if (tryParseCell(p, c)) {
            availableCells_.insert(c);
        }
    }
}

void StreamingManager::Update(float deltaTime, const Vec3& cameraPosition, const Vec3& cameraDirection, const Vec3& cameraVelocity) {
    if (!initialized_) {
        return;
    }

    currentFrame_++;
    elapsedTime_ += deltaTime;

    lastCameraPosition_ = cameraPosition;
    lastCameraDirection_ = cameraDirection;
    lastCameraVelocity_ = cameraVelocity;

    // Update sub-systems
    worldPartition_->Update(deltaTime, cameraPosition, cameraDirection);
    // Keep InterestManager in sync so priority/interest calculations can work off camera state.
    interestManager_->SetCameraPosition(cameraPosition, cameraDirection, cameraVelocity);
    interestManager_->Update(deltaTime);
    lodSystem_->Update(deltaTime, cameraPosition, Mat4::Identity());  // TODO: Pass actual view-projection matrix
    evictionPolicy_->Update(deltaTime, cameraPosition, currentFrame_);

    asyncIO_->Update();

    // Apply async load/unload completions from worker threads.
    ProcessCellOpCompletions();

    // Update streaming
    UpdateStreaming(deltaTime, cameraPosition, cameraDirection);

    // Update predictive streaming
    if (config_.enablePrediction) {
        UpdatePredictiveStreaming(cameraPosition, cameraDirection, cameraVelocity);
    }

    // Update priorities
    UpdatePriority(cameraPosition, cameraDirection);

    // Process queues
    ProcessLoadQueue();
    ProcessUnloadQueue();

    // Enforce memory budget
    EnforceMemoryBudget();

    // Update statistics
    UpdateStatistics(deltaTime);
}

void StreamingManager::ProcessCellOpCompletions() {
    std::vector<CellOpCompletion> local;
    {
        std::lock_guard<std::mutex> lock(completionMutex_);
        if (completions_.empty()) {
            return;
        }
        local.swap(completions_);
    }

    for (const CellOpCompletion& c : local) {
        CellData* cell = worldPartition_ ? worldPartition_->GetCell(c.coord) : nullptr;

        if (c.isLoad) {
            // Clear active op tracking.
            activeLoadOperations_.erase(c.coord);

            if (!cell) {
                // Cell no longer exists; drop the package ref if we loaded it.
                if (c.success && !c.packageName.empty()) {
                    Next::AssetManager::Instance().UnloadPackage(c.packageName);
                }
                continue;
            }

            if (!c.success) {
                worldPartition_->UpdateCellState(c.coord, CellLoadState::Error);
                stats_.failedLoads++;
                continue;
            }

            // If the cell was requested to unload while loading, immediately release the package.
            if (cell->state == CellLoadState::Unloading || cell->state == CellLoadState::Unloaded) {
                if (!c.packageName.empty()) {
                    Next::AssetManager::Instance().UnloadPackage(c.packageName);
                }
                cell->layers.clear();
                cell->metadata.memorySize = 0;
                cell->metadata.dataSize = 0;
                worldPartition_->UpdateCellState(c.coord, CellLoadState::Unloaded);
                continue;
            }

            // Track which package backs this cell (used for unload).
            if (!c.packageName.empty()) {
                cellToPackageName_[c.coord] = c.packageName;
            }

            // Mark the cell layer loaded. For now, a cell is backed by a single package (cell_x_z.*).
            CellData::LayerData ld;
            ld.layer = CellLayer::StaticMesh;
            ld.data = nullptr;
            ld.size = c.bytes;
            ld.state = CellLoadState::Loaded;
            cell->layers[CellLayer::StaticMesh] = ld;

            cell->metadata.dataSize = c.bytes;
            cell->metadata.memorySize = c.bytes; // approximate: package runtime memory ~= on-disk size (placeholder)
            worldPartition_->UpdateCellState(c.coord, CellLoadState::Loaded);
            evictionPolicy_->RecordAccess(c.coord, currentFrame_);
        } else {
            activeUnloadOperations_.erase(c.coord);

            if (!cell) {
                continue;
            }

            // Unload completion always transitions to Unloaded in this framework implementation.
            cell->layers.clear();
            cell->metadata.memorySize = 0;
            cell->metadata.dataSize = 0;
            worldPartition_->UpdateCellState(c.coord, CellLoadState::Unloaded);
        }
    }
}

void StreamingManager::LoadCell(const CellCoord& coord, float priority) {
    if (!initialized_) {
        return;
    }

    evictionPolicy_->SetCellPriority(coord, priority);

    CellLoadRequest request;
    request.coord = coord;
    request.priority = priority;
    request.frameIndex = currentFrame_;

    QueueCellLoad(request);
}

void StreamingManager::UnloadCell(const CellCoord& coord) {
    if (!initialized_) {
        return;
    }

    CellUnloadRequest request;
    request.coord = coord;
    request.frameIndex = currentFrame_;

    QueueCellUnload(request);
}

void StreamingManager::ReloadCell(const CellCoord& coord) {
    if (!initialized_) {
        return;
    }

    // Unload first
    UnloadCell(coord);

    // Then reload
    LoadCell(coord, 1.0f);  // High priority for reload
}

bool StreamingManager::IsCellLoaded(const CellCoord& coord) const {
    return worldPartition_->IsCellLoaded(coord);
}

CellData* StreamingManager::GetCell(const CellCoord& coord) {
    return worldPartition_->GetCell(coord);
}

std::vector<CellCoord> StreamingManager::GetLoadedCells() const {
    return worldPartition_->GetLoadedCells();
}

std::vector<CellCoord> StreamingManager::GetCellsInRange(const Vec3& position, float radius) const {
    std::vector<CellCoord> cells;

    if (!worldPartition_) {
        return cells;
    }

    const float cellSize = std::max(1.0f, worldPartition_->GetConfig().cellSize);
    const float invCellSize = 1.0f / cellSize;

    const int32_t minX = static_cast<int32_t>(std::floor((position.x - radius) * invCellSize));
    const int32_t maxX = static_cast<int32_t>(std::floor((position.x + radius) * invCellSize));
    const int32_t minZ = static_cast<int32_t>(std::floor((position.z - radius) * invCellSize));
    const int32_t maxZ = static_cast<int32_t>(std::floor((position.z + radius) * invCellSize));

    // Conservative reserve; we'll filter by circle check below.
    const int64_t spanX = static_cast<int64_t>(maxX) - static_cast<int64_t>(minX) + 1;
    const int64_t spanZ = static_cast<int64_t>(maxZ) - static_cast<int64_t>(minZ) + 1;
    if (spanX > 0 && spanZ > 0 && spanX * spanZ < 1024 * 1024) {
        cells.reserve(static_cast<size_t>(spanX * spanZ));
    }

    const float radiusSq = radius * radius;
    for (int32_t x = minX; x <= maxX; ++x) {
        for (int32_t z = minZ; z <= maxZ; ++z) {
            CellCoord coord{x, z};
            Vec3 cellCenter = worldPartition_->CellToWorld(coord);
            Vec3 toCell = cellCenter - position;
            if (toCell.Dot(toCell) <= radiusSq) {
                if (!availableCells_.empty() && availableCells_.find(coord) == availableCells_.end()) {
                    continue;
                }
                cells.push_back(coord);
            }
        }
    }

    return cells;
}

StreamingHandle StreamingManager::LoadAssetBundle(const std::wstring& bundlePath) {
    if (!initialized_) {
        return StreamingHandle{0};
    }

    std::filesystem::path p(bundlePath);
    if (!std::filesystem::exists(p)) {
        NEXT_LOG_ERROR("LoadAssetBundle: path does not exist: %S", bundlePath.c_str());
        return StreamingHandle{0};
    }

    auto tryParseCell = [](const std::filesystem::path& file, CellCoord& out) -> bool {
        const std::wstring stem = file.stem().wstring();  // without extension
        if (stem.rfind(L"cell_", 0) != 0) {
            return false;
        }

        // Accept:
        // - cell_X_Z
        // - cell_X_Z_anything...
        const std::wstring rest = stem.substr(5);
        const size_t us1 = rest.find(L'_');
        if (us1 == std::wstring::npos) {
            return false;
        }
        const size_t us2 = rest.find(L'_', us1 + 1);
        const std::wstring xStr = rest.substr(0, us1);
        const std::wstring zStr = (us2 == std::wstring::npos) ? rest.substr(us1 + 1) : rest.substr(us1 + 1, us2 - (us1 + 1));
        try {
            out.x = std::stoi(xStr);
            out.z = std::stoi(zStr);
        } catch (...) {
            return false;
        }
        return true;
    };

    AssetBundle bundle;
    static uint64_t sNextBundleId = 1;
    bundle.bundleId = sNextBundleId++;
    bundle.bundlePath = bundlePath;
    bundle.totalSize = 0;
    bundle.compressedSize = 0;

    if (std::filesystem::is_directory(p)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(p)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::wstring ext = entry.path().extension().wstring();
            const std::wstring want = config_.cellFileExtension.empty() ? L".ncell" : config_.cellFileExtension;
            if (ext != want && ext != L".ncell" && ext != L".npkg") {
                continue;
            }

            CellCoord c;
            if (!tryParseCell(entry.path(), c)) {
                continue;
            }

            bundle.cells.push_back(c);
            try {
                bundle.totalSize += static_cast<uint64_t>(entry.file_size());
            } catch (...) {
            }
        }
    } else {
        const std::wstring ext = p.extension().wstring();
        const std::wstring want = config_.cellFileExtension.empty() ? L".ncell" : config_.cellFileExtension;
        if (ext == want || ext == L".ncell" || ext == L".npkg") {
            CellCoord c;
            if (tryParseCell(p, c)) {
                bundle.cells.push_back(c);
                try {
                    bundle.totalSize += static_cast<uint64_t>(std::filesystem::file_size(p));
                } catch (...) {
                }
            }
        }
    }

    if (bundle.cells.empty()) {
        NEXT_LOG_WARNING("LoadAssetBundle: no cells found under %S", bundlePath.c_str());
    }

    const uint64_t id = bundle.bundleId;
    assetBundles_[id] = bundle;
    for (const CellCoord& c : assetBundles_[id].cells) {
        cellToBundle_[c] = id;
        LoadCell(c, 1.0f);
    }

    return StreamingHandle{id};
}

void StreamingManager::UnloadAssetBundle(StreamingHandle handle) {
    if (!initialized_ || !handle) {
        return;
    }

    auto it = assetBundles_.find(handle.id);
    if (it == assetBundles_.end()) {
        return;
    }

    for (const CellCoord& c : it->second.cells) {
        auto mapIt = cellToBundle_.find(c);
        if (mapIt != cellToBundle_.end() && mapIt->second == handle.id) {
            cellToBundle_.erase(mapIt);
        }
        UnloadCell(c);
    }

    assetBundles_.erase(it);
}

bool StreamingManager::IsAssetBundleLoaded(StreamingHandle handle) const {
    if (!handle) {
        return false;
    }
    return assetBundles_.find(handle.id) != assetBundles_.end();
}

void StreamingManager::LoadCellLayer(const CellCoord& coord, CellLayer layer, float priority) {
    if (!initialized_) {
        return;
    }

    // Framework behavior: only StaticMesh is backed by a real cell blob right now.
    if (layer == CellLayer::StaticMesh) {
        LoadCell(coord, priority);
        return;
    }

    if (!config_.allowPlaceholderCellLoad) {
        NEXT_LOG_WARNING("LoadCellLayer: placeholder layers disabled; ignoring layer=%u for cell(%d,%d)",
                         static_cast<uint32_t>(layer), coord.x, coord.z);
        return;
    }

    // Ensure the cell exists and is at least loaded at the cell level.
    if (!worldPartition_->GetCell(coord)) {
        LoadCell(coord, priority);
    }

    CellData* cell = worldPartition_->GetCell(coord);
    if (!cell) {
        return;
    }

    auto it = cell->layers.find(layer);
    if (it != cell->layers.end() && it->second.state == CellLoadState::Loaded) {
        return;
    }

    CellData::LayerData ld;
    ld.layer = layer;
    ld.data = nullptr;
    ld.size = 0;
    ld.state = CellLoadState::Loaded;
    cell->layers[layer] = ld;
}

void StreamingManager::UnloadCellLayer(const CellCoord& coord, CellLayer layer) {
    if (!initialized_) {
        return;
    }

    CellData* cell = worldPartition_->GetCell(coord);
    if (!cell) {
        return;
    }

    auto it = cell->layers.find(layer);
    if (it == cell->layers.end()) {
        return;
    }

    if (it->second.data && memoryPool_) {
        memoryPool_->Free(it->second.data);
    }
    cell->layers.erase(it);
}

bool StreamingManager::IsCellLayerLoaded(const CellCoord& coord, CellLayer layer) const {
    const CellData* cell = worldPartition_->GetCell(coord);
    if (cell) {
        return cell->IsLayerLoaded(layer);
    }
    return false;
}

void StreamingManager::SetCellPriority(const CellCoord& coord, float priority) {
    evictionPolicy_->SetCellPriority(coord, priority);
}

void StreamingManager::BoostCellPriority(const CellCoord& coord, float boost) {
    float currentPriority = evictionPolicy_->GetCellPriority(coord);
    evictionPolicy_->SetCellPriority(coord, currentPriority + boost);
}

void StreamingManager::SetGlobalPriorityOverride(std::function<float(const CellCoord&, float)> priorityFunc) {
    priorityOverride_ = priorityFunc;
}

void StreamingManager::SetConfig(const StreamingManagerConfig& config) {
    config_ = config;

    // Update sub-system configs
    WorldPartitionConfig partitionConfig;
    partitionConfig.cellSize = 64.0f;
    partitionConfig.loadRadius = config.loadRadius;
    partitionConfig.unloadRadius = config.unloadRadius;
    partitionConfig.maxLoadedCells = 256;
    partitionConfig.enableHLOD = config.enableHLOD;
    worldPartition_->SetConfig(partitionConfig);

    lodSystem_->SetConfig(LODSystemConfig());
    evictionPolicy_->SetConfig(EvictionPolicyConfig());
}

StreamingStatistics StreamingManager::GetStatistics() const {
    return stats_;
}

void StreamingManager::ResetStatistics() {
    stats_ = StreamingStatistics();
}

size_t StreamingManager::GetMemoryUsage() const {
    size_t usage = 0;

    auto loadedCells = worldPartition_->GetLoadedCells();
    for (const CellCoord& coord : loadedCells) {
        const CellData* cell = worldPartition_->GetCell(coord);
        if (cell) {
            usage += cell->metadata.memorySize;
        }
    }

    return usage;
}

size_t StreamingManager::GetMemoryBudget() const {
    return config_.memoryBudgetMB * 1024 * 1024;
}

float StreamingManager::GetMemoryUtilization() const {
    size_t budget = GetMemoryBudget();
    if (budget == 0) {
        return 0.0f;
    }
    return static_cast<float>(GetMemoryUsage()) / static_cast<float>(budget);
}

void StreamingManager::UnloadAll() {
    if (!initialized_) {
        return;
    }

    // Unload loaded cells + cancel any in-flight loads.
    std::vector<CellCoord> loadedCells = worldPartition_->GetLoadedCells();
    for (const CellCoord& coord : loadedCells) {
        UnloadCell(coord);
    }
    for (const auto& [coord, op] : activeLoadOperations_) {
        (void)op;
        UnloadCell(coord);
    }

    // Drain unload jobs so shutdown/level transitions don't leak package refs.
    constexpr uint32_t kMaxIters = 500;
    for (uint32_t i = 0; i < kMaxIters; ++i) {
        ProcessUnloadQueue();
        Next::JobSystem::Instance().Pump(0.25);
        ProcessCellOpCompletions();

        if (unloadQueue_.empty() && activeUnloadOperations_.empty() && activeLoadOperations_.empty()) {
            break;
        }
    }

    NEXT_LOG_INFO("UnloadAll requested for %zu cells (inflightLoads=%zu inflightUnloads=%zu)",
                  loadedCells.size(), activeLoadOperations_.size(), activeUnloadOperations_.size());
}

void StreamingManager::Shutdown() {
    if (!initialized_) {
        return;
    }

    UnloadAll();

    // Shutdown sub-systems
    if (memoryPool_) {
        memoryPool_->Shutdown();
        memoryPool_.reset();
    }

    if (evictionPolicy_) {
        evictionPolicy_->Shutdown();
        evictionPolicy_.reset();
    }

    if (lodSystem_) {
        lodSystem_->Shutdown();
        lodSystem_.reset();
    }

    if (interestManager_) {
        interestManager_->Shutdown();
        interestManager_.reset();
    }

    if (asyncIO_) {
        asyncIO_->Shutdown();
        asyncIO_.reset();
    }

    if (worldPartition_) {
        worldPartition_->Shutdown();
        worldPartition_.reset();
    }

    loadQueue_.clear();
    unloadQueue_.clear();
    activeLoadOperations_.clear();
    activeUnloadOperations_.clear();
    cellToPackageName_.clear();
    cellToBundle_.clear();
    assetBundles_.clear();
    {
        std::lock_guard<std::mutex> lock(completionMutex_);
        completions_.clear();
    }

    initialized_ = false;
    NEXT_LOG_INFO("StreamingManager shutdown complete");
}

// ===== Private Methods =====

void StreamingManager::UpdateStreaming(float deltaTime, const Vec3& cameraPosition, const Vec3& cameraDirection) {
    // If we're over budget, do not queue additional loads this frame. Let eviction catch up.
    if (GetMemoryUtilization() >= config_.evictionThreshold) {
        // Still allow distance-based unloads.
        std::vector<CellCoord> loadedCells = worldPartition_->GetLoadedCells();
        const float unloadRadiusSq = config_.unloadRadius * config_.unloadRadius;
        for (const CellCoord& coord : loadedCells) {
            const CellData* cell = worldPartition_->GetCell(coord);
            if (!cell) {
                continue;
            }

            Vec3 toCell = cell->metadata.worldPosition - cameraPosition;
            if (toCell.Dot(toCell) > unloadRadiusSq) {
                UnloadCell(coord);
            }
        }
        return;
    }

    // Find cells that should be loaded (includes unloaded cells).
    std::vector<CellCoord> desiredCells = GetCellsInRange(cameraPosition, config_.loadRadius);

    // Load / queue cells.
    for (const CellCoord& coord : desiredCells) {
        const CellData* existing = worldPartition_->GetCell(coord);
        if (existing) {
            if (existing->state == CellLoadState::Loaded ||
                existing->state == CellLoadState::Queued ||
                existing->state == CellLoadState::Loading ||
                existing->state == CellLoadState::Decompressing ||
                existing->state == CellLoadState::Uploading) {
                continue;
            }
        }

        float priority = CalculateCellPriority(coord, cameraPosition, cameraDirection);
        LoadCell(coord, priority);
    }

    // Unload cells that are too far away.
    std::vector<CellCoord> loadedCells = worldPartition_->GetLoadedCells();
    const float unloadRadiusSq = config_.unloadRadius * config_.unloadRadius;
    for (const CellCoord& coord : loadedCells) {
        const CellData* cell = worldPartition_->GetCell(coord);
        if (!cell) {
            continue;
        }

        Vec3 toCell = cell->metadata.worldPosition - cameraPosition;
        if (toCell.Dot(toCell) > unloadRadiusSq) {
            UnloadCell(coord);
        }
    }
}

void StreamingManager::UpdatePredictiveStreaming(const Vec3& cameraPosition, const Vec3& cameraDirection, const Vec3& cameraVelocity) {
    const float velSq = cameraVelocity.Dot(cameraVelocity);
    if (velSq < 1e-4f) {
        return;
    }

    const float horizon = std::max(0.1f, config_.predictionTime);
    const uint32_t samples = std::max(1u, config_.predictionSamples);
    const float denom = (samples > 1) ? static_cast<float>(samples - 1) : 1.0f;

    std::unordered_set<CellCoord, CellCoord::Hash> predicted;

    for (uint32_t i = 0; i < samples; ++i) {
        const float t = horizon * (static_cast<float>(i) / denom);
        const Vec3 pos = cameraPosition + cameraVelocity * t;
        const float r = std::max(0.0f, config_.prefetchRadius);
        if (r <= 0.0f) {
            continue;
        }

        for (const CellCoord& coord : GetCellsInRange(pos, r)) {
            predicted.insert(coord);
        }
    }

    for (const CellCoord& coord : predicted) {
        const CellData* existing = worldPartition_->GetCell(coord);
        if (existing) {
            if (existing->state == CellLoadState::Loaded ||
                existing->state == CellLoadState::Queued ||
                existing->state == CellLoadState::Loading ||
                existing->state == CellLoadState::Decompressing ||
                existing->state == CellLoadState::Uploading) {
                continue;
            }
        }

        float p = CalculateCellPriority(coord, cameraPosition, cameraDirection);
        p += 0.25f;  // predictive bias
        LoadCell(coord, p);
    }

    if (config_.logStreamingEvents && !predicted.empty()) {
        NEXT_LOG_INFO("Predictive streaming queued %zu cells", predicted.size());
    }
}

void StreamingManager::UpdatePriority(const Vec3& cameraPosition, const Vec3& cameraDirection) {
    // Update priorities for all queued cells
    for (auto& request : loadQueue_) {
        request.priority = CalculateCellPriority(request.coord, cameraPosition, cameraDirection);
    }
}

void StreamingManager::ProcessLoadQueue() {
    // If we're already over budget, don't start new loads this frame.
    if (GetMemoryUtilization() >= config_.evictionThreshold) {
        return;
    }

    // Sort by priority (highest first)
    std::sort(loadQueue_.begin(), loadQueue_.end(),
        [](const CellLoadRequest& a, const CellLoadRequest& b) {
            return a.priority > b.priority;
        }
    );

    // Process loads while under the in-flight cap.
    std::vector<CellCoord> started;
    size_t inflight = activeLoadOperations_.size();
    for (const auto& request : loadQueue_) {
        if (inflight >= config_.maxConcurrentLoads) {
            break;
        }
        if (activeLoadOperations_.count(request.coord) != 0) {
            continue;
        }

        ProcessCellLoad(request);
        started.push_back(request.coord);
        inflight = activeLoadOperations_.size();
    }

    // Remove started requests (queue is small; O(n^2) is fine).
    if (!started.empty()) {
        loadQueue_.erase(
            std::remove_if(loadQueue_.begin(), loadQueue_.end(),
                           [&](const CellLoadRequest& r) {
                               for (const CellCoord& c : started) {
                                   if (r.coord == c) return true;
                               }
                               return false;
                           }),
            loadQueue_.end());
    }
}

void StreamingManager::ProcessUnloadQueue() {
    std::vector<CellCoord> started;
    size_t inflight = activeUnloadOperations_.size();
    for (const auto& request : unloadQueue_) {
        if (inflight >= config_.maxConcurrentUnloads) {
            break;
        }
        if (activeUnloadOperations_.count(request.coord) != 0) {
            continue;
        }
        ProcessCellUnload(request);
        started.push_back(request.coord);
        inflight = activeUnloadOperations_.size();
    }

    if (!started.empty()) {
        unloadQueue_.erase(
            std::remove_if(unloadQueue_.begin(), unloadQueue_.end(),
                           [&](const CellUnloadRequest& r) {
                               for (const CellCoord& c : started) {
                                   if (r.coord == c) return true;
                               }
                               return false;
                           }),
            unloadQueue_.end());
    }
}

void StreamingManager::QueueCellLoad(const CellLoadRequest& request) {
    // Deduplicate (queue is small).
    for (const auto& r : loadQueue_) {
        if (r.coord == request.coord) {
            return;
        }
    }
    loadQueue_.push_back(request);
}

void StreamingManager::QueueCellUnload(const CellUnloadRequest& request) {
    for (const auto& r : unloadQueue_) {
        if (r.coord == request.coord) {
            return;
        }
    }
    unloadQueue_.push_back(request);
}

void StreamingManager::ProcessCellLoad(const CellLoadRequest& request) {
    worldPartition_->RequestCellLoad(request.coord, request.priority);

    CellData* cell = worldPartition_->GetCell(request.coord);
    if (!cell) {
        OnCellLoadComplete(request.coord, false, 0);
        return;
    }

    // If already in-flight, don't double-submit.
    if (activeLoadOperations_.count(request.coord) != 0) {
        return;
    }

    // Determine file path.
    const std::wstring filePath = GetCellFilePath(request.coord);
    const std::filesystem::path fsPath(filePath);

    // Missing data: allow placeholder to keep demo running.
    if (!std::filesystem::exists(fsPath)) {
        if (!config_.allowPlaceholderCellLoad) {
            NEXT_LOG_ERROR("Missing cell file: %S", filePath.c_str());
            worldPartition_->UpdateCellState(request.coord, CellLoadState::Error);
            stats_.failedLoads++;
            return;
        }

        // Placeholder load: no actual IO.
        worldPartition_->UpdateCellState(request.coord, CellLoadState::Loading);
        cell->metadata.memorySize = config_.placeholderCellSizeBytes;
        cell->metadata.dataSize = config_.placeholderCellSizeBytes;
        worldPartition_->UpdateCellState(request.coord, CellLoadState::Loaded);
        evictionPolicy_->RecordAccess(request.coord, currentFrame_);
        return;
    }

    uint64_t fileSize = 0;
    try {
        fileSize = static_cast<uint64_t>(std::filesystem::file_size(fsPath));
    } catch (...) {
        fileSize = 0;
    }

    if (fileSize == 0) {
        NEXT_LOG_ERROR("Cell file is empty/unreadable: %S", filePath.c_str());
        worldPartition_->UpdateCellState(request.coord, CellLoadState::Error);
        stats_.failedLoads++;
        return;
    }

    // Treat each cell file as an asset package (npkg-compatible). This makes streaming immediately useful
    // with the existing asset pipeline while we iterate toward layered manifests + HLOD production tooling.
    const std::string pkgName = fsPath.stem().string();
    const std::string pkgPath = fsPath.u8string();

    worldPartition_->UpdateCellState(request.coord, CellLoadState::Loading);

    ActiveCellOp op;
    op.filePath = filePath;
    op.packageName = pkgName;
    op.fileBytes = fileSize;

    auto& js = Next::JobSystem::Instance();
    op.job = js.Submit([this, coord = request.coord, pkgName, pkgPath, fileSize]() {
        bool ok = Next::AssetManager::Instance().LoadPackage(pkgPath);

        CellOpCompletion c;
        c.coord = coord;
        c.isLoad = true;
        c.success = ok;
        c.packageName = pkgName;
        c.bytes = fileSize;
        if (!ok) {
            c.error = "LoadPackage failed";
        }

        std::lock_guard<std::mutex> lock(completionMutex_);
        completions_.push_back(std::move(c));
    }, Next::JobPriority::High, {}, "CellLoadPackage");

    activeLoadOperations_[request.coord] = std::move(op);
}

void StreamingManager::ProcessCellUnload(const CellUnloadRequest& request) {
    CellData* cell = worldPartition_->GetCell(request.coord);
    if (!cell) {
        return;
    }

    // Cancel in-flight load (best-effort).
    auto itLoad = activeLoadOperations_.find(request.coord);
    if (itLoad != activeLoadOperations_.end() && itLoad->second.job.IsValid()) {
        Next::JobSystem::Instance().Cancel(itLoad->second.job);
        activeLoadOperations_.erase(itLoad);
    }

    worldPartition_->RequestCellUnload(request.coord);

    // Determine the backing package name (stored on load; fallback to naming convention).
    std::string pkgName;
    auto itPkg = cellToPackageName_.find(request.coord);
    if (itPkg != cellToPackageName_.end()) {
        pkgName = itPkg->second;
    } else {
        const std::filesystem::path fsPath(GetCellFilePath(request.coord));
        pkgName = fsPath.stem().string();
    }

    auto& js = Next::JobSystem::Instance();
    Next::JobHandle job = js.Submit([this, coord = request.coord, pkgName]() {
        if (!pkgName.empty()) {
            Next::AssetManager::Instance().UnloadPackage(pkgName);
        }

        CellOpCompletion c;
        c.coord = coord;
        c.isLoad = false;
        c.success = true;
        c.packageName = pkgName;

        std::lock_guard<std::mutex> lock(completionMutex_);
        completions_.push_back(std::move(c));
    }, Next::JobPriority::Normal, {}, "CellUnloadPackage");

    activeUnloadOperations_[request.coord] = job;
}

std::wstring StreamingManager::GetCellFilePath(const CellCoord& coord) const {
    std::wstring dir = config_.cellDataDirectory;
    if (!dir.empty()) {
        const wchar_t last = dir.back();
        if (last != L'/' && last != L'\\') {
            dir.push_back(L'\\');
        }
    }

    // Naming convention: cell_{x}_{z}<ext>
    const std::wstring ext = config_.cellFileExtension.empty() ? L".ncell" : config_.cellFileExtension;
    return dir + L"cell_" + std::to_wstring(coord.x) + L"_" + std::to_wstring(coord.z) + ext;
}

void StreamingManager::OnCellLoadComplete(const CellCoord& coord, bool success, uint64_t bytesProcessed) {
    // Update cell state
    if (success) {
        worldPartition_->UpdateCellState(coord, CellLoadState::Loaded);
    } else {
        worldPartition_->UpdateCellState(coord, CellLoadState::Error);
        stats_.failedLoads++;
    }

    activeLoadOperations_.erase(coord);
}

void StreamingManager::OnCellLoadFailed(const CellCoord& coord, const std::string& error) {
    NEXT_LOG_ERROR("Cell load failed: [%d, %d] - %s", coord.x, coord.z, error.c_str());
    worldPartition_->UpdateCellState(coord, CellLoadState::Error);
    stats_.failedLoads++;
    activeLoadOperations_.erase(coord);
}

void StreamingManager::LoadCellLayers(CellData* cell, const std::vector<CellLayer>& layers) {
    if (!cell) {
        return;
    }
    if (layers.empty()) {
        return;
    }

    for (CellLayer layer : layers) {
        LoadCellLayer(cell->coord, layer, cell->priority);
    }
}

void StreamingManager::UnloadCellLayers(CellData* cell, const std::vector<CellLayer>& layers) {
    if (!cell) {
        return;
    }
    if (layers.empty()) {
        return;
    }

    for (CellLayer layer : layers) {
        UnloadCellLayer(cell->coord, layer);
    }
}

bool StreamingManager::CheckMemoryBudget() const {
    return GetMemoryUtilization() < config_.evictionThreshold;
}

void StreamingManager::EnforceMemoryBudget() {
    if (CheckMemoryBudget()) {
        return;
    }

    const size_t budget = GetMemoryBudget();
    const size_t usage = GetMemoryUsage();
    if (budget == 0 || usage <= budget) {
        return;
    }

    // Free enough memory to get under a target utilization (hysteresis prevents thrashing).
    const float targetUtil = std::max(0.5f, config_.evictionThreshold - 0.1f);
    const size_t targetUsage = static_cast<size_t>(static_cast<double>(budget) * static_cast<double>(targetUtil));
    const size_t targetFree = (usage > targetUsage) ? (usage - targetUsage) : 0;
    if (targetFree == 0) {
        return;
    }

    // Use eviction policy to select cells to unload (non-owning pointers).
    std::unordered_map<CellCoord, const CellData*, CellCoord::Hash> loaded;
    for (const CellCoord& coord : worldPartition_->GetLoadedCells()) {
        if (const CellData* cell = worldPartition_->GetCell(coord)) {
            loaded.emplace(coord, cell);
        }
    }

    std::vector<EvictionCandidate> candidates = evictionPolicy_->SelectEvictionCandidates(
        loaded, targetFree, config_.maxConcurrentUnloads
    );

    if (config_.logStreamingEvents) {
        NEXT_LOG_INFO("EnforceMemoryBudget: usage=%.2fMB budget=%.2fMB targetFree=%.2fMB loaded=%zu candidates=%zu",
                      static_cast<double>(usage) / (1024.0 * 1024.0),
                      static_cast<double>(budget) / (1024.0 * 1024.0),
                      static_cast<double>(targetFree) / (1024.0 * 1024.0),
                      loaded.size(),
                      candidates.size());
    }

    // Evict selected cells
    for (const auto& candidate : candidates) {
        UnloadCell(candidate.coord);
    }

    // Apply unloads immediately so utilization reflects the new state within the same frame.
    ProcessUnloadQueue();
}

void StreamingManager::UpdateMemoryStatistics() {
    stats_.memoryUsed = GetMemoryUsage();
    stats_.memoryBudget = GetMemoryBudget();
    stats_.memoryUtilization = GetMemoryUtilization();

    evictionPolicy_->SetMemoryBudget(stats_.memoryBudget);
    evictionPolicy_->SetMemoryUsage(stats_.memoryUsed);
    evictionPolicy_->SetCurrentCellCount(stats_.loadedCells);
}

float StreamingManager::CalculateCellPriority(const CellCoord& coord, const Vec3& cameraPosition, const Vec3& cameraDirection) const {
    // Higher = more important to load.
    Vec3 cellPos = worldPartition_->CellToWorld(coord);
    Vec3 toCell = cellPos - cameraPosition;
    const float distance = toCell.Length();

    float distancePriority = 1.0f;
    if (config_.loadRadius > 1e-3f) {
        distancePriority = 1.0f - (distance / config_.loadRadius);
    }
    distancePriority = std::max(0.0f, distancePriority);

    float directionPriority = 0.0f;
    if (distance > 1e-3f) {
        Vec3 dirNorm = toCell * (1.0f / distance);
        const float dot = std::max(-1.0f, std::min(1.0f, dirNorm.Dot(cameraDirection)));
        directionPriority = (dot + 1.0f) * 0.5f;  // [0..1]
    }

    float basePriority = distancePriority + (directionPriority * 0.5f);

    // Apply priority override if set
    if (priorityOverride_) {
        basePriority = priorityOverride_(coord, basePriority);
    }

    return basePriority;
}

float StreamingManager::CalculateLayerPriority(CellLayer layer) const {
    // TODO: Implement layer-specific priority calculation
    return 1.0f;
}

void StreamingManager::UpdateStatistics(float deltaTime) {
    // Update basic statistics
    stats_.loadedCells = 0;
    stats_.loadingCells = 0;
    stats_.queuedCells = static_cast<uint32_t>(loadQueue_.size());

    auto loadedCells = worldPartition_->GetLoadedCells();
    stats_.loadedCells = static_cast<uint32_t>(loadedCells.size());
    stats_.loadingCells = static_cast<uint32_t>(activeLoadOperations_.size());

    // Update memory statistics
    UpdateMemoryStatistics();

    // Update LOD statistics
    // TODO: Get LOD statistics from LODSystem
}

} // namespace Streaming
} // namespace Next
