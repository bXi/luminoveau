# DependencySetup.cmake
# Configures external dependencies for the Luminoveau library using CPM, including GLM,
# SPIRV-Cross, glslang, SDL3, SDL3_ttf, harfbuzz, freetype, and physfs, with platform-specific settings.

# Ensuring CPM is available
if(NOT COMMAND CPMAddPackage)
    message(FATAL_ERROR "CPM.cmake is required for dependency setup but not included")
endif()

# Configuring SDL options
set(SDL_VIDEO_DRIVER_X11_SUPPORTS_GENERIC_EVENTS ON CACHE BOOL "Enable X11 generic events" FORCE)

# Disabling AV1 support in SDL3_image to avoid Perl/NASM issues on MSVC
set(SDL3IMAGE_AVIF OFF CACHE BOOL "Disable AVIF support in SDL3_image" FORCE)
set(SDL3IMAGE_AVIF_SAVE OFF CACHE BOOL "Disable AVIF saving in SDL3_image" FORCE)

# Platform-specific build settings
if(ANDROID)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries for Android" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "Disable SDL static library" FORCE)
    set(SDL_SHARED ON CACHE BOOL "Enable SDL shared library" FORCE)
    set(INTERFACE_SDL3_SHARED ON CACHE BOOL "Use shared SDL3 interface" FORCE)
else()
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries for non-Android platforms" FORCE)
    set(SDL_STATIC ON CACHE BOOL "Enable SDL static library" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "Disable SDL shared library" FORCE)
    set(INTERFACE_SDL3_SHARED OFF CACHE BOOL "Use static SDL3 interface" FORCE)
endif()

# Fetching GLM
CPMAddPackage(
    NAME glm
    GITHUB_REPOSITORY g-truc/glm
    GIT_TAG 69b130c
    OPTIONS
        "GLM_ENABLE_FAST_MATH ON"
        "GLM_BUILD_TESTS OFF"
        "GLM_BUILD_INSTALL OFF"
)
if(glm_ADDED)
    if(NOT EXISTS "${glm_SOURCE_DIR}")
        message(FATAL_ERROR "GLM source directory '${glm_SOURCE_DIR}' does not exist")
    endif()
    target_include_directories(luminoveau SYSTEM PUBLIC "${glm_SOURCE_DIR}")
    target_compile_definitions(luminoveau PUBLIC
        GLM_FORCE_INTRINSICS
        GLM_FORCE_INLINE
        GLM_FORCE_EXPLICIT_CTOR
        GLM_FORCE_SIZE_T_LENGTH
    )
    message(STATUS "Luminoveau: GLM configured")
else()
    message(WARNING "Failed to fetch GLM. Some functionality may be unavailable")
endif()

# Fetching fmt (for logging with source_location support)
CPMAddPackage(
    NAME fmt
    GITHUB_REPOSITORY fmtlib/fmt
    GIT_TAG 10.2.1
    OPTIONS
        "FMT_INSTALL OFF"
        "FMT_TEST OFF"
        "FMT_DOC OFF"
)
if(fmt_ADDED)
    target_link_libraries(luminoveau PUBLIC fmt::fmt)
    message(STATUS "Luminoveau: fmt configured")
else()
    message(WARNING "Failed to fetch fmt")
endif()

# Fetching glslang (disabling unwanted components)
set(ENABLE_OPT OFF CACHE BOOL "Disable glslang optimizations" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disable glslang tests" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "Disable glslang binaries" FORCE)

CPMAddPackage(
    NAME glslang
    GITHUB_REPOSITORY KhronosGroup/glslang
    GIT_TAG e435148
)
if(glslang_ADDED)
    if(NOT EXISTS "${glslang_SOURCE_DIR}")
        message(FATAL_ERROR "glslang source directory '${glslang_SOURCE_DIR}' does not exist")
    endif()
    target_include_directories(luminoveau SYSTEM PUBLIC "${glslang_SOURCE_DIR}")
    target_link_libraries(luminoveau PUBLIC glslang SPIRV)
    message(STATUS "Luminoveau: glslang configured")
else()
    message(WARNING "Failed to fetch glslang. Shader compilation may be unavailable")
endif()

# Fetching SDL3
CPMAddPackage(
    NAME SDL3
    GITHUB_REPOSITORY libsdl-org/SDL
    GIT_TAG a962f40
)
if(SDL3_ADDED)
    if(NOT EXISTS "${SDL3_SOURCE_DIR}/include")
        message(FATAL_ERROR "SDL3 include directory '${SDL3_SOURCE_DIR}/include' does not exist")
    endif()
    target_include_directories(luminoveau SYSTEM PUBLIC "${SDL3_SOURCE_DIR}/include")
    if(ANDROID)
        target_link_libraries(luminoveau PUBLIC SDL3::SDL3)
    else()
        target_link_libraries(luminoveau PUBLIC SDL3::SDL3-static)
    endif()
    message(STATUS "Luminoveau: SDL3 configured")
else()
    message(FATAL_ERROR "Failed to fetch SDL3. SDL3 is a required dependency")
endif()

# Fetching SDL_shadercross (shader translation library)
# NOTE: Must come AFTER SDL3 since it depends on it
set(SDLSHADERCROSS_SHARED OFF CACHE BOOL "Build static SDL_shadercross library" FORCE)
set(SDLSHADERCROSS_STATIC ON CACHE BOOL "Build static SDL_shadercross library" FORCE)
set(SDLSHADERCROSS_CLI OFF CACHE BOOL "Disable SDL_shadercross CLI" FORCE)
set(SDLSHADERCROSS_VENDORED ON CACHE BOOL "Use vendored dependencies (required)" FORCE)
set(SDLSHADERCROSS_SPIRVCROSS_SHARED OFF CACHE BOOL "Build SPIRV-Cross statically into SDL_shadercross" FORCE)
set(SDLSHADERCROSS_DXC OFF CACHE BOOL "Disable DXC compilation (use runtime DLLs instead)" FORCE)

CPMAddPackage(
    NAME SDL_shadercross
    GITHUB_REPOSITORY libsdl-org/SDL_shadercross
    GIT_TAG main
)
if(SDL_shadercross_ADDED)
    if(NOT EXISTS "${SDL_shadercross_SOURCE_DIR}")
        message(FATAL_ERROR "SDL_shadercross source directory '${SDL_shadercross_SOURCE_DIR}' does not exist")
    endif()
    target_include_directories(luminoveau SYSTEM PUBLIC "${SDL_shadercross_SOURCE_DIR}/include")
    target_link_libraries(luminoveau PUBLIC SDL3_shadercross::SDL3_shadercross-static)
    
    # Use SDL_shadercross's vendored SPIRV-Cross for our shader reflection needs
    target_include_directories(luminoveau SYSTEM PUBLIC "${SDL_shadercross_SOURCE_DIR}/external/SPIRV-Cross")
    # Link against the vendored SPIRV-Cross targets (built statically by SDL_shadercross)
    target_link_libraries(luminoveau PUBLIC
        spirv-cross-core
        spirv-cross-glsl
    )
    
    message(STATUS "Luminoveau: SDL_shadercross configured with static SPIRV-Cross")
else()
    message(WARNING "Failed to fetch SDL_shadercross. Runtime shader compilation may be unavailable")
endif()

# Fetching harfbuzz (non-MSVC only)
if(NOT MSVC)
    CPMAddPackage(
        NAME harfbuzz
        GITHUB_REPOSITORY harfbuzz/harfbuzz
        GIT_TAG 40ef6c0
        OPTIONS
            "HB_BUILD_UTILS OFF"
            "HB_BUILD_SUBSET OFF"
    )
    if(harfbuzz_ADDED)
        if(NOT EXISTS "${harfbuzz_SOURCE_DIR}")
            message(FATAL_ERROR "harfbuzz source directory '${harfbuzz_SOURCE_DIR}' does not exist")
        endif()
        target_include_directories(luminoveau SYSTEM PUBLIC "${harfbuzz_SOURCE_DIR}/src")
        target_link_libraries(luminoveau PUBLIC harfbuzz)
        # Add -Wa,-mbig-obj for MinGW to handle large harfbuzz.cc
        if(MINGW)
            target_compile_options(harfbuzz PRIVATE -Wa,-mbig-obj)
        endif()
        message(STATUS "Luminoveau: harfbuzz configured")
    else()
        message(WARNING "Failed to fetch harfbuzz. Text rendering may be limited")
    endif()
endif()


if (NOT MSVC)
# Fetching freetype (always fetched to support SDL3_ttf)
CPMAddPackage(
    NAME freetype
    GITHUB_REPOSITORY freetype/freetype
    GIT_TAG b1f4785
    OPTIONS
        "FT_DISABLE_ZLIB ON"
        "FT_DISABLE_BZIP2 ON"
        "FT_DISABLE_PNG ON"
        "FT_DISABLE_HARFBUZZ OFF"
        "FT_DISABLE_BROTLI ON"
)
if(freetype_ADDED)
    if(NOT EXISTS "${freetype_SOURCE_DIR}")
        message(FATAL_ERROR "freetype source directory '${freetype_SOURCE_DIR}' does not exist")
    endif()
    target_include_directories(luminoveau SYSTEM PUBLIC "${freetype_SOURCE_DIR}/include")
    target_link_libraries(luminoveau PUBLIC freetype)
    # Creating alias expected by SDL3_ttf
    add_library(Freetype::Freetype ALIAS freetype)
    message(STATUS "Luminoveau: freetype configured")
else()
    message(WARNING "Failed to fetch freetype. Font rendering may be unavailable")
endif()
endif()

# Fetching SDL3_ttf
CPMAddPackage(
    NAME SDL3_ttf
    GITHUB_REPOSITORY libsdl-org/SDL_ttf
    GIT_TAG a1ce367
)
if(SDL3_ttf_ADDED)
    if(NOT EXISTS "${SDL3_ttf_SOURCE_DIR}")
        message(FATAL_ERROR "SDL3_ttf include directory '${SDL3_ttf_SOURCE_DIR}' does not exist")
    endif()
    target_include_directories(luminoveau SYSTEM PUBLIC "${SDL3_ttf_SOURCE_DIR}")
    target_link_libraries(luminoveau PUBLIC SDL3_ttf::SDL3_ttf)
    message(STATUS "Luminoveau: SDL3_ttf configured")
else()
    message(WARNING "Failed to fetch SDL3_ttf. Text rendering may be unavailable")
endif()

# Fetching physfs
set(PHYSFS_BUILD_SHARED OFF CACHE BOOL "Disable physfs shared library" FORCE)
set(PHYSFS_BUILD_TEST OFF CACHE BOOL "Disable physfs tests" FORCE)
set(PHYSFS_BUILD_DOCS OFF CACHE BOOL "Disable physfs documentation" FORCE)
set(PHYSFS_DISABLE_INSTALL ON CACHE BOOL "Disable physfs installation" FORCE)

CPMAddPackage(
    NAME physfs
    GITHUB_REPOSITORY icculus/physfs
    GIT_TAG 7726d18
)
if(physfs_ADDED)
    if(NOT EXISTS "${physfs_SOURCE_DIR}/src")
        message(FATAL_ERROR "physfs source directory '${physfs_SOURCE_DIR}/src' does not exist")
    endif()
    target_include_directories(luminoveau SYSTEM PUBLIC "${physfs_SOURCE_DIR}/src")
    target_sources(luminoveau PRIVATE
        "${physfs_SOURCE_DIR}/src/physfs.c"
        "${physfs_SOURCE_DIR}/src/physfs_byteorder.c"
        "${physfs_SOURCE_DIR}/src/physfs_unicode.c"
        "${physfs_SOURCE_DIR}/src/physfs_platform_posix.c"
        "${physfs_SOURCE_DIR}/src/physfs_platform_unix.c"
        "${physfs_SOURCE_DIR}/src/physfs_platform_windows.c"
        "${physfs_SOURCE_DIR}/src/physfs_platform_ogc.c"
        "${physfs_SOURCE_DIR}/src/physfs_platform_os2.c"
        "${physfs_SOURCE_DIR}/src/physfs_platform_qnx.c"
        "${physfs_SOURCE_DIR}/src/physfs_platform_android.c"
        "${physfs_SOURCE_DIR}/src/physfs_platform_playdate.c"
        "${physfs_SOURCE_DIR}/src/physfs_archiver_dir.c"
        "${physfs_SOURCE_DIR}/src/physfs_archiver_unpacked.c"
        "${physfs_SOURCE_DIR}/src/physfs_archiver_grp.c"
        "${physfs_SOURCE_DIR}/src/physfs_archiver_hog.c"
        "${physfs_SOURCE_DIR}/src/physfs_archiver_7z.c"
        "${physfs_SOURCE_DIR}/src/physfs_archiver_mvl.c"
        "${physfs_SOURCE_DIR}/src/physfs_archiver_qpak.c"
        "${physfs_SOURCE_DIR}/src/physfs_archiver_wad.c"
        "${physfs_SOURCE_DIR}/src/physfs_archiver_csm.c"
        "${physfs_SOURCE_DIR}/src/physfs_archiver_zip.c"
        "${physfs_SOURCE_DIR}/src/physfs_archiver_slb.c"
        "${physfs_SOURCE_DIR}/src/physfs_archiver_iso9660.c"
        "${physfs_SOURCE_DIR}/src/physfs_archiver_vdf.c"
        "${physfs_SOURCE_DIR}/src/physfs_archiver_lec3d.c"
    )
    if(WINRT)
        target_sources(luminoveau PRIVATE
            "${physfs_SOURCE_DIR}/src/physfs_platform_winrt.cpp"
        )
    endif()
    if(APPLE)
        target_sources(luminoveau PRIVATE
            "${physfs_SOURCE_DIR}/src/physfs_platform_apple.m"
        )
        target_link_libraries(luminoveau PUBLIC "-framework IOKit" "-framework Foundation")
    endif()
    message(STATUS "Luminoveau: physfs configured")
else()
    message(WARNING "Failed to fetch physfs. File system access may be unavailable")
endif()