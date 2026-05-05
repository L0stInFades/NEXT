#pragma once

#include "next/platform/window.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Next {

enum class RendererBackend : uint8_t {
    Auto = 0,
    DX12,
    Metal,
    Null,
};

static constexpr size_t kMaxRendererDebugCells = 256;
static constexpr uint32_t kRendererDebugCellPlaceholder = 1u << 0;

struct RendererDebugCell {
    float center[3] = {0.0f, 0.0f, 0.0f};
    float size = 64.0f;
    uint32_t flags = 0;
};

struct RendererFrameDesc {
    float cameraPosition[3] = {0.0f, 0.0f, -5.0f};
    float cameraTarget[3] = {0.0f, 0.0f, 0.0f};
    float cameraUp[3] = {0.0f, 1.0f, 0.0f};
    float deltaSeconds = 1.0f / 60.0f;
    std::vector<RendererDebugCell> debugCells;
};

class Renderer {
public:
    static Renderer* Create(RendererBackend preferredBackend = RendererBackend::Auto);
    static const char* BackendToString(RendererBackend backend);
    static RendererBackend ParseBackend(const char* name);

    virtual ~Renderer() = default;

    virtual bool Initialize(Window* window) = 0;
    virtual void Shutdown() = 0;

    virtual const char* GetBackendName() const = 0;

    virtual void SetFrameDesc(const RendererFrameDesc& frame) { (void)frame; }

    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void Render() = 0;

    virtual void Resize(int width, int height) = 0;
};

} // namespace Next
