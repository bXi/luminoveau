# ThorVGIntegration.cmake
# Vector art rasterised on the CPU into textures (`VectorImage`), via ThorVG. Opt-in with
# LUMINOVEAU_BUILD_THORVG.
#
# ThorVG builds with Meson only, so the parts used here are compiled directly: the CPU rasteriser,
# the SVG loader, and the font loader SVG text needs. `config.h` stands in for Meson's.

if(LUMINOVEAU_BUILD_THORVG)
    set(_tvg_ref "v1.1.2")

    lumi_msg("Fetching ThorVG")
    lumi_fetch("thorvg" "https://github.com/thorvg/thorvg.git" "${_tvg_ref}" _tvg_src)

    if(_tvg_src)
        file(GLOB _tvg_sources
            "${_tvg_src}/src/common/*.cpp"
            "${_tvg_src}/src/renderer/*.cpp"
            "${_tvg_src}/src/renderer/cpu_engine/*.cpp"
            "${_tvg_src}/src/loaders/raw/*.cpp"
            "${_tvg_src}/src/loaders/svg/*.cpp"
            "${_tvg_src}/src/loaders/sfnt/*.cpp"
        )

        set(_tvg_config_dir "${CMAKE_BINARY_DIR}/thorvg_config")
        file(WRITE "${_tvg_config_dir}/config.h"
            "#pragma once\n"
            "#define THORVG_VERSION_STRING \"1.1.2\"\n"
            "#define THORVG_CPU_ENGINE_SUPPORT 1\n"
            "#define THORVG_SVG_LOADER_SUPPORT 1\n"
            "#define THORVG_SFNT_LOADER_SUPPORT 1\n"
            "#define THORVG_TTF_LOADER_SUPPORT 1\n"
        )

        add_library(thorvg STATIC ${_tvg_sources})
        target_include_directories(thorvg
            PUBLIC  "${_tvg_src}/inc"
            PRIVATE "${_tvg_config_dir}"
                    "${_tvg_src}/src/common"
                    "${_tvg_src}/src/renderer"
                    "${_tvg_src}/src/renderer/cpu_engine"
                    "${_tvg_src}/src/loaders/raw"
                    "${_tvg_src}/src/loaders/svg"
                    "${_tvg_src}/src/loaders/sfnt"
        )
        target_compile_definitions(thorvg PUBLIC TVG_STATIC)
        if(WIN32)
            target_compile_definitions(thorvg PRIVATE NOMINMAX)
        endif()
        set_target_properties(thorvg PROPERTIES POSITION_INDEPENDENT_CODE ON)

        target_link_libraries(luminoveau PUBLIC thorvg)
        target_compile_definitions(luminoveau PUBLIC LUMINOVEAU_WITH_THORVG)
        target_sources(luminoveau PRIVATE
            "${PROJECT_SOURCE_DIR}/src/integrations/thorvg/vectorimage.cpp"
            "${PROJECT_SOURCE_DIR}/src/integrations/thorvg/vectorimage.h"
        )

        lumi_add_notice("ThorVG" SPDX "MIT" VERSION "${_tvg_ref}"
            URL "https://github.com/thorvg/thorvg" DIR "${_tvg_src}")
        lumi_done("ThorVG")
    else()
        lumi_warn("ThorVG - fetch failed")
    endif()
endif()
