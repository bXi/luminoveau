#pragma once

#include <string>
#include <memory>

#include "../shader/shader.h"
#include "gpu/buffer/uniformobject.h"

/**
 * @brief Represents a shader effect with configurable parameters.
 * 
 * An Effect wraps a shader (vertex + fragment) along with a UniformBuffer
 * for shader parameters. Multiple Effect instances can share the same shader
 * but have different parameter values.
 */
struct EffectAsset {
    ShaderAsset vertShader;   ///< Vertex shader used by the effect.
    ShaderAsset fragShader;   ///< Fragment shader used by the effect.

    std::shared_ptr<UniformBuffer> uniforms;  ///< Shared uniform parameter buffer.

    /// @brief Constructs an effect with an empty uniform buffer and no shaders.
    EffectAsset() : uniforms(std::make_shared<UniformBuffer>()) {}

    /// @brief Constructs an effect from vertex and fragment shaders.
    /// @param vert Vertex shader.
    /// @param frag Fragment shader.
    EffectAsset(const ShaderAsset& vert, const ShaderAsset& frag)
        : vertShader(vert), fragShader(frag), uniforms(std::make_shared<UniformBuffer>()) {}

    // Proxy class for assignment through []
    class UniformProxy {
    public:
        UniformProxy(std::shared_ptr<UniformBuffer> buffer, const std::string& name)
            : buffer(buffer), name(name) {}
        
        template<typename T>
        UniformProxy& operator=(const T& value) {
            buffer->setVariable(name, value);
            return *this;
        }
        
    private:
        std::shared_ptr<UniformBuffer> buffer;
        std::string name;
    };
    
    /// @brief Accesses a named uniform for assignment, e.g. `effect["strength"] = 0.5f`.
    /// @param name The uniform variable name.
    /// @return A proxy that writes the assigned value into the uniform buffer.
    UniformProxy operator[](const std::string& name) {
        return UniformProxy(uniforms, name);
    }
};

using Effect = EffectAsset&;
