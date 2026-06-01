// WebGPU-backend stubs for the cross-backend Shaders API. The WGSL pipeline does not
// need an SPIRV cache or SDL_ShaderCross; the browser/Tint handles shader-module compile
// at createShaderModule time, so Init/Quit are no-ops.

#include "renderer/shaders.h"

namespace Shaders {

void Init() {}
void Quit() {}

const char* GetVertexEntryPoint()   { return "vs_main"; }
const char* GetFragmentEntryPoint() { return "fs_main"; }
const char* GetComputeEntryPoint()  { return "main"; }

}
