// Cross-backend public API for the engine's shader subsystem.
//
// Only the lifecycle + entry-point queries live here. Backend-specific machinery
// (SDL_ShaderCross integration, GLSL→SPIRV transpile, metadata caching, shader-asset
// creation) is declared in renderer/sdl/shaders_sdl.h and only used inside SDL-backend
// translation units. The WebGPU build provides stub implementations of the symbols below
// in renderer/webgpu/shaders.cpp.

#pragma once

/// @brief Cross-backend shader subsystem: lifecycle hooks and entry-point name queries.
namespace Shaders {
    /// @brief Engine startup hook. SDL wires up SDL_shadercross + the on-disk shader cache;
    ///        WebGPU is a no-op (WGSL is compiled by the browser at module-create time).
    void Init();

    /// @brief Engine shutdown hook. SDL persists the shader cache and tears down SDL_shadercross.
    void Quit();

    /// @brief Returns the entry-point name for the built-in vertex shaders on the active backend.
    const char* GetVertexEntryPoint();
    /// @brief Returns the entry-point name for the built-in fragment shaders on the active backend.
    const char* GetFragmentEntryPoint();
    /// @brief Returns the entry-point name for the built-in compute shaders on the active backend.
    const char* GetComputeEntryPoint();
}
