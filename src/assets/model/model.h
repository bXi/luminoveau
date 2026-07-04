#pragma once

#include <vector>
#include "gpu/types.h"
#include "math/vectors.h"
#include "assets/texture/texture.h"
#include "core/log/log.h"

/// @cond INTERNAL
struct Vertex3D {
    float x, y, z;     // position
    float nx, ny, nz;  // normal
    float u, v;        // texture coordinates
    float r, g, b, a;  // vertex color
};
/// @endcond

/**
 * @brief Defines which face of the cube
 */
enum class CubeFace {
    Front, Back, Top, Bottom, Right, Left
};

/// @brief A UV rectangle (min/max corners) applied to a cube face.
struct FaceUV {
    float uMin;  ///< Left U coordinate.
    float vMin;  ///< Top V coordinate.
    float uMax;  ///< Right U coordinate.
    float vMax;  ///< Bottom V coordinate.

    /// @brief Constructs a UV rectangle; defaults to the full 0..1 range.
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
    std::vector<Vertex3D> vertices;   ///< CPU-side vertex data.
    std::vector<uint32_t> indices;    ///< CPU-side triangle indices.

    TextureAsset texture;             ///< Model texture (defaults to a white pixel).

    GpuBufferHandle         vertexBuffer         = 0;  ///< GPU vertex buffer handle.
    GpuBufferHandle         indexBuffer          = 0;  ///< GPU index buffer handle.
    GpuTransferBufferHandle vertexTransferBuffer  = 0; ///< Staging buffer for vertex uploads.
    GpuTransferBufferHandle indexTransferBuffer   = 0; ///< Staging buffer for index uploads.

    const char* name = nullptr;       ///< Optional model name.

    /// @brief Returns the number of vertices in the model.
    size_t GetVertexCount() const { return vertices.size(); }
    /// @brief Returns the number of indices in the model.
    size_t GetIndexCount()  const { return indices.size(); }

    /// @brief Sets the UV rectangle for one face of a 24-vertex cube model.
    /// @param face Which cube face to set.
    /// @param uvs The UV rectangle to apply to that face.
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
