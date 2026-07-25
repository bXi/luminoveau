#include "gpu/geometry/geometry2d.h"
#include "gpu/halffloat.h"
#include "gpu/IGpu.h"
#include "renderer/renderer.h"
#include <cmath>
#include <unordered_map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

CompactVertex2D CompactVertex2D::FromVertex(const Vertex2D &v) {
    return {
        .posXy = packHalf2(v.x, v.y),
        .uv    = packHalf2(v.u, v.v)
    };
}

void Geometry2D::UploadToGPU() {
    IGpu &gpu = Renderer::GetGpu();

    // Convert to compact format
    std::vector<CompactVertex2D> compactVertices;
    compactVertices.reserve(vertices.size());
    for (const auto &v : vertices) {
        compactVertices.push_back(CompactVertex2D::FromVertex(v));
    }

    uint32_t vertexDataSize = static_cast<uint32_t>(compactVertices.size() * sizeof(CompactVertex2D));
    uint32_t indexDataSize  = static_cast<uint32_t>(indices.size() * sizeof(uint16_t));

    vertexTransferBuffer = gpu.CreateTransferBuffer({ vertexDataSize, GpuTransferUsage::Upload });
    vertexBuffer         = gpu.CreateBuffer({ vertexDataSize, GpuBufferUsage::Vertex });

    void *vertData = gpu.MapTransferBuffer(vertexTransferBuffer, false);
    std::memcpy(vertData, compactVertices.data(), vertexDataSize);
    gpu.UnmapTransferBuffer(vertexTransferBuffer);

    indexTransferBuffer = gpu.CreateTransferBuffer({ indexDataSize, GpuTransferUsage::Upload });
    indexBuffer         = gpu.CreateBuffer({ indexDataSize, GpuBufferUsage::Index });

    void *idxData = gpu.MapTransferBuffer(indexTransferBuffer, false);
    std::memcpy(idxData, indices.data(), indexDataSize);
    gpu.UnmapTransferBuffer(indexTransferBuffer);

    GpuCmdBufferHandle cmd = gpu.AcquireCommandBuffer();
    gpu.UploadToBuffer(cmd, vertexTransferBuffer, 0, vertexBuffer, 0, vertexDataSize);
    gpu.UploadToBuffer(cmd, indexTransferBuffer, 0, indexBuffer, 0, indexDataSize);
    gpu.SubmitCommandBuffer(cmd);
    gpu.WaitIdle();
}

void Geometry2D::Release() {
    IGpu &gpu = Renderer::GetGpu();
    if (vertexBuffer) {
        gpu.ReleaseBuffer(vertexBuffer);
        vertexBuffer = 0;
    }
    if (indexBuffer) {
        gpu.ReleaseBuffer(indexBuffer);
        indexBuffer = 0;
    }
    if (vertexTransferBuffer) {
        gpu.ReleaseTransferBuffer(vertexTransferBuffer);
        vertexTransferBuffer = 0;
    }
    if (indexTransferBuffer) {
        gpu.ReleaseTransferBuffer(indexTransferBuffer);
        indexTransferBuffer = 0;
    }
}

// Factory implementation with caching
Geometry2D *Geometry2DFactory::_createQuad() {
    const std::string key = "quad";
    auto              it  = _geometryCache.find(key);
    if (it != _geometryCache.end()) {
        return it->second;
    }

    auto *quad = new Geometry2D();
    quad->name = "Quad";

    quad->vertices = {
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 1.0f, 0.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        { 0.0f, 1.0f, 0.0f, 1.0f }
    };

    quad->indices = { 0, 1, 2, 0, 2, 3 };

    quad->UploadToGPU();
    _geometryCache[key] = quad;
    return quad;
}

Geometry2D *Geometry2DFactory::_createCircle(int segments) {
    std::string key = "circle_" + std::to_string(segments);
    auto        it  = _geometryCache.find(key);
    if (it != _geometryCache.end()) {
        return it->second;
    }

    auto *circle = new Geometry2D();
    circle->name = ("Circle" + std::to_string(segments)).c_str();

    circle->vertices.push_back({ 0.0f, 0.0f, 0.5f, 0.5f });

    for (int i = 0; i < segments; i++) {
        float angle = (2.0f * static_cast<float>(M_PI) * i) / segments;
        float x     = std::cos(angle);
        float y     = std::sin(angle);
        circle->vertices.push_back({ x, y,
            (x + 1.0f) * 0.5f, (y + 1.0f) * 0.5f });
    }

    for (int i = 0; i < segments; i++) {
        circle->indices.push_back(0);
        circle->indices.push_back(i + 1);
        circle->indices.push_back(((i + 1) % segments) + 1);
    }

    circle->UploadToGPU();
    _geometryCache[key] = circle;
    return circle;
}

Geometry2D *Geometry2DFactory::_createRoundedRect(float cornerRadiusX, float cornerRadiusY, int cornerSegments) {
    cornerRadiusX = std::max(0.0f, std::min(0.5f, cornerRadiusX));
    cornerRadiusY = std::max(0.0f, std::min(0.5f, cornerRadiusY));

    std::string key = "roundrect_" + std::to_string(cornerRadiusX) + "_" + std::to_string(cornerRadiusY) + "_" + std::to_string(cornerSegments);
    auto        it  = _geometryCache.find(key);
    if (it != _geometryCache.end()) {
        return it->second;
    }

    auto *roundedRect = new Geometry2D();
    roundedRect->name = "RoundedRect";

    roundedRect->vertices.push_back({ 0.5f, 0.5f, 0.5f, 0.5f });

    std::vector<Vertex2D> perimeter;
    const float           kPI = static_cast<float>(M_PI);

    for (int i = 0; i <= cornerSegments; i++) {
        float angle = kPI + (kPI * 0.5f) * (float)i / (float)cornerSegments;
        float x     = cornerRadiusX + std::cos(angle) * cornerRadiusX;
        float y     = cornerRadiusY + std::sin(angle) * cornerRadiusY;
        perimeter.push_back({ x, y, x, y });
    }

    for (int i = 0; i <= cornerSegments; i++) {
        float angle = kPI * 1.5f + (kPI * 0.5f) * (float)i / (float)cornerSegments;
        float x     = (1.0f - cornerRadiusX) + std::cos(angle) * cornerRadiusX;
        float y     = cornerRadiusY + std::sin(angle) * cornerRadiusY;
        perimeter.push_back({ x, y, x, y });
    }

    for (int i = 0; i <= cornerSegments; i++) {
        float angle = 0.0f + (kPI * 0.5f) * (float)i / (float)cornerSegments;
        float x     = (1.0f - cornerRadiusX) + std::cos(angle) * cornerRadiusX;
        float y     = (1.0f - cornerRadiusY) + std::sin(angle) * cornerRadiusY;
        perimeter.push_back({ x, y, x, y });
    }

    for (int i = 0; i <= cornerSegments; i++) {
        float angle = kPI * 0.5f + (kPI * 0.5f) * (float)i / (float)cornerSegments;
        float x     = cornerRadiusX + std::cos(angle) * cornerRadiusX;
        float y     = (1.0f - cornerRadiusY) + std::sin(angle) * cornerRadiusY;
        perimeter.push_back({ x, y, x, y });
    }

    for (const auto &v : perimeter) {
        roundedRect->vertices.push_back(v);
    }

    int perimeterCount = static_cast<int>(perimeter.size());
    for (int i = 0; i < perimeterCount; i++) {
        roundedRect->indices.push_back(0);
        roundedRect->indices.push_back(i + 1);
        roundedRect->indices.push_back(((i + 1) % perimeterCount) + 1);
    }

    roundedRect->UploadToGPU();
    _geometryCache[key] = roundedRect;
    return roundedRect;
}

void Geometry2DFactory::_releaseAll() {
    for (auto &[key, geom] : _geometryCache) {
        geom->Release();
        delete geom;
    }
    _geometryCache.clear();
    LOG_INFO("Released all 2D geometries");
}
