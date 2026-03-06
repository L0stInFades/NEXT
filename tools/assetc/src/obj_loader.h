#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Next {

struct ObjMeshVertex {
    float position[3];
    float normal[3];
    float uv[2];
};

struct ObjMesh {
    std::vector<ObjMeshVertex> vertices;
    std::vector<uint32_t> indices;  // always 32-bit; compiler may downcast to 16-bit
    float boundsMin[3] = {0.0f, 0.0f, 0.0f};
    float boundsMax[3] = {0.0f, 0.0f, 0.0f};
    bool hasNormals = false;
    bool hasUVs = false;
};

// Minimal OBJ loader: supports v/vt/vn + f (triangulates n-gons via fan).
// Notes:
// - Materials are ignored (usemtl/mtllib are skipped).
// - If normals are missing, generates smooth normals per position index.
bool LoadObjMesh(const std::string& path, ObjMesh& out, std::string& outError);

} // namespace Next

