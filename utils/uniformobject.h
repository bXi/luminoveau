#include <cstdint>
#include <utility>
#include <vector>
#include <string>
#include <tuple>

#include <type_traits>

template<typename T>
struct is_std_array : std::false_type {
};

template<typename T, std::size_t N>
struct is_std_array<std::array<T, N>> : std::true_type {
};

template<typename T>
inline constexpr bool is_std_array_v = is_std_array<T>::value;

class UniformBuffer {
public:
    UniformBuffer() : alignment(16), currentOffset(0) {
        buffer.reserve(1024);
    }

    // Proxy class for assignment through []
    class VariableProxy {
    public:
        VariableProxy(UniformBuffer &buffer, const std::string &name)
            : buffer(buffer), name(name) {}

        template<typename T>
        VariableProxy &operator=(const T &value) {
            buffer.setVariable(name, value);
            return *this;
        }

    private:
        UniformBuffer &buffer;
        std::string   name;
    };

    VariableProxy operator[](const std::string &name) {
        return {*this, name};
    }

    void addVariable(const std::string &name, size_t typeSize, size_t offset) {

        variables.emplace_back(name, typeSize, offset);

        size_t requiredCapacity = offset + typeSize;
        if (requiredCapacity > buffer.capacity()) {
            size_t newCapacity = ((requiredCapacity + 1023) / 1024) * 1024; // Round up to the nearest multiple of 1024
            buffer.resize(newCapacity);
        }

        currentOffset = std::max(currentOffset, offset);
    }

    void setAlignment(size_t newAlignment) {
        alignment = newAlignment;
    }

    template<typename T>
    void setVariable(const std::string &name, const T &value) {

        if constexpr (std::is_array<T>::value) {
            for (auto &var: variables) {
                size_t size   = sizeof(std::remove_extent<T>::type);
                size_t offset = std::get<2>(var);

                if (std::get<0>(var) == name) {
                    for (size_t i = 0; i < std::extent<T>::value; ++i) {
                        std::memcpy(&buffer[offset], &value[i], size);
                        offset += alignment;
                    }
                }
            }
            return;
        }
        else if constexpr (is_std_array_v<T>) {
            for (auto &var: variables) {
                if (std::get<0>(var) == name) {
                    size_t size   = sizeof(typename T::value_type);
                    size_t offset = std::get<2>(var);

                    for (size_t i = 0; i < value.size(); ++i) {
                        std::memcpy(&buffer[offset], &value[i], size);
                        offset += alignment;
                    }
                }
            }
            return;
        }

        for (auto &var: variables) {
            if (std::get<0>(var) == name) {
                size_t size   = std::get<1>(var);
                size_t offset = std::get<2>(var);

                std::memcpy(&buffer[offset], &value, size);
                return;
            }
        }
    }

    template<typename T>
    T getVariable(const std::string &name) const {
        for (const auto &var: variables) {
            if (std::get<0>(var) == name) {
                size_t offset = std::get<2>(var);
                return *reinterpret_cast<const T *>(&buffer[offset]);
            }
        }
        throw std::runtime_error("Variable not found: " + name);
    }

    [[nodiscard]] const void *getBufferPointer() const {
        return buffer.data();
    }

    [[nodiscard]] size_t getBufferSize() const {
        return currentOffset;
    }

private:
    std::vector<std::tuple<std::string, size_t, size_t>> variables;

    std::vector<uint8_t> buffer;
    size_t               currentOffset;
    size_t               alignment;
};
