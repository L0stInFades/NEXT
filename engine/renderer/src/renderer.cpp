#include "next/renderer/renderer.h"
#include "next/renderer/dx12/dx12_renderer.h"
#include "next/foundation/logger.h"
#include "next/platform/window.h"

namespace Next {

Renderer* Renderer::Create() {
#ifdef _WIN32
    return new DX12Renderer();
#else
    NEXT_LOG_ERROR("No renderer implementation available for this platform");
    return nullptr;
#endif
}

} // namespace Next
