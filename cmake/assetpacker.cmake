function(PrepareAssets SOURCE_DIR OUTPUT_FILE)
    # Ensure the output directory exists
    get_filename_component(OUTPUT_DIR "${OUTPUT_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")

    # Gather all files in the source directory
    file(GLOB_RECURSE FILES_IN_SOURCE "${SOURCE_DIR}/*")

    # Strip the base directory from each file path
    set(RELATIVE_FILES)
    foreach(FILE_PATH ${FILES_IN_SOURCE})
        set(RELATIVE_PATH)
        string(REPLACE "${CMAKE_SOURCE_DIR}/" "" RELATIVE_PATH "${FILE_PATH}")
        list(APPEND RELATIVE_FILES "${RELATIVE_PATH}")
    endforeach()

    # Add the custom command to create the archive
    add_custom_command(
        OUTPUT "${OUTPUT_FILE}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E tar cf "${CMAKE_BINARY_DIR}/${OUTPUT_FILE}" --format=zip ${RELATIVE_FILES}
        DEPENDS ${FILES_IN_SOURCE}
        COMMENT "Creating ${OUTPUT_FILE} from ${SOURCE_DIR}"
    )

    # Add a custom target to trigger the command
    add_custom_target(prepare_assets_${OUTPUT_FILE} ALL DEPENDS "${CMAKE_BINARY_DIR}/${OUTPUT_FILE}")
endfunction()
