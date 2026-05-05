#if !defined(_WIN32)

#include "next/platform/input.h"
#include <cstdio>

namespace Next {

class NullInput : public Input {
public:
    void Update() override {}
    bool IsKeyPressed(KeyCode) const override { return false; }
    bool IsKeyJustPressed(KeyCode) const override { return false; }
    bool IsMouseButtonPressed(MouseButton) const override { return false; }
    bool IsMouseButtonJustPressed(MouseButton) const override { return false; }
    int GetMouseX() const override { return 0; }
    int GetMouseY() const override { return 0; }
    int GetMouseDeltaX() const override { return 0; }
    int GetMouseDeltaY() const override { return 0; }
    void SetMousePosition(int, int) override {}
    void Reset() override {}
};

Input* GetInput() {
    static NullInput input;
    return &input;
}

void ShutdownInput() {
    std::fprintf(stderr, "[WARN] Input system is not implemented on this platform.\n");
}

} // namespace Next

#endif
