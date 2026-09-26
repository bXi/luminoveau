# DownloadDXC.cmake
# Automatically downloads DirectXShaderCompiler DLLs for runtime DXIL compilation

# Only relevant when using the DXIL (DirectX 12) backend on Windows
if(WIN32 AND LUMINOVEAU_GPU_BACKEND STREQUAL "DXIL")
    set(DXC_VERSION "v1.8.2407")
    set(DXC_ZIP_NAME "dxc_2024_07_31.zip")
    set(DXC_URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/${DXC_VERSION}/${DXC_ZIP_NAME}")
    set(DXC_DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/dxc_download")
    set(DXC_ZIP_PATH "${DXC_DOWNLOAD_DIR}/${DXC_ZIP_NAME}")

    # Determine output directory based on generator type
    if(CMAKE_CONFIGURATION_TYPES)
        # Multi-config generator (Visual Studio, Xcode)
        set(DXC_OUTPUT_DIR "${CMAKE_BINARY_DIR}/$<CONFIG>")
    else()
        # Single-config generator (Ninja, Unix Makefiles)
        set(DXC_OUTPUT_DIR "${CMAKE_BINARY_DIR}")
    endif()

    # Download DXC zip if not already downloaded
    if(NOT EXISTS ${DXC_ZIP_PATH})
        lumi_msg("Downloading DXC")
        file(MAKE_DIRECTORY ${DXC_DOWNLOAD_DIR})
        file(DOWNLOAD
            ${DXC_URL}
            ${DXC_ZIP_PATH}
            SHOW_PROGRESS
            STATUS DOWNLOAD_STATUS
        )

        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
        if(NOT STATUS_CODE EQUAL 0)
            list(GET DOWNLOAD_STATUS 1 ERROR_MESSAGE)
            message(WARNING "Failed to download DXC: ${ERROR_MESSAGE}")
            message(WARNING "You will need to manually download dxcompiler.dll and dxil.dll")
            message(WARNING "from: ${DXC_URL}")
            return()
        endif()
    else()
        lumi_msg("DXC cached")
    endif()

    # Extract entire zip to temp directory
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xf ${DXC_ZIP_PATH}
        WORKING_DIRECTORY ${DXC_DOWNLOAD_DIR}
        RESULT_VARIABLE EXTRACT_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )

    if(NOT EXTRACT_RESULT EQUAL 0)
        message(WARNING "Failed to extract DXC zip")
        return()
    endif()

    # Verify extracted files exist
    if(NOT EXISTS "${DXC_DOWNLOAD_DIR}/bin/x64/dxcompiler.dll")
        message(WARNING "dxcompiler.dll not found in extracted archive")
        return()
    endif()

    if(NOT EXISTS "${DXC_DOWNLOAD_DIR}/bin/x64/dxil.dll")
        message(WARNING "dxil.dll not found in extracted archive")
        return()
    endif()

    # Link against the DXC import library (required for extern DxcCreateInstance)
    set(DXC_LIB "${DXC_DOWNLOAD_DIR}/lib/x64/dxcompiler.lib")
    if(EXISTS "${DXC_LIB}")
        target_link_libraries(luminoveau PUBLIC "${DXC_LIB}")
    else()
        message(WARNING "dxcompiler.lib not found at ${DXC_LIB}")
    endif()

    # Determine the actual runtime output directory
    if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(DLL_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    else()
        set(DLL_OUTPUT_DIR "${CMAKE_BINARY_DIR}")
    endif()

    # Create a custom target to copy DLLs to output directory at build time
    add_custom_target(copy_dxc_dlls ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DLL_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DXC_DOWNLOAD_DIR}/bin/x64/dxcompiler.dll"
            "${DLL_OUTPUT_DIR}/dxcompiler.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DXC_DOWNLOAD_DIR}/bin/x64/dxil.dll"
            "${DLL_OUTPUT_DIR}/dxil.dll"
        COMMENT "Copying DXC DLLs to ${DLL_OUTPUT_DIR}"
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

            add_custom_command(TARGET copy_dxc_dlls POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CONFIG_DLL_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DXC_DOWNLOAD_DIR}/bin/x64/dxcompiler.dll"
                    "${CONFIG_DLL_DIR}/dxcompiler.dll"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DXC_DOWNLOAD_DIR}/bin/x64/dxil.dll"
                    "${CONFIG_DLL_DIR}/dxil.dll"
                COMMENT "Copying DXC DLLs to ${CONFIG} directory"
                VERBATIM
            )
        endforeach()
    endif()

    # The zip ships three licenses without saying which covers which DLL. dxcompiler.dll is built
    # from the open-source repository (LLVM's University of Illinois/NCSA license plus Microsoft's
    # MIT-licensed parts); dxil.dll contains closed-source code under LICENSE-MS.txt.
    lumi_add_notice("DirectX Shader Compiler" SPDX "LLVM Release License (University of Illinois/NCSA)"
        VERSION "${DXC_VERSION}" URL "https://github.com/microsoft/DirectXShaderCompiler"
        FILE "${DXC_DOWNLOAD_DIR}/LICENSE-LLVM.txt")
    lumi_add_notice("DirectX Shader Compiler (Microsoft)" SPDX "MIT"
        VERSION "${DXC_VERSION}" URL "https://github.com/microsoft/DirectXShaderCompiler"
        FILE "${DXC_DOWNLOAD_DIR}/LICENSE-MIT.txt")

    # A notice cannot cover dxil.dll. Its license is an agreement with whoever ships it, so the
    # engine can only make sure nobody ships it without knowing.
    message(WARNING
        "Luminoveau: the DXIL backend ships dxil.dll, which is NOT covered by the in-game "
        "licenses screen. It is under the Microsoft Software License Terms "
        "(${DXC_DOWNLOAD_DIR}/LICENSE-MS.txt), which require anyone distributing it to:\n"
        "  - use it on Windows only\n"
        "  - make distributors and end users agree to terms that protect it and Microsoft at least "
        "as much as that license (in practice: a EULA)\n"
        "  - indemnify and defend Microsoft against claims related to your application\n"
        "Read LICENSE-MS.txt and sort this out before releasing. The default SPIR-V (Vulkan) "
        "backend needs none of it.")

    lumi_done("DXC")
endif()
