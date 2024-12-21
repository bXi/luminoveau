#include <cstdint>
#include <vector>
#include <string>
#include <tuple>

enum class ShaderType {
    Float = 4,
    Int   = 4,
    UInt  = 4,
    Bool  = 1,
    Vec2  = 8, Vec3 = 12, Vec4 = 16,
    IVec2 = 8, IVec3 = 12, IVec4 = 16,
    UVec2 = 8, UVec3 = 12, UVec4 = 16,
    BVec2 = 4, BVec3 = 4, BVec4 = 4,
    Mat2  = 16, Mat3 = 36, Mat4 = 64
};

class UniformBuffer {
public:
    UniformBuffer() : alignment(16), currentOffset(0) {
        buffer.reserve(1024);
    }

    size_t getSizeOfType(ShaderType type) {
        return static_cast<size_t>(type);
    }

    void addVariable(const std::string &name, ShaderType type) {
        size_t typeSize = getSizeOfType(type);

        // Calculate the padding required to align the current offset to the next boundary
        size_t padding = (currentOffset % alignment) ? alignment - (currentOffset % alignment) : 0;


        if (padding + typeSize > alignment) //make sure it still
            currentOffset += padding;

        variables.emplace_back(name, type, currentOffset);

        currentOffset += typeSize;

        if (currentOffset > buffer.capacity()) {
            buffer.resize(buffer.capacity() + 1024);
        }
    }

    void setAlignment(size_t newAlignment) {
        alignment = newAlignment;
    }

    template<typename T>
    void setVariable(const std::string &name, const T &value) {
        for (auto &var: variables) {
            if (std::get<0>(var) == name) {
                size_t offset = std::get<2>(var);

                if constexpr (std::is_same_v<T, float>) {
                    if (std::get<1>(var) == ShaderType::Float) {
                        *reinterpret_cast<float *>(&buffer[offset]) = value;
                        return;
                    }
                } else if constexpr (std::is_same_v<T, glm::vec2>) {
                    if (std::get<1>(var) == ShaderType::Vec2) {
                        std::memcpy(&buffer[offset], &value, sizeof(glm::vec2));
                        return;
                    }
                } else if constexpr (std::is_same_v<T, glm::vec3>) {
                    if (std::get<1>(var) == ShaderType::Vec3) {
                        std::memcpy(&buffer[offset], &value, sizeof(glm::vec3));
                        return;
                    }
                } else if constexpr (std::is_same_v<T, glm::vec4>) {
                    if (std::get<1>(var) == ShaderType::Vec4) {
                        std::memcpy(&buffer[offset], &value, sizeof(glm::vec4));
                        return;
                    }
                } else if constexpr (std::is_same_v<T, glm::mat4x4>) {
                    if (std::get<1>(var) == ShaderType::Mat4) {
                        std::memcpy(&buffer[offset], &value, sizeof(glm::mat4x4));
                        return;
                    }
                } else {
                    throw std::logic_error("type not found.");
                }
            }
        }
    }

    template<typename T>
    T *getVariablePointer(const std::string &name) {

        for (auto &var: variables) {
            if (std::get<0>(var) == name) {
                size_t offset = std::get<2>(var);

                if constexpr (std::is_same_v<T, float>) {
                    if (std::get<1>(var) == ShaderType::Float) {
                        return reinterpret_cast<float *>(&buffer[offset]);
                    }
                } else if constexpr (std::is_same_v<T, float[2]>) {
                    if (std::get<1>(var) == ShaderType::Vec2) {
                        return reinterpret_cast<glm::vec2 *>(&buffer[offset]);
                    }
                } else if constexpr (std::is_same_v<T, float[3]>) {
                    if (std::get<1>(var) == ShaderType::Vec3) {
                        return reinterpret_cast<glm::vec3 *>(&buffer[offset]);
                    }
                } else if constexpr (std::is_same_v<T, float[4]>) {
                    if (std::get<1>(var) == ShaderType::Vec4) {
                        return reinterpret_cast<glm::vec4 *>(&buffer[offset]);
                    }
                }
            }
        }

        return nullptr;
    }

    [[nodiscard]] const void *getBufferPointer() const {
        return buffer.data();
    }

    [[nodiscard]] size_t getBufferSize() const {
        return currentOffset;
    }

private:
    std::vector<std::tuple<std::string, ShaderType, size_t>> variables;

    std::vector<uint8_t> buffer;
    size_t               currentOffset;
    size_t               alignment;
};
