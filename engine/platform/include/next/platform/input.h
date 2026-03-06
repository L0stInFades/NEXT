#pragma once

#include <cstdint>

namespace Next {

enum class KeyCode {
    Unknown = 0,
    Space, Enter, Escape, Tab,
    W, A, S, D, Q, E, R, F, G, H, J, K, L, Z, X, C, V, B, N, M,
    Left, Right, Up, Down,
    Key0, Key1, Key2, Key3, Key4, Key5, Key6, Key7, Key8, Key9,
    Last
};

enum class MouseButton {
    Left = 0,
    Right,
    Middle,
    Last
};

class Input {
public:
    virtual ~Input() = default;

    virtual void Update() = 0;

    virtual bool IsKeyPressed(KeyCode key) const = 0;
    virtual bool IsKeyJustPressed(KeyCode key) const = 0;

    virtual bool IsMouseButtonPressed(MouseButton button) const = 0;
    virtual bool IsMouseButtonJustPressed(MouseButton button) const = 0;

    virtual int GetMouseX() const = 0;
    virtual int GetMouseY() const = 0;
    virtual int GetMouseDeltaX() const = 0;
    virtual int GetMouseDeltaY() const = 0;

    virtual void SetMousePosition(int x, int y) = 0;
    virtual void Reset() = 0;
};

Input* GetInput();
void ShutdownInput();

} // namespace Next
