# Defining source files for the Luminoveau library

# ============================================================================
# GPU Backend Selection (platform defaults)
# ============================================================================
# Maps platform to shader format used by Sources.Shaders.cmake:
#   macOS/iOS    -> METALLIB  (Metal)
#   Windows      -> SPIRV     (Vulkan, override with -DLUMINOVEAU_GPU_BACKEND=DXIL for DX12)
#   Android      -> SPIRV     (Vulkan)
#   Linux        -> SPIRV     (Vulkan)
#   Emscripten   -> (not yet supported)
#   All others   -> SPIRV     (Vulkan)
#
# The shader format directly determines the graphics driver:
#   SPIRV     -> Vulkan
#   DXIL      -> DirectX 12
#   METALLIB  -> Metal

if(NOT DEFINED LUMINOVEAU_GPU_BACKEND)
    if(APPLE)
        set(LUMINOVEAU_GPU_BACKEND "METALLIB" CACHE STRING "GPU shader backend (SPIRV, DXIL, METALLIB)" FORCE)
    else()
        set(LUMINOVEAU_GPU_BACKEND "SPIRV" CACHE STRING "GPU shader backend (SPIRV, DXIL, METALLIB)" FORCE)
    endif()
else()
    # Validate stale or invalid cache values
    if(NOT LUMINOVEAU_GPU_BACKEND MATCHES "^(SPIRV|DXIL|METALLIB)$")
        message(WARNING "Invalid cached LUMINOVEAU_GPU_BACKEND='${LUMINOVEAU_GPU_BACKEND}', resetting to platform default")
        if(APPLE)
            set(LUMINOVEAU_GPU_BACKEND "METALLIB" CACHE STRING "GPU shader backend (SPIRV, DXIL, METALLIB)" FORCE)
        else()
            set(LUMINOVEAU_GPU_BACKEND "SPIRV" CACHE STRING "GPU shader backend (SPIRV, DXIL, METALLIB)" FORCE)
        endif()
    endif()
endif()

# Platform sanity checks
if(LUMINOVEAU_GPU_BACKEND STREQUAL "METALLIB" AND NOT APPLE)
    message(FATAL_ERROR "METALLIB backend is only supported on Apple platforms")
endif()
if(LUMINOVEAU_GPU_BACKEND STREQUAL "DXIL" AND NOT WIN32)
    message(FATAL_ERROR "DXIL backend is only supported on Windows")
endif()

# Include auto-generated shader sources (sets LUMINOVEAU_SHADER_SOURCES and compile define)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Sources.Shaders.cmake")
    include(cmake/Sources.Shaders.cmake)
else()
    message(FATAL_ERROR "Shader sources file not found: cmake/Sources.Shaders.cmake\nPlease run shaders/compile_shaders.ps1 to generate shader binaries.")
endif()

set(LUMINOVEAU_SOURCES
    # Audio module
    audio/audiohandler.cpp

    # Text handling
    text/texthandler.cpp

    # Event bus
    eventbus/eventbushandler.cpp

    # Input handling
    input/inputdevice.cpp
    input/inputhandler.cpp
    input/virtualcontrols.cpp

    # Drawing
    draw/drawhandler.cpp

    # Settings
    settings/settingshandler.cpp

    # State management
    state/state.cpp

    # Asset handling
    assethandler/assethandler.cpp
    assethandler/DroidSansMono.cpp

    # File handling
    file/filehandler.cpp

    # Shaders (auto-generated)
    ${LUMINOVEAU_SHADER_SOURCES}

    # Renderer
    renderer/sdl_gpu_structs.cpp
    renderer/rendererhandler.cpp
    renderer/spriterenderpass.cpp
    renderer/model3drenderpass.cpp
    renderer/shaderrenderpass.cpp
    renderer/shaderhandler.cpp
    renderer/resourcepack.cpp
    renderer/geometry2d.cpp

    # Logging
    log/loghandler.cpp

    # Utilities
    utils/helpers.cpp
    utils/lerp.cpp
    utils/rectangles.cpp
    utils/vectors.cpp
    utils/scene3d.cpp

    # Window management
    window/windowhandler.cpp

    # External sources
    extern/miniaudio.cpp
)

# Defining header files for installation and inclusion
set(LUMINOVEAU_HEADERS
    luminoveau.h

    # Audio module
    audio/audiohandler.h

    # Engine state
    enginestate/enginestate.h

    # Text handling
    text/texthandler.h

    # Event bus
    eventbus/eventbushandler.h

    # Input handling
    input/inputconstants.h
    input/inputdevice.h
    input/inputhandler.h
    input/virtualcontrols.h

    # Drawing
    draw/drawhandler.h

    # Settings
    settings/mini.h
    settings/settingshandler.h

    # State management
    state/basestate.h
    state/state.h

    # Asset handling
    assethandler/assethandler.h

    # File handling
    file/filehandler.h

    # Shaders (auto-generated)
    assethandler/shaders_generated.h

    # Asset types
    assettypes/font.h
    assettypes/model.h
    assettypes/music.h
    assettypes/shader.h
    assettypes/sound.h
    assettypes/texture.h

    # Renderer
    renderer/sdl_gpu_structs.h
    renderer/rendererhandler.h
    renderer/renderpass.h
    renderer/spriterenderpass.h
    renderer/model3drenderpass.h
    renderer/shaderrenderpass.h
    renderer/shaderhandler.h
    renderer/geometry2d.h

    # Logging
    log/loghandler.h

    # Utilities
    utils/camera.h
    utils/camera3d.h
    utils/colors.h
    utils/constants.h
    utils/easings.h
    utils/helpers.h
    utils/lerp.h
    utils/quadtree.h
    utils/rectangles.h
    utils/vectors.h
    utils/scene3d.h

    # Window management
    window/windowhandler.h
)