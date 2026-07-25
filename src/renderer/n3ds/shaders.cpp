// 3DS-backend stubs for the cross-backend Shaders API. PICA shaders are compiled
// offline by picasso and embedded at build time (cmake/N3dsShaders.cmake); there is
// no runtime shader toolchain, so Init/Quit are no-ops. The asset-shader creation
// methods are never referenced on this backend (AssetHandler::_loadShaderFromDisk
// returns an empty asset), mirroring the WebGPU build's linker story.

#include "renderer/shaders.h"

void Shaders::_init() { }
void Shaders::_quit() { }

const char *Shaders::_getVertexEntryPoint() { return "main"; }
const char *Shaders::_getFragmentEntryPoint() { return "main"; }
const char *Shaders::_getComputeEntryPoint() { return "main"; }
