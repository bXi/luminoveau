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

# lumi_fetch(name url ref out_src_dir [SUBMODULES])
#
# Fetches **one commit** of a repository — no history — into _deps/<name>-src, and hands back the
# directory. `ref` is a tag or a *full* commit hash; git can fetch either on its own, but not an
# abbreviated hash, which is why every pin in this engine is written out in full.
#
# A whole clone was the old way, and history was about 70% of every build directory's `_deps`:
# 2.4 GB of 3.5 GB in a typical game build, paid again for every build directory and restored from
# every CI cache. What gets built is the same checkout either way.
#
# SUBMODULES also fetches the submodules, one commit each. Opt-in, because several packages carry
# submodules nothing here builds from — SDL_image's vendored image libraries, SDL_shadercross's
# compilers — and fetching those was most of the cost.
#
# Set the environment variable LUMI_DEPS_CACHE to a directory to share sources between build
# directories: each package is kept once per ref there, rather than once per build directory.
#
# A directory already holding the ref — including a full clone from before this existed — is used
# as it is. One holding a different ref is replaced.
function(lumi_fetch name url ref out_src_dir)
    cmake_parse_arguments(_LF "SUBMODULES" "" "" ${ARGN})

    if(DEFINED ENV{LUMI_DEPS_CACHE} AND NOT "$ENV{LUMI_DEPS_CACHE}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{LUMI_DEPS_CACHE}" _cache)
        set(_dir "${_cache}/${name}/${ref}")
    else()
        set(_dir "${CMAKE_BINARY_DIR}/_deps/${name}-src")
    endif()

    # What this directory was fetched for. Written after a fetch completes, so a directory left
    # half-fetched by an interrupted configure has none and is fetched again.
    set(_marker "${_dir}/.git/lumi-ref")

    if(EXISTS "${_dir}/.git")
        set(_have "")
        if(EXISTS "${_marker}")
            file(READ "${_marker}" _have)
            string(STRIP "${_have}" _have)
        else()
            # A full clone from the old lumi_fetch or from CPM: it knows its tags and history, so
            # it can say whether it is already on the ref. If so it is adopted rather than refetched.
            execute_process(COMMAND git -C "${_dir}" rev-parse HEAD
                OUTPUT_VARIABLE _head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            execute_process(COMMAND git -C "${_dir}" rev-parse "${ref}^{commit}"
                OUTPUT_VARIABLE _want OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
                RESULT_VARIABLE _known)
            if(_known EQUAL 0 AND _head STREQUAL _want)
                set(_have "${ref}")
                file(WRITE "${_marker}" "${ref}\n")
            endif()
        endif()

        if(_have STREQUAL ref)
            set(${out_src_dir} "${_dir}" PARENT_SCOPE)
            return()
        endif()
        file(REMOVE_RECURSE "${_dir}")
    endif()

    file(MAKE_DIRECTORY "${_dir}")
    execute_process(COMMAND git -C "${_dir}" init -q OUTPUT_QUIET ERROR_QUIET)
    execute_process(COMMAND git -C "${_dir}" remote add origin "${url}" OUTPUT_QUIET ERROR_QUIET)
    execute_process(COMMAND git -C "${_dir}" fetch -q --depth 1 origin "${ref}"
        OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE _fetched)
    if(_fetched EQUAL 0)
        execute_process(COMMAND git -C "${_dir}" checkout -q FETCH_HEAD
            OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE _fetched)
    endif()

    # **Falls back to the whole clone** rather than failing, for a host that refuses to serve a
    # single commit by hash, or a ref written short. Slower, never wrong.
    if(NOT _fetched EQUAL 0)
        lumi_warn("${name} - single-commit fetch of ${ref} refused, cloning in full")
        file(REMOVE_RECURSE "${_dir}")
        execute_process(COMMAND git clone -q "${url}" "${_dir}"
            OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE _cloned)
        if(NOT _cloned EQUAL 0)
            lumi_warn("${name} - clone failed")
            set(${out_src_dir} "" PARENT_SCOPE)
            return()
        endif()
        execute_process(COMMAND git -C "${_dir}" checkout -q "${ref}"
            OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE _checked)
        if(NOT _checked EQUAL 0)
            lumi_warn("${name} - ${ref} not found")
            set(${out_src_dir} "" PARENT_SCOPE)
            return()
        endif()
    endif()

    if(_LF_SUBMODULES)
        execute_process(COMMAND git -C "${_dir}" submodule update -q --init --recursive --depth 1
            OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE _subs)
        if(NOT _subs EQUAL 0)
            # A submodule pinned to a commit its host will not serve alone: take its history.
            execute_process(COMMAND git -C "${_dir}" submodule update -q --init --recursive
                OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE _subs)
        endif()
        if(NOT _subs EQUAL 0)
            lumi_warn("${name} - submodules failed")
            set(${out_src_dir} "" PARENT_SCOPE)
            return()
        endif()
    endif()

    file(WRITE "${_marker}" "${ref}\n")
    set(${out_src_dir} "${_dir}" PARENT_SCOPE)
endfunction()

# lumi_add_package(NAME <name> GIT <url> REF <tag-or-full-hash> [SUBMODULES] [OPTIONS ...])
#
# CPMAddPackage on a source tree `lumi_fetch` fetched, so CPM configures the package as before —
# OPTIONS, <name>_ADDED, <name>_SOURCE_DIR, <name>_BINARY_DIR — without cloning its history.
# A macro, not a function, so the variables CPM sets land in the caller's scope.
#
# Leaves <name>_ADDED false when the fetch failed, which is what every caller's `else()` already
# reports.
macro(lumi_add_package)
    cmake_parse_arguments(_LAP "SUBMODULES" "NAME;GIT;REF" "OPTIONS" ${ARGN})
    string(TOLOWER "${_LAP_NAME}" _lap_lower)
    if(_LAP_SUBMODULES)
        lumi_fetch("${_lap_lower}" "${_LAP_GIT}" "${_LAP_REF}" _lap_src SUBMODULES)
    else()
        lumi_fetch("${_lap_lower}" "${_LAP_GIT}" "${_LAP_REF}" _lap_src)
    endif()
    if(_lap_src)
        CPMAddPackage(
            NAME ${_LAP_NAME}
            SOURCE_DIR "${_lap_src}"
            EXCLUDE_FROM_ALL YES
            OPTIONS ${_LAP_OPTIONS}
        )
    else()
        set(${_LAP_NAME}_ADDED FALSE)
    endif()
endmacro()
