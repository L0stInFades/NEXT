#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Next {

struct ImageRGBA8 {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // size = width * height * 4 (RGBA)
};

// Minimal TGA loader:
// - Supports true-color uncompressed (imageType=2), 24-bit BGR and 32-bit BGRA
// - Applies origin flag to return pixels in top-left origin order
bool LoadTgaRGBA8(const std::string& path, ImageRGBA8& out, std::string& outError);

} // namespace Next

