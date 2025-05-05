# ImGuiIntegration.cmake
# Configures ImGui integration for the Luminoveau library by fetching ImGui sources
# via CPM and adding them to the library target based on the LUMINOVEAU_BUILD_IMGUI option.

# Ensuring CPM is available
if(NOT COMMAND CPMAddPackage)
    message(FATAL_ERROR "CPM.cmake is required for ImGui integration but not included")
endif()

# Checking if ImGui integration is enabled
if(LUMINOVEAU_BUILD_IMGUI)
    # Option to use the official ImGui repository (for future use when bugs are fixed)
    option(LUMINOVEAU_USE_OFFICIAL_IMGUI "Use official ImGui repository instead of custom fork" OFF)

    if(LUMINOVEAU_USE_OFFICIAL_IMGUI)
        # Fetching official ImGui repository
        CPMAddPackage(
            NAME Imgui
            GITHUB_REPOSITORY ocornut/imgui
            GIT_TAG 8cc6eee
            OPTIONS
                "IMGUI_BUILD_SDL3_BACKEND OFF"
        )
        message(STATUS "Using official ImGui repository (ocornut/imgui)")
    else()
        # Fetching custom ImGui fork with SDL-GPU support
        CPMAddPackage(
            NAME Imgui
            GITHUB_REPOSITORY bXi/imgui-sdlgpu
            GIT_TAG d5edda1
        )
        message(STATUS "Using custom ImGui fork (bXi/imgui-sdlgpu)")
    endif()

    # Verifying that ImGui was successfully added
    if(Imgui_ADDED)
        # Adding ImGui compile definition
        target_compile_definitions(luminoveau PUBLIC LUMINOVEAU_WITH_IMGUI)

        # Including ImGui source directory
        if(NOT EXISTS "${Imgui_SOURCE_DIR}")
            message(FATAL_ERROR "ImGui source directory '${Imgui_SOURCE_DIR}' does not exist")
        endif()
        target_include_directories(luminoveau PUBLIC "${Imgui_SOURCE_DIR}")

        # Adding core ImGui sources
        target_sources(luminoveau PRIVATE
            "${Imgui_SOURCE_DIR}/imgui.cpp"
            "${Imgui_SOURCE_DIR}/imgui_demo.cpp"
            "${Imgui_SOURCE_DIR}/imgui_draw.cpp"
            "${Imgui_SOURCE_DIR}/imgui_tables.cpp"
            "${Imgui_SOURCE_DIR}/imgui_widgets.cpp"
        )

        # Adding platform-specific backends
        if(WIN32)
            target_sources(luminoveau PRIVATE
                "${Imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
                "${Imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp"
                "${Imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
                "${Imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3_shadercross.h"
            )
        else()
            target_sources(luminoveau PRIVATE
                "${Imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp"
                "${Imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
                "${Imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3_shadercross.h"
            )
        endif()

        message(STATUS "ImGui integration enabled for luminoveau")
    else()
        message(WARNING "Failed to fetch ImGui package. ImGui integration disabled")
    endif()
endif()