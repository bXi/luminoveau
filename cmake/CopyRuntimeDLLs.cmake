# CopyRuntimeDLLs.cmake
# Automatic runtime DLL copying - copies to parent project's build directory

if(WIN32)
    # Determine where to copy DLLs
    # If we're in a subdirectory (e.g., lumifps includes luminoveau), copy to parent's binary dir
    # Otherwise copy to our own binary dir
    if(NOT CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
        set(DLL_DEST_DIR "${CMAKE_BINARY_DIR}")
        message(STATUS "Luminoveau: Will copy runtime DLLs to parent project: ${DLL_DEST_DIR}")
    else()
        set(DLL_DEST_DIR "${CMAKE_BINARY_DIR}/bin")
        message(STATUS "Luminoveau: Will copy runtime DLLs to: ${DLL_DEST_DIR}")
    endif()
    
    # Create target that copies all DLLs to destination
    add_custom_target(luminoveau_copy_dlls ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DLL_DEST_DIR}"
        # Copy DXC DLLs from download directory
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/dxc_download/bin/x64/dxcompiler.dll"
            "${DLL_DEST_DIR}/dxcompiler.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/dxc_download/bin/x64/dxil.dll"
            "${DLL_DEST_DIR}/dxil.dll"
        COMMENT "Copying Luminoveau runtime DLLs"
        VERBATIM
    )
    
    # Add dependency on DXC download
    if(TARGET copy_dxc_dlls)
        add_dependencies(luminoveau_copy_dlls copy_dxc_dlls)
    endif()
    
    # Make luminoveau depend on DLL copying so it always happens
    add_dependencies(luminoveau luminoveau_copy_dlls)
endif()
