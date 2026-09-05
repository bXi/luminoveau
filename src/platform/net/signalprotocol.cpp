#include "platform/net/signalprotocol.h"

namespace {

constexpr const char *ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int valueOf(char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

} // namespace

namespace SignalProtocol {

std::string base64Encode(const uint8_t *data, size_t size) {
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (size_t i = 0; i < size; i += 3) {
        const size_t remaining = size - i;
        const uint32_t chunk   = ((uint32_t)data[i] << 16) |
                               (remaining > 1 ? (uint32_t)data[i + 1] << 8 : 0) |
                               (remaining > 2 ? (uint32_t)data[i + 2] : 0);
        out += ALPHABET[(chunk >> 18) & 0x3f];
        out += ALPHABET[(chunk >> 12) & 0x3f];
        out += remaining > 1 ? ALPHABET[(chunk >> 6) & 0x3f] : '=';
        out += remaining > 2 ? ALPHABET[chunk & 0x3f] : '=';
    }
    return out;
}

std::vector<uint8_t> base64Decode(const std::string &text) {
    std::vector<uint8_t> out;
    out.reserve(text.size() / 4 * 3);

    uint32_t chunk = 0;
    int      bits  = 0;
    for (char c : text) {
        if (c == '=')
            break;
        const int value = valueOf(c);
        if (value < 0)
            continue; // whitespace and anything else unrecognised
        chunk = (chunk << 6) | (uint32_t)value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((chunk >> bits) & 0xff));
        }
    }
    return out;
}

} // namespace SignalProtocol
