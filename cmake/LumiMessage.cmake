# LumiMessage.cmake
# Branded message helpers for Luminoveau build output.
# Uses ANSI colors matching the runtime loghandler:
#   - Dark blue  \033[34m  for brackets
#   - Light blue \033[94m  for "Lumi"
#   - Green      \033[32m  for DONE / success
#   - Yellow     \033[33m  for warnings
#   - Red        \033[31m  for errors
#   - Reset      \033[0m
#
# Uses execute_process(cmake -E echo) instead of message() so output
# bypasses CMAKE_MESSAGE_LOG_LEVEL and always prints, even when
# dependency STATUS/NOTICE spam is suppressed.

# Build the escape character once
string(ASCII 27 _LUMI_ESC)
set(_LUMI_DB "${_LUMI_ESC}[34m")   # dark blue
set(_LUMI_LB "${_LUMI_ESC}[94m")   # light blue
set(_LUMI_GR "${_LUMI_ESC}[32m")   # green
set(_LUMI_YL "${_LUMI_ESC}[33m")   # yellow
set(_LUMI_RD "${_LUMI_ESC}[31m")   # red
set(_LUMI_RS "${_LUMI_ESC}[0m")    # reset
set(_LUMI_TAG "${_LUMI_DB}[${_LUMI_LB}Lumi${_LUMI_DB}]${_LUMI_RS}")

# lumi_msg("Fetching SDL3")  →  --- [Lumi] Fetching SDL3
function(lumi_msg text)
    execute_process(COMMAND ${CMAKE_COMMAND} -E echo "--- ${_LUMI_TAG} ${text}")
endfunction()

# lumi_done("SDL3")  →  --- [Lumi] SDL3  DONE
function(lumi_done text)
    execute_process(COMMAND ${CMAKE_COMMAND} -E echo "--- ${_LUMI_TAG} ${text}  ${_LUMI_GR}DONE${_LUMI_RS}")
endfunction()

# lumi_warn("something")  →  --- [Lumi] something  WARN
function(lumi_warn text)
    execute_process(COMMAND ${CMAKE_COMMAND} -E echo "--- ${_LUMI_TAG} ${text}  ${_LUMI_YL}WARN${_LUMI_RS}")
endfunction()

# lumi_fail("something")  →  --- [Lumi] something  FAIL
function(lumi_fail text)
    execute_process(COMMAND ${CMAKE_COMMAND} -E echo "--- ${_LUMI_TAG} ${text}  ${_LUMI_RD}FAIL${_LUMI_RS}")
endfunction()

# lumi_fetch(name url tag out_src_dir) — silent git clone+checkout into _deps/<name>-src
function(lumi_fetch name url tag out_src_dir)
    set(_dir "${CMAKE_BINARY_DIR}/_deps/${name}-src")
    if(NOT EXISTS "${_dir}/.git")
        execute_process(
            COMMAND git clone "${url}" "${_dir}"
            OUTPUT_QUIET ERROR_QUIET
            RESULT_VARIABLE _clone_result
        )
        if(NOT _clone_result EQUAL 0)
            lumi_warn("${name} - clone failed")
            set(${out_src_dir} "" PARENT_SCOPE)
            return()
        endif()
        execute_process(
            COMMAND git -C "${_dir}" checkout "${tag}"
            OUTPUT_QUIET ERROR_QUIET
        )
    endif()
    set(${out_src_dir} "${_dir}" PARENT_SCOPE)
endfunction()
