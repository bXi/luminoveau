#pragma once

#include <vector>
#include "SDL3/SDL.h"
#include "utils/vectors.h"
#include "assettypes/texture.h"

struct Vertex3D {
    float x, y, z;           // Position
    float nx, ny, nz;        // Normal
    float u, v;              // Texture coordinates
    float r, g, b, a;        // Vertex color (optional)
};

/**
 * @brief Defines which face of the cube
 */
enum class CubeFace {
    Front,   // +Z
    Back,    // -Z
    Top,     // +Y
    Bottom,  // -Y
    Right,   // +X
    Left     // -X
};

/**
 * @brief UV coordinates for a single face (min and max)
 */
struct FaceUV {
    float uMin, vMin;  // Bottom-left corner
    float uMax, vMax;  // Top-right corner
    
    FaceUV(float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f)
        : uMin(u0), vMin(v0), uMax(u1), vMax(v1) {}
};

/**
 * @brief Predefined UV layout patterns for cube mapping
 */
enum class CubeUVLayout {
    SingleTexture,  // Each face uses full texture (0,0) to (1,1)
    Atlas4x4,       // 4x4 grid atlas (cross pattern)
    Atlas3x2,       // 3x2 grid atlas (horizontal cross)
    Skybox,         // 6 separate textures stitched horizontally
    Custom          // User provides custom UVs
};

/**
 * @brief Represents a 3D model asset with vertices, indices, and texture.
 */
struct ModelAsset {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    
    TextureAsset texture;  // Texture (defaults to white pixel)
    
    // GPU buffers
    SDL_GPUBuffer* vertexBuffer = nullptr;
    SDL_GPUBuffer* indexBuffer = nullptr;
    SDL_GPUTransferBuffer* vertexTransferBuffer = nullptr;
    SDL_GPUTransferBuffer* indexTransferBuffer = nullptr;
    
    const char* name = nullptr;
    
    /**
     * @brief Gets the number of vertices in the model.
     */
    size_t GetVertexCount() const { return vertices.size(); }
    
    /**
     * @brief Gets the number of indices in the model.
     */
    size_t GetIndexCount() const { return indices.size(); }
    
    /**
     * @brief Sets the UV coordinates for a specific cube face.
     * @param face Which face to modify
     * @param uvs UV coordinates (min and max)
     * @note Only works if the model is a cube with 24 vertices (6 faces x 4 vertices)
     */
    void SetCubeFaceUVs(CubeFace face, const FaceUV& uvs) {
        if (vertices.size() != 24) {
            SDL_Log("Warning: SetCubeFaceUVs called on non-cube model (expected 24 vertices, got %zu)", vertices.size());
            return;
        }
        
        int faceIndex = static_cast<int>(face);
        int baseVertex = faceIndex * 4;
        
        // Set UVs for the 4 vertices of this face
        vertices[baseVertex + 0].u = uvs.uMin; vertices[baseVertex + 0].v = uvs.vMax;  // Bottom-left
        vertices[baseVertex + 1].u = uvs.uMax; vertices[baseVertex + 1].v = uvs.vMax;  // Bottom-right
        vertices[baseVertex + 2].u = uvs.uMax; vertices[baseVertex + 2].v = uvs.vMin;  // Top-right
        vertices[baseVertex + 3].u = uvs.uMin; vertices[baseVertex + 3].v = uvs.vMin;  // Top-left
    }
    
    /**
     * @brief Releases GPU resources.
     * @param device Pointer to the SDL_GPUDevice.
     */
    void release(SDL_GPUDevice* device) {
        if (vertexBuffer) {
            SDL_ReleaseGPUBuffer(device, vertexBuffer);
            vertexBuffer = nullptr;
        }
        if (indexBuffer) {
            SDL_ReleaseGPUBuffer(device, indexBuffer);
            indexBuffer = nullptr;
        }
        if (vertexTransferBuffer) {
            SDL_ReleaseGPUTransferBuffer(device, vertexTransferBuffer);
            vertexTransferBuffer = nullptr;
        }
        if (indexTransferBuffer) {
            SDL_ReleaseGPUTransferBuffer(device, indexTransferBuffer);
            indexTransferBuffer = nullptr;
        }
    }
};

using Model = ModelAsset&;
