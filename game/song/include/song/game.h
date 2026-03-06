#pragma once

#include <memory>

namespace Next {
class Window;
class Renderer;
class Input;
}

namespace Next::Streaming {
class StreamingManager;
}

namespace Song {

class Game {
public:
    Game();
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

    bool running_;
    Next::Window* window_ = nullptr;
    Next::Renderer* renderer_ = nullptr;
    Next::Input* input_ = nullptr;

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
