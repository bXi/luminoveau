# Luminoveau example demos.
#
# Each demo is a self-contained directory under examples/ with a main.cpp implementing the
# AppInit/AppIterate/AppEvent/AppQuit callbacks, and optionally an assets/ folder. The engine's
# lumi_main.cpp (pulled in as an INTERFACE source because LUMINOVEAU_USE_CALLBACKS is forced on
# for example builds) provides the SDL3 entry point.
#
# Add a demo with:  add_lumi_example(<dir_name>)
# Build all demos with:  cmake -DLUMINOVEAU_BUILD_EXAMPLES=ON ...

# One shared target that copies every demo's assets, instead of a per-demo target. Each demo
# appends its own copy step to it (POST_BUILD commands accumulate) and depends on it, so we get a
# single `examples_assets` target rather than N. It's an ALL target, so it always runs and
# copy_if_different (in copy_assets.cmake) makes re-copying edited/new assets cheap — no reconfigure
# needed when an assets/ folder gains files. Not used on web (assets are preloaded at link time).
if(NOT EMSCRIPTEN)
    add_custom_target(examples_assets ALL COMMENT "Copying demo assets")
endif()

# Creates one executable per demo. Each lands in its own output directory so its assets/ can sit
# next to the binary (lumi_main.cpp chdir's to the executable's folder before mounting assets).
function(add_lumi_example name)
    set(demo_dir "${CMAKE_CURRENT_SOURCE_DIR}/${name}")
    set(demo_out "${CMAKE_BINARY_DIR}/examples/${name}")

    add_executable(${name} "${demo_dir}/main.cpp")
    target_link_libraries(${name} PRIVATE luminoveau)
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${demo_out}")

    # Register on the aggregate 'examples' target (built below) so one target builds every demo.
    set_property(GLOBAL APPEND PROPERTY LUMI_EXAMPLE_TARGETS ${name})

    if(EMSCRIPTEN)
        # Engine helper: .html shell, wasm tuning, GLSL→WGSL transpile + asset preloads.
        # These demos are embedded on the website, so use the auto-start sample shell (no click-to-
        # start gate — the demos run without audio, so no user gesture is needed to unlock it).
        luminoveau_configure_web_target(${name} SOURCE_DIR ${demo_dir} OUTPUT_DIR ${demo_out}
            SHELL_FILE ${LUMINOVEAU_ROOT_DIR}/assets/shell_sample.html)
    else()
        if(APPLE)
            target_link_libraries(${name} PRIVATE)
        else()
            target_link_libraries(${name} PRIVATE -static-libgcc -static-libstdc+)
        endif()
        # Register this demo's asset copy on the shared target, and make the demo depend on it so
        # building a single demo (e.g. in an IDE) still refreshes its assets.
        add_custom_command(TARGET examples_assets POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -DSRC=${demo_dir}/assets -DDST=${demo_out}/assets
                -P "${CMAKE_CURRENT_SOURCE_DIR}/copy_assets.cmake")
        add_dependencies(${name} examples_assets)
    endif()
endfunction()

# Scaffolds/templates (00_*): starting points to copy, not single-feature demos.
add_lumi_example(00_1_empty_project)
add_lumi_example(00_2_game_shell)
# Feature demos: each showcases one or two engine features.
add_lumi_example(01_bouncing_ball)
add_lumi_example(02_pong)
add_lumi_example(03_snake)
add_lumi_example(04_breakout)
add_lumi_example(05_sprite_animator)
add_lumi_example(06_asteroids)
add_lumi_example(07_top_down_shooter)
add_lumi_example(08_platformer)
add_lumi_example(09_3d_walker)
add_lumi_example(10_particles_fountain)
add_lumi_example(11_shadertoy)
add_lumi_example(12_post_fx)

# Aggregate target: `cmake --build . --target examples` (or pick "examples" in the IDE) builds
# every demo at once. Not part of ALL, so a plain build still only builds what you select.
get_property(_lumi_example_targets GLOBAL PROPERTY LUMI_EXAMPLE_TARGETS)
if(_lumi_example_targets)
    add_custom_target(examples)
    add_dependencies(examples ${_lumi_example_targets})
endif()
