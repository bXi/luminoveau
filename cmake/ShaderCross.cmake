# SpirvCross.cmake must be included before this file so SPIRV-Cross headers
# are already on the include path when SDL_shadercross.c is compiled.

lumi_msg("Fetching SDL_shadercross")
lumi_fetch("sdl-shadercross" "https://github.com/libsdl-org/SDL_shadercross.git" "6b06e55" _shadercross_src)

if(_shadercross_src)
    target_include_directories(luminoveau SYSTEM PUBLIC
        "${_shadercross_src}/include"
    )

    target_sources(luminoveau PRIVATE
        "${_shadercross_src}/src/SDL_shadercross.c"
    )

    # Force C17 — SDL_shadercross uses bool/NULL in ways that break C23
    set_source_files_properties(
        "${_shadercross_src}/src/SDL_shadercross.c"
        PROPERTIES C_STANDARD 17
    )

    # Enable DXC path only when targeting DXIL — matches DownloadDXC.cmake condition
    if(WIN32 AND LUMINOVEAU_GPU_BACKEND STREQUAL "DXIL")
        target_compile_definitions(luminoveau PRIVATE SDL_SHADERCROSS_DXC)
    endif()

    lumi_done("SDL_shadercross (source-only)")
else()
    lumi_warn("SDL_shadercross - fetch failed")
endif()
