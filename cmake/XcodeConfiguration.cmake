# XcodeConfiguration.cmake
# Helper functions for Xcode project configuration

# Enable Xcode scheme generation globally
if(CMAKE_GENERATOR MATCHES "Xcode")
    set(CMAKE_XCODE_GENERATE_SCHEME TRUE CACHE BOOL "Generate Xcode schemes" FORCE)
endif()

# Function to configure a target for Xcode with proper working directory
# Usage: luminoveau_configure_xcode(target_name working_directory)
# Example: luminoveau_configure_xcode(MyGame "${CMAKE_SOURCE_DIR}")
function(luminoveau_configure_xcode target_name working_directory)
    if(CMAKE_GENERATOR MATCHES "Xcode")
        set_target_properties(${target_name} PROPERTIES
            XCODE_GENERATE_SCHEME TRUE
            XCODE_SCHEME_WORKING_DIRECTORY "${working_directory}"
        )
        message(STATUS "Configured Xcode scheme for ${target_name} with working directory: ${working_directory}")
    endif()
endfunction()
