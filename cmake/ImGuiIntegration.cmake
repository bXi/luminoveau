# ImGuiIntegration.cmake
# Configures ImGui integration for the Luminoveau library by fetching ImGui sources
# via CPM and adding them to the library target based on the LUMINOVEAU_BUILD_IMGUI option.

# Ensuring CPM is available
if(NOT COMMAND CPMAddPackage)
    message(FATAL_ERROR "CPM.cmake is required for ImGui integration but not included")
endif()

# Checking if ImGui integration is enabled
if(LUMINOVEAU_BUILD_IMGUI)

    lumi_msg("Fetching ImGui")
    CPMAddPackage(
        NAME Imgui
        GITHUB_REPOSITORY ocornut/imgui
        GIT_TAG 7b3ad4a
        EXCLUDE_FROM_ALL YES
        OPTIONS
            "IMGUI_BUILD_SDL3_BACKEND OFF"
    )

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
            )
        else()
            target_sources(luminoveau PRIVATE
                "${Imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp"
                "${Imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
            )
        endif()

        lumi_done("ImGui")
    else()
        lumi_warn("ImGui - fetch failed")
    endif()
endif()