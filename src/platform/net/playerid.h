#pragma once

// The one identity the protocol carries. Deliberately opaque: a SteamID, an EOS
// ProductUserId and a plain address all fit here, and none of them leak into a public
// header or onto the wire as a platform type.

#include <array>
#include <cstdint>
#include <string>
#include <type_traits>

/// @brief Opaque cross-platform player identity.
struct PlayerId {
    /// @brief Which brokerage minted this id.
    enum class Provider : uint8_t { None,
        Address,
        Steam,
        Eos,
        Custom };

    Provider                provider = Provider::None;
    uint8_t                 length   = 0; ///< Bytes used in `bytes`.
    std::array<uint8_t, 32> bytes{};      ///< Provider-defined encoding.

    /// @brief Returns true if this id names somebody.
    [[nodiscard]] bool valid() const { return provider != Provider::None && length > 0; }

    bool operator==(const PlayerId &other) const;
    bool operator!=(const PlayerId &other) const { return !(*this == other); }

    /// @brief Renders as "provider:hex", for logs and join codes. Never parsed by the protocol.
    [[nodiscard]] std::string toString() const;

    /// @brief Builds an id from a provider's raw bytes. Anything past 32 bytes is dropped.
    static PlayerId From(Provider provider, const void *data, uint8_t size);
    /// @brief Builds an id from a provider's 64-bit handle, little-endian.
    static PlayerId From(Provider provider, uint64_t value);
};

static_assert(std::is_trivially_copyable_v<PlayerId>, "PlayerId travels inside packets");
