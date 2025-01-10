
set(SDL_VIDEO_DRIVER_X11_SUPPORTS_GENERIC_EVENTS ON)

#remove AV1 support in SDL3Image because it requires perl and nasm which just breaks in MSVC
set(SDL3IMAGE_AVIF_SAVE OFF)
set(SDL3IMAGE_AVIF OFF)

if (ANDROID) #for android we want to build shared libs
    set(BUILD_SHARED_LIBS ON)
    set(SDL_STATIC OFF)
    set(SDL_SHARED ON)
    set(INTERFACE_SDL3_SHARED ON)
else ()

    set(BUILD_SHARED_LIBS OFF)
    set(SDL_STATIC ON)
    set(SDL_SHARED OFF)
    set(INTERFACE_SDL3_SHARED OFF)
    set(SDL_VIDEO_DRIVER_X11_SUPPORTS_GENERIC_EVENTS ON)

endif ()

CPMAddPackage(
        NAME glm
        GIT_TAG 33b4a62
        GITHUB_REPOSITORY g-truc/glm
)

if (glm_ADDED)
    target_include_directories("luminoveau" PUBLIC ${glm_SOURCE_DIR})
endif ()

set(SPIRV_CROSS_STATIC ON)
set(SPIRV_CROSS_CLI OFF)
set(SPIRV_CROSS_ENABLE_TESTS OFF)

CPMAddPackage(
        NAME SPIRV-Cross
        GIT_TAG 6173e24
        GITHUB_REPOSITORY KhronosGroup/SPIRV-Cross
)

if (SPIRV-Cross_ADDED)
    target_include_directories("luminoveau" PUBLIC ${SPIRV-Cross_SOURCE_DIR})
endif ()

CPMAddPackage(
        NAME SDL3
        GIT_TAG c030e6f
        GITHUB_REPOSITORY libsdl-org/SDL
)

if (SDL3_ADDED)
    target_include_directories("luminoveau" PUBLIC ${SDL3_SOURCE_DIR}/include)
endif ()

CPMAddPackage(
        NAME harfbuzz
        GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
        GIT_TAG 40ef6c0
)

if (NOT MSVC AND NOT ANDROID)
    CPMAddPackage(
            NAME freetype
            GIT_REPOSITORY https://github.com/freetype/freetype.git
            GIT_TAG 59320b2
    )

    if (freetype_ADDED)
        add_library(Freetype::Freetype ALIAS freetype)
    endif ()
endif ()

CPMAddPackage(
        NAME SDL3_ttf
        GIT_TAG 35f67d2
        GITHUB_REPOSITORY libsdl-org/SDL_ttf
)

if (SDL3_ttf_ADDED)
    target_include_directories("luminoveau" PUBLIC ${SDL3_ttf_SOURCE_DIR}/include)
endif ()

set(PHYSFS_BUILD_SHARED OFF)
set(PHYSFS_BUILD_TEST OFF)
set(PHYSFS_DISABLE_INSTALL ON)
set(PHYSFS_BUILD_DOCS OFF)

CPMAddPackage(
        NAME physfs
        GIT_TAG 7726d18
        GITHUB_REPOSITORY icculus/physfs
        DOWNLOAD_ONLY
)

if (physfs_ADDED)

    target_sources("luminoveau" PRIVATE
            ${physfs_SOURCE_DIR}/src/physfs.c
            ${physfs_SOURCE_DIR}/src/physfs_byteorder.c
            ${physfs_SOURCE_DIR}/src/physfs_unicode.c
            ${physfs_SOURCE_DIR}/src/physfs_platform_posix.c
            ${physfs_SOURCE_DIR}/src/physfs_platform_unix.c
            ${physfs_SOURCE_DIR}/src/physfs_platform_windows.c
            ${physfs_SOURCE_DIR}/src/physfs_platform_ogc.c
            ${physfs_SOURCE_DIR}/src/physfs_platform_os2.c
            ${physfs_SOURCE_DIR}/src/physfs_platform_qnx.c
            ${physfs_SOURCE_DIR}/src/physfs_platform_android.c
            ${physfs_SOURCE_DIR}/src/physfs_platform_playdate.c
            ${physfs_SOURCE_DIR}/src/physfs_archiver_dir.c
            ${physfs_SOURCE_DIR}/src/physfs_archiver_unpacked.c
            ${physfs_SOURCE_DIR}/src/physfs_archiver_grp.c
            ${physfs_SOURCE_DIR}/src/physfs_archiver_hog.c
            ${physfs_SOURCE_DIR}/src/physfs_archiver_7z.c
            ${physfs_SOURCE_DIR}/src/physfs_archiver_mvl.c
            ${physfs_SOURCE_DIR}/src/physfs_archiver_qpak.c
            ${physfs_SOURCE_DIR}/src/physfs_archiver_wad.c
            ${physfs_SOURCE_DIR}/src/physfs_archiver_csm.c
            ${physfs_SOURCE_DIR}/src/physfs_archiver_zip.c
            ${physfs_SOURCE_DIR}/src/physfs_archiver_slb.c
            ${physfs_SOURCE_DIR}/src/physfs_archiver_iso9660.c
            ${physfs_SOURCE_DIR}/src/physfs_archiver_vdf.c
            ${physfs_SOURCE_DIR}/src/physfs_archiver_lec3d.c
    )

    if (WINRT)
        target_sources("luminoveau" PRIVATE
                ${physfs_SOURCE_DIR}/src/physfs_platform_winrt.cpp
        )

    endif ()

    if (APPLE)
        set(OTHER_LDFLAGS ${OTHER_LDFLAGS} "-framework IOKit -framework Foundation")
        target_sources("luminoveau" PRIVATE
                ${physfs_SOURCE_DIR}/src/physfs_platform_apple.m
        )
    endif ()

    target_include_directories("luminoveau" PUBLIC ${physfs_SOURCE_DIR}/src)
endif ()

if (ANDROID)
    target_link_libraries("luminoveau" PUBLIC
            SDL3::SDL3
            SDL3_ttf::SDL3_ttf
            spirv-cross-c
    )
else ()
    target_link_libraries("luminoveau" PUBLIC
            SDL3::SDL3-static
            SDL3_ttf::SDL3_ttf
            spirv-cross-c
    )
endif ()