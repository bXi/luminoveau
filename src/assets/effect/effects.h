#pragma once

#include "assets/effect/effect.h"
#include "assets/shader/shader.h"
#include "core/log/log.h"

class EffectHandler {
public:
    /// @brief Creates an effect from a vertex + fragment shader, wiring up its uniforms
    ///        from shader reflection.
    /// @param vertShader Vertex shader.
    /// @param fragShader Fragment shader (its reflected uniforms populate the effect).
    /// @return The created effect asset.
    static EffectAsset Create(const ShaderAsset& vertShader, const ShaderAsset& fragShader) {
        return get()._create(vertShader, fragShader);
    }

private:
    EffectAsset _create(const ShaderAsset& vertShader, const ShaderAsset& fragShader) {
        EffectAsset effect(vertShader, fragShader);

        // Uniform layout is reflected at shader-load time on both backends and
        // stored on the ShaderAsset itself.
        for (const auto& [name, offset] : fragShader.uniformOffsets) {
            effect.uniforms->addVariable(name, fragShader.uniformSizes.at(name), offset);
        }
        if (!fragShader.uniformOffsets.empty()) {
            LOG_INFO("Effect '{}': Initialized {} uniform variables from shader reflection",
                    fragShader.shaderFilename, fragShader.uniformOffsets.size());
        }

        return effect;
    }

public:
    /// @cond INTERNAL
    EffectHandler(const EffectHandler&) = delete;

    static EffectHandler& get() {
        static EffectHandler instance;
        return instance;
    }
    /// @endcond

private:
    EffectHandler() = default;
};

namespace Effects {
    /// @brief Creates an effect from a vertex + fragment shader (convenience wrapper over EffectHandler::Create).
    /// @param vertShader Vertex shader.
    /// @param fragShader Fragment shader (its reflected uniforms populate the effect).
    /// @return The created effect asset.
    inline EffectAsset Create(const ShaderAsset& vertShader, const ShaderAsset& fragShader) {
        return EffectHandler::Create(vertShader, fragShader);
    }
}
