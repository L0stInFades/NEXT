#if !defined(_WIN32) && !defined(__APPLE__)

#include "next/platform/window.h"
#include <cstdio>

namespace Next {

class UnsupportedWindow : public Window {
public:
    bool Initialize(const WindowDesc&) override {
        return false;
    }

    void Shutdown() override {}

    void PollEvents() override {}
    void SwapBuffers() override {}

    void SetTitle(const char*) override {}
    void Resize(int, int) override {}

    int GetWidth() const override { return 0; }
    int GetHeight() const override { return 0; }
    bool ShouldClose() const override { return true; }
    void* GetNativeHandle() const override { return nullptr; }
};

Window* CreateWindow() {
    std::fprintf(stderr, "[WARN] CreateWindow is not implemented on this platform (fallback stub).\n");
    return new UnsupportedWindow();
}

} // namespace Next

#endif
