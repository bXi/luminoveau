# ── GPU backend selection ─────────────────────────────────────────────────────
# Emscripten/WebGPU uses WGSL shaders, not the SDL_GPU shader backends.
if(LUMINOVEAU_WEBGPU_BACKEND OR EMSCRIPTEN)
    set(LUMINOVEAU_GPU_BACKEND "WGSL" CACHE STRING "GPU shader backend (SPIRV, DXIL, METALLIB, WGSL)" FORCE)
elseif(NOT DEFINED LUMINOVEAU_GPU_BACKEND)
    if(APPLE)
        set(LUMINOVEAU_GPU_BACKEND "METALLIB" CACHE STRING "GPU shader backend (SPIRV, DXIL, METALLIB, WGSL)" FORCE)
    else()
        set(LUMINOVEAU_GPU_BACKEND "SPIRV" CACHE STRING "GPU shader backend (SPIRV, DXIL, METALLIB, WGSL)" FORCE)
    endif()
else()
    if(NOT LUMINOVEAU_GPU_BACKEND MATCHES "^(SPIRV|DXIL|METALLIB|WGSL)$")
        message(WARNING "Invalid LUMINOVEAU_GPU_BACKEND='${LUMINOVEAU_GPU_BACKEND}', resetting to platform default")
        if(APPLE)
            set(LUMINOVEAU_GPU_BACKEND "METALLIB" CACHE STRING "" FORCE)
        else()
            set(LUMINOVEAU_GPU_BACKEND "SPIRV" CACHE STRING "" FORCE)
        endif()
    endif()
endif()

if(LUMINOVEAU_GPU_BACKEND STREQUAL "METALLIB" AND NOT APPLE)
    message(FATAL_ERROR "METALLIB backend is only supported on Apple platforms")
endif()
if(LUMINOVEAU_GPU_BACKEND STREQUAL "DXIL" AND NOT WIN32)
    message(FATAL_ERROR "DXIL backend is only supported on Windows")
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Sources.Shaders.cmake")
    include(cmake/Sources.Shaders.cmake)
else()
    message(FATAL_ERROR "Shader sources not found. Run shaders/compile_shaders.ps1 first.")
endif()

set(LUMINOVEAU_SOURCES
    # Platform
    src/platform/audio/audio.cpp
    src/platform/input/inputdevice.cpp
    src/platform/input/input.cpp
    src/platform/input/virtualcontrols.cpp
    src/platform/window/window.cpp
    src/platform/net/net.cpp

    # Core
    src/core/eventbus/eventbus.cpp
    src/core/settings/settings.cpp
    src/core/log/log.cpp

    # Profiler
    src/profiler/perf.cpp

    # File
    src/file/filehandler.cpp
    src/file/resourcepack.cpp

    # Math
    src/math/rectangles.cpp
    src/math/vectors.cpp

    # Util
    src/util/helpers.cpp
    src/util/lerp.cpp

    # GPU
    src/gpu/buffer/buffermanager.cpp
    src/gpu/IGpu.cpp

    # Renderer
    src/renderer/renderer.cpp

    # Assets
    src/assets/assethandler.cpp
    src/assets/DroidSansMono.cpp

    # Scene
    src/scene/scene3d.cpp

    # GPU geometry
    src/gpu/geometry/geometry2d.cpp

    # Draw
    src/renderer/compute.cpp
    src/draw/text.cpp
    src/draw/particles.cpp
    src/draw/draw.cpp

    # Shaders (auto-generated)
    ${LUMINOVEAU_SHADER_SOURCES}

    # External
    src/extern/miniaudio.cpp
    src/extern/stb_image_write.cpp
)

if(LUMINOVEAU_WEBGPU_BACKEND)
    list(APPEND LUMINOVEAU_SOURCES
        src/gpu/backends/webgpu/WebGpuGpuBackend.cpp
        src/renderer/webgpu/init.cpp
        src/assets/webgpu/assethandler.cpp
        src/renderer/passes/webgpu/spriterenderpass.cpp
        src/renderer/passes/webgpu/model3drenderpass.cpp
        src/renderer/passes/webgpu/shaderrenderpass.cpp
        src/draw/webgpu/particles_builtin.cpp
        src/platform/input/webgpu/mouseinput.cpp
        src/platform/window/webgpu/window_backend.cpp
        src/renderer/webgpu/shaders.cpp
        src/platform/net/webgpu/transport_web.cpp
    )
else()
    list(APPEND LUMINOVEAU_SOURCES
        src/gpu/backends/sdl/SdlGpuBackend.cpp
        src/gpu/backends/sdl/sdlgpu.cpp
        src/renderer/sdl/init.cpp
        src/assets/sdl/assethandler.cpp
        src/renderer/passes/sdl/spriterenderpass.cpp
        src/renderer/passes/sdl/model3drenderpass.cpp
        src/renderer/passes/sdl/shaderrenderpass.cpp
        src/draw/sdl/particles_builtin.cpp
        src/platform/input/sdl/mouseinput.cpp
        src/platform/window/sdl/window_backend.cpp
        src/renderer/shaders.cpp
        src/platform/net/sdl/transport_sdlnet.cpp
    )
endif()

set(LUMINOVEAU_HEADERS
    luminoveau.h

    # Interfaces
    src/interfaces/IRenderer.h
    src/interfaces/IAudio.h
    src/interfaces/IInput.h
    src/interfaces/IWindow.h
    src/interfaces/IFileSystem.h

    # Math
    src/math/angles.h
    src/math/constants.h
    src/math/easings.h
    src/math/rectangles.h
    src/math/vectors.h

    # Types
    src/types/color.h

    # Util
    src/util/helpers.h
    src/util/lerp.h
    src/util/quadtree.h

    # GPU buffer
    src/gpu/buffer/uniformobject.h

    # GPU
    src/gpu/types.h
    src/gpu/IGpu.h
    src/gpu/presets.h
    src/gpu/IBackendAccess.h
    src/gpu/renderpass.h
    src/gpu/renderable.h
    src/gpu/geometry/geometry2d.h
    src/gpu/buffer/buffer.h
    src/gpu/buffer/buffermanager.h

    # GPU backends
    src/gpu/backends/sdl/SdlGpuBackend.h
    src/gpu/backends/sdl/SdlGpuHandles.h
    src/gpu/backends/sdl/sdlgpu.h
    src/gpu/backends/gl/OpenGLGpuBackend.h
    src/gpu/backends/sw/SoftwareGpuBackend.h

    # Assets
    src/assets/assethandler.h
    src/assets/shaders_generated.h
    src/assets/texture/texture.h
    src/assets/shader/shader.h
    src/assets/font/font.h
    src/assets/audio/sound.h
    src/assets/audio/music.h
    src/assets/audio/pcmsound.h
    src/assets/model/model.h
    src/assets/effect/effect.h
    src/assets/effect/effects.h
    src/assets/compute/computepipeline.h

    # Renderer
    src/renderer/renderer.h
    src/renderer/shaders.h
    src/renderer/compute.h
    src/renderer/passes/spriterenderpass.h
    src/renderer/passes/model3drenderpass.h
    src/renderer/passes/shaderrenderpass.h

    # Core
    src/core/eventbus/eventbus.h
    src/core/enginestate/enginestate.h
    src/core/settings/settings.h
    src/core/settings/mini.h
    src/core/state/state.h
    src/core/state/basestate.h
    src/core/log/log.h

    # Platform
    src/platform/audio/audio.h
    src/platform/input/input.h
    src/platform/input/inputconstants.h
    src/platform/input/inputdevice.h
    src/platform/input/virtualcontrols.h
    src/platform/window/window.h

    # Scene
    src/scene/camera.h
    src/scene/camera3d.h
    src/scene/scene3d.h

    # Draw
    src/draw/text.h
    src/draw/particles.h
    src/draw/particlesystem.h
    src/draw/draw.h

    # File
    src/file/filehandler.h
    src/file/resourcepack.h
)

# app/lumi_main.cpp is INTERFACE — compiles into the executable, not the library.
