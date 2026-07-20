// SDL-backend implementation of AssetHandler::_loadShaderFromDisk.
// Uses the precompiled shader blobs + cached metadata via the Shaders subsystem.

#include "assets/assethandler.h"
#include "core/log/log.h"
#include "renderer/shaders.h"
#include "renderer/renderer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

ShaderAsset AssetHandler::_loadShaderFromDisk(const std::string &fileName) {
    GpuShaderStage stage;
    if (SDL_strstr(fileName.c_str(), ".vert")) {
        stage = GpuShaderStage::Vertex;
    } else if (SDL_strstr(fileName.c_str(), ".frag")) {
        stage = GpuShaderStage::Fragment;
    } else {
        LOG_CRITICAL("invalid shader stage");
        return {};
    }

    return Shaders::CreateShaderAsset(fileName, stage);
}
