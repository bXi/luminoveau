# WebGPU + Emscripten dependency setup.
# Pulled in from CMakeLists.txt only when LUMINOVEAU_WEBGPU_BACKEND=ON.
#
# Emscripten:  uses the emdawnwebgpu *port* — no separate build, headers come
#              from the sysroot automatically when --use-port=emdawnwebgpu is set.
# Native:      fetches Dawn via eliemichel/WebGPU-distribution + sdl3webgpu.

if(NOT COMMAND CPMAddPackage)
    message(FATAL_ERROR "CPM.cmake is required for WebGPU dependency setup")
endif()

if(EMSCRIPTEN)
    # ── Emscripten ────────────────────────────────────────────────────────────
    # The emdawnwebgpu port supplies webgpu.h and the JS glue at link time.
    # No CPM fetch needed — just forward the port flag to every target that
    # includes luminoveau so the compiler can find <webgpu/webgpu.h>.
    target_compile_options(luminoveau PUBLIC "SHELL:--use-port=emdawnwebgpu")
    target_link_options(luminoveau PUBLIC
        "SHELL:--use-port=emdawnwebgpu"
        "SHELL:-sASYNCIFY=1"
        "SHELL:-sALLOW_MEMORY_GROWTH=1"
        "SHELL:-sUSE_SDL=0"
        "SHELL:-sFORCE_FILESYSTEM=1"
        "SHELL:-lidbfs.js"
        "SHELL:-sASYNCIFY_IMPORTS=['emscripten_asm_const_int','emscripten_asm_const_int_sync_on_main_thread']"
    )
    target_compile_definitions(luminoveau PUBLIC LUMINOVEAU_USE_CALLBACKS)
    lumi_done("WebGPU (Emscripten port - emdawnwebgpu)")
else()
    # ── Native (Dawn) ─────────────────────────────────────────────────────────
    # Selects the right backend:
    #   default         → DAWN
    #   override with   -DWEBGPU_BACKEND=WGPU for wgpu-native
    if(NOT DEFINED WEBGPU_BACKEND)
        set(WEBGPU_BACKEND "DAWN" CACHE STRING "WebGPU backend (DAWN | WGPU)")
    endif()

    lumi_msg("Fetching WebGPU-distribution (backend=${WEBGPU_BACKEND})")
    CPMAddPackage(
        NAME webgpu
        GITHUB_REPOSITORY eliemichel/WebGPU-distribution
        GIT_TAG v0.2.0
        EXCLUDE_FROM_ALL YES
    )
    if(webgpu_ADDED OR webgpu_SOURCE_DIR)
        target_link_libraries(luminoveau PUBLIC webgpu)
        target_copy_webgpu_binaries(luminoveau)
        lumi_done("WebGPU-distribution (${WEBGPU_BACKEND})")
    else()
        message(FATAL_ERROR "WebGPU-distribution fetch failed — required for WEBGPU backend")
    endif()

    # ── sdl3webgpu ────────────────────────────────────────────────────────────
    lumi_msg("Fetching sdl3webgpu")
    CPMAddPackage(
        NAME sdl3webgpu
        GITHUB_REPOSITORY eliemichel/sdl3webgpu
        GIT_TAG main
        EXCLUDE_FROM_ALL YES
    )
    if(sdl3webgpu_ADDED OR sdl3webgpu_SOURCE_DIR)
        target_sources(luminoveau PRIVATE "${sdl3webgpu_SOURCE_DIR}/sdl3webgpu.c")
        target_include_directories(luminoveau PUBLIC "${sdl3webgpu_SOURCE_DIR}")
        lumi_done("sdl3webgpu")
    else()
        message(FATAL_ERROR "sdl3webgpu fetch failed — required for WEBGPU backend")
    endif()
endif()
