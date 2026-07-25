# N3dsShaders.cmake
# Compiles the hand-written PICA200 vertex shaders (shaders/n3ds/*.v.pica) with picasso
# and embeds the resulting shbins into the luminoveau library as linkable symbols:
#
#   lumi_sprite_shbin[]    / lumi_sprite_shbin_size     (sprite pass: pos/uv/color)
#   lumi_composite_shbin[] / lumi_composite_shbin_size  (framebuffer->screen blit quad)
#
# ctr_add_shader_library / dkp_add_embedded_binary_library ship with devkitPro's CMake
# support ($DEVKITPRO/cmake) and are available once the 3DS.cmake toolchain is active.

if(NOT NINTENDO_3DS)
    message(FATAL_ERROR "N3dsShaders.cmake is 3DS-only; include it under LUMINOVEAU_N3DS_BACKEND")
endif()

ctr_add_shader_library(lumi_sprite    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/n3ds/sprite.v.pica")
ctr_add_shader_library(lumi_composite "${CMAKE_CURRENT_SOURCE_DIR}/shaders/n3ds/composite.v.pica")

# Symbol names derive from the embedded file names:
# lumi_sprite.shbin -> lumi_sprite_shbin[] / lumi_sprite_shbin_size.
dkp_add_embedded_binary_library(lumi_n3ds_shaders
    lumi_sprite
    lumi_composite)

target_link_libraries(luminoveau PRIVATE lumi_n3ds_shaders)
