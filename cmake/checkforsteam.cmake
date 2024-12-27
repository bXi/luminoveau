target_sources("luminoveau" PRIVATE
    steam/steamhandler.cpp
    steam/steamhandler.h
)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/steam/sdk/public/steam/steam_api.h")
    find_package(Threads REQUIRED)

    target_include_directories("luminoveau" PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/steam/sdk/public/steam/"
    )

    target_link_libraries(${PROJECT_NAME}
        PRIVATE Threads::Threads
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/steam/sdk/redistributable_bin/win64/steam_api64.lib
    )

    add_custom_command(
    TARGET "luminoveau" POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/steam/sdk/redistributable_bin/win64/steam_api64.dll"
        ${CMAKE_BINARY_DIR}
    )
    message("[Luminoveau]: adding SteamWorks SDK")
endif()
