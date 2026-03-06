#include "next/platform/input.h"
#include "next/platform/window.h"
#include <windows.h>
#include <windowsx.h>

namespace Next {

class InputWin32 : public Input {
public:
    InputWin32() {
        // Initialize key states
        for (int i = 0; i < static_cast<int>(KeyCode::Last); ++i) {
            currentKeyStates_[i] = false;
            previousKeyStates_[i] = false;
        }
        for (int i = 0; i < static_cast<int>(MouseButton::Last); ++i) {
            currentMouseStates_[i] = false;
            previousMouseStates_[i] = false;
        }
    }

    void Update() override {
        // Update previous states
        for (int i = 0; i < static_cast<int>(KeyCode::Last); ++i) {
            previousKeyStates_[i] = currentKeyStates_[i];
        }
        for (int i = 0; i < static_cast<int>(MouseButton::Last); ++i) {
            previousMouseStates_[i] = currentMouseStates_[i];
        }

        // Get current keyboard state
        BYTE keyState[256];
        GetKeyboardState(keyState);

        currentKeyStates_[static_cast<int>(KeyCode::W)] = (keyState[0x57] & 0x80) != 0;
        currentKeyStates_[static_cast<int>(KeyCode::A)] = (keyState[0x41] & 0x80) != 0;
        currentKeyStates_[static_cast<int>(KeyCode::S)] = (keyState[0x53] & 0x80) != 0;
        currentKeyStates_[static_cast<int>(KeyCode::D)] = (keyState[0x44] & 0x80) != 0;
        currentKeyStates_[static_cast<int>(KeyCode::Q)] = (keyState[0x51] & 0x80) != 0;
        currentKeyStates_[static_cast<int>(KeyCode::E)] = (keyState[0x45] & 0x80) != 0;
        currentKeyStates_[static_cast<int>(KeyCode::R)] = (keyState[0x52] & 0x80) != 0;
        currentKeyStates_[static_cast<int>(KeyCode::Space)] = (keyState[VK_SPACE] & 0x80) != 0;
        currentKeyStates_[static_cast<int>(KeyCode::Enter)] = (keyState[VK_RETURN] & 0x80) != 0;
        currentKeyStates_[static_cast<int>(KeyCode::Escape)] = (keyState[VK_ESCAPE] & 0x80) != 0;
        currentKeyStates_[static_cast<int>(KeyCode::Tab)] = (keyState[VK_TAB] & 0x80) != 0;
        currentKeyStates_[static_cast<int>(KeyCode::Left)] = (keyState[VK_LEFT] & 0x80) != 0;
        currentKeyStates_[static_cast<int>(KeyCode::Right)] = (keyState[VK_RIGHT] & 0x80) != 0;
        currentKeyStates_[static_cast<int>(KeyCode::Up)] = (keyState[VK_UP] & 0x80) != 0;
        currentKeyStates_[static_cast<int>(KeyCode::Down)] = (keyState[VK_DOWN] & 0x80) != 0;

        // Get current mouse state
        currentMouseStates_[static_cast<int>(MouseButton::Left)] = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
        currentMouseStates_[static_cast<int>(MouseButton::Right)] = (GetKeyState(VK_RBUTTON) & 0x8000) != 0;
        currentMouseStates_[static_cast<int>(MouseButton::Middle)] = (GetKeyState(VK_MBUTTON) & 0x8000) != 0;

        // Get mouse position
        POINT pos;
        GetCursorPos(&pos);
        prevMouseX_ = mouseX_;
        prevMouseY_ = mouseY_;
        mouseX_ = pos.x;
        mouseY_ = pos.y;
    }

    bool IsKeyPressed(KeyCode key) const override {
        return currentKeyStates_[static_cast<int>(key)];
    }

    bool IsKeyJustPressed(KeyCode key) const override {
        return currentKeyStates_[static_cast<int>(key)] && !previousKeyStates_[static_cast<int>(key)];
    }

    bool IsMouseButtonPressed(MouseButton button) const override {
        return currentMouseStates_[static_cast<int>(button)];
    }

    bool IsMouseButtonJustPressed(MouseButton button) const override {
        return currentMouseStates_[static_cast<int>(button)] && !previousMouseStates_[static_cast<int>(button)];
    }

    int GetMouseX() const override { return mouseX_; }
    int GetMouseY() const override { return mouseY_; }
    int GetMouseDeltaX() const override { return mouseX_ - prevMouseX_; }
    int GetMouseDeltaY() const override { return mouseY_ - prevMouseY_; }

    void SetMousePosition(int x, int y) override {
        SetCursorPos(x, y);
        mouseX_ = x;
        mouseY_ = y;
    }

    void Reset() override {
        // Reset all states to initial values
        for (int i = 0; i < static_cast<int>(KeyCode::Last); ++i) {
            currentKeyStates_[i] = false;
            previousKeyStates_[i] = false;
        }
        for (int i = 0; i < static_cast<int>(MouseButton::Last); ++i) {
            currentMouseStates_[i] = false;
            previousMouseStates_[i] = false;
        }
        mouseX_ = 0;
        mouseY_ = 0;
        prevMouseX_ = 0;
        prevMouseY_ = 0;
    }

private:
    bool currentKeyStates_[static_cast<int>(KeyCode::Last)];
    bool previousKeyStates_[static_cast<int>(KeyCode::Last)];
    bool currentMouseStates_[static_cast<int>(MouseButton::Last)];
    bool previousMouseStates_[static_cast<int>(MouseButton::Last)];
    int mouseX_ = 0;
    int mouseY_ = 0;
    int prevMouseX_ = 0;
    int prevMouseY_ = 0;
};

static InputWin32* g_input = nullptr;

Input* GetInput() {
    if (!g_input) {
        g_input = new InputWin32();
    }
    return g_input;
}

void ShutdownInput() {
    if (g_input) {
        delete g_input;
        g_input = nullptr;
    }
}

} // namespace Next
