#pragma once

#include "next/renderer/renderer.h"

namespace Next {

class MetalRenderer final : public Renderer {
public:
    MetalRenderer();
    ~MetalRenderer() override;

    bool Initialize(Window* window) override;
    void Shutdown() override;

    const char* GetBackendName() const override { return "metal"; }

    void SetFrameDesc(const RendererFrameDesc& frame) override;
    void BeginFrame() override;
    void EndFrame() override;
    void Render() override;
    void Resize(int width, int height) override;

private:
    struct Impl;

    Impl* impl_;
    Window* window_;
    int width_;
    int height_;
    bool initialized_;
    bool frameActive_;
    float time_;
    RendererFrameDesc frameDesc_;
};

} // namespace Next
