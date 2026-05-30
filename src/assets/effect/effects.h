#pragma once

#include "assets/effect/effect.h"
#include "assets/shader/shader.h"
#include "core/log/log.h"

#ifndef LUMINOVEAU_WEBGPU_BACKEND
#include "renderer/shaders.h"
#endif

class EffectHandler {
public:
    static EffectAsset Create(const ShaderAsset& vertShader, const ShaderAsset& fragShader) {
        return get()._create(vertShader, fragShader);
    }

private:
    EffectAsset _create(const ShaderAsset& vertShader, const ShaderAsset& fragShader) {
        EffectAsset effect(vertShader, fragShader);

#ifndef LUMINOVEAU_WEBGPU_BACKEND
        ShaderMetadata metadata = Shaders::GetShaderMetadata(fragShader.shaderFilename);
        for (const auto& [name, offset] : metadata.uniform_offsets) {
            effect.uniforms->addVariable(name, metadata.uniform_sizes.at(name), offset);
        }
        if (!metadata.uniform_offsets.empty()) {
            LOG_INFO("Effect '{}': Initialized {} uniform variables from shader reflection",
                    fragShader.shaderFilename, metadata.uniform_offsets.size());
        }
#else
        for (const auto& [name, offset] : fragShader.uniformOffsets) {
            effect.uniforms->addVariable(name, fragShader.uniformSizes.at(name), offset);
        }
        if (!fragShader.uniformOffsets.empty()) {
            LOG_INFO("Effect '{}': Initialized {} uniform variables from WebGPU reflection",
                    fragShader.shaderFilename, fragShader.uniformOffsets.size());
        }
#endif

        return effect;
    }

public:
    EffectHandler(const EffectHandler&) = delete;

    static EffectHandler& get() {
        static EffectHandler instance;
        return instance;
    }

private:
    EffectHandler() = default;
};

namespace Effects {
    inline EffectAsset Create(const ShaderAsset& vertShader, const ShaderAsset& fragShader) {
        return EffectHandler::Create(vertShader, fragShader);
    }
}
