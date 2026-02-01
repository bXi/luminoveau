#pragma once

#include <string>
#include <memory>

#include "shader.h"
#include "utils/uniformobject.h"

/**
 * @brief Represents a shader effect with configurable parameters.
 * 
 * An Effect wraps a shader (vertex + fragment) along with a UniformBuffer
 * for shader parameters. Multiple Effect instances can share the same shader
 * but have different parameter values.
 */
struct EffectAsset {
    ShaderAsset vertShader;
    ShaderAsset fragShader;
    
    std::shared_ptr<UniformBuffer> uniforms;
    
    // Default constructor
    EffectAsset() : uniforms(std::make_shared<UniformBuffer>()) {}
    
    // Constructor with shaders
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
    
    // Array subscript operator for setting uniforms
    UniformProxy operator[](const std::string& name) {
        return UniformProxy(uniforms, name);
    }
};

using Effect = EffectAsset&;
