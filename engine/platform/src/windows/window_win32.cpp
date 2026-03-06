#include "next/platform/window.h"
#include <windows.h>
#include <windowsx.h>

#ifdef CreateWindow
#undef CreateWindow
#endif

namespace Next {

class WindowWin32 : public Window {
public:
    WindowWin32() : hwnd_(nullptr), width_(0), height_(0), shouldClose_(false) {}
    ~WindowWin32() override { Shutdown(); }

    bool Initialize(const WindowDesc& desc) override {
        WNDCLASSEXA wc = {};
        wc.cbSize = sizeof(WNDCLASSEXA);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.lpszClassName = "NEXT_WINDOW_CLASS";

        if (!RegisterClassExA(&wc)) {
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                return false;
            }
        }

        DWORD style = WS_OVERLAPPEDWINDOW;
        if (desc.fullscreen) {
            style = WS_POPUP | WS_VISIBLE;
        }

        RECT rect = {0, 0, desc.width, desc.height};
        AdjustWindowRect(&rect, style, FALSE);

        hwnd_ = CreateWindowExA(
            0,
            "NEXT_WINDOW_CLASS",
            desc.title,
            style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left,
            rect.bottom - rect.top,
            nullptr, nullptr, GetModuleHandleA(nullptr), this
        );

        if (!hwnd_) {
            return false;
        }

        width_ = desc.width;
        height_ = desc.height;

        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        // After DPI-awareness changes, the initial CreateWindowEx size can differ from the actual client size.
        // Query the real client rect so the renderer swapchain matches what Windows gives us.
        RECT client = {};
        if (GetClientRect(hwnd_, &client)) {
            const int cw = client.right - client.left;
            const int ch = client.bottom - client.top;
            if (cw > 0 && ch > 0) {
                width_ = cw;
                height_ = ch;
            }
        }

        return true;
    }

    void Shutdown() override {
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }

    void PollEvents() override {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    void SwapBuffers() override {
        // SwapBuffers requires DirectX/OpenGL context integration
        // This will be implemented when the renderer subsystem is connected
        // For now, window operates in headless mode
    }

    void SetTitle(const char* title) override {
        if (hwnd_) {
            SetWindowTextA(hwnd_, title);
        }
    }

    void Resize(int width, int height) override {
        if (hwnd_) {
            // Treat input as desired *client* size, not outer window size.
            // This avoids mismatch between renderer backbuffer and the visible client area (blur/mouse offset),
            // and stays correct with DPI-aware windows where non-client metrics change with DPI.
            RECT rect = {0, 0, width, height};
            const DWORD style = static_cast<DWORD>(GetWindowLongPtrA(hwnd_, GWL_STYLE));
            const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrA(hwnd_, GWL_EXSTYLE));

            // Prefer DPI-aware adjustment when available (Win10+), fall back otherwise.
            UINT dpi = 96;
            if (HMODULE user32 = GetModuleHandleA("user32.dll")) {
                using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
                auto* getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
                if (getDpiForWindow) {
                    dpi = getDpiForWindow(hwnd_);
                }

                using AdjustWindowRectExForDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
                auto* adjustForDpi = reinterpret_cast<AdjustWindowRectExForDpiFn>(GetProcAddress(user32, "AdjustWindowRectExForDpi"));
                if (adjustForDpi) {
                    adjustForDpi(&rect, style, FALSE, exStyle, dpi);
                } else {
                    AdjustWindowRectEx(&rect, style, FALSE, exStyle);
                }
            } else {
                AdjustWindowRectEx(&rect, style, FALSE, exStyle);
            }

            const int outerW = rect.right - rect.left;
            const int outerH = rect.bottom - rect.top;
            SetWindowPos(hwnd_, nullptr, 0, 0, outerW, outerH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

            // Keep cached sizes coherent even if WM_SIZE is delayed.
            // Best-effort: query actual client size after resize.
            RECT client = {};
            if (GetClientRect(hwnd_, &client)) {
                const int cw = client.right - client.left;
                const int ch = client.bottom - client.top;
                if (cw > 0 && ch > 0) {
                    width_ = cw;
                    height_ = ch;
                }
            }
        }
    }

    int GetWidth() const override { return width_; }
    int GetHeight() const override { return height_; }
    bool ShouldClose() const override { return shouldClose_; }

    void* GetNativeHandle() const override { return hwnd_; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        WindowWin32* window = nullptr;

        if (msg == WM_CREATE) {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            window = reinterpret_cast<WindowWin32*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        } else {
            window = reinterpret_cast<WindowWin32*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }

        // Allow clients (e.g. editor UI) to intercept native messages.
        // Do not swallow WM_CLOSE / WM_SIZE / WM_DPICHANGED so engine lifecycle, resize callbacks,
        // and default DPI handling remain reliable.
        if (window && window->messageCallback_) {
            int64_t result = 0;
            bool handled = window->messageCallback_(
                hwnd,
                static_cast<uint32_t>(msg),
                static_cast<uint64_t>(wParam),
                static_cast<int64_t>(lParam),
                &result);
            if (handled && msg != WM_CLOSE && msg != WM_SIZE && msg != WM_DPICHANGED) {
                return static_cast<LRESULT>(result);
            }
        }

        switch (msg) {
            case WM_CLOSE:
                if (window) {
                    window->shouldClose_ = true;
                }
                return 0;
            case WM_SIZE:
                if (window) {
                    window->width_ = LOWORD(lParam);
                    window->height_ = HIWORD(lParam);
                    if (window->resizeCallback_) {
                        window->resizeCallback_(window->width_, window->height_);
                    }
                }
                return 0;
            default:
                return DefWindowProcA(hwnd, msg, wParam, lParam);
        }
    }

    HWND hwnd_;
    int width_;
    int height_;
    bool shouldClose_;
};

Window* CreateWindow() {
    return new WindowWin32();
}

} // namespace Next
