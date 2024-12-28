
if (ADD_IMGUI)
#    CPMAddPackage(
#        NAME Imgui
#        GIT_TAG 8cc6eee
#        GITHUB_REPOSITORY ocornut/imgui
#    )
    CPMAddPackage(
        NAME Imgui
        GIT_TAG d5edda1
        GITHUB_REPOSITORY bXi/imgui-sdlgpu
    )



    if(Imgui_ADDED)
        add_definitions(-DADD_IMGUI)
        target_include_directories("luminoveau" PUBLIC ${Imgui_SOURCE_DIR})
        target_sources("luminoveau" PUBLIC
            ${Imgui_SOURCE_DIR}/imgui.cpp
            ${Imgui_SOURCE_DIR}/imgui_demo.cpp
            ${Imgui_SOURCE_DIR}/imgui_draw.cpp
            ${Imgui_SOURCE_DIR}/imgui_tables.cpp
            ${Imgui_SOURCE_DIR}/imgui_widgets.cpp
        )

        if (WIN32)
            target_sources("luminoveau" PRIVATE
                ${Imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
                ${Imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
                ${Imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
                ${Imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3_shadercross.h
            )
        else()
            target_sources("luminoveau" PRIVATE
                ${Imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
                ${Imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
                ${Imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3_shadercross.h
            )
        endif()
    endif()
endif()
