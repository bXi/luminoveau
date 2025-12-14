# Defining source files for the Luminoveau library
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

    # Drawing
    draw/drawhandler.cpp

    # Settings
    settings/settingshandler.cpp

    # State management
    state/state.cpp

    # Asset handling
    assethandler/assethandler.cpp
    assethandler/DroidSansMono.cpp
    assethandler/spritefrag.cpp
    assethandler/spritevert.cpp
    assethandler/spritebatchfrag.cpp
    assethandler/spritebatchvert.cpp

    assethandler/spriteinstancedfrag.cpp
    assethandler/spriteinstancedvert.cpp

    # Renderer
    renderer/sdl_gpu_structs.cpp
    renderer/rendererhandler.cpp
    renderer/spriterenderpass.cpp
    renderer/shaderrenderpass.cpp
    renderer/shaderhandler.cpp
    renderer/resourcepack.cpp

    # Utilities
    utils/helpers.cpp
    utils/lerp.cpp
    utils/rectangles.cpp
    utils/vectors.cpp

    # Window management
    window/windowhandler.cpp

    # External sources
    extern/miniaudio.cpp
    extern/SDL_stbimage.cpp
    extern/stb_image.cpp
    extern/stb_image_write.cpp
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

    # Renderer
    renderer/sdl_gpu_structs.h
    renderer/rendererhandler.h
    renderer/renderpass.h
    renderer/spriterenderpass.h
    renderer/shaderrenderpass.h
    renderer/shaderhandler.h

    # Utilities
    utils/camera.h
    utils/colors.h
    utils/constants.h
    utils/easings.h
    utils/helpers.h
    utils/lerp.h
    utils/quadtree.h
    utils/rectangles.h
    utils/vectors.h

    # Window management
    window/windowhandler.h
)