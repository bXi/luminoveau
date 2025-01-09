
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