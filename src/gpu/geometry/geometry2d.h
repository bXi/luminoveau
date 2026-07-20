#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include "gpu/types.h"
#include "core/log/log.h"

/**
 * @brief 2D vertex with position and UV coordinates
 * Using half-float precision for memory efficiency
 */
struct Vertex2D {
    float x, y; // Local position (e.g., 0-1 for unit quad, -1 to 1 for unit circle)
    float u, v; // UV coordinates
};

/**
 * @brief Compressed 2D vertex using half-floats packed into uint32
 * 8 bytes per vertex (4 half-floats)
 */
struct CompactVertex2D {
    uint32_t pos_xy; // x,y as half-floats
    uint32_t uv;     // u,v as half-floats

    // Helper to pack from Vertex2D
    static CompactVertex2D FromVertex(const Vertex2D &v);
};

/**
 * @brief Represents 2D geometry with vertices and indices
 */
struct Geometry2D {
    std::vector<Vertex2D> vertices;
    std::vector<uint16_t> indices; // Using uint16 for smaller index buffers

    // GPU buffers
    GpuBufferHandle         vertexBuffer         = 0;
    GpuBufferHandle         indexBuffer          = 0;
    GpuTransferBufferHandle vertexTransferBuffer = 0;
    GpuTransferBufferHandle indexTransferBuffer  = 0;

    const char *name = nullptr;

    /**
     * @brief Gets the number of vertices in the geometry.
     */
    size_t GetVertexCount() const { return vertices.size(); }

    /**
     * @brief Gets the number of indices in the geometry.
     */
    size_t GetIndexCount() const { return indices.size(); }

    /**
     * @brief Uploads geometry data to GPU buffers via IGpu.
     */
    void UploadToGPU();

    /**
     * @brief Releases GPU resources via IGpu.
     */
    void Release();
};

/**
 * @brief Factory for creating (and caching) common 2D geometries.
 */
class Geometry2DFactory {
public:
    /**
     * @brief Creates a unit quad geometry (0,0) to (1,1)
     * Vertices are at corners, UVs match positions
     */
    static Geometry2D *CreateQuad() { return get()._createQuad(); }

    /**
     * @brief Creates a unit circle geometry centered at origin
     * @param segments Number of segments around the circle (triangles = segments)
     */
    static Geometry2D *CreateCircle(int segments = 32) { return get()._createCircle(segments); }

    /**
     * @brief Creates a unit rounded rectangle geometry (0,0) to (1,1) with rounded corners
     * @param cornerRadiusX Normalized radius along X axis (0.0 to 0.5)
     * @param cornerRadiusY Normalized radius along Y axis (0.0 to 0.5)
     * @param cornerSegments Number of segments per corner arc
     */
    static Geometry2D *CreateRoundedRect(float cornerRadiusX, float cornerRadiusY, int cornerSegments = 8) { return get()._createRoundedRect(cornerRadiusX, cornerRadiusY, cornerSegments); }

    /**
     * @brief Releases all cached geometries
     */
    static void ReleaseAll() { get()._releaseAll(); }

private:
    Geometry2D *_createQuad();
    Geometry2D *_createCircle(int segments);
    Geometry2D *_createRoundedRect(float cornerRadiusX, float cornerRadiusY, int cornerSegments);
    void        _releaseAll();

    std::unordered_map<std::string, Geometry2D *> _geometryCache;

public:
    /// @cond INTERNAL
    Geometry2DFactory(const Geometry2DFactory &) = delete;

    static Geometry2DFactory &get() {
        static Geometry2DFactory instance;
        return instance;
    }
    /// @endcond

private:
    Geometry2DFactory() = default;
};
