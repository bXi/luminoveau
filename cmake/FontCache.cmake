# Optional pre-baked font cache (LUMINOVEAU_BAKE_FONT_CACHE).
#
# When ON, luminoveau_bake_font_cache() scans a game's asset tree for fonts (*.otf/*.ttf) and bakes
# every one into a `font.cache` ResourcePack via the PREBUILT font_baker tool, so the game ships
# pre-generated MSDF atlases. On 3DS this is the only way to get non-default fonts (no runtime MSDF
# generator); on PC/web it just skips the first-run generation spike.
#
# The tool is a committed host binary (tools/bin/<platform>/font_baker) built by tools/build-tools.*.
# Because it's prebuilt for the BUILD MACHINE, the bake runs on any target build — native OR cross
# (3DS/web) — without needing a host compiler in the cross toolchain. Rebuild it (tools/build-tools)
# only when font_baker.cpp changes; regenerate font.cache automatically when the fonts change.

option(LUMINOVEAU_BAKE_FONT_CACHE "Bake assets/*.otf into a shipped font.cache" OFF)

# Locate the prebuilt font_baker. Probe each platform's committed binary rather than trusting the
# host cmake's OS report (the devkitPro msys2 cmake reports UNIX during a 3DS build). Windows first.
function(_lumi_font_baker_bin OUTVAR)
    foreach(_p "win_x64/font_baker.exe" "linux_x64/font_baker" "mac_arm64/font_baker" "mac_x64/font_baker")
        if(EXISTS "${LUMINOVEAU_ROOT_DIR}/tools/bin/${_p}")
            set(${OUTVAR} "${LUMINOVEAU_ROOT_DIR}/tools/bin/${_p}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${OUTVAR} "" PARENT_SCOPE)
endfunction()

# luminoveau_bake_font_cache(<target> <assets_src_dir> <output_font_cache>)
function(luminoveau_bake_font_cache TARGET ASSETS_SRC_DIR OUTPUT_FONT_CACHE)
    if(NOT LUMINOVEAU_BAKE_FONT_CACHE)
        return()
    endif()

    _lumi_font_baker_bin(_baker)
    if(NOT EXISTS "${_baker}")
        message(WARNING "font cache: prebuilt font_baker not found at ${_baker} — run tools/build-tools "
                        "on a host with a C++ compiler and commit tools/bin/. Skipping.")
        return()
    endif()

    file(GLOB_RECURSE _fonts CONFIGURE_DEPENDS
        "${ASSETS_SRC_DIR}/*.otf" "${ASSETS_SRC_DIR}/*.ttf"
        "${ASSETS_SRC_DIR}/*.OTF" "${ASSETS_SRC_DIR}/*.TTF")
    if(NOT _fonts)
        message(STATUS "font cache: no fonts found under ${ASSETS_SRC_DIR}")
        return()
    endif()

    # Keys are computed relative to the game root (the dir the game treats as CWD) so they match the
    # "assets/..." paths the game passes to GetFont. That root is the parent of the assets dir.
    get_filename_component(_gameroot "${ASSETS_SRC_DIR}" DIRECTORY)
    add_custom_command(
        OUTPUT "${OUTPUT_FONT_CACHE}"
        COMMAND "${_baker}" --cache "${OUTPUT_FONT_CACHE}" --base "${_gameroot}" ${_fonts}
        DEPENDS "${_baker}" ${_fonts}
        COMMENT "Baking ${OUTPUT_FONT_CACHE} from ${ASSETS_SRC_DIR} fonts")
    add_custom_target(${TARGET}_font_cache DEPENDS "${OUTPUT_FONT_CACHE}")
    add_dependencies(${TARGET} ${TARGET}_font_cache)
endfunction()
