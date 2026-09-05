#include "platform/net/playerid.h"

#include <cstring>

bool PlayerId::operator==(const PlayerId &other) const {
    return provider == other.provider && length == other.length &&
           std::memcmp(bytes.data(), other.bytes.data(), length) == 0;
}

std::string PlayerId::toString() const {
    static constexpr const char *names[] = { "none", "addr", "steam", "eos", "custom" };
    static constexpr char        hex[]   = "0123456789abcdef";

    std::string out = names[static_cast<uint8_t>(provider)];
    out += ':';
    for (uint8_t i = 0; i < length; ++i) {
        out += hex[bytes[i] >> 4];
        out += hex[bytes[i] & 0x0f];
    }
    return out;
}

PlayerId PlayerId::From(Provider provider, const void *data, uint8_t size) {
    PlayerId id;
    id.provider = provider;
    id.length   = size > id.bytes.size() ? (uint8_t)id.bytes.size() : size;
    if (id.length)
        std::memcpy(id.bytes.data(), data, id.length);
    return id;
}

PlayerId PlayerId::From(Provider provider, uint64_t value) {
    uint8_t raw[8];
    for (int i = 0; i < 8; ++i)
        raw[i] = (uint8_t)(value >> (i * 8));
    return From(provider, raw, sizeof(raw));
}
