// WebGPU-backend stubs for the cross-backend Shaders API. The WGSL pipeline does not
// need an SPIRV cache or SDL_ShaderCross; the browser/Tint handles shader-module compile
// at createShaderModule time, so Init/Quit are no-ops.

#include "renderer/shaders.h"


void Shaders::_init() { }
void Shaders::_quit() { }

const char *Shaders::_getVertexEntryPoint() { return "vs_main"; }
const char *Shaders::_getFragmentEntryPoint() { return "fs_main"; }
const char *Shaders::_getComputeEntryPoint() { return "main"; }

