#pragma once

#include <cstdint>
#include <functional>
#include <utility>

namespace Next {

struct WindowDesc {
    const char* title = "NEXT Engine";
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool resizable = true;
};

using WindowResizeCallback = std::function<void(int width, int height)>;
// Platform-agnostic message hook. On Windows, nativeWindow is HWND and message is a Win32 message id.
// Return true if handled; outResult maps to platform return value (e.g. LRESULT on Windows).
using WindowMessageCallback =
    std::function<bool(void* nativeWindow, uint32_t message, uint64_t wParam, int64_t lParam, int64_t* outResult)>;

class Window {
public:
    virtual ~Window() = default;

    virtual bool Initialize(const WindowDesc& desc) = 0;
    virtual void Shutdown() = 0;

    virtual void PollEvents() = 0;
    virtual void SwapBuffers() = 0;

    virtual void SetTitle(const char* title) = 0;
    virtual void Resize(int width, int height) = 0;

    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
    virtual bool ShouldClose() const = 0;

    virtual void* GetNativeHandle() const = 0;

    void SetResizeCallback(WindowResizeCallback callback) {
        resizeCallback_ = std::move(callback);
    }

    void SetMessageCallback(WindowMessageCallback callback) {
        messageCallback_ = std::move(callback);
    }

protected:
    WindowResizeCallback resizeCallback_;
    WindowMessageCallback messageCallback_;
};

Window* CreateWindow();

} // namespace Next
