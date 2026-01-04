# CopyAssets.cmake - Helper for copying assets to Android/Desktop

function(copy_assets_directory SOURCE_DIR TARGET_BASE_DIR)
    # Get all files recursively from source directory
    file(GLOB_RECURSE ASSET_FILES 
        RELATIVE "${SOURCE_DIR}"
        "${SOURCE_DIR}/*"
    )
    
    # Filter out hidden files and directories
    list(FILTER ASSET_FILES EXCLUDE REGEX "^\\..*")
    list(FILTER ASSET_FILES EXCLUDE REGEX "/\\.")
    
    set(COPIED_ASSETS "")
    
    foreach(ASSET_FILE ${ASSET_FILES})
        # Skip directories (only process files)
        set(SOURCE_PATH "${SOURCE_DIR}/${ASSET_FILE}")
        if(NOT IS_DIRECTORY "${SOURCE_PATH}")
            # Get the directory path for this file
            get_filename_component(ASSET_DIR "${ASSET_FILE}" DIRECTORY)
            
            if(ANDROID)
                # Android: copy to MOBILE_ASSETS_DIR maintaining structure
                if(NOT MOBILE_ASSETS_DIR)
                    message(FATAL_ERROR "MOBILE_ASSETS_DIR not set for Android build")
                endif()
                set(DEST_PATH "${MOBILE_ASSETS_DIR}/${ASSET_FILE}")
            else()
                # Desktop: copy to build directory maintaining structure
                set(DEST_PATH "${TARGET_BASE_DIR}/${ASSET_FILE}")
            endif()
            
            # Create the destination directory if needed
            get_filename_component(DEST_DIR "${DEST_PATH}" DIRECTORY)
            file(MAKE_DIRECTORY "${DEST_DIR}")
            
            # Add command to copy the file
            add_custom_command(
                OUTPUT "${DEST_PATH}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SOURCE_PATH}" "${DEST_PATH}"
                DEPENDS "${SOURCE_PATH}"
                COMMENT "Copying asset: ${ASSET_FILE}"
            )
            
            # Add to list of outputs
            list(APPEND COPIED_ASSETS "${DEST_PATH}")
        endif()
    endforeach()
    
    # Create custom target that depends on all copied assets
    if(COPIED_ASSETS)
        add_custom_target(copy_assets_target ALL DEPENDS ${COPIED_ASSETS})
        
        # Make the executable depend on asset copying
        if(TARGET ${EXECUTABLE_NAME})
            add_dependencies(${EXECUTABLE_NAME} copy_assets_target)
        endif()
        
        list(LENGTH COPIED_ASSETS ASSET_COUNT)
        message(STATUS "Configured copying of ${ASSET_COUNT} assets from ${SOURCE_DIR}")
    else()
        message(WARNING "No assets found in ${SOURCE_DIR}")
    endif()
endfunction()
