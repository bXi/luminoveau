lumi_msg("Fetching SPIRV-Cross")
lumi_fetch("spirv-cross" "https://github.com/KhronosGroup/SPIRV-Cross.git" "39e6a39c" SPIRVCROSS_ROOT)

if(SPIRVCROSS_ROOT)

    target_include_directories(luminoveau SYSTEM PUBLIC
        "${SPIRVCROSS_ROOT}"
    )

    target_sources(luminoveau PRIVATE
        "${SPIRVCROSS_ROOT}/spirv_cross.cpp"
        "${SPIRVCROSS_ROOT}/spirv_parser.cpp"
        "${SPIRVCROSS_ROOT}/spirv_cross_parsed_ir.cpp"
        "${SPIRVCROSS_ROOT}/spirv_cfg.cpp"
        "${SPIRVCROSS_ROOT}/spirv_glsl.cpp"
        "${SPIRVCROSS_ROOT}/spirv_cross_c.cpp"
    )

    if(APPLE)
        target_sources(luminoveau PRIVATE
            "${SPIRVCROSS_ROOT}/spirv_msl.cpp"
        )
        target_compile_definitions(luminoveau PRIVATE
            SPIRV_CROSS_C_API_GLSL=1
            SPIRV_CROSS_C_API_MSL=1
            SPIRV_CROSS_C_API_HLSL=0
            SPIRV_CROSS_C_API_CPP=0
            SPIRV_CROSS_C_API_REFLECT=0
        )
    elseif(WIN32)
        target_sources(luminoveau PRIVATE
            "${SPIRVCROSS_ROOT}/spirv_hlsl.cpp"
        )
        target_compile_definitions(luminoveau PRIVATE
            SPIRV_CROSS_C_API_GLSL=1
            SPIRV_CROSS_C_API_MSL=0
            SPIRV_CROSS_C_API_HLSL=1
            SPIRV_CROSS_C_API_CPP=0
            SPIRV_CROSS_C_API_REFLECT=0
        )
    else()
        target_compile_definitions(luminoveau PRIVATE
            SPIRV_CROSS_C_API_GLSL=1
            SPIRV_CROSS_C_API_MSL=0
            SPIRV_CROSS_C_API_HLSL=0
            SPIRV_CROSS_C_API_CPP=0
            SPIRV_CROSS_C_API_REFLECT=0
        )
    endif()

    lumi_done("SPIRV-Cross (source-only)")
else()
    lumi_warn("SPIRV-Cross - fetch failed")
endif()
