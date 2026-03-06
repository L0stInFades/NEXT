#pragma once

#include "next/platform/window.h"

namespace Next {

// Simple renderer for CP0 - just clears the screen
class Renderer {
public:
    static Renderer* Create();

    virtual ~Renderer() = default;

    virtual bool Initialize(Window* window) = 0;
    virtual void Shutdown() = 0;

    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void Render() = 0;

    virtual void Resize(int width, int height) = 0;
};

} // namespace Next
