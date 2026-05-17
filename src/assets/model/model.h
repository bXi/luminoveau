#pragma once

#include <vector>
#include "gpu/types.h"
#include "math/vectors.h"
#include "assets/texture/texture.h"
#include "core/log/loghandler.h"

struct Vertex3D {
    float x, y, z;     // position
    float nx, ny, nz;  // normal
    float u, v;        // texture coordinates
    float r, g, b, a;  // vertex color
};

/**
 * @brief Defines which face of the cube
 */
enum class CubeFace {
    Front, Back, Top, Bottom, Right, Left
};

struct FaceUV {
    float uMin, vMin;
    float uMax, vMax;

    FaceUV(float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f)
        : uMin(u0), vMin(v0), uMax(u1), vMax(v1) {}
};

enum class CubeUVLayout {
    SingleTexture,
    Atlas4x4,
    Atlas3x2,
    Skybox,
    Custom,
};

/**
 * @brief Represents a 3D model asset with vertices, indices, and GPU buffers.
 */
struct ModelAsset {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    TextureAsset texture;  // defaults to white pixel

    GpuBufferHandle         vertexBuffer         = 0;
    GpuBufferHandle         indexBuffer          = 0;
    GpuTransferBufferHandle vertexTransferBuffer  = 0;
    GpuTransferBufferHandle indexTransferBuffer   = 0;

    const char* name = nullptr;

    size_t GetVertexCount() const { return vertices.size(); }
    size_t GetIndexCount()  const { return indices.size(); }

    void SetCubeFaceUVs(CubeFace face, const FaceUV& uvs) {
        if (vertices.size() != 24) {
            LOG_WARNING("SetCubeFaceUVs called on non-cube model (expected 24 vertices, got {})", vertices.size());
            return;
        }
        int base = static_cast<int>(face) * 4;
        vertices[base + 0].u = uvs.uMin; vertices[base + 0].v = uvs.vMax;
        vertices[base + 1].u = uvs.uMax; vertices[base + 1].v = uvs.vMax;
        vertices[base + 2].u = uvs.uMax; vertices[base + 2].v = uvs.vMin;
        vertices[base + 3].u = uvs.uMin; vertices[base + 3].v = uvs.vMin;
    }
};

using Model = ModelAsset&;
