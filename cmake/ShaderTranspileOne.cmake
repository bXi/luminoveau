# ShaderTranspileOne.cmake
# Transpiles a single shader: GLSL → SPIR-V → WGSL
# Called as a script (-P) from custom commands. **Fatal on failure**: a shader that will not
# convert cannot be drawn with, and the alternative — an empty .wgsl and a warning — hid the
# error until run time and then only once. See the note on the Tint step.
#
# Input variables (set via -D):
#   GLSLANG     - path to glslangValidator
#   TINT        - path to tint executable
#   GLSL_FILE   - input .glsl file
#   SPV_FILE    - intermediate .spv output
#   WGSL_FILE   - final .wgsl output
#   SHADER_NAME - display name for messages

# Step 1: GLSL → SPIR-V
execute_process(
    COMMAND "${GLSLANG}" -V "${GLSL_FILE}" -o "${SPV_FILE}" --target-env vulkan1.0
    RESULT_VARIABLE _spv_result
    OUTPUT_VARIABLE _spv_out
    ERROR_VARIABLE _spv_err
)

if(NOT _spv_result EQUAL 0)
    message(WARNING "[Lumi] GLSL→SPIR-V failed for ${SHADER_NAME}: ${_spv_err}")
    file(WRITE "${WGSL_FILE}" "")
    return()
endif()

# Step 2: SPIR-V → WGSL
execute_process(
    COMMAND "${TINT}" "${SPV_FILE}" --format wgsl -o "${WGSL_FILE}"
    RESULT_VARIABLE _wgsl_result
    OUTPUT_VARIABLE _wgsl_out
    ERROR_VARIABLE _wgsl_err
)

if(NOT _wgsl_result EQUAL 0)
    # **Fatal, and the empty file is not written.**
    #
    # Warning and writing an empty `.wgsl` was worse than it looks. The build succeeded, so the
    # warning scrolled past once and was gone — and it only ever appeared *once*, because the
    # empty output then satisfied the custom command and the build system never ran it again. What
    # was left was a shader that fails at run time with "pre-transpiled WGSL not found", naming a
    # file that is sitting right there with nothing in it. Two people looked in the wrong place.
    #
    # A shader that cannot be converted is a shader the game cannot draw with, so stopping here
    # puts the error in front of whoever introduced it, with Tint's own words.
    message(FATAL_ERROR
        "[Lumi] SPIR-V→WGSL failed for ${SHADER_NAME}\n"
        "${_wgsl_err}\n"
        "The SPIR-V is kept at ${SPV_FILE} for inspection.")
endif()

# Step 3: renumber the bind groups.
#
# **The two backends do not agree on what a set number means, and Tint copies the source's
# numbering straight through.** Game shaders are authored to SDL_gpu's SPIR-V convention, because
# that is what the native build compiles them with:
#
#     set 0  vertex storage buffers
#     set 1  vertex uniforms
#     set 2  fragment samplers
#     set 3  fragment uniforms
#
# `WebGpuGpuBackend::CreateGraphicsPipeline` builds its bind group layouts in a fixed order that
# is not the same one:
#
#     group 0  vertex uniforms
#     group 1  fragment uniforms
#     group 2  fragment samplers
#     group 3  vertex storage buffers (or fragment storage textures)
#
# Left alone, a shader with both a vertex storage buffer and a vertex uniform declares them at
# groups 0 and 1, the backend declares layouts for vertex and fragment *uniforms* there, and the
# module is rejected — "references multiple variables that use the same resource binding". Every
# pipeline built from it then fails with "invalid due to a previous error", which is what it
# actually looks like from the console.
#
# Only shaders using vertex storage buffers collide outright, which is why this went unnoticed:
# a shader with nothing but a sampler happens to land on the right number either way.
#
# Substituted through placeholder tokens because the mapping is a permutation — replacing 1→0
# and then 0→3 in sequence would send the original 1 all the way to 3.
file(READ "${WGSL_FILE}" _wgsl)

string(REGEX REPLACE "@group\\(0u?\\)" "@group(LUMI_G3)" _wgsl "${_wgsl}")
string(REGEX REPLACE "@group\\(1u?\\)" "@group(LUMI_G0)" _wgsl "${_wgsl}")
string(REGEX REPLACE "@group\\(3u?\\)" "@group(LUMI_G1)" _wgsl "${_wgsl}")

string(REPLACE "@group(LUMI_G0)" "@group(0)" _wgsl "${_wgsl}")
string(REPLACE "@group(LUMI_G1)" "@group(1)" _wgsl "${_wgsl}")
string(REPLACE "@group(LUMI_G3)" "@group(3)" _wgsl "${_wgsl}")

file(WRITE "${WGSL_FILE}" "${_wgsl}")

# Clean up intermediate .spv
file(REMOVE "${SPV_FILE}")
