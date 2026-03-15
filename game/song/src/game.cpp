#include "song/game.h"
#include "next/platform/platform.h"
#include "next/platform/window.h"
#include "next/platform/input.h"
#include "next/foundation/logger.h"
#include "next/runtime/world.h"
#include "next/renderer/renderer.h"
#include "next/profiler/profiler.h"
#include "next/profiler/cpu_scope.h"
#include "next/jobsystem/job_system.h"
#include "next/runtime/asset/asset_manager.h"
#include "next/streaming/streaming_manager.h"
#include <filesystem>
#include <atomic>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#ifdef CreateWindow
#undef CreateWindow
#endif
#endif

namespace {

std::filesystem::path GetExecutableDirectory() {
#ifdef _WIN32
    char path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(path).parent_path();
#else
    return {};
#endif
}

void PushUniquePath(std::vector<std::filesystem::path>& roots, const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }

    for (const auto& existing : roots) {
        if (existing == path) {
            return;
        }
    }

    roots.push_back(path);
}

std::filesystem::path ResolveRuntimePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return path;
    }

    std::error_code ec;
    std::vector<std::filesystem::path> roots;
    PushUniquePath(roots, std::filesystem::current_path(ec));

    std::filesystem::path probe = GetExecutableDirectory();
    for (int i = 0; i < 6 && !probe.empty(); ++i) {
        PushUniquePath(roots, probe);
        const std::filesystem::path parent = probe.parent_path();
        if (parent == probe) {
            break;
        }
        probe = parent;
    }

    std::filesystem::path fallback;
    for (const auto& root : roots) {
        const std::filesystem::path candidate = root / path;
        if (fallback.empty()) {
            const std::filesystem::path absoluteCandidate = std::filesystem::absolute(candidate, ec);
            fallback = ec ? candidate : absoluteCandidate;
        }

        if (std::filesystem::exists(candidate, ec)) {
            const std::filesystem::path absoluteCandidate = std::filesystem::absolute(candidate, ec);
            return ec ? candidate : absoluteCandidate;
        }
    }

    return fallback.empty() ? path : fallback;
}

} // namespace

namespace Song {

Game::Game(const GameOptions& options)
    : options_(options)
    , running_(false) {
}

Game::~Game() {
    Shutdown();
}

bool Game::Initialize() {
    NEXT_LOG_INFO("Initializing Song Dynasty Game...");
    NEXT_LOG_INFO("Platform: %s", Next::GetPlatformName());

    if (running_) {
        NEXT_LOG_WARNING("Game already initialized");
        return true;
    }

    if (!InitializeEngine()) {
        NEXT_LOG_ERROR("Failed to initialize engine");
        return false;
    }

    NEXT_LOG_INFO("Game initialized successfully");
    return true;
}

bool Game::InitializeEngine() {
    // Initialize platform
    if (!Next::PlatformInitialize()) {
        NEXT_LOG_ERROR("Failed to initialize platform");
        return false;
    }

    // Initialize logger
    Next::Logger::Initialize();

    // Create window
    window_ = Next::CreateWindow();
    Next::WindowDesc windowDesc;
    windowDesc.title = "NEXT Engine - Song Dynasty (CP0 Demo)";
    windowDesc.width = 1280;
    windowDesc.height = 720;

    if (!window_ || !window_->Initialize(windowDesc)) {
        NEXT_LOG_ERROR("Failed to create window");
        delete window_;
        window_ = nullptr;
        return false;
    }

    // Input (singleton owned by platform)
    input_ = Next::GetInput();

    // Create renderer
    renderer_ = Next::Renderer::Create();
    if (!renderer_ || !renderer_->Initialize(window_)) {
        NEXT_LOG_ERROR("Failed to initialize renderer");
        delete renderer_;
        renderer_ = nullptr;
        window_->Shutdown();
        delete window_;
        window_ = nullptr;
        return false;
    }

    // Initialize job system (leave one core for main thread)
    auto& jobSystem = Next::JobSystem::Instance();
    jobSystem.Initialize(0);

    // Initialize asset manager (CP3)
    auto& assetManager = Next::AssetManager::Instance();
    if (!assetManager.Initialize()) {
        NEXT_LOG_ERROR("Failed to initialize asset manager");
        return false;
    }

    world_ = std::make_unique<Next::World>();

    // Initialize World Streaming (CP7) - demo integration.
    streaming_ = std::make_unique<Next::Streaming::StreamingManager>();
    Next::Streaming::StreamingManagerConfig streamingCfg;
    streamingCfg.memoryBudgetMB = 256;
    streamingCfg.loadRadius = 256.0f;
    streamingCfg.unloadRadius = 384.0f;
    streamingCfg.maxConcurrentLoads = 32;
    streamingCfg.maxConcurrentUnloads = 16;
    streamingCfg.enablePrediction = false;
    streamingCfg.allowPlaceholderCellLoad = options_.allowPlaceholderCells;
    streamingCfg.cellDataDirectory = L"data\\world\\cells";
    streamingCfg.cellFileExtension = L".ncell";
    streamingCfg.logStreamingEvents = true;
    if (!streaming_->Initialize(streamingCfg)) {
        NEXT_LOG_WARNING("World streaming failed to initialize (continuing without streaming)");
        streaming_.reset();
    }

    running_ = true;
    NEXT_LOG_INFO("Engine initialized");

    return true;
}

void Game::Shutdown() {
    if (running_) {
        running_ = false;
        ShutdownEngine();
        NEXT_LOG_INFO("Game shutdown complete");
    }
}

void Game::ShutdownEngine() {
    // Cleanup in reverse order
    NEXT_LOG_INFO("Shutting down engine...");

    if (renderer_) {
        renderer_->Shutdown();
        delete renderer_;
        renderer_ = nullptr;
    }

    Next::JobSystem::Instance().Shutdown();

    // Shutdown asset manager (CP3)
    Next::AssetManager::Instance().Shutdown();

    world_.reset();

    if (streaming_) {
        streaming_->Shutdown();
        streaming_.reset();
    }

    if (window_) {
        window_->Shutdown();
        delete window_;
        window_ = nullptr;
    }

    Next::Logger::Shutdown();
    Next::PlatformShutdown();
}

void Game::Run() {
    NEXT_LOG_INFO("Starting game loop");

    if (!running_) {
        NEXT_LOG_ERROR("Game not initialized; call Initialize() first");
        return;
    }

    auto* window = window_;
    auto* input = input_;
    auto& profiler = Next::Profiler::Instance();
    auto& jobSystem = Next::JobSystem::Instance();

    if (options_.runSelfTests) {
        RunJobSystemSelfTest();
        RunAssetSystemTest();
        RunECSSelfTest();
    }

    double previousTime = Next::GetTimeInSeconds();
    const double smokeStart = previousTime;
    uint32_t frameCount = 0;
    uint32_t totalFrames = 0;
    const uint32_t LOG_INTERVAL_FRAMES = 60; // Log stats every 60 frames (approx 1 second at 60fps)

    while (!window->ShouldClose() && running_) {
        profiler.BeginFrame();

        double currentTime = Next::GetTimeInSeconds();
        float deltaTime = static_cast<float>(currentTime - previousTime);
        previousTime = currentTime;

        // Poll window events
        window->PollEvents();

        // Update input
        input->Update();

        // Handle exit
        if (input->IsKeyJustPressed(Next::KeyCode::Escape)) {
            running_ = false;
        }

        // Tick game
        Tick(deltaTime);

        // Main thread helps consume budgeted jobs (up to 0.25ms)
        jobSystem.Pump(0.25);

        // Cap framerate at 120 FPS for development
        if (deltaTime < 0.00833f) {
            Next::SleepMs(static_cast<uint32_t>((0.00833f - deltaTime) * 1000.0f));
        }

        profiler.EndFrame();

        // Log stats periodically
        frameCount++;
        totalFrames++;
        if (frameCount >= LOG_INTERVAL_FRAMES) {
            profiler.LogStats();
            frameCount = 0;
        }

        if (options_.smokeFrames > 0 && totalFrames >= options_.smokeFrames) {
            running_ = false;
        }
        if (options_.smokeSeconds > 0.0 && (currentTime - smokeStart) >= options_.smokeSeconds) {
            running_ = false;
        }
    }

    NEXT_LOG_INFO("Game loop finished");
}

void Game::Tick(float deltaTime) {
    HandleInput(deltaTime);
    UpdateGame(deltaTime);
    Render();
}

void Game::HandleInput(float deltaTime) {
    auto* input = Next::GetInput();

    // WASD movement (minimal camera proxy for driving streaming)
    const float speed = 240.0f;  // units/sec
    if (input->IsKeyPressed(Next::KeyCode::W)) {
        camZ_ += speed * deltaTime;
    }
    if (input->IsKeyPressed(Next::KeyCode::S)) {
        camZ_ -= speed * deltaTime;
    }
    if (input->IsKeyPressed(Next::KeyCode::A)) {
        camX_ -= speed * deltaTime;
    }
    if (input->IsKeyPressed(Next::KeyCode::D)) {
        camX_ += speed * deltaTime;
    }
}

void Game::UpdateGame(float deltaTime) {
    if (world_) {
        world_->Update(deltaTime);
    }

    if (streaming_) {
        const Next::Vec3 pos(camX_, camY_, camZ_);
        const Next::Vec3 last(lastCamX_, lastCamY_, lastCamZ_);
        const Next::Vec3 vel = (pos - last) * (deltaTime > 1e-6f ? (1.0f / deltaTime) : 0.0f);
        const Next::Vec3 dir(0.0f, 0.0f, 1.0f);

        streaming_->Update(deltaTime, pos, dir, vel);

        lastCamX_ = camX_;
        lastCamY_ = camY_;
        lastCamZ_ = camZ_;
    }
}

void Game::Render() {
    if (!renderer_ || !window_) {
        return;
    }

    renderer_->BeginFrame();
    renderer_->Render();
    renderer_->EndFrame();
    window_->SwapBuffers();
}

void Game::RunJobSystemSelfTest() {
    auto& jobSystem = Next::JobSystem::Instance();

    NEXT_LOG_INFO("Running JobSystem self-test (CP2 sanity)");
    const int taskCount = 32;
    std::atomic<int> counter{0};

    std::vector<Next::JobHandle> handles;
    handles.reserve(taskCount);

    for (int i = 0; i < taskCount; ++i) {
        auto handle = jobSystem.Submit([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        }, (i % 3 == 0) ? Next::JobPriority::High : Next::JobPriority::Normal, {}, "SelfTest");
        handles.push_back(handle);
    }

    // Wait for completion
    for (auto& h : handles) {
        jobSystem.Wait(h);
    }

    NEXT_LOG_INFO("JobSystem self-test completed: %d/%d tasks finished",
                  counter.load(std::memory_order_relaxed), taskCount);
}

void Game::RunAssetSystemTest() {
    auto& assetManager = Next::AssetManager::Instance();
    auto& jobSystem = Next::JobSystem::Instance();
    
    NEXT_LOG_INFO("Running Asset System test (CP3)");
    NEXT_CPU_SCOPE("AssetSystemTest");
    
    namespace fs = std::filesystem;
    fs::path packagePath = ResolveRuntimePath(fs::path("assets") / "test_package.npkg");
    std::string packageName = packagePath.stem().string();
    
    if (!fs::exists(packagePath)) {
        NEXT_LOG_WARNING("Package %s not found. Please run: build\\bin\\Debug\\next_assetc.exe test assets", packagePath.string().c_str());
        NEXT_LOG_WARNING("Or import a mesh package: build\\bin\\Debug\\next_assetc.exe import SourceAssets\\your.obj assets\\your.npkg");
        return;
    }
    
    if (!assetManager.LoadPackage(packagePath.string())) {
        NEXT_LOG_ERROR("Failed to load package for asset system test");
        return;
    }

    auto loadAndLog = [&](const char* asset) {
        NEXT_CPU_SCOPE("LoadAssetSync");
        auto handle = assetManager.LoadAssetSync(asset);
        if (!handle.IsValid()) {
            NEXT_LOG_ERROR("Failed to load asset: %s", asset);
            return false;
        }
        NEXT_LOG_INFO("Loaded asset: %s (id=%llu)", asset, handle.GetID());
        assetManager.Release(handle);
        return true;
    };

    NEXT_LOG_INFO("Loading assets from %s", packagePath.string().c_str());
    loadAndLog("TestCube");
    loadAndLog("TestChecker");
    loadAndLog("TestPBR");

    // Optional: if an imported package exists, load it too.
    {
        fs::path imported = ResolveRuntimePath(fs::path("assets") / "tri_import.npkg");
        if (fs::exists(imported)) {
            NEXT_LOG_INFO("Loading imported package: %s", imported.string().c_str());
            if (assetManager.LoadPackage(imported.string())) {
                loadAndLog("tri_import");
            }
        }
    }
    
    // Test 1: Synchronous loading simulation
    {
        NEXT_CPU_SCOPE("Test1_SyncLoad");
        NEXT_LOG_INFO("Test 1: Synchronous loading simulation");
        
        // Simulate loading two assets
        auto startTime = Next::GetTimeInSeconds();
        
        // In real implementation, we would load actual assets
        // For CP3 demo, we'll simulate with a small job
        auto loadJob = jobSystem.Submit([]() {
            NEXT_LOG_DEBUG("Simulating asset loading...");
            Next::SleepMs(10); // Simulate IO delay
        }, Next::JobPriority::High, {}, "AssetLoadSim");
        
        jobSystem.Wait(loadJob);
        
        auto endTime = Next::GetTimeInSeconds();
        NEXT_LOG_INFO("  Simulated sync load completed in %.2f ms", (endTime - startTime) * 1000.0);
    }
    
    // Test 2: Reference counting simulation
    {
        NEXT_CPU_SCOPE("Test2_RefCounting");
        NEXT_LOG_INFO("Test 2: Reference counting simulation");
        
        // Simulate loading an asset multiple times
        NEXT_LOG_INFO("  Simulating asset 'TestCube' loaded 3 times...");
        
        // In real implementation, we would track actual ref counts
        NEXT_LOG_INFO("  Reference count simulation: loaded=3, refcount=3");
        
        // Simulate releasing references
        NEXT_LOG_INFO("  Releasing 2 references...");
        NEXT_LOG_INFO("  Reference count simulation: loaded=3, refcount=1");
        
        // Simulate unloading when refcount reaches 0
        NEXT_LOG_INFO("  Releasing final reference...");
        NEXT_LOG_INFO("  Asset marked for cleanup (refcount=0)");
    }
    
    // Test 3: Async loading with JobSystem integration
    {
        NEXT_CPU_SCOPE("Test3_AsyncLoad");
        NEXT_LOG_INFO("Test 3: Async loading with JobSystem integration");
        
        std::atomic<int> asyncLoadCount{0};
        const int asyncLoads = 4;
        
        NEXT_LOG_INFO("  Submitting %d async asset load simulations...", asyncLoads);
        
        std::vector<Next::JobHandle> asyncHandles;
        for (int i = 0; i < asyncLoads; ++i) {
            auto handle = jobSystem.Submit([&asyncLoadCount, i]() {
                NEXT_LOG_DEBUG("Async asset load simulation %d", i);
                Next::SleepMs(5 + (i * 2)); // Varying delays
                asyncLoadCount.fetch_add(1, std::memory_order_relaxed);
            }, Next::JobPriority::Normal, {}, "AsyncAssetLoad");
            
            asyncHandles.push_back(handle);
        }
        
        // Wait for all async loads
        for (auto& handle : asyncHandles) {
            jobSystem.Wait(handle);
        }
        
        NEXT_LOG_INFO("  Async loads completed: %d/%d", 
                     asyncLoadCount.load(std::memory_order_relaxed), asyncLoads);
    }
    
    // Test 4: Asset statistics
    {
        NEXT_CPU_SCOPE("Test4_Stats");
        NEXT_LOG_INFO("Test 4: Asset statistics reporting");
        
        // Get simulated stats
        auto stats = assetManager.GetStats();
        
        NEXT_LOG_INFO("  Asset Manager Statistics:");
        NEXT_LOG_INFO("    Loaded assets: %llu", stats.loadedAssets);
        NEXT_LOG_INFO("    Total memory: %llu bytes", stats.totalMemory);
        NEXT_LOG_INFO("    Pending loads: %llu", stats.pendingLoads);
        NEXT_LOG_INFO("    Failed loads: %llu", stats.failedLoads);
    }
    
    assetManager.UnloadPackage(packageName);

    NEXT_LOG_INFO("Asset System test completed (CP3 demo)");
}

void Game::RunECSSelfTest() {
    NEXT_LOG_INFO("Running ECS self-test (CP4)");
    NEXT_CPU_SCOPE("ECSSelfTest");

    using namespace Next;

    // Create world
    World world;
    NEXT_LOG_INFO("World created");

    // Test 1: Basic entity creation
    {
        NEXT_CPU_SCOPE("Test1_EntityCreation");
        NEXT_LOG_INFO("Test 1: Basic entity creation");

        auto startTime = Next::GetTimeInSeconds();

        std::vector<Entity> entities;
        for (int i = 0; i < 100; ++i) {
            Entity e = world.CreateEntity();
            entities.push_back(e);
        }

        auto endTime = Next::GetTimeInSeconds();
        NEXT_LOG_INFO("  Created 100 entities in %.2f ms", (endTime - startTime) * 1000.0);
        NEXT_LOG_INFO("  World entity count: %llu", world.GetEntityCount());
    }

    // Test 2: Component operations
    {
        NEXT_CPU_SCOPE("Test2_ComponentOperations");
        NEXT_LOG_INFO("Test 2: Component operations");

        auto startTime = Next::GetTimeInSeconds();

        // Create entity with components
        Entity e1 = world.CreateEntity();
        world.AddComponent<NameComponent>(e1, "TestEntity1");
        world.AddComponent<TransformComponent>(e1);

        Entity e2 = world.CreateEntity();
        world.AddComponent<NameComponent>(e2, "TestEntity2");
        world.AddComponent<TransformComponent>(e2);
        world.AddComponent<HierarchyComponent>(e2);

        // Check components
        bool hasTransform = world.HasComponent<TransformComponent>(e1);
        bool hasHierarchy = world.HasComponent<HierarchyComponent>(e1);
        bool e2HasHierarchy = world.HasComponent<HierarchyComponent>(e2);

        auto endTime = Next::GetTimeInSeconds();
        NEXT_LOG_INFO("  Component operations in %.2f ms", (endTime - startTime) * 1000.0);

        const char* hasTransformStr = hasTransform ? "yes" : "no";
        const char* hasHierarchyStr = hasHierarchy ? "yes" : "no";
        NEXT_LOG_INFO("  e1 has Transform: %s, has Hierarchy: %s", hasTransformStr, hasHierarchyStr);

        const char* e2HasHierarchyStr = e2HasHierarchy ? "yes" : "no";
        NEXT_LOG_INFO("  e2 has Hierarchy: %s", e2HasHierarchyStr);
    }

    // Test 3: Query entities
    {
        NEXT_CPU_SCOPE("Test3_QueryEntities");
        NEXT_LOG_INFO("Test 3: Query entities with components");

        auto startTime = Next::GetTimeInSeconds();

        // Create entities with various components
        for (int i = 0; i < 50; ++i) {
            Entity e = world.CreateEntity();
            world.AddComponent<TransformComponent>(e);

            if (i % 2 == 0) {
                world.AddComponent<NameComponent>(e, "EvenEntity");
            }
            if (i % 3 == 0) {
                world.AddComponent<HierarchyComponent>(e);
            }
        }

        // Query entities with TransformComponent
        auto withTransform = world.QueryEntitiesWith<TransformComponent>();
        NEXT_LOG_INFO("  Entities with Transform: %llu", withTransform.size());

        // Query entities with both Transform and Name
        auto withTransformAndName = world.QueryEntitiesWith<TransformComponent, NameComponent>();
        NEXT_LOG_INFO("  Entities with Transform + Name: %llu", withTransformAndName.size());

        auto endTime = Next::GetTimeInSeconds();
        NEXT_LOG_INFO("  Query completed in %.2f ms", (endTime - startTime) * 1000.0);
    }

    // Test 4: Entity destruction
    {
        NEXT_CPU_SCOPE("Test4_EntityDestruction");
        NEXT_LOG_INFO("Test 4: Entity destruction");

        size_t beforeCount = world.GetEntityCount();

        // Destroy some entities
        auto entities = world.GetAllEntities();
        if (entities.size() > 10) {
            for (size_t i = 0; i < 10; ++i) {
                world.DestroyEntity(entities[i]);
            }
        }

        size_t afterCount = world.GetEntityCount();
        NEXT_LOG_INFO("  Before: %llu entities, After: %llu entities", beforeCount, afterCount);
    }

    // Test 5: Stress test with 10,000 entities
    {
        NEXT_CPU_SCOPE("Test5_StressTest");
        NEXT_LOG_INFO("Test 5: Stress test (10,000 entities)");

        auto startTime = Next::GetTimeInSeconds();

        World stressWorld;
        std::vector<Entity> stressEntities;

        // Create entities
        for (int i = 0; i < 10000; ++i) {
            Entity e = stressWorld.CreateEntity();
            stressEntities.push_back(e);

            // Add components
            stressWorld.AddComponent<TransformComponent>(e);
            auto* transform = stressWorld.GetComponent<TransformComponent>(e);
            if (transform) {
                transform->position[0] = (float)i;
            }

            if (i % 2 == 0) {
                stressWorld.AddComponent<NameComponent>(e);
            }
        }

        auto createEndTime = Next::GetTimeInSeconds();
        NEXT_LOG_INFO("  Created 10,000 entities in %.2f ms", (createEndTime - startTime) * 1000.0);

        // Query test
        auto queryStartTime = Next::GetTimeInSeconds();
        auto results = stressWorld.QueryEntitiesWith<TransformComponent>();
        auto queryEndTime = Next::GetTimeInSeconds();

        NEXT_LOG_INFO("  Query returned %llu entities in %.2f ms",
                     results.size(), (queryEndTime - queryStartTime) * 1000.0);

        // Update test (simulate 60 frames)
        auto updateStartTime = Next::GetTimeInSeconds();
        for (int frame = 0; frame < 60; ++frame) {
            for (auto& e : results) {
                auto* transform = stressWorld.GetComponent<TransformComponent>(e);
                if (transform) {
                    transform->position[1] += 0.01f; // Simple animation
                }
            }
        }
        auto updateEndTime = Next::GetTimeInSeconds();

        NEXT_LOG_INFO("  60 frames update in %.2f ms (avg: %.2f ms/frame)",
                     (updateEndTime - updateStartTime) * 1000.0,
                     (updateEndTime - updateStartTime) * 1000.0 / 60.0);

        // World stats
        auto stats = stressWorld.GetStats();
        NEXT_LOG_INFO("  World Stats:");
        NEXT_LOG_INFO("    Entities: %llu", stats.entityCount);
        NEXT_LOG_INFO("    Components: %llu", stats.totalComponents);
        NEXT_LOG_INFO("    Systems: %llu", stats.systemCount);

        auto totalTime = Next::GetTimeInSeconds();
        NEXT_LOG_INFO("  Total stress test time: %.2f ms", (totalTime - startTime) * 1000.0);
    }

    NEXT_LOG_INFO("ECS self-test completed (CP4)");
}

} // namespace Song
