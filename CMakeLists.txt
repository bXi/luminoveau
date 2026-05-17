cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/LumiMessage.cmake)
set(CMAKE_MESSAGE_LOG_LEVEL "WARNING")

project(Luminoveau
    VERSION 0.1.0
    DESCRIPTION "Luminoveau Game Engine Library"
    LANGUAGES C CXX
)

if(APPLE)
    enable_language(OBJC)
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    endif()
endif()

# ── Build options ─────────────────────────────────────────────────────────────
option(LUMINOVEAU_BUILD_IMGUI    "Include ImGui support"                        OFF)
option(LUMINOVEAU_BUILD_RMLUI    "Include RmlUi support"                        OFF)
option(LUMINOVEAU_ENABLE_LOGGING "Include logging support"                      ON)
option(LUMINOVEAU_ENABLE_WARNINGS "Enable all compiler warnings"                OFF)
option(LUMINOVEAU_BUILD_TESTS    "Build test suite"                             OFF)
option(LUMINOVEAU_USE_CALLBACKS  "Use SDL3 callback-based main loop"            OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()
if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()

# ── Platform fixes ────────────────────────────────────────────────────────────
if(ANDROID)
    string(REPLACE "-fuse-ld=gold" "" CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS}")
    string(REPLACE "-fuse-ld=gold" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=lld")
endif()

# ── Optimisation flags ────────────────────────────────────────────────────────
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -w")

if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE        "${CMAKE_CXX_FLAGS_RELEASE}        /O2 /Ob2 /Oi /Ot /GL /fp:fast")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG /OPT:REF /OPT:ICF")
    set(CMAKE_CXX_FLAGS_DEBUG          "${CMAKE_CXX_FLAGS_DEBUG}          /Od /Zi /RTC1")
else()
    if(ANDROID)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -ffast-math -funroll-loops")
    elseif(APPLE)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -mcpu=native -ffast-math -funroll-loops")
    else()
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -mtune=native -ffast-math -funroll-loops")
    endif()

    include(CheckIPOSupported)
    check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT LTO_ERROR)
    if(LTO_SUPPORTED)
        set(CMAKE_CXX_FLAGS_RELEASE         "${CMAKE_CXX_FLAGS_RELEASE}         -flto")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE  "${CMAKE_EXE_LINKER_FLAGS_RELEASE}  -flto")
        set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -flto")
    endif()

    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O1 -g")
endif()

# ── Sources ───────────────────────────────────────────────────────────────────
include(cmake/Sources.cmake)

add_library(luminoveau STATIC ${LUMINOVEAU_SOURCES})

# ── Include dirs ──────────────────────────────────────────────────────────────
target_include_directories(luminoveau PUBLIC
    "${PROJECT_SOURCE_DIR}"        # for luminoveau.h
    "${PROJECT_SOURCE_DIR}/src"    # for all engine headers
    "${PROJECT_SOURCE_DIR}/src/extern"  # for miniaudio.h and other single-header libs
)

# ── Optional: callback main loop ─────────────────────────────────────────────
if(LUMINOVEAU_USE_CALLBACKS)
    target_compile_definitions(luminoveau PUBLIC LUMINOVEAU_USE_CALLBACKS)
    target_sources(luminoveau INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src/app/lumi_main.cpp")
    lumi_done("SDL3 callback main loop")
endif()

# ── Optional: logging ─────────────────────────────────────────────────────────
if(LUMINOVEAU_ENABLE_LOGGING)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/log/log.cpp")
        target_sources(luminoveau PRIVATE
            src/core/log/log.cpp
            src/core/log/log.h
        )
        lumi_done("Logging support")
    endif()
endif()

# ── Compiler warnings (library code only) ────────────────────────────────────
if(MSVC)
    target_compile_options(luminoveau PRIVATE /W4 /wd4820 /wd4514 /wd5045)
else()
    target_compile_options(luminoveau PRIVATE
        -Wall -Wextra -Wno-missing-field-initializers
        -Wno-unused-parameter -Wno-unused-function)
endif()

# ── Platform linking ──────────────────────────────────────────────────────────
if(ANDROID)
    target_compile_definitions(luminoveau PUBLIC __ANDROID__)
    target_link_libraries(luminoveau PUBLIC log android)
elseif(APPLE)
    target_link_libraries(luminoveau PUBLIC
        "-framework Cocoa"
        "-framework Metal"
        "-framework QuartzCore"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework CoreVideo"
    )
    set(CMAKE_MACOSX_RPATH ON)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/XcodeConfiguration.cmake)
elseif(UNIX)
    target_compile_options(luminoveau PUBLIC -static-libstdc++ -static-libgcc)
    target_link_options(luminoveau PUBLIC   -static-libstdc++ -static-libgcc)
endif()

target_compile_features(luminoveau PUBLIC cxx_std_20)

# ── Dependencies (CPM) ────────────────────────────────────────────────────────
file(DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.5/CPM.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
    EXPECTED_HASH SHA256=c46b876ae3b9f994b4f05a4c15553e0485636862064f1fcc9d8b4f832086bc5d
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
find_package(Threads REQUIRED)

include(cmake/AssetPreparation.cmake)
include(cmake/SteamDetection.cmake)

set(CMAKE_FOLDER "Dependencies")
include(cmake/DependencySetup.cmake)
include(cmake/ImGuiIntegration.cmake)
include(cmake/RmlUiIntegration.cmake)
include(cmake/DownloadDXC.cmake)
unset(CMAKE_FOLDER)

if(ANDROID)
    string(REPLACE "-fuse-ld=gold" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    if(NOT CMAKE_EXE_LINKER_FLAGS MATCHES "-fuse-ld=lld")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld")
    endif()
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyRuntimeDLLs.cmake)

if(LUMINOVEAU_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS luminoveau ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
install(FILES ${LUMINOVEAU_HEADERS} DESTINATION include/luminoveau)
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyRuntimeDLLs.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/XcodeConfiguration.cmake"
    DESTINATION lib/cmake/Luminoveau
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/LuminoveauConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LuminoveauConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LuminoveauConfig.cmake"
    @ONLY
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LuminoveauConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LuminoveauConfigVersion.cmake"
    DESTINATION lib/cmake/Luminoveau
)

unset(CMAKE_MESSAGE_LOG_LEVEL)
