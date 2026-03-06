#include "next/runtime/asset/asset_types.h"
#include "next/foundation/logger.h"
#include <string>

namespace Next {

// Utility functions for asset types

const char* AssetTypeToString(AssetType type) {
    switch (type) {
        case AssetType::Unknown: return "Unknown";
        case AssetType::Mesh: return "Mesh";
        case AssetType::Texture: return "Texture";
        case AssetType::Material: return "Material";
        default: return "Invalid";
    }
}

AssetType StringToAssetType(const std::string& str) {
    if (str == "Mesh") return AssetType::Mesh;
    if (str == "Texture") return AssetType::Texture;
    if (str == "Material") return AssetType::Material;
    return AssetType::Unknown;
}

bool ValidateMeshHeader(const MeshHeader& header) {
    if (!header.common.Validate()) {
        NEXT_LOG_ERROR("Invalid common header in mesh: %s", header.common.name);
        return false;
    }
    
    if (header.vertexCount == 0) {
        NEXT_LOG_ERROR("Mesh has zero vertices: %s", header.common.name);
        return false;
    }
    
    if (header.indexCount == 0) {
        NEXT_LOG_ERROR("Mesh has zero indices: %s", header.common.name);
        return false;
    }
    
    if (header.vertexStride < 12) { // At least position (3 floats)
        NEXT_LOG_ERROR("Mesh vertex stride too small: %s", header.common.name);
        return false;
    }
    
    // Validate bounding box
    if (header.boundingBox[0] > header.boundingBox[3] ||
        header.boundingBox[1] > header.boundingBox[4] ||
        header.boundingBox[2] > header.boundingBox[5]) {
        NEXT_LOG_WARNING("Mesh has invalid bounding box: %s", header.common.name);
    }
    
    return true;
}

bool ValidateTextureHeader(const TextureHeader& header) {
    if (!header.common.Validate()) {
        NEXT_LOG_ERROR("Invalid common header in texture: %s", header.common.name);
        return false;
    }
    
    if (header.width == 0 || header.height == 0) {
        NEXT_LOG_ERROR("Texture has zero dimensions: %s", header.common.name);
        return false;
    }
    
    if (header.mipLevels == 0) {
        NEXT_LOG_ERROR("Texture has zero mip levels: %s", header.common.name);
        return false;
    }
    
    if (header.arraySize == 0) {
        NEXT_LOG_ERROR("Texture has zero array size: %s", header.common.name);
        return false;
    }
    
    // Basic format validation
    if (header.format > 200) { // Arbitrary limit for DXGI_FORMAT
        NEXT_LOG_WARNING("Texture has unusual format: %s", header.common.name);
    }
    
    return true;
}

bool ValidateMaterialHeader(const MaterialHeader& header) {
    if (!header.common.Validate()) {
        NEXT_LOG_ERROR("Invalid common header in material: %s", header.common.name);
        return false;
    }
    
    // Materials can have zero textures (unlit materials)
    // Materials can have zero parameters (default values)
    
    return true;
}

uint64_t CalculateCRC64(const void* data, size_t size) {
    // Simple placeholder CRC implementation
    // In production, use a proper CRC64 algorithm
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
    uint64_t crc = 0xFFFFFFFFFFFFFFFF;
    
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0x42F0E1EBA9EA3693;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc ^ 0xFFFFFFFFFFFFFFFF;
}

bool ValidateAssetChecksum(const AssetHeader& header, const void* data) {
    uint64_t calculated = CalculateCRC64(data, header.dataSize);
    if (calculated != header.checksum) {
        NEXT_LOG_ERROR("Checksum mismatch for asset: %s (expected: %llx, got: %llx)", 
                     header.name, header.checksum, calculated);
        return false;
    }
    
    return true;
}

} // namespace Next
