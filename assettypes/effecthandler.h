#pragma once

#include "assettypes/effect.h"
#include "assettypes/shader.h"
#include "renderer/shaderhandler.h"
#include "log/loghandler.h"

/**
 * @brief Provides functionality for creating and managing shader effects.
 */
class EffectHandler {
public:
    /**
     * @brief Creates a new effect instance from vertex and fragment shaders.
     * 
     * Each call creates a new Effect with its own parameter buffer, allowing
     * multiple instances of the same shader with different parameters.
     * The uniform buffer is automatically populated with the correct layout
     * from shader reflection data.
     * 
     * @param vertShader Vertex shader asset
     * @param fragShader Fragment shader asset
     * @return New effect instance
     */
    static EffectAsset Create(const ShaderAsset& vertShader, const ShaderAsset& fragShader) {
        return get()._create(vertShader, fragShader);
    }

private:
    EffectAsset _create(const ShaderAsset& vertShader, const ShaderAsset& fragShader) {
        EffectAsset effect(vertShader, fragShader);
        
        // Get fragment shader metadata to set up uniform buffer layout
        ShaderMetadata metadata = Shaders::GetShaderMetadata(fragShader.shaderFilename);
        
        // Pre-populate uniform buffer with variables at correct offsets
        for (const auto& [name, offset] : metadata.uniform_offsets) {
            size_t size = metadata.uniform_sizes.at(name);
            effect.uniforms->addVariable(name, size, offset);
        }
        
        if (!metadata.uniform_offsets.empty()) {
            LOG_INFO("Effect '{}': Initialized {} uniform variables from shader reflection",
                    fragShader.shaderFilename, metadata.uniform_offsets.size());
        }
        
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

// Namespace alias for cleaner API
namespace Effects {
    inline EffectAsset Create(const ShaderAsset& vertShader, const ShaderAsset& fragShader) {
        return EffectHandler::Create(vertShader, fragShader);
    }
}
