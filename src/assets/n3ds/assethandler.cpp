// 3DS-backend implementation of AssetHandler::_loadShaderFromDisk. The PICA200 has
// no runtime shader compilation (and no fragment shaders at all), so user shaders
// cannot be loaded; callers get an empty asset and the passes that would consume it
// (shader passes, sprite effects) warn and render plain.

#include "assets/assethandler.h"
#include "core/log/log.h"

ShaderAsset AssetHandler::_loadShaderFromDisk(const std::string &fileName) {
    LOG_WARNING("Runtime shaders are not supported on 3DS; '{}' not loaded", fileName.c_str());
    return {};
}
