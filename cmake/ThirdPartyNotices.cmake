# ThirdPartyNotices.cmake
# Ships the license text of everything linked into a game, so nobody building on the engine has
# to assemble it by hand. Two outputs, generated when the top-level CMakeLists finishes:
#
#   third_party_notices.cpp   compiled into luminoveau. Licenses:: reads it, and it is what puts
#                             the text in every copy of every game binary.
#   THIRD_PARTY_NOTICES.txt   copied next to every executable that links luminoveau.
#
# Register a component beside the code that fetches it, inside the same option guard, so a build
# lists exactly what it linked:
#
#   lumi_add_notice(<name> SPDX <id> [VERSION <v>] [URL <url>] (FILE <license file> | DIR <source dir>))
#
# DIR takes the first LICENSE* or COPYING* file in that directory. A game can call this for its
# own dependencies anywhere after add_subdirectory(luminoveau).

include_guard(GLOBAL)

set_property(GLOBAL PROPERTY LUMI_NOTICES_OUTPUT_DIR "${CMAKE_BINARY_DIR}/luminoveau_notices")

function(lumi_add_notice name)
    cmake_parse_arguments(PARSE_ARGV 1 _n "" "SPDX;VERSION;URL;FILE;DIR" "")

    set(_file "${_n_FILE}")
    if(NOT _file AND _n_DIR)
        file(GLOB _candidates LIST_DIRECTORIES false
            "${_n_DIR}/LICENSE*" "${_n_DIR}/License*" "${_n_DIR}/license*"
            "${_n_DIR}/COPYING*" "${_n_DIR}/Copying*" "${_n_DIR}/copying*")
        if(_candidates)
            # Case-insensitive filesystems match the same file under every spelling.
            list(REMOVE_DUPLICATES _candidates)
            list(SORT _candidates)
            list(GET _candidates 0 _file)
        endif()
    endif()

    if(NOT _file OR NOT EXISTS "${_file}")
        lumi_warn("${name} - no license file found, left out of the third-party notices")
        return()
    endif()

    get_property(_names GLOBAL PROPERTY LUMI_NOTICE_NAMES)
    if(name IN_LIST _names)
        return()
    endif()

    list(LENGTH _names _index)
    set_property(GLOBAL APPEND PROPERTY LUMI_NOTICE_NAMES "${name}")
    set_property(GLOBAL PROPERTY "LUMI_NOTICE_${_index}_SPDX" "${_n_SPDX}")
    set_property(GLOBAL PROPERTY "LUMI_NOTICE_${_index}_VERSION" "${_n_VERSION}")
    set_property(GLOBAL PROPERTY "LUMI_NOTICE_${_index}_URL" "${_n_URL}")
    set_property(GLOBAL PROPERTY "LUMI_NOTICE_${_index}_FILE" "${_file}")
endfunction()

function(_lumi_c_string out value)
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    set(${out} "\"${value}\"" PARENT_SCOPE)
endfunction()

# Rewriting an unchanged file would recompile luminoveau on every configure.
function(_lumi_write_if_changed path content)
    if(EXISTS "${path}")
        file(READ "${path}" _existing)
        if("${_existing}" STREQUAL "${content}")
            return()
        endif()
    endif()
    file(WRITE "${path}" "${content}")
endfunction()

function(_lumi_collect_executables dir out)
    set(_found "")

    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
        get_target_property(_type ${_target} TYPE)
        if(NOT _type STREQUAL "EXECUTABLE")
            continue()
        endif()
        get_target_property(_libs ${_target} LINK_LIBRARIES)
        if(_libs AND "luminoveau" IN_LIST _libs)
            list(APPEND _found ${_target})
        endif()
    endforeach()

    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_subdir IN LISTS _subdirs)
        _lumi_collect_executables("${_subdir}" _nested)
        list(APPEND _found ${_nested})
    endforeach()

    set(${out} "${_found}" PARENT_SCOPE)
endfunction()

function(_lumi_generate_notices)
    get_property(_names GLOBAL PROPERTY LUMI_NOTICE_NAMES)
    get_property(_outDir GLOBAL PROPERTY LUMI_NOTICES_OUTPUT_DIR)

    set(_arrays "")
    set(_records "")
    set(_hashes "")
    set(_txt "THIRD-PARTY SOFTWARE NOTICES\n\nThis software includes the following components, each listed with the license it is\ndistributed under.\n")

    set(_index 0)
    foreach(_name IN LISTS _names)
        foreach(_field SPDX VERSION URL FILE)
            get_property(_${_field} GLOBAL PROPERTY "LUMI_NOTICE_${_index}_${_field}")
        endforeach()

        # Byte-identical license files are embedded once and shared by every record that uses them.
        file(SHA1 "${_FILE}" _hash)
        list(FIND _hashes "${_hash}" _text)
        if(_text EQUAL -1)
            list(LENGTH _hashes _text)
            list(APPEND _hashes "${_hash}")
            # Hex bytes, not a string literal: MSVC caps string literals far below the size of
            # the longer licenses.
            file(READ "${_FILE}" _hex HEX)
            string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
            string(APPEND _arrays "const unsigned char TEXT_${_text}[] = { ${_bytes}0x00 };\n")
        endif()

        _lumi_c_string(_cName "${_name}")
        _lumi_c_string(_cSpdx "${_SPDX}")
        _lumi_c_string(_cVersion "${_VERSION}")
        _lumi_c_string(_cUrl "${_URL}")
        string(APPEND _records "    { ${_cName}, ${_cSpdx}, ${_cVersion}, ${_cUrl}, reinterpret_cast<const char *>(TEXT_${_text}), sizeof(TEXT_${_text}) - 1 },\n")

        set(_heading "${_name}")
        if(_VERSION)
            string(APPEND _heading " ${_VERSION}")
        endif()
        string(LENGTH "${_heading}" _headingLength)
        string(REPEAT "=" ${_headingLength} _underline)
        file(READ "${_FILE}" _content)
        string(APPEND _txt "\n\n${_heading}\n${_underline}\n")
        if(_SPDX)
            string(APPEND _txt "License: ${_SPDX}\n")
        endif()
        if(_URL)
            string(APPEND _txt "Source:  ${_URL}\n")
        endif()
        string(APPEND _txt "\n${_content}\n")

        math(EXPR _index "${_index} + 1")
    endforeach()

    set(_cpp "// Generated by cmake/ThirdPartyNotices.cmake. Do not edit.\n")
    string(APPEND _cpp "// NOLINTBEGIN\n#include \"core/licenses/noticerecord.h\"\n\nnamespace {\n${_arrays}} // namespace\n\n")
    string(APPEND _cpp "extern const NoticeRecord LUMI_NOTICE_RECORDS[] = {\n${_records}};\n\n")
    string(APPEND _cpp "extern const size_t LUMI_NOTICE_RECORD_COUNT = sizeof(LUMI_NOTICE_RECORDS) / sizeof(LUMI_NOTICE_RECORDS[0]);\n// NOLINTEND\n")

    _lumi_write_if_changed("${_outDir}/third_party_notices.cpp" "${_cpp}")
    _lumi_write_if_changed("${_outDir}/THIRD_PARTY_NOTICES.txt" "${_txt}")

    target_sources(luminoveau PRIVATE "${_outDir}/third_party_notices.cpp")

    # A custom target per executable rather than a POST_BUILD step: POST_BUILD can only be attached
    # from the directory that created the executable, and this runs from the top-level one.
    _lumi_collect_executables("${CMAKE_SOURCE_DIR}" _executables)
    foreach(_exe IN LISTS _executables)
        if(TARGET ${_exe}_notices)
            continue()
        endif()
        add_custom_target(${_exe}_notices
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${_exe}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_outDir}/THIRD_PARTY_NOTICES.txt" "$<TARGET_FILE_DIR:${_exe}>/THIRD_PARTY_NOTICES.txt"
            VERBATIM)
        set_target_properties(${_exe}_notices PROPERTIES FOLDER "Luminoveau")
        add_dependencies(${_exe} ${_exe}_notices)
    endforeach()

    list(LENGTH _names _count)
    lumi_done("Third-party notices (${_count} components)")
endfunction()

# Deferred to the end of the top-level directory, so notices a game registers after
# add_subdirectory(luminoveau), and executables it creates afterwards, are all included.
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _lumi_generate_notices)
