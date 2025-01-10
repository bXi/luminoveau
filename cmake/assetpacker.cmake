function(PrepareAssets SOURCE_DIR OUTPUT_FILE)
    get_filename_component(OUTPUT_DIR "${OUTPUT_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")

    file(GLOB_RECURSE FILES_IN_SOURCE CONFIGURE_DEPENDS "${SOURCE_DIR}/*")

    set(RELATIVE_FILES)
    foreach(FILE_PATH ${FILES_IN_SOURCE})
        file(RELATIVE_PATH output_relative "${CMAKE_SOURCE_DIR}" "${FILE_PATH}")
        list(APPEND RELATIVE_FILES "${output_relative}")
    endforeach()

    add_custom_command(
        OUTPUT "${CMAKE_BINARY_DIR}/${OUTPUT_FILE}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E tar cf "${CMAKE_BINARY_DIR}/${OUTPUT_FILE}" --format=zip ${RELATIVE_FILES}
        DEPENDS ${FILES_IN_SOURCE}
        COMMENT "Creating ${OUTPUT_FILE} from ${SOURCE_DIR}"
    )

    add_custom_target(prepare_assets_${OUTPUT_FILE} ALL DEPENDS "${CMAKE_BINARY_DIR}/${OUTPUT_FILE}")
    add_dependencies("luminoveau" prepare_assets_${OUTPUT_FILE})
    target_compile_definitions("luminoveau" PRIVATE PACKED_ASSET_FILE="${OUTPUT_FILE}")
endfunction()
