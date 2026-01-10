#include "geometry2d.h"
#include "renderer/rendererhandler.h"
#include <cmath>
#include <unordered_map>

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
    
    // Handle denormalized numbers and underflow
    if (exponent <= 0) {
        if (exponent < -10) {
            return static_cast<uint16_t>(sign);
        }
        mantissa = (mantissa | 0x400) >> (1 - exponent);
        return static_cast<uint16_t>(sign | mantissa);
    }
    
    // Handle overflow to infinity
    if (exponent >= 31) {
        return static_cast<uint16_t>(sign | 0x7C00);
    }
    
    // Normal number
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

void Geometry2D::UploadToGPU(SDL_GPUDevice* device) {
    if (!device) {
        LOG_ERROR("Cannot upload geometry to GPU: device is null");
        return;
    }
    
    // Convert to compact format
    std::vector<CompactVertex2D> compactVertices;
    compactVertices.reserve(vertices.size());
    for (const auto& v : vertices) {
        compactVertices.push_back(CompactVertex2D::FromVertex(v));
    }
    
    // Create vertex buffer
    size_t vertexDataSize = compactVertices.size() * sizeof(CompactVertex2D);
    
    SDL_GPUTransferBufferCreateInfo vertTransferInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (Uint32)vertexDataSize
    };
    vertexTransferBuffer = SDL_CreateGPUTransferBuffer(device, &vertTransferInfo);
    
    SDL_GPUBufferCreateInfo vertBufferInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = (Uint32)vertexDataSize
    };
    vertexBuffer = SDL_CreateGPUBuffer(device, &vertBufferInfo);
    
    // Upload vertex data
    void* vertData = SDL_MapGPUTransferBuffer(device, vertexTransferBuffer, false);
    std::memcpy(vertData, compactVertices.data(), vertexDataSize);
    SDL_UnmapGPUTransferBuffer(device, vertexTransferBuffer);
    
    // Create index buffer
    size_t indexDataSize = indices.size() * sizeof(uint16_t);
    
    SDL_GPUTransferBufferCreateInfo idxTransferInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (Uint32)indexDataSize
    };
    indexTransferBuffer = SDL_CreateGPUTransferBuffer(device, &idxTransferInfo);
    
    SDL_GPUBufferCreateInfo idxBufferInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = (Uint32)indexDataSize
    };
    indexBuffer = SDL_CreateGPUBuffer(device, &idxBufferInfo);
    
    // Upload index data
    void* idxData = SDL_MapGPUTransferBuffer(device, indexTransferBuffer, false);
    std::memcpy(idxData, indices.data(), indexDataSize);
    SDL_UnmapGPUTransferBuffer(device, indexTransferBuffer);
    
    // Transfer to GPU
    SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmd);
    
    // Upload vertices
    SDL_GPUTransferBufferLocation vertTransferLoc = {
        .transfer_buffer = vertexTransferBuffer,
        .offset = 0
    };
    SDL_GPUBufferRegion vertRegion = {
        .buffer = vertexBuffer,
        .offset = 0,
        .size = (Uint32)vertexDataSize
    };
    SDL_UploadToGPUBuffer(copyPass, &vertTransferLoc, &vertRegion, false);
    
    // Upload indices
    SDL_GPUTransferBufferLocation idxTransferLoc = {
        .transfer_buffer = indexTransferBuffer,
        .offset = 0
    };
    SDL_GPUBufferRegion idxRegion = {
        .buffer = indexBuffer,
        .offset = 0,
        .size = (Uint32)indexDataSize
    };
    SDL_UploadToGPUBuffer(copyPass, &idxTransferLoc, &idxRegion, false);
    
    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(uploadCmd);
    
    if (name) {
        LOG_INFO("Uploaded 2D geometry '{}': {} vertices, {} indices", 
                 name, vertices.size(), indices.size());
    }
}

void Geometry2D::Release(SDL_GPUDevice* device) {
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
        
        // Unit quad from (0,0) to (1,1)
        quad->vertices = {
            {0.0f, 0.0f, 0.0f, 0.0f},  // Top-left
            {1.0f, 0.0f, 1.0f, 0.0f},  // Top-right
            {1.0f, 1.0f, 1.0f, 1.0f},  // Bottom-right
            {0.0f, 1.0f, 0.0f, 1.0f}   // Bottom-left
        };
        
        // Two triangles: 0-1-2, 0-2-3
        quad->indices = {0, 1, 2, 0, 2, 3};
        
        quad->UploadToGPU(Renderer::GetDevice());
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
        
        // Center vertex at origin with UV (0.5, 0.5)
        circle->vertices.push_back({0.0f, 0.0f, 0.5f, 0.5f});
        
        // Create ring vertices

        for (int i = 0; i < segments; i++) {
            float angle = (2.0f * PI * i) / segments;
            float x = std::cos(angle);
            float y = std::sin(angle);
            
            // Position normalized to -1 to 1, UV mapped to 0 to 1
            circle->vertices.push_back({
                x, y,
                (x + 1.0f) * 0.5f, (y + 1.0f) * 0.5f
            });
        }
        
        // Create triangle fan indices
        for (int i = 0; i < segments; i++) {
            circle->indices.push_back(0);              // Center
            circle->indices.push_back(i + 1);          // Current ring vertex
            circle->indices.push_back(((i + 1) % segments) + 1);  // Next ring vertex (wraps around)
        }
        
        circle->UploadToGPU(Renderer::GetDevice());
        geometryCache[key] = circle;
        return circle;
    }
    
    void ReleaseAll(SDL_GPUDevice* device) {
        for (auto& [key, geom] : geometryCache) {
            geom->Release(device);
            delete geom;
        }
        geometryCache.clear();
        LOG_INFO("Released all 2D geometries");
    }
}
