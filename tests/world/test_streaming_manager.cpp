#include <gtest/gtest.h>

#include "next/streaming/streaming_manager.h"

#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <unordered_set>

namespace {

struct CellCoordHash {
    size_t operator()(const Next::Streaming::CellCoord& c) const noexcept {
        return Next::Streaming::CellCoord::Hash{}(c);
    }
};

static void WriteCellFile(const std::filesystem::path& dir, int32_t x, int32_t z, size_t sizeBytes) {
    std::filesystem::create_directories(dir);
    const std::filesystem::path p = dir / ("cell_" + std::to_string(x) + "_" + std::to_string(z) + ".ncell");
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.good());
    std::string blob(sizeBytes, '\0');
    for (size_t i = 0; i < blob.size(); ++i) {
        blob[i] = static_cast<char>((i + static_cast<size_t>(x) * 131u + static_cast<size_t>(z) * 17u) & 0xFF);
    }
    out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    out.close();
    ASSERT_TRUE(std::filesystem::exists(p));
}

}  // namespace

TEST(WorldStreaming, LoadsCellsAroundCamera) {
    using namespace Next::Streaming;
    using Next::Vec3;

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 128.0f;
    cfg.unloadRadius = 160.0f;
    cfg.maxConcurrentLoads = 64;
    cfg.maxConcurrentUnloads = 64;
    cfg.enablePrediction = false;

    ASSERT_TRUE(mgr.Initialize(cfg));

    mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 0.0f));

    auto loaded = mgr.GetLoadedCells();
    EXPECT_FALSE(loaded.empty());
    EXPECT_GE(loaded.size(), 4u);
    EXPECT_LE(loaded.size(), 64u);

    mgr.Shutdown();
}

TEST(WorldStreaming, LoadsFromDiskViaAsyncIO) {
    using namespace Next::Streaming;
    using Next::Vec3;

    // Create a temp cell directory with at least the origin-adjacent cells.
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_world_streaming_test_cells";
    std::filesystem::remove_all(tmp);

    for (int x = -4; x <= 4; ++x) {
        for (int z = -4; z <= 4; ++z) {
            WriteCellFile(tmp, x, z, 16 * 1024);
        }
    }

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 128.0f;
    cfg.unloadRadius = 160.0f;
    cfg.maxConcurrentLoads = 64;
    cfg.maxConcurrentUnloads = 64;
    cfg.enablePrediction = false;
    cfg.cellDataDirectory = tmp.wstring();
    cfg.allowPlaceholderCellLoad = false;

    ASSERT_TRUE(mgr.Initialize(cfg));

    // Drive a few frames until at least one cell is loaded from disk.
    for (int i = 0; i < 50; ++i) {
        mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 0.0f));
        if (!mgr.GetLoadedCells().empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto loaded = mgr.GetLoadedCells();
    ASSERT_FALSE(loaded.empty());

    // Spot-check that one loaded cell has a real layer blob.
    CellData* cell = mgr.GetCell(loaded[0]);
    ASSERT_NE(cell, nullptr);
    ASSERT_EQ(cell->state, CellLoadState::Loaded);
    EXPECT_GT(cell->metadata.memorySize, 0ull);

    auto it = cell->layers.find(CellLayer::StaticMesh);
    ASSERT_TRUE(it != cell->layers.end());
    EXPECT_NE(it->second.data, nullptr);
    EXPECT_GT(it->second.size, 0ull);

    mgr.Shutdown();

    std::filesystem::remove_all(tmp);
}

TEST(WorldStreaming, UnloadsCellsOutsideRadius) {
    using namespace Next::Streaming;
    using Next::Vec3;

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 128.0f;
    cfg.unloadRadius = 160.0f;
    cfg.maxConcurrentLoads = 64;
    cfg.maxConcurrentUnloads = 64;
    cfg.enablePrediction = false;

    ASSERT_TRUE(mgr.Initialize(cfg));

    mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f));
    auto loaded0 = mgr.GetLoadedCells();
    ASSERT_FALSE(loaded0.empty());

    // Move the camera far enough that all previously loaded cells should be out of unload radius.
    mgr.Update(0.016f, Vec3(1024.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f));
    auto loaded1 = mgr.GetLoadedCells();
    ASSERT_FALSE(loaded1.empty());

    std::unordered_set<CellCoord, CellCoordHash> set0(loaded0.begin(), loaded0.end());
    size_t intersection = 0;
    for (const auto& c : loaded1) {
        if (set0.count(c)) {
            intersection++;
        }
    }

    // The loaded set should largely change after a large teleport.
    EXPECT_LT(intersection, loaded0.size() / 4 + 1);

    mgr.Shutdown();
}

TEST(WorldStreaming, EvictsWhenOverBudget) {
    using namespace Next::Streaming;
    using Next::Vec3;

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 1;          // 1 MB budget
    cfg.loadRadius = 256.0f;         // load multiple cells
    cfg.unloadRadius = 512.0f;
    cfg.maxConcurrentLoads = 256;
    cfg.maxConcurrentUnloads = 256;
    cfg.enablePrediction = false;

    ASSERT_TRUE(mgr.Initialize(cfg));

    // A few updates to allow load + enforcement to run.
    for (int i = 0; i < 3; ++i) {
        mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 0.0f));
    }

    // StreamingManager's framework load uses 256KB per cell; with a 1MB budget and 0.9 threshold,
    // eviction should kick in and keep utilization bounded.
    // Note: the eviction policy protects cells near the camera (default protected radius),
    // so utilization may clamp close to 1.0f for tiny budgets.
    EXPECT_LE(mgr.GetMemoryUtilization(), 1.05f);

    mgr.Shutdown();
}
