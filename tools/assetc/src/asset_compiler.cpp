#include "asset_compiler.h"
#include "obj_loader.h"
#include "tga_loader.h"
#include "next/foundation/logger.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>

namespace Next {

AssetCompiler::AssetCompiler() {
    NEXT_LOG_DEBUG("AssetCompiler created");
}

AssetCompiler::~AssetCompiler() {
    NEXT_LOG_DEBUG("AssetCompiler destroyed");
}

namespace {

static std::string AssetNameFromPath(const std::string& sourcePath, const std::string& outputPath) {
    std::filesystem::path out(outputPath);
    if (!out.stem().empty()) {
        return out.stem().string();
    }
    std::filesystem::path in(sourcePath);
    return in.stem().string();
}

static void CopyAssetName(char dst[64], const std::string& name) {
    std::memset(dst, 0, 64);
    if (name.empty()) return;
    std::strncpy(dst, name.c_str(), 63);
    dst[63] = '\0';
}

static bool WriteBytes(const std::string& outputPath, const std::vector<uint8_t>& bytes) {
    std::ofstream file(outputPath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.close();
    return file.good();
}

} // namespace

bool AssetCompiler::CompileMesh(const std::string& sourcePath, const std::string& outputPath) {
    NEXT_LOG_INFO("Compiling mesh: %s -> %s", sourcePath.c_str(), outputPath.c_str());

    const std::filesystem::path src(sourcePath);
    const std::string ext = src.extension().string();

    if (ext != ".obj") {
        NEXT_LOG_ERROR("CompileMesh: only .obj is supported right now (got %s). Export OBJ from DCC as a workaround.", ext.c_str());
        return false;
    }

    ObjMesh mesh;
    std::string err;
    if (!LoadObjMesh(sourcePath, mesh, err)) {
        NEXT_LOG_ERROR("CompileMesh: OBJ import failed: %s", err.c_str());
        return false;
    }

    MeshHeader header;
    std::memset(&header, 0, sizeof(header));
    header.common.magic = AssetHeader::MAGIC;
    header.common.version = AssetHeader::CURRENT_VERSION;
    header.common.assetType = AssetType::Mesh;
    CopyAssetName(header.common.name, AssetNameFromPath(sourcePath, outputPath));

    header.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    header.indexCount = static_cast<uint32_t>(mesh.indices.size());
    header.vertexStride = sizeof(ObjMeshVertex); // pos + normal + uv
    header.indexType = (header.vertexCount > 65535) ? 1u : 0u;
    header.vertexFormat = 0;
    header.boundingBox[0] = mesh.boundsMin[0];
    header.boundingBox[1] = mesh.boundsMin[1];
    header.boundingBox[2] = mesh.boundsMin[2];
    header.boundingBox[3] = mesh.boundsMax[0];
    header.boundingBox[4] = mesh.boundsMax[1];
    header.boundingBox[5] = mesh.boundsMax[2];
    header.materialCount = 1;
    header.flags = 0;
    if (mesh.hasNormals) header.flags |= MeshHeader::HAS_NORMALS;
    if (mesh.hasUVs) header.flags |= MeshHeader::HAS_UVS;

    const size_t vertexBytes = mesh.vertices.size() * sizeof(ObjMeshVertex);
    const size_t indexBytes = mesh.indices.size() * ((header.indexType == 0) ? sizeof(uint16_t) : sizeof(uint32_t));
    const uint32_t submeshRange[2] = {0u, header.indexCount};
    const size_t submeshBytes = sizeof(submeshRange);

    header.common.dataSize = static_cast<uint32_t>(vertexBytes + indexBytes + submeshBytes);
    header.common.checksum = 0;

    std::vector<uint8_t> out;
    out.resize(sizeof(MeshHeader) + header.common.dataSize);
    uint8_t* ptr = out.data();
    std::memcpy(ptr, &header, sizeof(MeshHeader));
    ptr += sizeof(MeshHeader);

    std::memcpy(ptr, mesh.vertices.data(), vertexBytes);
    ptr += vertexBytes;

    if (header.indexType == 0) {
        std::vector<uint16_t> idx16;
        idx16.reserve(mesh.indices.size());
        for (uint32_t v : mesh.indices) {
            idx16.push_back(static_cast<uint16_t>(v));
        }
        std::memcpy(ptr, idx16.data(), idx16.size() * sizeof(uint16_t));
        ptr += idx16.size() * sizeof(uint16_t);
    } else {
        std::memcpy(ptr, mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));
        ptr += mesh.indices.size() * sizeof(uint32_t);
    }

    std::memcpy(ptr, submeshRange, submeshBytes);

    if (!WriteBytes(outputPath, out)) {
        NEXT_LOG_ERROR("CompileMesh: failed to write %s", outputPath.c_str());
        return false;
    }
    return true;
}

bool AssetCompiler::CompileTexture(const std::string& sourcePath, const std::string& outputPath) {
    NEXT_LOG_INFO("Compiling texture: %s -> %s", sourcePath.c_str(), outputPath.c_str());

    const std::filesystem::path src(sourcePath);
    const std::string ext = src.extension().string();
    if (ext != ".tga") {
        NEXT_LOG_ERROR("CompileTexture: only .tga is supported right now (got %s). Export TGA as a workaround.", ext.c_str());
        return false;
    }

    ImageRGBA8 img;
    std::string err;
    if (!LoadTgaRGBA8(sourcePath, img, err)) {
        NEXT_LOG_ERROR("CompileTexture: TGA import failed: %s", err.c_str());
        return false;
    }

    TextureHeader header;
    std::memset(&header, 0, sizeof(header));
    header.common.magic = AssetHeader::MAGIC;
    header.common.version = AssetHeader::CURRENT_VERSION;
    header.common.assetType = AssetType::Texture;
    CopyAssetName(header.common.name, AssetNameFromPath(sourcePath, outputPath));

    header.width = static_cast<uint32_t>(img.width);
    header.height = static_cast<uint32_t>(img.height);
    header.depth = 1;
    header.mipLevels = 1;
    header.arraySize = 1;
    header.format = 28; // DXGI_FORMAT_R8G8B8A8_UNORM
    header.flags = 0;

    const size_t pixelBytes = img.pixels.size();
    header.common.dataSize = static_cast<uint32_t>(pixelBytes);
    header.common.checksum = 0;

    std::vector<uint8_t> out;
    out.resize(sizeof(TextureHeader) + pixelBytes);
    std::memcpy(out.data(), &header, sizeof(TextureHeader));
    std::memcpy(out.data() + sizeof(TextureHeader), img.pixels.data(), pixelBytes);

    if (!WriteBytes(outputPath, out)) {
        NEXT_LOG_ERROR("CompileTexture: failed to write %s", outputPath.c_str());
        return false;
    }

    return true;
}

bool AssetCompiler::CompileMaterial(const std::string& sourcePath, const std::string& outputPath) {
    NEXT_LOG_INFO("Compiling material: %s -> %s", sourcePath.c_str(), outputPath.c_str());
    
    // For CP3, generate a test material
    std::vector<uint8_t> materialData = GenerateTestMaterial();
    return WriteAssetFile(outputPath, materialData.data(), materialData.size());
}

bool AssetCompiler::CreatePackage(const std::string& packageName,
                                 const std::vector<std::string>& assetFiles,
                                 const std::string& outputPath) {
    NEXT_LOG_INFO("Creating package: %s with %llu assets", packageName.c_str(), assetFiles.size());
    
    if (assetFiles.empty()) {
        NEXT_LOG_ERROR("No assets specified for package");
        return false;
    }
    
    // Read all asset files
    std::vector<std::vector<uint8_t>> assetData;
    std::vector<AssetEntry> entries;
    
    uint32_t dataOffset = 0;
    
    for (const auto& assetFile : assetFiles) {
        std::vector<uint8_t> data;
        if (!ReadAssetFile(assetFile, data)) {
            NEXT_LOG_ERROR("Failed to read asset file: %s", assetFile.c_str());
            return false;
        }
        
        if (data.size() < sizeof(AssetHeader)) {
            NEXT_LOG_ERROR("Asset file too small: %s", assetFile.c_str());
            return false;
        }
        
        // Extract asset info from header
        AssetHeader header;
        memcpy(&header, data.data(), sizeof(AssetHeader));
        
        if (!header.Validate()) {
            NEXT_LOG_ERROR("Invalid asset header in: %s", assetFile.c_str());
            return false;
        }
        
        // Create asset entry
        AssetEntry entry;
        entry.assetType = header.assetType;
        entry.assetSize = static_cast<uint32_t>(data.size());
        entry.dataOffset = dataOffset;
        entry.compressedSize = 0; // No compression for CP3
        entry.decompressedSize = static_cast<uint32_t>(data.size());
        memset(entry.name, 0, sizeof(entry.name));
        strncpy(entry.name, header.name, sizeof(entry.name) - 1);
        
        entries.push_back(entry);
        assetData.push_back(std::move(data));
        
        dataOffset += static_cast<uint32_t>(data.size());
    }
    
    // Create package header
    PackageHeader packageHeader;
    packageHeader.magic = PackageHeader::MAGIC;
    packageHeader.version = PackageHeader::CURRENT_VERSION;
    packageHeader.assetCount = static_cast<uint32_t>(assetFiles.size());
    packageHeader.indexOffset = sizeof(PackageHeader);
    packageHeader.dataOffset = packageHeader.indexOffset + sizeof(AssetEntry) * packageHeader.assetCount;
    packageHeader.checksum = 0; // Would calculate in real implementation
    memset(packageHeader.name, 0, sizeof(packageHeader.name));
    strncpy(packageHeader.name, packageName.c_str(), sizeof(packageHeader.name) - 1);
    
    return WritePackage(outputPath, packageHeader, entries, assetData);
}

bool AssetCompiler::GenerateTestAssets(const std::string& outputDir) {
    NEXT_LOG_INFO("Generating test assets in: %s", outputDir.c_str());
    
    // Create output directory if it doesn't exist
    std::filesystem::create_directories(outputDir);
    
    // Generate test assets
    std::vector<uint8_t> meshData = GenerateTestMesh();
    std::vector<uint8_t> textureData = GenerateTestTexture();
    std::vector<uint8_t> materialData = GenerateTestMaterial();
    
    // Write individual asset files
    std::string meshPath = outputDir + "/test_cube.mesh";
    std::string texturePath = outputDir + "/test_checker.texture";
    std::string materialPath = outputDir + "/test_pbr.material";
    
    if (!WriteAssetFile(meshPath, meshData.data(), meshData.size())) {
        NEXT_LOG_ERROR("Failed to write mesh asset");
        return false;
    }
    
    if (!WriteAssetFile(texturePath, textureData.data(), textureData.size())) {
        NEXT_LOG_ERROR("Failed to write texture asset");
        return false;
    }
    
    if (!WriteAssetFile(materialPath, materialData.data(), materialData.size())) {
        NEXT_LOG_ERROR("Failed to write material asset");
        return false;
    }
    
    // Create a package containing all test assets
    std::vector<std::string> assetFiles = {meshPath, texturePath, materialPath};
    std::string packagePath = outputDir + "/test_package.npkg";
    
    if (!CreatePackage("TestPackage", assetFiles, packagePath)) {
        NEXT_LOG_ERROR("Failed to create test package");
        return false;
    }
    
    NEXT_LOG_INFO("Test assets generated successfully");
    NEXT_LOG_INFO("  Mesh: %s", meshPath.c_str());
    NEXT_LOG_INFO("  Texture: %s", texturePath.c_str());
    NEXT_LOG_INFO("  Material: %s", materialPath.c_str());
    NEXT_LOG_INFO("  Package: %s", packagePath.c_str());
    
    return true;
}

bool AssetCompiler::WriteAssetFile(const std::string& path, const void* data, size_t size) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        NEXT_LOG_ERROR("Failed to open file for writing: %s", path.c_str());
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data), size);
    file.close();
    
    NEXT_LOG_DEBUG("Wrote asset file: %s (%llu bytes)", path.c_str(), size);
    return true;
}

bool AssetCompiler::ReadAssetFile(const std::string& path, std::vector<uint8_t>& data) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        NEXT_LOG_ERROR("Failed to open file for reading: %s", path.c_str());
        return false;
    }
    
    size_t size = file.tellg();
    file.seekg(0);
    
    data.resize(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    file.close();
    
    NEXT_LOG_DEBUG("Read asset file: %s (%llu bytes)", path.c_str(), size);
    return true;
}

std::vector<uint8_t> AssetCompiler::GenerateTestMesh() {
    // Generate a simple cube mesh (8 vertices, 36 indices)
    MeshHeader header;
    memset(&header, 0, sizeof(MeshHeader));
    header.common.magic = AssetHeader::MAGIC;
    header.common.version = AssetHeader::CURRENT_VERSION;
    header.common.assetType = AssetType::Mesh;
    strncpy(header.common.name, "TestCube", sizeof(header.common.name) - 1);
    
    header.vertexCount = 8;
    header.indexCount = 36;
    header.vertexStride = 32; // Position (3 floats) + Normal (3 floats) + UV (2 floats)
    header.indexType = 0; // uint16
    header.vertexFormat = 0; // Simple format
    header.materialCount = 1;
    header.flags = MeshHeader::HAS_NORMALS | MeshHeader::HAS_UVS;
    
    // Bounding box for unit cube
    header.boundingBox[0] = -0.5f; // minX
    header.boundingBox[1] = -0.5f; // minY
    header.boundingBox[2] = -0.5f; // minZ
    header.boundingBox[3] = 0.5f;  // maxX
    header.boundingBox[4] = 0.5f;  // maxY
    header.boundingBox[5] = 0.5f;  // maxZ
    
    // Simple vertex data (position + normal + uv)
    struct Vertex {
        float pos[3];
        float normal[3];
        float uv[2];
    };
    
    std::vector<Vertex> vertices = {
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
        {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}
    };
    
    // Cube indices (12 triangles)
    uint16_t indices[] = {
        0, 1, 2, 2, 3, 0, // front
        4, 5, 6, 6, 7, 4, // back
        0, 4, 7, 7, 3, 0, // left
        1, 5, 6, 6, 2, 1, // right
        3, 2, 6, 6, 7, 3, // top
        0, 1, 5, 5, 4, 0  // bottom
    };
    
    // Submesh range
    uint32_t submeshRange[] = {0, 36};
    
    // Calculate total size
    size_t vertexDataSize = vertices.size() * sizeof(Vertex);
    size_t indexDataSize = sizeof(indices);
    size_t submeshDataSize = sizeof(submeshRange);
    size_t totalSize = sizeof(MeshHeader) + vertexDataSize + indexDataSize + submeshDataSize;
    
    header.common.dataSize = static_cast<uint32_t>(totalSize - sizeof(MeshHeader));
    header.common.checksum = 0; // Would calculate in real implementation
    
    // Build mesh data
    std::vector<uint8_t> meshData(totalSize);
    uint8_t* ptr = meshData.data();
    
    memcpy(ptr, &header, sizeof(MeshHeader));
    ptr += sizeof(MeshHeader);
    
    memcpy(ptr, vertices.data(), vertexDataSize);
    ptr += vertexDataSize;
    
    memcpy(ptr, indices, indexDataSize);
    ptr += indexDataSize;
    
    memcpy(ptr, submeshRange, submeshDataSize);
    
    NEXT_LOG_DEBUG("Generated test mesh: %s (%llu bytes)", header.common.name, meshData.size());
    return meshData;
}

std::vector<uint8_t> AssetCompiler::GenerateTestTexture() {
    // Generate a simple 4x4 checkerboard texture
    TextureHeader header;
    memset(&header, 0, sizeof(TextureHeader));
    header.common.magic = AssetHeader::MAGIC;
    header.common.version = AssetHeader::CURRENT_VERSION;
    header.common.assetType = AssetType::Texture;
    strncpy(header.common.name, "TestChecker", sizeof(header.common.name) - 1);
    
    header.width = 4;
    header.height = 4;
    header.depth = 1;
    header.mipLevels = 1;
    header.arraySize = 1;
    header.format = 28; // DXGI_FORMAT_R8G8B8A8_UNORM
    header.flags = TextureHeader::SRGB;
    
    // Simple 4x4 checkerboard RGBA data
    uint32_t pixelData[] = {
        0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0x00000000,
        0x00000000, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF,
        0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0x00000000,
        0x00000000, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF
    };
    
    size_t pixelDataSize = sizeof(pixelData);
    size_t totalSize = sizeof(TextureHeader) + pixelDataSize;
    
    header.common.dataSize = static_cast<uint32_t>(pixelDataSize);
    header.common.checksum = 0;
    
    std::vector<uint8_t> textureData(totalSize);
    memcpy(textureData.data(), &header, sizeof(TextureHeader));
    memcpy(textureData.data() + sizeof(TextureHeader), pixelData, pixelDataSize);
    
    NEXT_LOG_DEBUG("Generated test texture: %s (%llu bytes)", header.common.name, textureData.size());
    return textureData;
}

std::vector<uint8_t> AssetCompiler::GenerateTestMaterial() {
    // Generate a simple PBR material
    MaterialHeader header;
    memset(&header, 0, sizeof(MaterialHeader));
    header.common.magic = AssetHeader::MAGIC;
    header.common.version = AssetHeader::CURRENT_VERSION;
    header.common.assetType = AssetType::Material;
    strncpy(header.common.name, "TestPBR", sizeof(header.common.name) - 1);
    
    header.textureCount = 2;
    header.parameterCount = 4;
    header.shaderID = 1; // PBR shader
    header.flags = MaterialHeader::OPAQUE_FLAG;
    
    // Texture references
    TextureRef textureRefs[2];
    memset(textureRefs, 0, sizeof(textureRefs));
    strncpy(textureRefs[0].name, "TestChecker", sizeof(textureRefs[0].name) - 1);
    textureRefs[0].slot = 0;
    textureRefs[0].type = TextureRef::ALBEDO;

    strncpy(textureRefs[1].name, "DefaultNormal", sizeof(textureRefs[1].name) - 1);
    textureRefs[1].slot = 1;
    textureRefs[1].type = TextureRef::NORMAL;
    
    // Material parameters
    MaterialParam params[4];
    memset(params, 0, sizeof(params));

    strncpy(params[0].name, "metallic", sizeof(params[0].name) - 1);
    params[0].type = MaterialParam::FLOAT;
    params[0].value[0] = 0.5f;

    strncpy(params[1].name, "roughness", sizeof(params[1].name) - 1);
    params[1].type = MaterialParam::FLOAT;
    params[1].value[0] = 0.3f;

    strncpy(params[2].name, "baseColor", sizeof(params[2].name) - 1);
    params[2].type = MaterialParam::COLOR;
    params[2].value[0] = 0.8f; // R
    params[2].value[1] = 0.8f; // G
    params[2].value[2] = 0.8f; // B
    params[2].value[3] = 1.0f; // A

    strncpy(params[3].name, "emissive", sizeof(params[3].name) - 1);
    params[3].type = MaterialParam::COLOR;
    params[3].value[0] = 0.0f;
    params[3].value[1] = 0.0f;
    params[3].value[2] = 0.0f;
    params[3].value[3] = 0.0f;
    
    // Calculate sizes
    size_t textureRefsSize = header.textureCount * sizeof(TextureRef);
    size_t paramsSize = header.parameterCount * sizeof(MaterialParam);
    size_t totalSize = sizeof(MaterialHeader) + textureRefsSize + paramsSize;
    
    header.common.dataSize = static_cast<uint32_t>(textureRefsSize + paramsSize);
    header.common.checksum = 0;
    
    // Build material data
    std::vector<uint8_t> materialData(totalSize);
    uint8_t* ptr = materialData.data();
    
    memcpy(ptr, &header, sizeof(MaterialHeader));
    ptr += sizeof(MaterialHeader);
    
    memcpy(ptr, textureRefs, textureRefsSize);
    ptr += textureRefsSize;
    
    memcpy(ptr, params, paramsSize);
    
    NEXT_LOG_DEBUG("Generated test material: %s (%llu bytes)", header.common.name, materialData.size());
    return materialData;
}

bool AssetCompiler::WritePackage(const std::string& path,
                                const PackageHeader& header,
                                const std::vector<AssetEntry>& entries,
                                const std::vector<std::vector<uint8_t>>& assetData) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        NEXT_LOG_ERROR("Failed to open package file: %s", path.c_str());
        return false;
    }
    
    // Write package header
    file.write(reinterpret_cast<const char*>(&header), sizeof(PackageHeader));
    
    // Write asset index
    file.write(reinterpret_cast<const char*>(entries.data()), 
               sizeof(AssetEntry) * entries.size());
    
    // Write asset data
    for (const auto& data : assetData) {
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
    
    file.close();
    
    NEXT_LOG_INFO("Package written: %s (%llu assets, %llu total bytes)", 
                 path.c_str(), entries.size(), 
                 sizeof(PackageHeader) + sizeof(AssetEntry) * entries.size());
    
    for (const auto& entry : entries) {
        NEXT_LOG_DEBUG("  Asset: %s (%s, %u bytes)", 
                      entry.name, 
                      AssetTypeToString(entry.assetType),
                      entry.assetSize);
    }
    
    return true;
}

} // namespace Next
