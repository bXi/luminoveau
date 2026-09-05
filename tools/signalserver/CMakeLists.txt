# Standalone build for the signalling service, so a server does not have to configure the
# engine to get it. Nothing here needs SDL, a renderer or any asset pipeline — the service
# shares exactly two files with the engine and is otherwise self-contained.
#
#   cmake -S tools/signalserver -B build-signal -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-signal -j
#   ./build-signal/lumi-signalserver --port 9000
#
# The engine's own build also offers this as the `lumi-signalserver` target, which is the
# convenient path on a development machine. This one is for the box it runs on.

cmake_minimum_required(VERSION 3.24)
project(lumi-signalserver LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# Neither dependency installs cleanly as a subproject, and nothing here is ever installed —
# it is all linked statically into one binary — so their install rules are skipped wholesale.
set(CMAKE_SKIP_INSTALL_RULES ON)

set(ENABLE_TESTING    OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS   OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG        v3.6.2
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(mbedtls)

# libdatachannel names the DTLS-SRTP profile symbols even with media disabled, and Mbed TLS
# ships that feature off. Enabled through Mbed TLS's own hook so neither is patched.
foreach(_target mbedtls mbedx509 mbedcrypto)
    target_compile_definitions(${_target} PUBLIC
        MBEDTLS_USER_CONFIG_FILE="${ENGINE_ROOT}/cmake/mbedtls_user_config.h")
endforeach()

# libdatachannel only calls find_package when this target is missing, so defining it keeps
# its search off the system entirely.
if(NOT TARGET MbedTLS::MbedTLS)
    add_library(lumi_mbedtls INTERFACE)
    target_link_libraries(lumi_mbedtls INTERFACE mbedtls mbedx509 mbedcrypto)
    add_library(MbedTLS::MbedTLS ALIAS lumi_mbedtls)
endif()

set(USE_MBEDTLS ON  CACHE BOOL "" FORCE)
set(NO_MEDIA    ON  CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON  CACHE BOOL "" FORCE)
set(NO_TESTS    ON  CACHE BOOL "" FORCE)
FetchContent_Declare(libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG        v0.23.1
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(libdatachannel)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(nlohmann_json)

set(CMAKE_SKIP_INSTALL_RULES OFF)

add_executable(lumi-signalserver
    main.cpp
    "${ENGINE_ROOT}/src/platform/net/signalprotocol.cpp")

# The protocol is defined once, in the engine tree, and shared rather than mirrored — a copy
# here would drift the first time a message changed.
target_include_directories(lumi-signalserver PRIVATE "${ENGINE_ROOT}/src")
target_link_libraries(lumi-signalserver PRIVATE datachannel-static nlohmann_json::nlohmann_json)
