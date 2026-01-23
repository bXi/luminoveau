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

    CPMAddPackage(
        NAME RmlUi
        GITHUB_REPOSITORY mikke89/RmlUi
        GIT_TAG master
        OPTIONS
            "BUILD_SHARED_LIBS OFF"
            "RMLUI_FONT_ENGINE freetype"
            "RMLUI_SAMPLES OFF"
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
            "${PROJECT_SOURCE_DIR}/rmlui/rmluihandler.cpp"
            "${PROJECT_SOURCE_DIR}/rmlui/rmluihandler.h"
            "${PROJECT_SOURCE_DIR}/rmlui/rmluibackend.cpp"
            "${PROJECT_SOURCE_DIR}/rmlui/rmluibackend.h"
        )

        # Manually add RmlUi SDL_GPU backend sources
        # Even though RMLUI_BACKEND=SDL_GPU, the backend sources aren't included in the static library
        target_sources(luminoveau PRIVATE
            "${RmlUi_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp"
            "${RmlUi_SOURCE_DIR}/Backends/RmlUi_Renderer_SDL_GPU.cpp"
        )

        # Optional: Enable RmlUi debugger in debug builds
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_definitions(luminoveau PUBLIC RMLUI_DEBUGGER_ENABLED)
            target_sources(luminoveau PRIVATE
                "${RmlUi_SOURCE_DIR}/Source/Debugger/Debugger.cpp"
            )
        endif()

        message(STATUS "RmlUi integration enabled for luminoveau")
        message(STATUS "  - Font engine: freetype")
        message(STATUS "  - Debugger: ${CMAKE_BUILD_TYPE} builds only")
    else()
        message(WARNING "Failed to fetch RmlUi package. RmlUi integration disabled")
    endif()
endif()
