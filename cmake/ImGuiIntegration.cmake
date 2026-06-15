# ImGuiIntegration.cmake
# Configures ImGui integration for the Luminoveau library by fetching ImGui sources
# via CPM and adding them to the library target based on the LUMINOVEAU_BUILD_IMGUI option.

# Ensuring CPM is available
if(NOT COMMAND CPMAddPackage)
    message(FATAL_ERROR "CPM.cmake is required for ImGui integration but not included")
endif()

# Checking if ImGui integration is enabled
if(LUMINOVEAU_BUILD_IMGUI)

    # Docking mode (opt-in) pulls the docking branch; otherwise the pinned master.
    if(LUMINOVEAU_IMGUI_DOCKING)
        set(_lumi_imgui_tag 2af6dd9)
        lumi_msg("Fetching ImGui (docking branch)")
    else()
        set(_lumi_imgui_tag fbcf951)
        lumi_msg("Fetching ImGui")
    endif()
    CPMAddPackage(
        NAME Imgui
        GITHUB_REPOSITORY ocornut/imgui
        GIT_TAG ${_lumi_imgui_tag}
        EXCLUDE_FROM_ALL YES
        OPTIONS
            "IMGUI_BUILD_SDL3_BACKEND OFF"
    )

    # Verifying that ImGui was successfully added
    if(Imgui_ADDED)
        # Adding ImGui compile definition
        target_compile_definitions(luminoveau PUBLIC LUMINOVEAU_WITH_IMGUI)
        if(LUMINOVEAU_IMGUI_DOCKING)
            target_compile_definitions(luminoveau PUBLIC LUMINOVEAU_WITH_IMGUI_DOCKING)
        endif()

        # Including ImGui source directory
        if(NOT EXISTS "${Imgui_SOURCE_DIR}")
            message(FATAL_ERROR "ImGui source directory '${Imgui_SOURCE_DIR}' does not exist")
        endif()
        target_include_directories(luminoveau PUBLIC "${Imgui_SOURCE_DIR}")

        # Luminoveau ImGui integration wrapper (shared + per-backend bridge)
        target_sources(luminoveau PRIVATE
            "${PROJECT_SOURCE_DIR}/src/integrations/imgui/imgui_integration.cpp"
            "${PROJECT_SOURCE_DIR}/src/integrations/imgui/imgui_integration.h"
            "${PROJECT_SOURCE_DIR}/src/integrations/imgui/imgui_backend.h"
        )
        if(LUMINOVEAU_WEBGPU_BACKEND)
            target_sources(luminoveau PRIVATE
                "${PROJECT_SOURCE_DIR}/src/integrations/imgui/webgpu/imgui_backend.cpp"
            )
        else()
            target_sources(luminoveau PRIVATE
                "${PROJECT_SOURCE_DIR}/src/integrations/imgui/sdl/imgui_backend.cpp"
            )
        endif()

        # Adding core ImGui sources
        target_sources(luminoveau PRIVATE
            "${Imgui_SOURCE_DIR}/imgui.cpp"
            "${Imgui_SOURCE_DIR}/imgui_demo.cpp"
            "${Imgui_SOURCE_DIR}/imgui_draw.cpp"
            "${Imgui_SOURCE_DIR}/imgui_tables.cpp"
            "${Imgui_SOURCE_DIR}/imgui_widgets.cpp"
        )

        # SDL3 event/input backend — needed on all platforms
        target_sources(luminoveau PRIVATE
            "${Imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
        )

        # GPU rendering backends — SDL GPU or WebGPU depending on build target
        if(NOT LUMINOVEAU_WEBGPU_BACKEND)
            target_sources(luminoveau PRIVATE
                "${Imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp"
            )
            if(WIN32)
                target_sources(luminoveau PRIVATE
                    "${Imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
                )
            endif()
        else()
            target_sources(luminoveau PRIVATE
                "${Imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp"
            )
        endif()

        lumi_done("ImGui")
    else()
        lumi_warn("ImGui - fetch failed")
    endif()
endif()