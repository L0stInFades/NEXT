#pragma once

#include "next/runtime/entity.h"
#include <cstdint>

namespace Next {

struct TransformComponent {
    float position[3] = {0.0f, 0.0f, 0.0f};
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // quaternion
    float scale[3] = {1.0f, 1.0f, 1.0f};
    Entity parent;
};

} // namespace Next
