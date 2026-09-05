# SteamDetection.cmake
# Detects the presence of the Steam SDK and configures the Luminoveau library to include
# Steam integration, adding source files, include directories, libraries, and runtime files.

# Adding Steam handler sources (always included, but only functional if Steam SDK is found)
target_sources(luminoveau PRIVATE
    src/integrations/steam/steam.cpp
    src/integrations/steam/steam.h
    src/integrations/steam/steambroker.cpp
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

# Read by DependencySetup.cmake, which must not fetch GameNetworkingSockets when the
# Steamworks SDK is present: both install a header called steam/steamnetworkingtypes.h, and
# letting include order decide the winner is an ABI mismatch on a vtable.
set(STEAM_SDK_FOUND FALSE)

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

    # Telling consumers where the runtime library is.
    #
    # **The copy below is not enough, because luminoveau is a static library.** Its
    # `TARGET_FILE_DIR` is wherever the archive lands (`lib/`), and no executable looks there —
    # so a game that inherits the `steam_api64.lib` import above fails to start with
    # STATUS_DLL_NOT_FOUND (0xC0000135), before `main` and with no message. Only the consumer
    # knows where its own executable goes, so it has to do that copy itself; this is the path it
    # needs. Cached because `add_subdirectory` would otherwise keep it in a child scope.
    set(LUMINOVEAU_STEAM_RUNTIME "${STEAM_API_DLL}" CACHE INTERNAL
        "Steam API runtime library, to be copied beside the consuming executable")

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

    # Adding compile definition to enable Steam functionality. STEAMNETWORKINGSOCKETS_STEAMAPI
    # is what the SDK's headers default to anyway; saying it out loud keeps the either/or with
    # STANDALONELIB visible where the choice is actually made.
    target_compile_definitions(luminoveau PUBLIC LUMINOVEAU_WITH_STEAM STEAMNETWORKINGSOCKETS_STEAMAPI)
    set(STEAM_SDK_FOUND TRUE)

    lumi_done("Steam SDK")
else()
    lumi_msg("Steam SDK not found")
endif()