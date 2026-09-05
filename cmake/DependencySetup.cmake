# DependencySetup.cmake
# Configures external dependencies for the Luminoveau library using CPM, including GLM,
# SPIRV-Cross, glslang, SDL3, SDL3_image, SDL_shadercross, freetype, MSDF-atlas-gen, and physfs,
# with platform-specific settings.
#
# All dependencies use EXCLUDE_FROM_ALL to keep their targets out of the default build.
# Library targets remain fully linkable - only tests, examples, and CLI tools are excluded.

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
lumi_msg("Fetching GLM")
CPMAddPackage(
    NAME glm
    GITHUB_REPOSITORY g-truc/glm
    GIT_TAG 69b130c
    EXCLUDE_FROM_ALL YES
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
    lumi_done("GLM")
else()
    lumi_warn("GLM - fetch failed")
endif()

# Fetching fmt
# Using 9.1.0: 10.x uses consteval which breaks Emscripten's clang (consteval-in-lambda bug).
lumi_msg("Fetching fmt")

if(MSVC)
    set(FMT_VERSION 10.2.1)
else()
    set(FMT_VERSION 9.1.0)
endif()

CPMAddPackage(
    NAME fmt
    GITHUB_REPOSITORY fmtlib/fmt
    GIT_TAG ${FMT_VERSION}
    EXCLUDE_FROM_ALL YES
    OPTIONS
        "FMT_INSTALL OFF"
        "FMT_TEST OFF"
        "FMT_DOC OFF"
        "FMT_FUZZ OFF"
        "FMT_OS OFF"
)
if(fmt_ADDED)
    target_link_libraries(luminoveau PUBLIC fmt::fmt)
    # Treat fmt headers as system includes to suppress deprecation warnings from fmt internals
    get_target_property(_fmt_inc fmt::fmt INTERFACE_INCLUDE_DIRECTORIES)
    if(_fmt_inc)
        target_include_directories(luminoveau SYSTEM PUBLIC ${_fmt_inc})
    endif()
    lumi_done("fmt")
else()
    lumi_warn("fmt - fetch failed")
endif()

# Fetching SDL3
# Disable examples, tests, and test applications to avoid unnecessary builds
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Disable SDL test library" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Disable SDL tests" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "Disable SDL examples" FORCE)
set(SDL_INSTALL_TESTS OFF CACHE BOOL "Disable SDL test installation" FORCE)
set(SDL_DISABLE_INSTALL OFF CACHE BOOL "Enable SDL installation" FORCE)
set(SDL3_DISABLE_INSTALL OFF CACHE BOOL "Enable SDL3 installation" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "Disable SDL audio (Luminoveau uses miniaudio)" FORCE)
set(SDL_VULKAN ON CACHE BOOL "Force SDL Vulkan video backend" FORCE)
if(WIN32)
    set(SDL_DIRECTX ON CACHE BOOL "Force SDL DirectX video backend" FORCE)
endif()

lumi_msg("Fetching SDL3")
CPMAddPackage(
    NAME SDL3
    GITHUB_REPOSITORY libsdl-org/SDL
    GIT_TAG a962f40
    EXCLUDE_FROM_ALL YES
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
    lumi_done("SDL3")
else()
    lumi_fail("SDL3 - required dependency")
    message(FATAL_ERROR "Failed to fetch SDL3")
endif()

# Fetching SDL3_image
set(SDL3IMAGE_INSTALL OFF CACHE BOOL "Disable SDL3_image installation" FORCE)
set(SDL3IMAGE_DEPS_SHARED OFF CACHE BOOL "Use static dependencies" FORCE)
set(SDL3IMAGE_VENDORED ON CACHE BOOL "Use vendored dependencies" FORCE)
set(SDL3IMAGE_BUILD_SHARED_LIBS OFF CACHE BOOL "Build static SDL3_image" FORCE)
set(SDL3IMAGE_SAMPLES OFF CACHE BOOL "Disable SDL3_image samples" FORCE)
set(SDL3IMAGE_TESTS OFF CACHE BOOL "Disable SDL3_image tests" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)

lumi_msg("Fetching SDL3_image")
CPMAddPackage(
    NAME SDL3_image
    GITHUB_REPOSITORY libsdl-org/SDL_image
    GIT_TAG cc0b1ff
    EXCLUDE_FROM_ALL YES
)
if(SDL3_image_ADDED)
    if(NOT EXISTS "${SDL3_image_SOURCE_DIR}/include")
        message(FATAL_ERROR "SDL3_image include directory '${SDL3_image_SOURCE_DIR}/include' does not exist")
    endif()
    target_include_directories(luminoveau SYSTEM PUBLIC "${SDL3_image_SOURCE_DIR}/include")
    target_link_libraries(luminoveau PUBLIC SDL3_image::SDL3_image-static)
    lumi_done("SDL3_image")
else()
    lumi_warn("SDL3_image - fetch failed")
endif()

# Fetching SDL3_net (networking — native only; browsers can't do raw TCP/UDP).
if(NOT EMSCRIPTEN)
    set(SDL3NET_INSTALL    OFF CACHE BOOL "Disable SDL3_net installation"   FORCE)
    set(SDL3NET_SAMPLES    OFF CACHE BOOL "Disable SDL3_net samples"        FORCE)
    set(SDL3NET_TESTS      OFF CACHE BOOL "Disable SDL3_net tests"          FORCE)
    set(BUILD_SHARED_LIBS  OFF CACHE BOOL "Build static libraries"          FORCE)

    lumi_msg("Fetching SDL3_net")
    CPMAddPackage(
        NAME SDL3_net
        GITHUB_REPOSITORY libsdl-org/SDL_net
        GIT_TAG 4ffa92a
        EXCLUDE_FROM_ALL YES
    )
    if(SDL3_net_ADDED)
        if(EXISTS "${SDL3_net_SOURCE_DIR}/include")
            target_include_directories(luminoveau SYSTEM PUBLIC "${SDL3_net_SOURCE_DIR}/include")
        endif()
        target_link_libraries(luminoveau PUBLIC SDL3_net::SDL3_net-static)
        target_compile_definitions(luminoveau PUBLIC LUMINOVEAU_WITH_NET=1)
        lumi_done("SDL3_net")
    else()
        lumi_warn("SDL3_net - fetch failed (networking disabled)")
    endif()
endif()

# Fetching GameNetworkingSockets (encrypted reliable transport; the path to Steam P2P).
#
# ISteamNetworkingSockets is not Steam-only — GNS ships the same public headers under BSD-3,
# so one transport implementation drives either library. Which one is decided at configure
# time by SteamDetection.cmake, because both install a header called
# steam/steamnetworkingtypes.h and letting include order pick the winner is an ABI mismatch
# on a vtable waiting to happen.
if(LUMINOVEAU_WITH_GNS AND NOT EMSCRIPTEN)
    if(STEAM_SDK_FOUND)
        # The Steamworks SDK already provides these sockets, and its own build of them is the
        # one that gets SDR relay. Fetching GNS as well would only create the header clash.
        target_compile_definitions(luminoveau PUBLIC LUMINOVEAU_WITH_GNS=1)
        target_sources(luminoveau PRIVATE src/platform/net/gns/transport_gns.cpp)
        lumi_done("GameNetworkingSockets (via the Steamworks SDK)")
    else()
        # GNS does not vendor protobuf — its own CMakeLists does find_package(Protobuf REQUIRED)
        # — so protobuf is fetched here first rather than expected from the system. 3.21.12 is
        # the last release before protobuf took a dependency on Abseil, which keeps this to one
        # extra source tree instead of two.
        set(protobuf_BUILD_TESTS       OFF CACHE BOOL "Disable protobuf tests"       FORCE)
        set(protobuf_BUILD_EXAMPLES    OFF CACHE BOOL "Disable protobuf examples"    FORCE)
        set(protobuf_BUILD_CONFORMANCE OFF CACHE BOOL "Disable conformance tests"    FORCE)
        set(protobuf_WITH_ZLIB         OFF CACHE BOOL "No zlib in protobuf"          FORCE)
        # Left ON deliberately: GNS exports a target that links libprotobuf, and CMake refuses
        # to generate an install(EXPORT) naming a target that is in no export set.
        set(protobuf_INSTALL           ON  CACHE BOOL "Export the protobuf targets"  FORCE)
        set(BUILD_SHARED_LIBS          OFF CACHE BOOL "Build static libraries"       FORCE)

        lumi_msg("Fetching protobuf")
        CPMAddPackage(
            NAME protobuf
            GITHUB_REPOSITORY protocolbuffers/protobuf
            GIT_TAG v3.21.12
            EXCLUDE_FROM_ALL YES
        )

        if(NOT protobuf_ADDED)
            lumi_warn("protobuf - fetch failed (GNS transport disabled)")
        else()
            # CMake's own FindProtobuf needs exactly these two to report success; everything else
            # it hands GNS is a target name, and adding protobuf above already defined
            # protobuf::libprotobuf and protobuf::protoc. Pre-seeding them keeps its find_path and
            # find_library off the system.
            set(Protobuf_INCLUDE_DIR "${protobuf_SOURCE_DIR}/src" CACHE PATH   "" FORCE)
            set(Protobuf_LIBRARIES   protobuf::libprotobuf        CACHE STRING "" FORCE)

            # GNS calls protobuf_generate_cpp, which exists only because FindProtobuf defines it.
            # Adding protobuf above leaves a config file behind for it, and that redirect wins
            # over module mode — even for find_package(Protobuf MODULE) — so the module never
            # loads and the function never appears. Including it outright is what actually runs
            # it. protoc is then invoked through the protobuf::protoc target rather than through
            # Protobuf_PROTOC_EXECUTABLE, so a system protoc is neither needed nor consulted.
            include(FindProtobuf)
            set(BUILD_STATIC_LIB   ON  CACHE BOOL "Build the GNS static library" FORCE)
            set(BUILD_SHARED_LIB   OFF CACHE BOOL "No GNS shared library"        FORCE)
            set(BUILD_TESTS        OFF CACHE BOOL "Disable GNS tests"            FORCE)
            set(BUILD_TOOLS        OFF CACHE BOOL "Disable GNS tools"            FORCE)
            # BCrypt ships with Windows, which keeps OpenSSL off that platform entirely. Elsewhere
            # GNS still wants OpenSSL or libsodium for AES/SHA256 — it offers no bundled option.
            if(WIN32)
                set(USE_CRYPTO "BCrypt" CACHE STRING "" FORCE)
            endif()

            if(NOT COMMAND protobuf_generate_cpp)
                lumi_warn("protobuf_generate_cpp unavailable - GNS transport disabled")
            else()
                lumi_msg("Fetching GameNetworkingSockets")
                CPMAddPackage(
                    NAME GameNetworkingSockets
                    GITHUB_REPOSITORY ValveSoftware/GameNetworkingSockets
                    GIT_TAG v1.6.0
                    EXCLUDE_FROM_ALL YES
                )
                if(GameNetworkingSockets_ADDED)
                    target_link_libraries(luminoveau PUBLIC GameNetworkingSockets::static)
                    target_compile_definitions(luminoveau PUBLIC STEAMNETWORKINGSOCKETS_STANDALONELIB)
                    target_compile_definitions(luminoveau PUBLIC LUMINOVEAU_WITH_GNS=1)
                    target_sources(luminoveau PRIVATE src/platform/net/gns/transport_gns.cpp)
                    lumi_done("GameNetworkingSockets")
                else()
                    lumi_warn("GameNetworkingSockets - fetch failed (GNS transport disabled)")
                endif()
            endif()
        endif()
    endif()
endif()

# Fetching WebRTC data channels (the one transport that reaches a browser).
#
# Two libraries, one API: libdatachannel natively and datachannel-wasm in the browser, which
# deliberately mirrors its interface. That is what lets a single ITransport implementation
# serve both, so nothing here is written twice.
#
# Licensing: libdatachannel and libjuice are MPL-2.0, the rest MIT/BSD/Apache-2.0. MPL is
# per-file copyleft, so linking leaves the engine and its games untouched — but it obliges
# whoever ships a binary to keep the source of those files available. Vendored unmodified so
# that obligation stays a link to upstream. Do not patch them.
if(LUMINOVEAU_WITH_WEBRTC)
    # Signalling messages are JSON. libdatachannel vendors this too, but only to build its
    # examples, which are off — so fetching it here collides with nothing and gives the
    # browser half, the native half and the signalling server one shared definition. The
    # release archive rather than the repository: the repository carries a very large test
    # corpus that nothing here needs.
    lumi_msg("Fetching nlohmann/json")
    CPMAddPackage(
        NAME nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        VERSION 3.11.3
        OPTIONS "JSON_BuildTests OFF" "JSON_Install OFF"
        EXCLUDE_FROM_ALL YES
    )
    if(nlohmann_json_ADDED)
        target_link_libraries(luminoveau PUBLIC nlohmann_json::nlohmann_json)
        lumi_done("nlohmann/json")
    else()
        lumi_warn("nlohmann/json - fetch failed (WebRTC signalling disabled)")
    endif()

    if(EMSCRIPTEN)
        lumi_msg("Fetching datachannel-wasm")
        CPMAddPackage(
            NAME datachannel-wasm
            GITHUB_REPOSITORY paullouisageneau/datachannel-wasm
            GIT_TAG v0.4.0
            EXCLUDE_FROM_ALL YES
        )
        if(datachannel-wasm_ADDED)
            # Carries the --js-library flags for its own glue as PUBLIC link options, so they
            # reach the game executable through this link.
            target_link_libraries(luminoveau PUBLIC datachannel-wasm)
            target_compile_definitions(luminoveau PUBLIC LUMINOVEAU_WITH_WEBRTC=1)
            target_sources(luminoveau PRIVATE
                src/platform/net/webrtc/transport_webrtc.cpp
                src/platform/net/brokers/signalbroker.cpp)
            lumi_done("datachannel-wasm")
        else()
            lumi_warn("datachannel-wasm - fetch failed (WebRTC transport disabled)")
        endif()
    else()
        # Neither library installs cleanly as a subproject: libdatachannel exports targets that
        # Mbed TLS never adds to an export set, because Mbed TLS skips its own install rules
        # when it is not the top-level project. Nothing here is ever installed — it is all
        # linked statically into luminoveau — so the install rules are skipped wholesale
        # rather than chased target by target.
        set(CMAKE_SKIP_INSTALL_RULES ON)

        set(ENABLE_TESTING     OFF CACHE BOOL "Disable Mbed TLS tests"    FORCE)
        set(ENABLE_PROGRAMS    OFF CACHE BOOL "Disable Mbed TLS programs" FORCE)
        set(BUILD_SHARED_LIBS  OFF CACHE BOOL "Build static libraries"    FORCE)

        lumi_msg("Fetching Mbed TLS")
        CPMAddPackage(
            NAME mbedtls
            GITHUB_REPOSITORY Mbed-TLS/mbedtls
            GIT_TAG v3.6.2
            EXCLUDE_FROM_ALL YES
        )

        if(NOT mbedtls_ADDED)
            lumi_warn("Mbed TLS - fetch failed (WebRTC transport disabled)")
        else()
            foreach(_mbedtls_target mbedtls mbedx509 mbedcrypto)
                target_compile_definitions(${_mbedtls_target} PUBLIC
                    MBEDTLS_USER_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/cmake/mbedtls_user_config.h")
            endforeach()

            # libdatachannel looks for MbedTLS::MbedTLS and only calls find_package when that
            # target is missing, so defining it keeps its search off the system entirely.
            if(NOT TARGET MbedTLS::MbedTLS)
                add_library(lumi_mbedtls INTERFACE)
                target_link_libraries(lumi_mbedtls INTERFACE mbedtls mbedx509 mbedcrypto)
                add_library(MbedTLS::MbedTLS ALIAS lumi_mbedtls)
            endif()

            set(USE_MBEDTLS ON  CACHE BOOL "DTLS through Mbed TLS, not OpenSSL" FORCE)
            set(NO_MEDIA    ON  CACHE BOOL "Data channels only, no RTP/SRTP"    FORCE)
            set(NO_EXAMPLES ON  CACHE BOOL "Disable libdatachannel examples"    FORCE)
            set(NO_TESTS    ON  CACHE BOOL "Disable libdatachannel tests"       FORCE)

            lumi_msg("Fetching libdatachannel")
            CPMAddPackage(
                NAME libdatachannel
                GITHUB_REPOSITORY paullouisageneau/libdatachannel
                GIT_TAG v0.23.1
                EXCLUDE_FROM_ALL YES
            )
            if(libdatachannel_ADDED)
                target_link_libraries(luminoveau PUBLIC datachannel-static)
                target_compile_definitions(luminoveau PUBLIC LUMINOVEAU_WITH_WEBRTC=1)
                target_sources(luminoveau PRIVATE
                    src/platform/net/webrtc/transport_webrtc.cpp
                    src/platform/net/brokers/signalbroker.cpp)

                # The service that introduces peers. Not part of ALL — it is deployed to a
                # box, not shipped with a game — so build it explicitly when you need it.
                add_executable(lumi-signalserver EXCLUDE_FROM_ALL
                    "${CMAKE_CURRENT_SOURCE_DIR}/tools/signalserver/main.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/net/signalprotocol.cpp")
                target_include_directories(lumi-signalserver PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
                target_link_libraries(lumi-signalserver PRIVATE
                    datachannel-static nlohmann_json::nlohmann_json)
                target_compile_features(lumi-signalserver PRIVATE cxx_std_20)

                lumi_done("libdatachannel")
            else()
                lumi_warn("libdatachannel - fetch failed (WebRTC transport disabled)")
            endif()
        endif()

        set(CMAKE_SKIP_INSTALL_RULES OFF)
    endif()
endif()

# KTX2/Basis texture support is standard (on by default). Escape hatch for build issues:
# configure with -DLUMINOVEAU_KTX2=OFF to drop it (AssetHandler then loads RGBA only).
option(LUMINOVEAU_KTX2 "KTX2/Basis (UASTC->BC7) texture support" ON)
if(LUMINOVEAU_KTX2)
    # Source-only, same pattern as SPIRV-Cross/glslang: clone the repo and compile just the
    # transcoder .cpp (no encoder) plus the bundled decompress-only zstd (KTX2 supercompression).
    # No CPM / add_subdirectory -> no configure-time CMake for basis. Defines LUMINOVEAU_WITH_KTX2.
    lumi_msg("Fetching basis_universal")
    lumi_fetch("basis_universal" "https://github.com/BinomialLLC/basis_universal.git" "1.16.4" BASIS_ROOT)
    if(BASIS_ROOT AND EXISTS "${BASIS_ROOT}/transcoder/basisu_transcoder.cpp")
        set(BASIS_SRCS "${BASIS_ROOT}/transcoder/basisu_transcoder.cpp")
        target_sources(luminoveau PRIVATE "${BASIS_ROOT}/transcoder/basisu_transcoder.cpp")
        if(EXISTS "${BASIS_ROOT}/zstd/zstddeclib.c")
            target_sources(luminoveau PRIVATE "${BASIS_ROOT}/zstd/zstddeclib.c")
            list(APPEND BASIS_SRCS "${BASIS_ROOT}/zstd/zstddeclib.c")
        endif()
        # ALWAYS optimize the transcoder + zstd, even in Debug builds. These are hot scalar block
        # decoders with no SIMD; at -O0 the UASTC->BC7/ASTC *pack* path is ~10-50x slower (a map's
        # KTX2 transcode went from milliseconds to ~5s). The trivial UASTC->RGBA unpack survives -O0,
        # which is why only the compressed path was slow. Per-file flag so the rest of the engine
        # stays debuggable. (Xcode/Ninja/Make: the later -O wins over the config's -O0; MSVC: /O2.)
        if(MSVC)
            # /O2 is incompatible with Debug's /RTC1 (D8016), so skip it in Debug.
            # KTX2 transcode is slow in MSVC Debug as a result; fine for debugging.
            set_source_files_properties(${BASIS_SRCS} PROPERTIES COMPILE_OPTIONS "$<$<NOT:$<CONFIG:Debug>>:/O2>")
        else()
            set_source_files_properties(${BASIS_SRCS} PROPERTIES COMPILE_OPTIONS "-O2")
        endif()
        target_include_directories(luminoveau SYSTEM PUBLIC "${BASIS_ROOT}/transcoder")
        target_compile_definitions(luminoveau PUBLIC LUMINOVEAU_WITH_KTX2=1)
        lumi_done("basis_universal (source-only, forced -O2)")
    else()
        message(FATAL_ERROR "basis_universal fetch failed. Fix the network/tag, or configure with -DLUMINOVEAU_KTX2=OFF.")
    endif()
else()
    lumi_msg("KTX2 disabled (LUMINOVEAU_KTX2=OFF)")
endif()

# Fetching freetype (required by MSDF-atlas-gen's msdfgen)
if (NOT ANDROID)
lumi_msg("Fetching freetype")
CPMAddPackage(
    NAME freetype
    GITHUB_REPOSITORY freetype/freetype
    GIT_TAG b1f4785
    EXCLUDE_FROM_ALL YES
    OPTIONS
        "FT_DISABLE_ZLIB ON"
        "FT_DISABLE_BZIP2 ON"
        "FT_DISABLE_PNG ON"
        "FT_DISABLE_HARFBUZZ ON"
        "FT_DISABLE_BROTLI ON"
)
if(freetype_ADDED)
    if(NOT EXISTS "${freetype_SOURCE_DIR}")
        message(FATAL_ERROR "freetype source directory '${freetype_SOURCE_DIR}' does not exist")
    endif()
    target_include_directories(luminoveau SYSTEM PUBLIC "${freetype_SOURCE_DIR}/include")
    target_link_libraries(luminoveau PUBLIC freetype)
    # Creating alias for MSDF-atlas-gen compatibility
    if(NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
    if(NOT TARGET freetype::freetype)
        add_library(freetype::freetype ALIAS freetype)
    endif()
    lumi_done("freetype")
else()
    lumi_warn("freetype - fetch failed")
endif()

# NOTE: HarfBuzz is no longer needed - MSDF-atlas-gen doesn't require it
# (it was only needed for SDL3_ttf which has been removed)
endif()

# MSDF-atlas-gen configuration
set(MSDFGEN_DISABLE_PNG ON CACHE BOOL "" FORCE)

set(MSDFGEN_DISABLE_SVG OFF CACHE BOOL "Disable SVG functionality")

set(MSDF_ATLAS_BUILD_STANDALONE OFF)
set(MSDF_ATLAS_USE_VCPKG ${OS_WINDOWS})
set(MSDF_ATLAS_USE_SKIA OFF)
set(MSDF_ATLAS_DYNAMIC_RUNTIME OFF)
set(MSDF_ATLAS_MSDFGEN_EXTERNAL OFF)
set(MSDF_ATLAS_INSTALL OFF)

set(MSDF_ATLAS_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_USE_VCPKG OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_USE_SKIA OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_MSDFGEN_EXTERNAL OFF CACHE BOOL "" FORCE)

    # tinyxml2
    lumi_msg("Fetching tinyxml2")
    lumi_fetch("tinyxml2" "https://github.com/leethomason/tinyxml2.git" "10.0.0" _tinyxml2_src)
    if(_tinyxml2_src)
        add_library(tinyxml2 STATIC "${_tinyxml2_src}/tinyxml2.cpp")
        add_library(tinyxml2::tinyxml2 ALIAS tinyxml2)
        target_include_directories(tinyxml2 PUBLIC "${_tinyxml2_src}")
        set_target_properties(tinyxml2 PROPERTIES POSITION_INDEPENDENT_CODE ON)
        lumi_done("tinyxml2")
    else()
        lumi_warn("tinyxml2 - fetch failed")
    endif()

lumi_msg("Fetching MSDF-atlas-gen")
CPMAddPackage(
    NAME MSDF-atlas-gen
    GITHUB_REPOSITORY Chlumsky/msdf-atlas-gen
    GIT_TAG c76a323
    EXCLUDE_FROM_ALL YES
)
if(MSDF-atlas-gen_ADDED)
    if(NOT EXISTS "${MSDF-atlas-gen_SOURCE_DIR}")
        message(FATAL_ERROR "MSDF-atlas-gen include directory '${MSDF-atlas-gen_SOURCE_DIR}' does not exist")
    endif()
    
    target_include_directories(luminoveau SYSTEM PUBLIC "${MSDF-atlas-gen_SOURCE_DIR}")
    target_link_libraries(luminoveau PUBLIC msdf-atlas-gen)
    lumi_done("MSDF-atlas-gen")
else()
    lumi_warn("MSDF-atlas-gen - fetch failed")
endif()


# Fetching physfs
set(PHYSFS_BUILD_SHARED OFF CACHE BOOL "Disable physfs shared library" FORCE)
set(PHYSFS_BUILD_TEST OFF CACHE BOOL "Disable physfs tests" FORCE)
set(PHYSFS_BUILD_DOCS OFF CACHE BOOL "Disable physfs documentation" FORCE)
set(PHYSFS_DISABLE_INSTALL ON CACHE BOOL "Disable physfs installation" FORCE)

lumi_msg("Fetching physfs")
CPMAddPackage(
    NAME physfs
    GITHUB_REPOSITORY icculus/physfs
    GIT_TAG 7726d18
    EXCLUDE_FROM_ALL YES
)
if(physfs_ADDED)
    if(NOT EXISTS "${physfs_SOURCE_DIR}/src")
        message(FATAL_ERROR "physfs source directory '${physfs_SOURCE_DIR}/src' does not exist")
    endif()
    target_include_directories(luminoveau SYSTEM PUBLIC "${physfs_SOURCE_DIR}/src")
    set(_physfs_common_sources
        "${physfs_SOURCE_DIR}/src/physfs.c"
        "${physfs_SOURCE_DIR}/src/physfs_byteorder.c"
        "${physfs_SOURCE_DIR}/src/physfs_unicode.c"
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
    if(EMSCRIPTEN)
        list(APPEND _physfs_common_sources
            "${physfs_SOURCE_DIR}/src/physfs_platform_posix.c"
            "${physfs_SOURCE_DIR}/src/physfs_platform_unix.c"
        )
    elseif(WIN32)
        list(APPEND _physfs_common_sources
            "${physfs_SOURCE_DIR}/src/physfs_platform_windows.c"
        )
    elseif(ANDROID)
        list(APPEND _physfs_common_sources
            "${physfs_SOURCE_DIR}/src/physfs_platform_posix.c"
            "${physfs_SOURCE_DIR}/src/physfs_platform_android.c"
        )
    elseif(UNIX)
        list(APPEND _physfs_common_sources
            "${physfs_SOURCE_DIR}/src/physfs_platform_posix.c"
            "${physfs_SOURCE_DIR}/src/physfs_platform_unix.c"
        )
    endif()
    target_sources(luminoveau PRIVATE ${_physfs_common_sources})
    if(WINRT)
        target_sources(luminoveau PRIVATE
            "${physfs_SOURCE_DIR}/src/physfs_platform_winrt.cpp"
        )
    endif()
    if(APPLE)
        target_sources(luminoveau PRIVATE
            "${physfs_SOURCE_DIR}/src/physfs_platform_apple.m"
        )
        # Set ObjC compilation for the .m file
        set_source_files_properties(
            "${physfs_SOURCE_DIR}/src/physfs_platform_apple.m"
            PROPERTIES LANGUAGE OBJC
        )
        target_link_libraries(luminoveau PUBLIC
            "-framework IOKit"
            "-framework Foundation"
            "-framework CoreFoundation"
        )
    endif()
    lumi_done("physfs")
else()
    lumi_warn("physfs - fetch failed")
endif()
