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
    
    # DXC DLLs are only needed for the DXIL (DirectX 12) backend
    if(LUMINOVEAU_GPU_BACKEND STREQUAL "DXIL")
        add_custom_target(luminoveau_copy_dlls ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory "${DLL_DEST_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_BINARY_DIR}/dxc_download/bin/x64/dxcompiler.dll"
                "${DLL_DEST_DIR}/dxcompiler.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_BINARY_DIR}/dxc_download/bin/x64/dxil.dll"
                "${DLL_DEST_DIR}/dxil.dll"
            COMMENT "Copying Luminoveau runtime DLLs"
            VERBATIM
        )

        if(TARGET copy_dxc_dlls)
            add_dependencies(luminoveau_copy_dlls copy_dxc_dlls)
        endif()

        add_dependencies(luminoveau luminoveau_copy_dlls)
    endif()
endif()
