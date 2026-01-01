# DownloadSPIRVCross.cmake
# Automatically downloads SPIRV-Cross DLL for runtime shader cross-compilation

# Only download on Windows
if(WIN32)
    # SPIRV-Cross is included in Vulkan SDK, but we can also build it ourselves
    # For now, we'll extract it from our vendored SPIRV-Cross static library build
    
    set(SPIRV_CROSS_DLL_DIR "${CMAKE_BINARY_DIR}/spirv_cross_dll")
    
    message(STATUS "Setting up SPIRV-Cross shared library for SDL_shadercross...")
    
    # We need to build spirv-cross-c-shared as a DLL
    # SDL_shadercross's vendored build should handle this, but let's verify
    
    # Check if SDL_shadercross built the DLL for us
    if(TARGET spirv-cross-c-shared)
        message(STATUS "spirv-cross-c-shared target found from SDL_shadercross")
        
        # Determine the actual runtime output directory
        if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
            set(DLL_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
        else()
            set(DLL_OUTPUT_DIR "${CMAKE_BINARY_DIR}")
        endif()
        
        # Create custom target to copy the DLL after it's built
        add_custom_target(copy_spirv_cross_dll ALL
            COMMAND ${CMAKE_COMMAND} -E echo "Copying spirv-cross-c-shared.dll..."
            COMMAND ${CMAKE_COMMAND} -E make_directory "${DLL_OUTPUT_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:spirv-cross-c-shared>"
                "${DLL_OUTPUT_DIR}/spirv-cross-c-shared.dll"
            DEPENDS spirv-cross-c-shared
            COMMENT "Copying SPIRV-Cross DLL to ${DLL_OUTPUT_DIR}"
            VERBATIM
        )
        
        # For multi-config generators, copy to per-config directories
        if(CMAKE_CONFIGURATION_TYPES)
            foreach(CONFIG ${CMAKE_CONFIGURATION_TYPES})
                # Use per-config runtime output directory if set, otherwise use default pattern
                if(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG})
                    set(CONFIG_DLL_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG}}")
                elseif(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
                    set(CONFIG_DLL_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CONFIG}")
                else()
                    set(CONFIG_DLL_DIR "${CMAKE_BINARY_DIR}/${CONFIG}")
                endif()
                
                add_custom_command(TARGET copy_spirv_cross_dll POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${CONFIG_DLL_DIR}"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:spirv-cross-c-shared>"
                        "${CONFIG_DLL_DIR}/spirv-cross-c-shared.dll"
                    COMMENT "Copying SPIRV-Cross DLL to ${CONFIG} directory"
                    VERBATIM
                )
            endforeach()
        endif()
        
        message(STATUS "SPIRV-Cross DLL will be copied at build time")
    else()
        message(WARNING "spirv-cross-c-shared target not found!")
        message(WARNING "SDL_shadercross may not work without spirv-cross-c-shared.dll")
        message(WARNING "You may need to manually download it from Vulkan SDK:")
        message(WARNING "https://vulkan.lunarg.com/")
    endif()
    
else()
    message(STATUS "Not on Windows, skipping SPIRV-Cross DLL setup")
endif()
