# ShaderTranspile.cmake
# Build-time GLSL → SPIR-V → WGSL transpilation for WebGPU effect shaders.
#
# Automatically fetches and builds the Tint shader compiler (from Google's Dawn
# project) as a host tool, then provides lumi_transpile_shaders() to convert
# GLSL shaders to WGSL at build time.
#
# Usage in game CMakeLists.txt:
#   include(path/to/ShaderTranspile.cmake)
#   lumi_transpile_shaders(
#       SHADER_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders"
#       OUTPUT_DIR  "${CMAKE_BINARY_DIR}/_transpiled"
#   )

if(NOT LUMINOVEAU_WEBGPU_BACKEND)
    return()
endif()

# ── Locate or build the Tint CLI tool ────────────────────────────────────────

set(LUMI_TINT_HOST_DIR "${CMAKE_BINARY_DIR}/_deps/dawn-host")
set(LUMI_DAWN_SRC      "${CMAKE_BINARY_DIR}/_deps/dawn-src")

# Use HOST platform for exe name (not cross-compile target)
if(CMAKE_HOST_WIN32)
    set(TINT_EXE_NAME "tint.exe")
else()
    set(TINT_EXE_NAME "tint")
endif()

# Also need glslangValidator for GLSL → SPIR-V
find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS
        "$ENV{VULKAN_SDK}/bin"
        "$ENV{VULKAN_SDK}/Bin"
)

# Path where tint binary should be
set(LUMI_TINT_EXECUTABLE "${LUMI_TINT_HOST_DIR}/${TINT_EXE_NAME}")

if(NOT EXISTS "${LUMI_TINT_EXECUTABLE}")
    lumi_msg("Building Tint shader compiler (first time only)...")

    # Step 1: Clone Dawn (shallow)
    if(NOT EXISTS "${LUMI_DAWN_SRC}/.git")
        lumi_msg("Cloning Dawn repository (shallow)...")
        execute_process(
            COMMAND git clone --depth 1 --single-branch
                "https://dawn.googlesource.com/dawn.git"
                "${LUMI_DAWN_SRC}"
            RESULT_VARIABLE _clone_result
            OUTPUT_QUIET
        )
        if(NOT _clone_result EQUAL 0)
            # Fallback to official source
            execute_process(
                COMMAND git clone --depth 1 --single-branch
                    "https://dawn.googlesource.com/dawn.git"
                    "${LUMI_DAWN_SRC}"
                RESULT_VARIABLE _clone_result
            )
        endif()
        if(NOT _clone_result EQUAL 0)
            lumi_warn("Failed to clone Dawn - shader transpilation unavailable")
            set(LUMI_TINT_EXECUTABLE "")
        endif()
    endif()

    # Step 2: Fetch Dawn's dependencies
    if(EXISTS "${LUMI_DAWN_SRC}" AND NOT EXISTS "${LUMI_DAWN_SRC}/third_party/abseil-cpp/CMakeLists.txt")
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_FOUND AND EXISTS "${LUMI_DAWN_SRC}/tools/fetch_dawn_dependencies.py")
            lumi_msg("Fetching Dawn dependencies...")
            execute_process(
                COMMAND ${Python3_EXECUTABLE}
                    "${LUMI_DAWN_SRC}/tools/fetch_dawn_dependencies.py"
                WORKING_DIRECTORY "${LUMI_DAWN_SRC}"
                RESULT_VARIABLE _fetch_result
                OUTPUT_QUIET
            )
            if(NOT _fetch_result EQUAL 0)
                lumi_warn("Dawn dependency fetch failed")
            endif()
        else()
            lumi_warn("Python3 not found or fetch script missing")
        endif()
    endif()

    # Step 3: Configure and build Tint for the HOST platform
    if(EXISTS "${LUMI_DAWN_SRC}/CMakeLists.txt")
        lumi_msg("Configuring Tint (host build)...")

        # Use the host compiler, not Emscripten's cross-compiler
        # When cross-compiling, CMAKE_C/CXX_COMPILER point to emcc.
        # We need the native compiler instead.
        if(EMSCRIPTEN)
            # Try to find the native compiler
            find_program(_NATIVE_CC NAMES cc gcc clang cl
                PATHS /usr/bin /usr/local/bin
                NO_CMAKE_FIND_ROOT_PATH)
            find_program(_NATIVE_CXX NAMES c++ g++ clang++ cl
                PATHS /usr/bin /usr/local/bin
                NO_CMAKE_FIND_ROOT_PATH)

            if(WIN32 OR CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
                # On Windows, use cl.exe if available, else gcc from PATH
                find_program(_NATIVE_CC NAMES cl gcc cc
                    NO_CMAKE_FIND_ROOT_PATH)
                find_program(_NATIVE_CXX NAMES cl g++ c++
                    NO_CMAKE_FIND_ROOT_PATH)
            endif()

            set(_HOST_COMPILER_ARGS
                -DCMAKE_C_COMPILER=${_NATIVE_CC}
                -DCMAKE_CXX_COMPILER=${_NATIVE_CXX}
            )
        else()
            set(_HOST_COMPILER_ARGS "")
        endif()

        file(MAKE_DIRECTORY "${LUMI_TINT_HOST_DIR}")

        execute_process(
            COMMAND ${CMAKE_COMMAND}
                -S "${LUMI_DAWN_SRC}"
                -B "${LUMI_TINT_HOST_DIR}"
                -G Ninja
                ${_HOST_COMPILER_ARGS}
                -DCMAKE_BUILD_TYPE=Release
                -DDAWN_BUILD_SAMPLES=OFF
                -DDAWN_BUILD_TESTS=OFF
                -DDAWN_BUILD_BENCHMARKS=OFF
                -DTINT_BUILD_TESTS=OFF
                -DTINT_BUILD_FUZZERS=OFF
                -DTINT_BUILD_BENCHMARKS=OFF
                -DTINT_BUILD_CMD_TOOLS=ON
                -DTINT_BUILD_SPV_READER=ON
                -DTINT_BUILD_WGSL_WRITER=ON
                -DTINT_BUILD_GLSL_WRITER=OFF
                -DTINT_BUILD_HLSL_WRITER=OFF
                -DTINT_BUILD_MSL_WRITER=OFF
            RESULT_VARIABLE _configure_result
            OUTPUT_QUIET
        )

        if(_configure_result EQUAL 0)
            lumi_msg("Building Tint CLI...")
            execute_process(
                COMMAND ${CMAKE_COMMAND}
                    --build "${LUMI_TINT_HOST_DIR}"
                    --target tint_cmd_tint_cmd
                    --config Release
                    -j
                RESULT_VARIABLE _build_result
            )
            if(NOT _build_result EQUAL 0)
                lumi_warn("Tint build failed")
                set(LUMI_TINT_EXECUTABLE "")
            endif()
        else()
            lumi_warn("Tint configure failed")
            set(LUMI_TINT_EXECUTABLE "")
        endif()
    endif()
endif()

# Re-derive path and verify (handles first-build where file didn't exist at cache time)
set(LUMI_TINT_EXECUTABLE "${LUMI_TINT_HOST_DIR}/${TINT_EXE_NAME}")
if(EXISTS "${LUMI_TINT_EXECUTABLE}")
    lumi_done("Tint shader compiler: ${LUMI_TINT_EXECUTABLE}")
else()
    set(LUMI_TINT_EXECUTABLE "")
    lumi_warn("Tint not available - effect shaders won't be transpiled to WGSL")
    lumi_warn("Install tint CLI and set LUMI_TINT_EXECUTABLE, or ensure git+python3+ninja are available for auto-build")
endif()

if(GLSLANG_VALIDATOR)
    lumi_done("glslangValidator: ${GLSLANG_VALIDATOR}")
else()
    lumi_warn("glslangValidator not found - install Vulkan SDK or add to PATH")
endif()

# ── Shader transpilation function ────────────────────────────────────────────
#
# lumi_transpile_shaders(
#     TARGET      <cmake-target>      # Target to add shader dependencies to
#     SHADER_DIRS <dir1> [dir2...]     # Directories containing .vert/.frag GLSL files
#     OUTPUT_DIR  <dir>                # Where to put .wgsl output (mirrors source structure)
# )
#
# Generates custom commands that:
#   1. GLSL → SPIR-V via glslangValidator
#   2. SPIR-V → WGSL via tint
#   3. Outputs to OUTPUT_DIR preserving relative paths

function(lumi_transpile_shaders)
    cmake_parse_arguments(TS "" "TARGET;OUTPUT_DIR;SOURCE_ROOT" "SHADER_DIRS" ${ARGN})

    # Always create output dir so Emscripten --preload-file doesn't fail
    file(MAKE_DIRECTORY "${TS_OUTPUT_DIR}")

    if(NOT LUMI_TINT_EXECUTABLE OR NOT EXISTS "${LUMI_TINT_EXECUTABLE}")
        lumi_warn("Skipping shader transpilation (tint not available)")
        return()
    endif()

    if(NOT GLSLANG_VALIDATOR)
        lumi_warn("Skipping shader transpilation (glslangValidator not available)")
        return()
    endif()

    set(_wgsl_outputs "")

    foreach(_shader_dir ${TS_SHADER_DIRS})
        # Find all GLSL shaders (vert, frag, compute)
        file(GLOB_RECURSE _glsl_shaders
            "${_shader_dir}/*.vert"
            "${_shader_dir}/*.frag"
            "${_shader_dir}/*.comp"
        )

        foreach(_glsl_file ${_glsl_shaders})
            # Compute relative path from source root (preserves assets/shaders/... structure)
            if(TS_SOURCE_ROOT)
                file(RELATIVE_PATH _rel_path "${TS_SOURCE_ROOT}" "${_glsl_file}")
            else()
                file(RELATIVE_PATH _rel_path "${_shader_dir}" "${_glsl_file}")
            endif()

            # Determine shader stage for glslangValidator
            get_filename_component(_ext "${_glsl_file}" LAST_EXT)

            # Output paths
            set(_spv_file "${TS_OUTPUT_DIR}/${_rel_path}.spv")
            set(_wgsl_file "${TS_OUTPUT_DIR}/${_rel_path}.wgsl")

            get_filename_component(_spv_dir "${_spv_file}" DIRECTORY)
            get_filename_component(_wgsl_dir "${_wgsl_file}" DIRECTORY)

            # Custom command: GLSL → SPIR-V → WGSL (non-fatal: shaders using
            # unsupported features like f64 will be skipped gracefully)
            add_custom_command(
                OUTPUT "${_wgsl_file}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_spv_dir}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_wgsl_dir}"
                COMMAND ${CMAKE_COMMAND}
                    -DGLSLANG=${GLSLANG_VALIDATOR}
                    -DTINT=${LUMI_TINT_EXECUTABLE}
                    -DGLSL_FILE=${_glsl_file}
                    -DSPV_FILE=${_spv_file}
                    -DWGSL_FILE=${_wgsl_file}
                    -DSHADER_NAME=${_rel_path}
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ShaderTranspileOne.cmake"
                DEPENDS "${_glsl_file}"
                COMMENT "Transpile: ${_rel_path}"
                VERBATIM
            )

            list(APPEND _wgsl_outputs "${_wgsl_file}")
        endforeach()
    endforeach()

    if(_wgsl_outputs)
        # Create a custom target for all transpiled shaders
        add_custom_target(${TS_TARGET}_transpiled_shaders ALL
            DEPENDS ${_wgsl_outputs}
        )

        if(TARGET ${TS_TARGET})
            add_dependencies(${TS_TARGET} ${TS_TARGET}_transpiled_shaders)
        endif()

        list(LENGTH _wgsl_outputs _count)
        lumi_done("Shader transpilation: ${_count} shaders → WGSL")
    endif()

    # Export output dir for Emscripten preload
    set(LUMI_TRANSPILED_SHADER_DIR "${TS_OUTPUT_DIR}" PARENT_SCOPE)
endfunction()
