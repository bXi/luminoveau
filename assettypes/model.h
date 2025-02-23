#pragma once

#include <string>
#include <vector>
#include "SDL3/SDL_gpu.h"
#include "utils/vectors.h" // Assuming vi2d is defined here
#include "texture.h"       // For TextureAsset

/**
 * @brief Represents a 3D model asset for rendering using SDL_GPU.
 */
struct ModelAsset {
    std::string filename; /**< Filename of the model file (e.g., .obj). */

    struct Vertex {
        float pos[3];  /**< Vertex position (x, y, z). */
        float norm[3]; /**< Vertex normal (x, y, z). */
        float uv[2];   /**< Texture coordinates (u, v). */
    };
    std::vector<Vertex> vertices;      /**< Vertex data (positions, normals, UVs). */
    std::vector<uint32_t> indices;     /**< Index data for indexed drawing. */
    SDL_GPUBuffer* vertexBuffer = nullptr; /**< Pointer to the SDL_GPU vertex buffer. */
    SDL_GPUBuffer* indexBuffer = nullptr;  /**< Pointer to the SDL_GPU index buffer. */
    uint32_t indexCount = 0;           /**< Number of indices (triangles * 3). */

    TextureAsset texture;              /**< Associated texture for the model. */

    /**
     * @brief Retrieves the number of vertices in the model.
     * @return The vertex count.
     */
    size_t getVertexCount() const {
        return vertices.size();
    }

    /**
     * @brief Retrieves the number of triangles in the model.
     * @return The triangle count (indices / 3).
     */
    size_t getTriangleCount() const {
        return indices.size() / 3;
    }

    /**
     * @brief Releases the GPU resources associated with the model.
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
        texture.release(device); // Delegate texture cleanup
    }
};

using Model = ModelAsset&;