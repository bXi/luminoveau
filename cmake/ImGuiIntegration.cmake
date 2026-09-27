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
    # The short form names it in the licence notice; the full one is what gets fetched — see
    # `lumi_fetch` for why only a full hash can be fetched on its own.
    if(LUMINOVEAU_IMGUI_DOCKING)
        set(_lumi_imgui_tag 2af6dd9)
        set(_lumi_imgui_ref 2af6dd9694288e6befe1edb7ce25510911693c22)
        lumi_msg("Fetching ImGui (docking branch)")
    else()
        set(_lumi_imgui_tag fbcf951)
        set(_lumi_imgui_ref fbcf95193f40fc57a3f0a3e8f59798de06e69bc6)
        lumi_msg("Fetching ImGui")
    endif()
    lumi_add_package(
        NAME Imgui
        GIT https://github.com/ocornut/imgui.git
        REF ${_lumi_imgui_ref}
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

        lumi_add_notice("Dear ImGui" SPDX "MIT" VERSION "${_lumi_imgui_tag}"
            URL "https://github.com/ocornut/imgui" DIR "${Imgui_SOURCE_DIR}")
        lumi_done("ImGui")
    else()
        lumi_warn("ImGui - fetch failed")
    endif()
endif()