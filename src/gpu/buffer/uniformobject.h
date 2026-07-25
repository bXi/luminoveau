#pragma once

#include <cstdint>
#include <utility>
#include <vector>
#include <string>
#include <tuple>
#include <cstring>
#include <type_traits>

#include "core/log/log.h"

// std-idiom type trait (mirrors std::is_* / std::is_*_v naming); kept lowercase to
// read as the standard-library trait it imitates.
// NOLINTBEGIN(readability-identifier-naming)
template <typename T>
struct is_std_array : std::false_type {
};

template <typename T, std::size_t N>
struct is_std_array<std::array<T, N>> : std::true_type {
};

template <typename T>
inline constexpr bool is_std_array_v = is_std_array<T>::value;
// NOLINTEND(readability-identifier-naming)

class UniformBuffer {
public:
    UniformBuffer()
        : _currentOffset(0)
        , _alignment(16) {
        _buffer.resize(1024, 0);
    }

    class VariableProxy {
    public:
        VariableProxy(UniformBuffer &buffer, const std::string &name)
            : _buffer(buffer)
            , _name(name) { }

        template <typename T>
        VariableProxy &operator=(const T &value) {
            _buffer.SetVariable(_name, value);
            return *this;
        }

    private:
        UniformBuffer &_buffer;
        std::string    _name;
    };

    VariableProxy operator[](const std::string &name) {
        return { *this, name };
    }

    void AddVariable(const std::string &name, size_t typeSize, size_t offset) {
        _variables.emplace_back(name, typeSize, offset);

        size_t requiredCapacity = offset + typeSize;
        if (requiredCapacity > _buffer.capacity()) {
            size_t newCapacity = ((requiredCapacity + 1023) / 1024) * 1024;
            _buffer.resize(newCapacity, 0);
        }

        _currentOffset = std::max(_currentOffset, requiredCapacity);
    }

    void SetAlignment(size_t newAlignment) {
        _alignment = newAlignment;
    }

    template <typename T>
    void SetVariable(const std::string &name, const T &value) {
        if constexpr (std::is_array<T>::value) {
            for (auto &var : _variables) {
                size_t size   = sizeof(std::remove_extent<T>::type);
                size_t offset = std::get<2>(var);
                if (std::get<0>(var) == name) {
                    for (size_t i = 0; i < std::extent<T>::value; ++i) {
                        std::memcpy(&_buffer[offset], &value[i], size);
                        offset += _alignment;
                    }
                }
            }
            return;
        } else if constexpr (is_std_array_v<T>) {
            for (auto &var : _variables) {
                if (std::get<0>(var) == name) {
                    size_t size   = sizeof(typename T::value_type);
                    size_t offset = std::get<2>(var);
                    for (size_t i = 0; i < value.size(); ++i) {
                        std::memcpy(&_buffer[offset], &value[i], size);
                        offset += _alignment;
                    }
                }
            }
            return;
        }

        for (auto &var : _variables) {
            if (std::get<0>(var) == name) {
                size_t size   = std::get<1>(var);
                size_t offset = std::get<2>(var);
                std::memcpy(&_buffer[offset], &value, size);
                return;
            }
        }
    }

    template <typename T>
    T GetVariable(const std::string &name) const {
        for (const auto &var : _variables) {
            if (std::get<0>(var) == name) {
                size_t offset = std::get<2>(var);
                return *reinterpret_cast<const T *>(&_buffer[offset]);
            }
        }
        LOG_CRITICAL("variable not found: {}", name);
    }

    [[nodiscard]] const void *GetBufferPointer() const { return _buffer.data(); }
    [[nodiscard]] size_t      GetBufferSize() const { return _currentOffset; }

private:
    std::vector<std::tuple<std::string, size_t, size_t>> _variables;
    std::vector<uint8_t>                                 _buffer;
    size_t                                               _currentOffset = 0;
    size_t                                               _alignment     = 0;
};
