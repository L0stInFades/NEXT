//=============================================================================
// Advanced Rendering Features Demo
// Demonstrates GTAO, Light Probes, and Global Illumination
//=============================================================================

#include "next/renderer/dx12/dx12_renderer.h"
#include "next/renderer/dx12/ambient_occlusion.h"
#include "next/renderer/dx12/light_probe.h"
#include "next/renderer/dx12/global_illumination.h"
#include "next/renderer/dx12/rendering_stats.h"
#include "next/platform/window.h"
#include "next/platform/input.h"
#include "next/foundation/logger.h"
#include <iostream>
#include <vector>

using namespace Next;

//=============================================================================
// Demo Configuration
//=============================================================================

struct DemoConfig {
    bool useGTAO = true;
    bool useHBAO = false;
    bool useVXAO = false;
    bool useLightProbes = true;
    bool useGI = true;
    bool showProbeVisualization = false;
    bool showPerformanceStats = true;

    GTAOParameters gtaoParams;
    HBAOParameters hbaoParams;
    GISettings giSettings;

    DemoConfig() {
        // GTAO settings
        gtaoParams.radius = 0.5f;
        gtaoParams.power = 2.0f;
        gtaoParams.samples = 8;
        gtaoParams.temporalStability = 0.9f;
        gtaoParams.bilateralFilter = true;

        // HBAO settings
        hbaoParams.radius = 0.3f;
        hbaoParams.bias = 0.1f;
        hbaoParams.steps = 4;
        hbaoParams.power = 2.0f;
        hbaoParams.blurEnabled = true;

        // GI settings
        giSettings.primaryTechnique = GITechnique::Hybrid;
        giSettings.giIntensity = 1.0f;
        giSettings.indirectIntensity = 0.5f;
        giSettings.aoInfluence = 0.3f;
        giSettings.probeInfluence = 0.7f;
        giSettings.giQuality = 2; // High
        giSettings.enableTemporalAccumulation = true;
        giSettings.enableBilateralFilter = true;
        giSettings.maxBounces = 2;
    }
};

//=============================================================================
// Advanced Rendering Demo
//=============================================================================

class AdvancedRenderingDemo {
public:
    AdvancedRenderingDemo()
        : window_(nullptr)
        , renderer_(nullptr)
        , input_(nullptr)
        , initialized_(false)
        , frameCount_(0)
        , lastToggleTime_(0.0)
    {
    }

    ~AdvancedRenderingDemo() {
        Shutdown();
    }

    bool Initialize() {
        // Initialize logger
        Logger::Initialize();
        NEXT_LOG_INFO("=== Advanced Rendering Demo ===");

        // Create window
        WindowDesc windowDesc;
        windowDesc.title = "Advanced Rendering Demo - GTAO, Light Probes, GI";
        windowDesc.width = 1920;
        windowDesc.height = 1080;
        windowDesc.vsync = false;

        window_ = CreateWindow();
        if (!window_->Initialize(windowDesc)) {
            NEXT_LOG_ERROR("Failed to create window");
            return false;
        }

        // Get input
        input_ = GetInput();
        input_->Reset();

        // Create renderer
        renderer_ = new DX12Renderer();
        if (!renderer_->Initialize(window_)) {
            NEXT_LOG_ERROR("Failed to initialize renderer");
            return false;
        }

        // Setup advanced rendering features
        if (!SetupAdvancedFeatures()) {
            NEXT_LOG_ERROR("Failed to setup advanced features");
            return false;
        }

        // Setup demo scene
        if (!SetupDemoScene()) {
            NEXT_LOG_ERROR("Failed to setup demo scene");
            return false;
        }

        initialized_ = true;
        NEXT_LOG_INFO("Demo initialized successfully");
        PrintControls();

        return true;
    }

    void Shutdown() {
        if (!initialized_) {
            return;
        }

        NEXT_LOG_INFO("Shutting down demo...");

        // Shutdown performance monitor
        if (performanceMonitor_) {
            performanceMonitor_->Shutdown();
        }

        // Renderer cleanup is handled by RAII
        if (renderer_) {
            delete renderer_;
            renderer_ = nullptr;
        }

        if (window_) {
            window_->Shutdown();
            // window_ is managed by platform, don't delete
        }

        // Shutdown input
        ShutdownInput();

        Logger::Shutdown();
        initialized_ = false;
    }

    void Run() {
        if (!initialized_) {
            return;
        }

        NEXT_LOG_INFO("Starting main loop...");

        bool running = true;
        auto lastTime = std::chrono::steady_clock::now();

        while (running) {
            // Calculate delta time
            auto currentTime = std::chrono::steady_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;

            // Handle input
            running = HandleInput(deltaTime);

            // Update
            Update(deltaTime);

            // Render
            Render();

            frameCount_++;
        }
    }

private:
    bool SetupAdvancedFeatures() {
        // Get GI manager
        GIManager* giManager = renderer_->GetGIManager();
        if (!giManager) {
            NEXT_LOG_WARNING("GI manager not available");
            return false;
        }

        // Setup AO
        AmbientOcclusionManager* aoManager = renderer_->GetAOManager();
        if (aoManager) {
            // Set GTAO as default
            aoManager->SetAOType(AOType::GTAO);

            // Apply demo config
            if (config_.useGTAO) {
                aoManager->SetGTAOParameters(config_.gtaoParams);
            } else if (config_.useHBAO) {
                aoManager->SetHBAOParameters(config_.hbaoParams);
                aoManager->SetAOType(AOType::HBAO);
            } else if (config_.useVXAO) {
                aoManager->SetAOType(AOType::VXAO);
            }
        }

        // Setup GI
        giManager->SetSettings(config_.giSettings);

        // Setup light probes
        LightProbeManager* probeManager = renderer_->GetProbeManager();
        if (probeManager && config_.useLightProbes) {
            // Create a probe volume covering the scene
            Vec3 origin(-10.0f, 0.0f, -10.0f);
            Vec3 size(20.0f, 10.0f, 20.0f);
            int probesX = 4;
            int probesY = 2;
            int probesZ = 4;

            LightProbeVolume* volume = probeManager->CreateVolume(
                origin, size, probesX, probesY, probesZ
            );

            if (volume) {
                NEXT_LOG_INFO("Created light probe volume: %dx%dx%d probes",
                              probesX, probesY, probesZ);
            }
        }

        // Setup performance monitoring
        performanceMonitor_ = std::make_unique<PerformanceMonitor>();
        if (!performanceMonitor_->Initialize(renderer_->GetDevice())) {
            NEXT_LOG_WARNING("Failed to initialize performance monitor");
        }

        return true;
    }

    bool SetupDemoScene() {
        // Setup lighting scene
        LightingScene& lightingScene = renderer_->GetLightingScene();

        // Add directional light (sun)
        lightingScene.directionalLight.direction = Vec3(0.3f, -1.0f, 0.5f).Normalize();
        lightingScene.directionalLight.intensity = 1.2f;
        lightingScene.directionalLight.color = Vec3(1.0f, 0.95f, 0.8f);

        // Add point lights
        PointLight pointLight1;
        pointLight1.position = Vec3(-3.0f, 2.0f, 3.0f);
        pointLight1.color = Vec3(1.0f, 0.5f, 0.3f); // Warm orange
        pointLight1.intensity = 2.0f;
        pointLight1.radius = 10.0f;
        lightingScene.AddPointLight(pointLight1);

        PointLight pointLight2;
        pointLight2.position = Vec3(3.0f, 1.5f, -2.0f);
        pointLight2.color = Vec3(0.3f, 0.5f, 1.0f); // Cool blue
        pointLight2.intensity = 1.5f;
        pointLight2.radius = 8.0f;
        lightingScene.AddPointLight(pointLight2);

        // Setup camera
        cameraPosition_ = Vec3(0.0f, 3.0f, -8.0f);
        cameraTarget_ = Vec3(0.0f, 0.0f, 0.0f);
        cameraUp_ = Vec3(0.0f, 1.0f, 0.0f);

        NEXT_LOG_INFO("Demo scene setup complete");
        return true;
    }

    bool HandleInput(float deltaTime) {
        if (!input_) {
            return false;
        }

        // Update input state
        input_->Update();

        // Check for quit
        if (input_->IsKeyPressed(KeyCode::Escape)) {
            NEXT_LOG_INFO("Quit requested");
            return false;
        }

        // Camera controls
        float cameraSpeed = 5.0f * deltaTime;
        Vec3 cameraDir = (cameraTarget_ - cameraPosition_).Normalize();
        Vec3 cameraRight = Vec3(-cameraDir.z, 0.0f, cameraDir.x).Normalize();
        Vec3 cameraUp = cameraUp_;

        if (input_->IsKeyPressed(KeyCode::W)) {
            cameraPosition_ = cameraPosition_ + cameraDir * cameraSpeed;
        }
        if (input_->IsKeyPressed(KeyCode::S)) {
            cameraPosition_ = cameraPosition_ - cameraDir * cameraSpeed;
        }
        if (input_->IsKeyPressed(KeyCode::A)) {
            cameraPosition_ = cameraPosition_ - cameraRight * cameraSpeed;
        }
        if (input_->IsKeyPressed(KeyCode::D)) {
            cameraPosition_ = cameraPosition_ + cameraRight * cameraSpeed;
        }
        if (input_->IsKeyPressed(KeyCode::Q)) {
            cameraPosition_ = cameraPosition_ + Vec3(0.0f, 1.0f, 0.0f) * cameraSpeed;
        }
        if (input_->IsKeyPressed(KeyCode::E)) {
            cameraPosition_ = cameraPosition_ - Vec3(0.0f, 1.0f, 0.0f) * cameraSpeed;
        }

        // Update target based on mouse
        int mouseDX = input_->GetMouseDeltaX();
        int mouseDY = input_->GetMouseDeltaY();

        if (input_->IsMouseButtonPressed(MouseButton::Right)) {
            // Rotate camera around target
            float rotationSpeed = 0.002f;
            float angleX = mouseDX * rotationSpeed;
            float angleY = mouseDY * rotationSpeed;

            // Orbit camera
            Vec3 toCamera = cameraPosition_ - cameraTarget_;
            float distance = toCamera.Length();

            float currentAngle = atan2(toCamera.x, toCamera.z);
            float newAngle = currentAngle + angleX;

            float elevation = asin(toCamera.y / distance);
            float newElevation = elevation + angleY;
            newElevation = std::max(-1.5f, std::min(1.5f, newElevation));

            cameraPosition_.x = cameraTarget_.x + distance * cos(newElevation) * sin(newAngle);
            cameraPosition_.y = cameraTarget_.y + distance * sin(newElevation);
            cameraPosition_.z = cameraTarget_.z + distance * cos(newElevation) * cos(newAngle);
        }

        // Toggle features (with cooldown)
        auto currentTime = std::chrono::steady_clock::now();
        float timeSinceLastToggle = std::chrono::duration<float>(currentTime - lastToggleTime_).count();

        if (timeSinceLastToggle > 0.5f) {
            bool featureToggled = false;

            // Toggle AO
            if (input_->IsKeyJustPressed(KeyCode::R)) {
                AmbientOcclusionManager* aoManager = renderer_->GetAOManager();
                if (aoManager) {
                    AOType currentType = aoManager->GetCurrentType();
                    AOType newType = static_cast<AOType>((static_cast<int>(currentType) + 1) % 4);
                    aoManager->SetAOType(newType);
                    NEXT_LOG_INFO("Switched AO to: %d", static_cast<int>(newType));
                    featureToggled = true;
                }
            }

            // Toggle GI
            if (input_->IsKeyJustPressed(KeyCode::T)) {
                GIManager* giManager = renderer_->GetGIManager();
                if (giManager) {
                    GITechnique current = config_.giSettings.primaryTechnique;
                    current = static_cast<GITechnique>((static_cast<int>(current) + 1) % 5);
                    config_.giSettings.primaryTechnique = current;
                    giManager->SetGITechnique(current);
                    NEXT_LOG_INFO("Switched GI to: %d", static_cast<int>(current));
                    featureToggled = true;
                }
            }

            // Toggle stats
            if (input_->IsKeyJustPressed(KeyCode::Tab)) {
                config_.showPerformanceStats = !config_.showPerformanceStats;
                NEXT_LOG_INFO("Stats display: %s", config_.showPerformanceStats ? "ON" : "OFF");
                featureToggled = true;
            }

            // Toggle probe visualization
            if (input_->IsKeyJustPressed(KeyCode::P)) {
                config_.showProbeVisualization = !config_.showProbeVisualization;
                NEXT_LOG_INFO("Probe visualization: %s", config_.showProbeVisualization ? "ON" : "OFF");
                featureToggled = true;
            }

            // Quality presets
            if (input_->IsKeyJustPressed(KeyCode::Num1)) {
                ApplyQualityPreset("Low");
                NEXT_LOG_INFO("Quality: Low");
                featureToggled = true;
            }
            if (input_->IsKeyJustPressed(KeyCode::Num2)) {
                ApplyQualityPreset("Medium");
                NEXT_LOG_INFO("Quality: Medium");
                featureToggled = true;
            }
            if (input_->IsKeyJustPressed(KeyCode::Num3)) {
                ApplyQualityPreset("High");
                NEXT_LOG_INFO("Quality: High");
                featureToggled = true;
            }
            if (input_->IsKeyJustPressed(KeyCode::Num4)) {
                ApplyQualityPreset("Ultra");
                NEXT_LOG_INFO("Quality: Ultra");
                featureToggled = true;
            }

            if (featureToggled) {
                lastToggleTime_ = currentTime;
            }
        }

        return true;
    }

    void Update(float deltaTime) {
        // Update camera target
        cameraTarget_ = cameraPosition_ + Vec3(0.0f, 0.0f, 1.0f);

        // Update renderer camera
        // (in real implementation, this would update camera matrices)
    }

    void Render() {
        if (!renderer_) {
            return;
        }

        // Begin frame
        renderer_->BeginFrame();

        // Start performance monitoring
        if (performanceMonitor_) {
            performanceMonitor_->BeginFrame(renderer_->GetCommandList());
        }

        // Render scene
        renderer_->Render();

        // End frame
        renderer_->EndFrame();

        // End performance monitoring
        if (performanceMonitor_) {
            performanceMonitor_->EndFrame(renderer_->GetCommandList());
        }

        // Print stats periodically
        if (performanceMonitor_ && frameCount_ % 60 == 0) {
            const RenderingStats& stats = performanceMonitor_->GetStats();
            NEXT_LOG_INFO("=== Frame %u ===", frameCount_);
            NEXT_LOG_INFO("FPS: %.1f | Frame Time: %.2f ms", stats.fps, stats.frameTime);
            NEXT_LOG_INFO("Draw Calls: %u", stats.drawCalls);
        }
    }

    void ApplyQualityPreset(const std::string& preset) {
        QualityPreset quality;

        if (preset == "Low") {
            quality = QualitySettings::GetLowPreset();
        } else if (preset == "Medium") {
            quality = QualitySettings::GetMediumPreset();
        } else if (preset == "High") {
            quality = QualitySettings::GetHighPreset();
        } else if (preset == "Ultra") {
            quality = QualitySettings::GetUltraPreset();
        } else {
            quality = QualitySettings::GetCustomPreset();
        }

        // Apply to AO
        if (renderer_->GetAOManager()) {
            GTAOParameters params;
            params.samples = quality.aoSamples;
            renderer_->GetAOManager()->SetGTAOParameters(params);
        }

        // Apply to GI
        if (renderer_->GetGIManager()) {
            GISettings settings;
            settings.giQuality = quality.aoSamples > 8 ? 2 : 1;
            renderer_->GetGIManager()->SetSettings(settings);
        }

        NEXT_LOG_INFO("Applied %s quality preset: AO samples=%d, Probe rays=%d",
                      quality.name, quality.aoSamples, quality.probeRays);
    }

    void PrintControls() const {
        NEXT_LOG_INFO("");
        NEXT_LOG_INFO("=== Controls ===");
        NEXT_LOG_INFO("WASD        - Move camera");
        NEXT_LOG_INFO("QE          - Move up/down");
        NEXT_LOG_INFO("Right Mouse - Rotate camera");
        NEXT_LOG_INFO("ESC         - Quit");
        NEXT_LOG_INFO("");
        NEXT_LOG_INFO("Feature Toggles:");
        NEXT_LOG_INFO("R           - Cycle AO type (GTAO/HBAO/VXAO/None)");
        NEXT_LOG_INFO("T           - Cycle GI technique");
        NEXT_LOG_INFO("P           - Toggle probe visualization");
        NEXT_LOG_INFO("TAB         - Toggle performance stats");
        NEXT_LOG_INFO("");
        NEXT_LOG_INFO("Quality Presets:");
        NEXT_LOG_INFO("1           - Low quality");
        NEXT_LOG_INFO("2           - Medium quality");
        NEXT_LOG_INFO("3           - High quality");
        NEXT_LOG_INFO("4           - Ultra quality");
        NEXT_LOG_INFO("=================");
    }

private:
    Window* window_;
    DX12Renderer* renderer_;
    Input* input_;
    std::unique_ptr<PerformanceMonitor> performanceMonitor_;

    DemoConfig config_;
    Vec3 cameraPosition_;
    Vec3 cameraTarget_;
    Vec3 cameraUp_;

    uint64_t frameCount_;
    std::chrono::steady_clock::time_point lastToggleTime_;

    bool initialized_;
};

//=============================================================================
// Main Entry Point
//=============================================================================

int main(int argc, char* argv[])
{
    std::cout << "=== Advanced Rendering Demo ===" << std::endl;
    std::cout << "Features:" << std::endl;
    std::cout << "  - GTAO (Ground Truth Ambient Occlusion)" << std::endl;
    std::cout << "  - HBAO (Horizon-Based Ambient Occlusion)" << std::endl;
    std::cout << "  - VXAO (Voxel-based Ambient Occlusion)" << std::endl;
    std::cout << "  - Light Probes with Spherical Harmonics" << std::endl;
    std::cout << "  - Global Illumination (DDGI/VXGI)" << std::endl;
    std::cout << "  - Performance Monitoring" << std::endl;
    std::cout << "=============================" << std::endl;
    std::cout << std::endl;

    AdvancedRenderingDemo demo;

    if (!demo.Initialize()) {
        std::cerr << "Failed to initialize demo!" << std::endl;
        return 1;
    }

    demo.Run();

    return 0;
}
