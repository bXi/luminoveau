# RmlUiIntegration.cmake
# Configures RmlUi integration for the Luminoveau library by fetching RmlUi sources
# via CPM and adding them to the library target based on the LUMINOVEAU_BUILD_RMLUI option.

# Ensuring CPM is available
if(NOT COMMAND CPMAddPackage)
    message(FATAL_ERROR "CPM.cmake is required for RmlUi integration but not included")
endif()

# Checking if RmlUi integration is enabled
if(LUMINOVEAU_BUILD_RMLUI)

    # Define SDL version for RmlUi
    set(RMLUI_SDL_VERSION_MAJOR 3)

    lumi_msg("Fetching RmlUi")
    CPMAddPackage(
        NAME RmlUi
        GITHUB_REPOSITORY mikke89/RmlUi
        GIT_TAG a60d823
        EXCLUDE_FROM_ALL YES
        OPTIONS
            "BUILD_SHARED_LIBS OFF"
            "RMLUI_FONT_ENGINE freetype"
            "RMLUI_SAMPLES OFF"
            "RMLUI_TESTS OFF"
            "RMLUI_INSTALL OFF"
            "RMLUI_BACKEND SDL_GPU"
            "RMLUI_THIRDPARTY_CONTAINERS ON"
            "RMLUI_CUSTOM_RTTI OFF"
            "RMLUI_PRECOMPILED_HEADERS ON"
    )

    # Verifying that RmlUi was successfully added
    if(RmlUi_ADDED)
        # Adding RmlUi compile definition
        target_compile_definitions(luminoveau PUBLIC 
            LUMINOVEAU_WITH_RMLUI
            RMLUI_SDL_VERSION_MAJOR=3
        )

        # Including RmlUi directories
        if(NOT EXISTS "${RmlUi_SOURCE_DIR}")
            message(FATAL_ERROR "RmlUi source directory '${RmlUi_SOURCE_DIR}' does not exist")
        endif()
        
        target_include_directories(luminoveau PUBLIC 
            "${RmlUi_SOURCE_DIR}/Include"
            "${RmlUi_SOURCE_DIR}/Backends"
        )

        # Link RmlUi libraries
        target_link_libraries(luminoveau PUBLIC RmlUi::RmlUi)

        # Add Luminoveau RmlUI wrapper sources
        target_sources(luminoveau PRIVATE
            "${PROJECT_SOURCE_DIR}/src/integrations/rmlui/rmlui.cpp"
            "${PROJECT_SOURCE_DIR}/src/integrations/rmlui/rmlui.h"
            "${PROJECT_SOURCE_DIR}/src/integrations/rmlui/rmluibackend.cpp"
            "${PROJECT_SOURCE_DIR}/src/integrations/rmlui/rmluibackend.h"
            "${PROJECT_SOURCE_DIR}/src/integrations/rmlui/rmluirenderinterface.cpp"
            "${PROJECT_SOURCE_DIR}/src/integrations/rmlui/rmluirenderinterface.h"
        )

        # RmlUi's platform layer is SDL-based on every backend here — it reads SDL events and
        # sets the SDL cursor, neither of which is renderer-specific — so it stays.
        #
        # **The SDL_GPU *renderer* does not.** RmlUi ships no WebGPU backend, so on that backend
        # the integration had no render interface at all and every document failed to appear.
        # `rmluirenderinterface.cpp` replaces it with one written against IGpu, which works on
        # both backends; keeping the SDL one alongside would mean two implementations of the same
        # few hundred lines, only one of which is ever exercised.
        target_sources(luminoveau PRIVATE
            "${RmlUi_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp"
        )

        # The render interface loads its shaders as assets rather than as embedded blobs, so they
        # have to reach the game's asset tree — PhysFS mounts the working directory, which is the
        # *game's* assets, not the engine's.
        #
        # Copied at configure time rather than POST_BUILD: the web build's shader transpile reads
        # this directory during its own build step, which can run before any POST_BUILD would.
        #
        # A game that transpiles shaders for the web (`lumi_transpile_shaders`) picks these up for
        # free, because they now live in the same directory as its own.
        file(COPY "${PROJECT_SOURCE_DIR}/assets/shaders/rmlui.vert"
                  "${PROJECT_SOURCE_DIR}/assets/shaders/rmlui.frag"
             DESTINATION "${CMAKE_SOURCE_DIR}/assets/shaders")

        # Optional: Enable RmlUi debugger in debug builds
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_definitions(luminoveau PUBLIC RMLUI_DEBUGGER_ENABLED)
            target_sources(luminoveau PRIVATE
                "${RmlUi_SOURCE_DIR}/Source/Debugger/Debugger.cpp"
            )
        endif()

        lumi_done("RmlUi")
    else()
        lumi_warn("RmlUi - fetch failed")
    endif()
endif()
