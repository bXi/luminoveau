#pragma once

#include <vector>
#include "SDL3/SDL.h"
#include "log/loghandler.h"

/**
 * @brief 2D vertex with position and UV coordinates
 * Using half-float precision for memory efficiency
 */
struct Vertex2D {
    float x, y;     // Local position (e.g., 0-1 for unit quad, -1 to 1 for unit circle)
    float u, v;     // UV coordinates
};

/**
 * @brief Compressed 2D vertex using half-floats packed into uint32
 * 8 bytes per vertex (4 half-floats)
 */
struct CompactVertex2D {
    uint32_t pos_xy;    // x,y as half-floats
    uint32_t uv;        // u,v as half-floats
    
    // Helper to pack from Vertex2D
    static CompactVertex2D FromVertex(const Vertex2D& v);
};

/**
 * @brief Represents 2D geometry with vertices and indices
 */
struct Geometry2D {
    std::vector<Vertex2D> vertices;
    std::vector<uint16_t> indices;  // Using uint16 for smaller index buffers
    
    // GPU buffers
    SDL_GPUBuffer* vertexBuffer = nullptr;
    SDL_GPUBuffer* indexBuffer = nullptr;
    SDL_GPUTransferBuffer* vertexTransferBuffer = nullptr;
    SDL_GPUTransferBuffer* indexTransferBuffer = nullptr;
    
    const char* name = nullptr;
    
    /**
     * @brief Gets the number of vertices in the geometry.
     */
    size_t GetVertexCount() const { return vertices.size(); }
    
    /**
     * @brief Gets the number of indices in the geometry.
     */
    size_t GetIndexCount() const { return indices.size(); }
    
    /**
     * @brief Uploads geometry data to GPU buffers.
     * @param device Pointer to the SDL_GPUDevice.
     */
    void UploadToGPU(SDL_GPUDevice* device);
    
    /**
     * @brief Releases GPU resources.
     * @param device Pointer to the SDL_GPUDevice.
     */
    void Release(SDL_GPUDevice* device);
};

/**
 * @brief Factory functions for creating common 2D geometries
 */
namespace Geometry2DFactory {
    /**
     * @brief Creates a unit quad geometry (0,0) to (1,1)
     * Vertices are at corners, UVs match positions
     */
    Geometry2D* CreateQuad();
    
    /**
     * @brief Creates a unit circle geometry centered at origin
     * @param segments Number of segments around the circle (triangles = segments)
     */
    Geometry2D* CreateCircle(int segments = 32);
    
    /**
     * @brief Creates a unit rounded rectangle geometry (0,0) to (1,1) with rounded corners
     * @param cornerRadius Radius of the rounded corners (0.0 to 0.5)
     * @param cornerSegments Number of segments per corner arc
     */
    Geometry2D* CreateRoundedRect(float cornerRadius = 0.1f, int cornerSegments = 8);
    
    /**
     * @brief Releases all cached geometries
     * @param device Pointer to the SDL_GPUDevice.
     */
    void ReleaseAll(SDL_GPUDevice* device);
}
