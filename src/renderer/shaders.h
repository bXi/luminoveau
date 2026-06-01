// Cross-backend public API for the engine's shader subsystem.
//
// Only the lifecycle + entry-point queries live here. Backend-specific machinery
// (SDL_ShaderCross integration, GLSL→SPIRV transpile, metadata caching, shader-asset
// creation) is declared in renderer/sdl/shaders_sdl.h and only used inside SDL-backend
// translation units. The WebGPU build provides stub implementations of the symbols below
// in renderer/webgpu/shaders.cpp.

#pragma once

namespace Shaders {
    // Engine startup hook. SDL backend wires up SDL_shadercross + the on-disk shader cache;
    // WebGPU backend is a no-op (WGSL is compiled by the browser/Tint at module-create time).
    void Init();

    // Engine shutdown hook. SDL backend persists the shader cache and tears down SDL_shadercross.
    void Quit();

    // Entry-point names for the engine's built-in shaders. SDL uses SPIRV (named "main"),
    // except MSL output which spirv-cross renames to "main0". WebGPU's WGSL pipeline emits
    // "vs_main" / "fs_main" / "main" via Tint.
    const char* GetVertexEntryPoint();
    const char* GetFragmentEntryPoint();
    const char* GetComputeEntryPoint();
}
