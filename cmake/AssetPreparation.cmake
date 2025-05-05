# AssetPreparation.cmake
# Defines a function to package assets from a source directory into a zip archive,
# creating a custom build target and setting a compile definition for the Luminoveau library.

function(PrepareAssets SOURCE_DIR OUTPUT_FILE)
    # Validating input parameters
    if(NOT SOURCE_DIR)
        message(FATAL_ERROR "SOURCE_DIR must be specified")
    endif()
    if(NOT OUTPUT_FILE)
        message(FATAL_ERROR "OUTPUT_FILE must be specified")
    endif()

    # Ensuring the source directory exists
    if(NOT IS_DIRECTORY "${SOURCE_DIR}")
        message(FATAL_ERROR "Source directory '${SOURCE_DIR}' does not exist")
    endif()

    # Creating output directory if it doesn't exist
    get_filename_component(OUTPUT_DIR "${OUTPUT_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")

    # Collecting all files in the source directory recursively
    file(GLOB_RECURSIVE FILES_IN_SOURCE CONFIGURE_DEPENDS "${SOURCE_DIR}/*")

    # Converting absolute paths to relative paths for archiving
    set(RELATIVE_FILES)
    foreach(FILE_PATH ${FILES_IN_SOURCE})
        file(RELATIVE_PATH OUTPUT_RELATIVE "${CMAKE_SOURCE_DIR}" "${FILE_PATH}")
        list(APPEND RELATIVE_FILES "${OUTPUT_RELATIVE}")
    endforeach()

    # Ensuring there are files to archive
    if(NOT RELATIVE_FILES)
        message(WARNING "No files found in '${SOURCE_DIR}' to package into '${OUTPUT_FILE}'")
    endif()

    # Generating a unique target name based on the output file
    string(REPLACE "/" "_" TARGET_SUFFIX "${OUTPUT_FILE}")
    string(REPLACE "\\" "_" TARGET_SUFFIX "${TARGET_SUFFIX}")
    set(TARGET_NAME "prepare_assets_${TARGET_SUFFIX}")

    # Creating custom command to generate the zip archive
    add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/${OUTPUT_FILE}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E tar cf "${CMAKE_BINARY_DIR}/${OUTPUT_FILE}" --format=zip ${RELATIVE_FILES}
        DEPENDS ${FILES_IN_SOURCE}
        COMMENT "Packaging assets into ${OUTPUT_FILE} from ${SOURCE_DIR}"
        VERBATIM
    )

    # Creating custom target to ensure the archive is built
    add_custom_target(${TARGET_NAME} ALL
        DEPENDS "${CMAKE_BINARY_DIR}/${OUTPUT_FILE}"
    )

    # Adding dependency to the luminoveau library
    add_dependencies(luminoveau ${TARGET_NAME})

    # Setting compile definition for the asset file path
    target_compile_definitions(luminoveau PRIVATE PACKED_ASSET_FILE="${OUTPUT_FILE}")
endfunction()