#include "gpu/geometry/geometry2d.h"
#include "gpu/IGpu.h"
#include "renderer/renderer.h"
#include <cmath>
#include <unordered_map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Fast inline clamp - compiles to conditional moves (branchless)
static inline float fast_clamp(float v, float min, float max) {
    return (v < min) ? min : (v > max ? max : v);
}

// Float32 to Float16 conversion
static inline uint16_t float_to_half(float f) {
    union { float f; uint32_t i; } u = {f};
    uint32_t bits = u.i;

    uint32_t sign = (bits >> 16) & 0x8000;
    int32_t exponent = ((bits >> 23) & 0xFF) - 127 + 15;
    uint32_t mantissa = (bits >> 13) & 0x3FF;

    if (exponent <= 0) {
        if (exponent < -10) {
            return static_cast<uint16_t>(sign);
        }
        mantissa = (mantissa | 0x400) >> (1 - exponent);
        return static_cast<uint16_t>(sign | mantissa);
    }

    if (exponent >= 31) {
        return static_cast<uint16_t>(sign | 0x7C00);
    }

    return static_cast<uint16_t>(sign | (exponent << 10) | mantissa);
}

// Pack two half-floats into a uint32
static inline uint32_t pack_half2(float a, float b) {
    if (!std::isfinite(a)) a = 0.0f;
    if (!std::isfinite(b)) b = 0.0f;

    a = fast_clamp(a, -65504.0f, 65504.0f);
    b = fast_clamp(b, -65504.0f, 65504.0f);

    uint16_t ha = float_to_half(a);
    uint16_t hb = float_to_half(b);
    return static_cast<uint32_t>(ha) | (static_cast<uint32_t>(hb) << 16);
}

CompactVertex2D CompactVertex2D::FromVertex(const Vertex2D& v) {
    return {
        .pos_xy = pack_half2(v.x, v.y),
        .uv = pack_half2(v.u, v.v)
    };
}

void Geometry2D::UploadToGPU() {
    IGpu& gpu = Renderer::GetGpu();

    // Convert to compact format
    std::vector<CompactVertex2D> compactVertices;
    compactVertices.reserve(vertices.size());
    for (const auto& v : vertices) {
        compactVertices.push_back(CompactVertex2D::FromVertex(v));
    }

    uint32_t vertexDataSize = static_cast<uint32_t>(compactVertices.size() * sizeof(CompactVertex2D));
    uint32_t indexDataSize  = static_cast<uint32_t>(indices.size() * sizeof(uint16_t));

    vertexTransferBuffer = gpu.createTransferBuffer({ vertexDataSize, GpuTransferUsage::Upload });
    vertexBuffer         = gpu.createBuffer({ vertexDataSize, GpuBufferUsage::Vertex });

    void* vertData = gpu.mapTransferBuffer(vertexTransferBuffer, false);
    std::memcpy(vertData, compactVertices.data(), vertexDataSize);
    gpu.unmapTransferBuffer(vertexTransferBuffer);

    indexTransferBuffer = gpu.createTransferBuffer({ indexDataSize, GpuTransferUsage::Upload });
    indexBuffer         = gpu.createBuffer({ indexDataSize, GpuBufferUsage::Index });

    void* idxData = gpu.mapTransferBuffer(indexTransferBuffer, false);
    std::memcpy(idxData, indices.data(), indexDataSize);
    gpu.unmapTransferBuffer(indexTransferBuffer);

    GpuCmdBufferHandle cmd = gpu.acquireCommandBuffer();
    gpu.uploadToBuffer(cmd, vertexTransferBuffer, 0, vertexBuffer, 0, vertexDataSize);
    gpu.uploadToBuffer(cmd, indexTransferBuffer,  0, indexBuffer,  0, indexDataSize);
    gpu.submitCommandBuffer(cmd);
    gpu.waitIdle();
}

void Geometry2D::Release() {
    IGpu& gpu = Renderer::GetGpu();
    if (vertexBuffer)         { gpu.releaseBuffer(vertexBuffer);                 vertexBuffer         = 0; }
    if (indexBuffer)          { gpu.releaseBuffer(indexBuffer);                  indexBuffer          = 0; }
    if (vertexTransferBuffer) { gpu.releaseTransferBuffer(vertexTransferBuffer); vertexTransferBuffer = 0; }
    if (indexTransferBuffer)  { gpu.releaseTransferBuffer(indexTransferBuffer);  indexTransferBuffer  = 0; }
}

// Factory implementation with caching
namespace Geometry2DFactory {
    static std::unordered_map<std::string, Geometry2D*> geometryCache;

    Geometry2D* CreateQuad() {
        const std::string key = "quad";
        auto it = geometryCache.find(key);
        if (it != geometryCache.end()) {
            return it->second;
        }

        auto* quad = new Geometry2D();
        quad->name = "Quad";

        quad->vertices = {
            {0.0f, 0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 1.0f, 0.0f},
            {1.0f, 1.0f, 1.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 1.0f}
        };

        quad->indices = {0, 1, 2, 0, 2, 3};

        quad->UploadToGPU();
        geometryCache[key] = quad;
        return quad;
    }

    Geometry2D* CreateCircle(int segments) {
        std::string key = "circle_" + std::to_string(segments);
        auto it = geometryCache.find(key);
        if (it != geometryCache.end()) {
            return it->second;
        }

        auto* circle = new Geometry2D();
        circle->name = ("Circle" + std::to_string(segments)).c_str();

        circle->vertices.push_back({0.0f, 0.0f, 0.5f, 0.5f});

        for (int i = 0; i < segments; i++) {
            float angle = (2.0f * static_cast<float>(M_PI) * i) / segments;
            float x = std::cos(angle);
            float y = std::sin(angle);
            circle->vertices.push_back({
                x, y,
                (x + 1.0f) * 0.5f, (y + 1.0f) * 0.5f
            });
        }

        for (int i = 0; i < segments; i++) {
            circle->indices.push_back(0);
            circle->indices.push_back(i + 1);
            circle->indices.push_back(((i + 1) % segments) + 1);
        }

        circle->UploadToGPU();
        geometryCache[key] = circle;
        return circle;
    }

    Geometry2D* CreateRoundedRect(float cornerRadiusX, float cornerRadiusY, int cornerSegments) {
        cornerRadiusX = std::max(0.0f, std::min(0.5f, cornerRadiusX));
        cornerRadiusY = std::max(0.0f, std::min(0.5f, cornerRadiusY));

        std::string key = "roundrect_" + std::to_string(cornerRadiusX) + "_" + std::to_string(cornerRadiusY) + "_" + std::to_string(cornerSegments);
        auto it = geometryCache.find(key);
        if (it != geometryCache.end()) {
            return it->second;
        }

        auto* roundedRect = new Geometry2D();
        roundedRect->name = "RoundedRect";

        roundedRect->vertices.push_back({0.5f, 0.5f, 0.5f, 0.5f});

        std::vector<Vertex2D> perimeter;
        const float kPI = static_cast<float>(M_PI);

        for (int i = 0; i <= cornerSegments; i++) {
            float angle = kPI + (kPI * 0.5f) * (float)i / (float)cornerSegments;
            float x = cornerRadiusX + std::cos(angle) * cornerRadiusX;
            float y = cornerRadiusY + std::sin(angle) * cornerRadiusY;
            perimeter.push_back({x, y, x, y});
        }

        for (int i = 0; i <= cornerSegments; i++) {
            float angle = kPI * 1.5f + (kPI * 0.5f) * (float)i / (float)cornerSegments;
            float x = (1.0f - cornerRadiusX) + std::cos(angle) * cornerRadiusX;
            float y = cornerRadiusY + std::sin(angle) * cornerRadiusY;
            perimeter.push_back({x, y, x, y});
        }

        for (int i = 0; i <= cornerSegments; i++) {
            float angle = 0.0f + (kPI * 0.5f) * (float)i / (float)cornerSegments;
            float x = (1.0f - cornerRadiusX) + std::cos(angle) * cornerRadiusX;
            float y = (1.0f - cornerRadiusY) + std::sin(angle) * cornerRadiusY;
            perimeter.push_back({x, y, x, y});
        }

        for (int i = 0; i <= cornerSegments; i++) {
            float angle = kPI * 0.5f + (kPI * 0.5f) * (float)i / (float)cornerSegments;
            float x = cornerRadiusX + std::cos(angle) * cornerRadiusX;
            float y = (1.0f - cornerRadiusY) + std::sin(angle) * cornerRadiusY;
            perimeter.push_back({x, y, x, y});
        }

        for (const auto& v : perimeter) {
            roundedRect->vertices.push_back(v);
        }

        int perimeterCount = static_cast<int>(perimeter.size());
        for (int i = 0; i < perimeterCount; i++) {
            roundedRect->indices.push_back(0);
            roundedRect->indices.push_back(i + 1);
            roundedRect->indices.push_back(((i + 1) % perimeterCount) + 1);
        }

        roundedRect->UploadToGPU();
        geometryCache[key] = roundedRect;
        return roundedRect;
    }

    void ReleaseAll() {
        for (auto& [key, geom] : geometryCache) {
            geom->Release();
            delete geom;
        }
        geometryCache.clear();
        LOG_INFO("Released all 2D geometries");
    }
}
