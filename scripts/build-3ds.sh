#!/usr/bin/env bash
# Nintendo 3DS build driver. Run from the repo root inside devkitPro's msys2
# (or any shell where $DEVKITPRO points at a devkitPro install with devkitARM,
# libctru, citro3d and the 3DS CMake toolchain).
#
#   ./scripts/build-3ds.sh              # configure + build (Release, examples on)
#   ./scripts/build-3ds.sh --fresh      # wipe build-3ds/ first
#
# From Windows use scripts\build-3ds.cmd, which enters the bundled msys2.
set -e

export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
# Cache CPM source checkouts across fresh builds (SDL3 etc. are large clones).
export CPM_SOURCE_CACHE=${CPM_SOURCE_CACHE:-$HOME/.cache/cpm}

if [ ! -f "$DEVKITPRO/cmake/3DS.cmake" ]; then
    echo "error: \$DEVKITPRO/cmake/3DS.cmake not found (DEVKITPRO=$DEVKITPRO)" >&2
    exit 1
fi

if [ "$1" = "--fresh" ]; then
    rm -rf build-3ds
    shift
fi

cmake -S . -B build-3ds -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/3DS.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DLUMINOVEAU_BUILD_EXAMPLES=ON \
    "$@"
cmake --build build-3ds -j"$(nproc)"
