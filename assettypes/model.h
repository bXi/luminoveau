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
