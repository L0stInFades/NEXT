#pragma once

#include <cstdint>
#include <memory>

namespace Next {
class Window;
class Renderer;
class Input;
class World;
}

namespace Next::Streaming {
class StreamingManager;
}

namespace Song {

struct GameOptions {
    bool runSelfTests = false;
    bool allowPlaceholderCells = false;
    uint32_t smokeFrames = 0;
    double smokeSeconds = 0.0;
};

class Game {
public:
    explicit Game(const GameOptions& options = {});
    ~Game();

    bool Initialize();
    void Shutdown();

    void Run();
    void Tick(float deltaTime);

private:
    bool InitializeEngine();
    void ShutdownEngine();

    void HandleInput(float deltaTime);
    void UpdateGame(float deltaTime);
    void Render();
    void RunJobSystemSelfTest();
    void RunAssetSystemTest();
    void RunECSSelfTest();

    GameOptions options_;
    bool running_;
    Next::Window* window_ = nullptr;
    Next::Renderer* renderer_ = nullptr;
    Next::Input* input_ = nullptr;
    std::unique_ptr<Next::World> world_;

    // CP7: World Streaming integration (kept in Game until Runtime owns it).
    std::unique_ptr<Next::Streaming::StreamingManager> streaming_;

    // Minimal camera state for driving streaming in the demo.
    float camX_ = 0.0f;
    float camY_ = 0.0f;
    float camZ_ = 0.0f;
    float lastCamX_ = 0.0f;
    float lastCamY_ = 0.0f;
    float lastCamZ_ = 0.0f;
};

} // namespace Song
