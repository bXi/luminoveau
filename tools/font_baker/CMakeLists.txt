# font_baker — host tool that bakes the default-font MSDF atlas into src/assets/font_atlas_generated.*
# Not built for Emscripten: it runs on the host to produce the committed blob (like the shader
# blobs). Guarded by the caller (see the root CMakeLists include).

# Pull the embedded font bytes WITHOUT compiling any engine headers: a stub assethandler.h that
# declares only the one symbol, plus a copy of the engine's DroidSansMono.cpp next to it. The copy's
# `#include "assethandler.h"` resolves to the sibling stub (quote-include searches the file's own
# directory first), so it compiles standalone and just carries the byte array.
set(_fd "${CMAKE_CURRENT_BINARY_DIR}/fontdata")
file(MAKE_DIRECTORY "${_fd}")
file(WRITE "${_fd}/assethandler.h"
     "#pragma once\nclass AssetHandler { public: static const unsigned char DroidSansMono_ttf[]; };\n")
configure_file("${LUMINOVEAU_ROOT_DIR}/src/assets/DroidSansMono.cpp" "${_fd}/DroidSansMono.cpp" COPYONLY)

# zstd encoder — host/offline only. The runtime decodes with the decode-only zstd already bundled by
# basis_universal, so this never enters the engine/wasm build.
CPMAddPackage(
    NAME zstd
    GITHUB_REPOSITORY facebook/zstd
    VERSION 1.5.6
    SOURCE_SUBDIR build/cmake
    OPTIONS "ZSTD_BUILD_SHARED OFF" "ZSTD_BUILD_STATIC ON"
            "ZSTD_BUILD_PROGRAMS OFF" "ZSTD_BUILD_TESTS OFF")

add_executable(font_baker
    "${CMAKE_CURRENT_SOURCE_DIR}/font_baker.cpp"
    "${_fd}/DroidSansMono.cpp")

# msdf-atlas-gen brings msdfgen + freetype + the include dirs.
target_link_libraries(font_baker PRIVATE msdf-atlas-gen libzstd_static)
if(zstd_SOURCE_DIR)
    target_include_directories(font_baker PRIVATE "${zstd_SOURCE_DIR}/lib")
endif()
set_target_properties(font_baker PROPERTIES CXX_STANDARD 20)

# `cmake --build . --target bake_font` regenerates the committed blob in place.
add_custom_target(bake_font
    COMMAND font_baker "${LUMINOVEAU_ROOT_DIR}/src/assets/font_atlas_generated.cpp"
    DEPENDS font_baker
    COMMENT "Baking default-font MSDF atlas -> src/assets/font_atlas_generated.cpp")
