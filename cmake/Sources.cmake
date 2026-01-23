# Defining source files for the Luminoveau library

# Include auto-generated shader sources
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