# SteamDetection.cmake
# Detects the presence of the Steam SDK and configures the Luminoveau library to include
# Steam integration, adding source files, include directories, libraries, and runtime files.

# Adding Steam handler sources (always included, but only functional if Steam SDK is found)
target_sources(luminoveau PRIVATE
    src/integrations/steam/steam.cpp
    src/integrations/steam/steam.h
)

# There is no Steamworks in a browser, so the SDK is skipped outright on the web. The stub
# sources above are still compiled: `steam.cpp` is a no-op without LUMINOVEAU_WITH_STEAM, so a
# game can call Steam::Init/IsReady unconditionally and simply get "not ready" there.
#
# **Returning early rather than falling through the platform ladder below**, which would
# otherwise pick the wrong library: Emscripten sets `UNIX` to 1 and leaves `WIN32`/`APPLE` off,
# so `elseif(UNIX AND NOT APPLE)` matches and hands wasm-ld a Linux `.so` — which fails the link
# with "unknown file type" after every object has already compiled.
if(EMSCRIPTEN)
    lumi_msg("Steam SDK skipped (no Steamworks on the web)")
    return()
endif()

# Checking for Steam SDK presence
set(STEAM_SDK_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/integrations/steam/sdk/public/steam/steam_api.h")
if(EXISTS "${STEAM_SDK_HEADER}")
    # Finding required threads library
    find_package(Threads REQUIRED)

    # Setting include directories for Steam SDK
    target_include_directories(luminoveau PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src/integrations/steam/sdk/public/steam"
    )

    # Configuring platform-specific Steam API library
    if(WIN32)
        set(STEAM_API_LIB "${CMAKE_CURRENT_SOURCE_DIR}/src/integrations/steam/sdk/redistributable_bin/win64/steam_api64.lib")
        set(STEAM_API_DLL "${CMAKE_CURRENT_SOURCE_DIR}/src/integrations/steam/sdk/redistributable_bin/win64/steam_api64.dll")
    elseif(UNIX AND NOT APPLE)
        set(STEAM_API_LIB "${CMAKE_CURRENT_SOURCE_DIR}/src/integrations/steam/sdk/redistributable_bin/linux64/libsteam_api.so")
        set(STEAM_API_DLL "${CMAKE_CURRENT_SOURCE_DIR}/src/integrations/steam/sdk/redistributable_bin/linux64/libsteam_api.so")
    elseif(APPLE)
        set(STEAM_API_LIB "${CMAKE_CURRENT_SOURCE_DIR}/src/integrations/steam/sdk/redistributable_bin/osx/libsteam_api.dylib")
        set(STEAM_API_DLL "${CMAKE_CURRENT_SOURCE_DIR}/src/integrations/steam/sdk/redistributable_bin/osx/libsteam_api.dylib")
    else()
        message(WARNING "Unsupported platform for Steam SDK integration")
        return()
    endif()

    # Verifying Steam API library exists
    if(NOT EXISTS "${STEAM_API_LIB}")
        message(WARNING "Steam API library '${STEAM_API_LIB}' not found. Steam integration disabled")
        return()
    endif()

    # Linking Steam API library and threads
    target_link_libraries(luminoveau
        PRIVATE Threads::Threads
        PUBLIC "${STEAM_API_LIB}"
    )

    # Copying Steam API runtime library to the build directory
    if(EXISTS "${STEAM_API_DLL}")
        add_custom_command(
            TARGET luminoveau POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${STEAM_API_DLL}"
                "$<TARGET_FILE_DIR:luminoveau>"
            COMMENT "Copying Steam API runtime library to build directory"
            VERBATIM
        )
    else()
        message(WARNING "Steam API runtime library '${STEAM_API_DLL}' not found")
    endif()

    # Adding compile definition to enable Steam functionality
    target_compile_definitions(luminoveau PUBLIC LUMINOVEAU_WITH_STEAM)

    lumi_done("Steam SDK")
else()
    lumi_msg("Steam SDK not found")
endif()