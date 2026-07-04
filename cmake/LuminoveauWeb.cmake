# LuminoveauWeb.cmake — Emscripten/web build helper for apps that embed Luminoveau.
#
# Provides luminoveau_configure_web_target(), which applies the standard web build settings so each
# app doesn't hand-roll them:
#   * .html output suffix + Luminoveau's HTML shell
#   * wasm runtime tuning (stack size; assertions + exception catching on Debug, off on Release)
#   * GLSL effect shaders transpiled to WGSL (Tint) and preloaded, if the app has assets/shaders/
#   * the app's assets/ folder preloaded, if present
#
# No-op on non-Emscripten builds, so it's safe to call unconditionally.
#
# Usage (after add_subdirectory(luminoveau)):
#   add_executable(mygame main.cpp)
#   target_link_libraries(mygame PRIVATE luminoveau)
#   luminoveau_configure_web_target(mygame)          # auto-detects assets/ + assets/shaders/
#   # or, when sources/outputs aren't the current dir:
#   luminoveau_configure_web_target(mygame SOURCE_DIR <dir> OUTPUT_DIR <dir>)

# Captured at include time (CMAKE_CURRENT_LIST_DIR changes once the function is called elsewhere),
# so the function can always find the engine's shell.html + cmake helpers.
set(LUMINOVEAU_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE INTERNAL "Luminoveau engine root")

if(EMSCRIPTEN)
    include("${CMAKE_CURRENT_LIST_DIR}/ShaderTranspile.cmake")
endif()

function(luminoveau_configure_web_target target)
    if(NOT EMSCRIPTEN)
        return()
    endif()

    cmake_parse_arguments(LW "" "SOURCE_DIR;OUTPUT_DIR;SHELL_FILE" "" ${ARGN})
    if(NOT LW_SOURCE_DIR)
        set(LW_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    if(NOT LW_OUTPUT_DIR)
        set(LW_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    if(NOT LW_SHELL_FILE)
        set(LW_SHELL_FILE "${LUMINOVEAU_ROOT_DIR}/assets/shell.html")
    endif()

    set_target_properties(${target} PROPERTIES SUFFIX ".html")

    # GLSL effect shaders → WGSL (Tint), preloaded into the virtual FS. Only apps with a shaders/
    # folder need this; the transpile helper skips gracefully if Tint isn't available.
    if(EXISTS "${LW_SOURCE_DIR}/assets/shaders")
        lumi_transpile_shaders(
            TARGET      ${target}
            SHADER_DIRS "${LW_SOURCE_DIR}/assets/shaders"
            OUTPUT_DIR  "${LW_OUTPUT_DIR}/_transpiled"
            SOURCE_ROOT "${LW_SOURCE_DIR}"
        )
        target_link_options(${target} PRIVATE
            "SHELL:--preload-file ${LW_OUTPUT_DIR}/_transpiled@/_transpiled")
    endif()

    # Preload the app's assets if present.
    if(EXISTS "${LW_SOURCE_DIR}/assets")
        target_link_options(${target} PRIVATE
            "SHELL:--preload-file ${LW_SOURCE_DIR}/assets@/assets")
    endif()

    # HTML shell + runtime tuning. Debug/RelWithDebInfo keep assertions + exception catching;
    # Release drops them for smaller/faster wasm.
    #   -Oz          : optimize the linked module for size (runs Binaryen wasm-opt -Oz). Big lever for
    #                  the embedded website demos; combine with the smaller emmalloc allocator.
    #   -sMALLOC=emmalloc : ~10-20 KB smaller than the default dlmalloc, fine for these demos.
    target_link_options(${target} PRIVATE
        "SHELL:--shell-file ${LW_SHELL_FILE}"
        -sSTACK_SIZE=262144
        "$<$<CONFIG:Debug,RelWithDebInfo>:-sASSERTIONS=1>"
        "$<$<CONFIG:Debug,RelWithDebInfo>:-sNO_DISABLE_EXCEPTION_CATCHING>"
        "$<$<NOT:$<CONFIG:Debug,RelWithDebInfo>>:-sASSERTIONS=0>"
        "$<$<NOT:$<CONFIG:Debug,RelWithDebInfo>>:-sDISABLE_EXCEPTION_CATCHING=1>"
        "$<$<NOT:$<CONFIG:Debug,RelWithDebInfo>>:-Oz>"
        "$<$<NOT:$<CONFIG:Debug,RelWithDebInfo>>:-sMALLOC=emmalloc>"
    )
endfunction()
