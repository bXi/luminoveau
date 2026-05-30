# ShaderTranspileOne.cmake
# Transpiles a single shader: GLSL → SPIR-V → WGSL
# Called as a script (-P) from custom commands. Non-fatal: creates an empty
# .wgsl output on failure so the build continues.
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
    message(WARNING "[Lumi] SPIR-V→WGSL failed for ${SHADER_NAME} (unsupported features?): ${_wgsl_err}")
    file(WRITE "${WGSL_FILE}" "")
    return()
endif()

# Clean up intermediate .spv
file(REMOVE "${SPV_FILE}")
