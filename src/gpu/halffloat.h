#pragma once

// Half-float packing helpers for compact GPU vertex formats.
//
// Vertex data is uploaded as pairs of IEEE-754 binary16 values packed into a uint32,
// which halves vertex bandwidth. Shared by the sprite render passes and the 2D geometry
// builder — previously duplicated in both.

#include <cstdint>
#include <cmath>

/// @brief Branch-light clamp of v into [min, max].
inline float fastClamp(float v, float min, float max) {
    return (v < min) ? min : (v > max ? max : v);
}

/// @brief Branch-light lower bound: returns v, or min if v is smaller.
inline float fastMax(float v, float min) {
    return (v < min) ? min : v;
}

/// @brief Converts a float32 to an IEEE-754 binary16 bit pattern.
/// Handles denormals/underflow (flushed toward zero) and overflow (saturated to infinity).
inline uint16_t floatToHalf(float f) {
    union {
        float    f;
        uint32_t i;
    } u           = { f };
    uint32_t bits = u.i;

    uint32_t sign     = (bits >> 16) & 0x8000;
    int32_t  exponent = ((bits >> 23) & 0xFF) - 127 + 15;
    uint32_t mantissa = (bits >> 13) & 0x3FF;

    // Denormalized numbers and underflow
    if (exponent <= 0) {
        if (exponent < -10) {
            return static_cast<uint16_t>(sign); // too small, flush to zero
        }
        mantissa = (mantissa | 0x400) >> (1 - exponent);
        return static_cast<uint16_t>(sign | mantissa);
    }

    // Overflow to infinity
    if (exponent >= 31) {
        return static_cast<uint16_t>(sign | 0x7C00);
    }

    return static_cast<uint16_t>(sign | (exponent << 10) | mantissa);
}

/// @brief Packs two floats as half-floats into a uint32 (a in the low 16 bits, b in the high).
/// Non-finite inputs become 0 and values are clamped to the half-float range first, so
/// NaN/Inf never propagate into vertex buffers.
inline uint32_t packHalf2(float a, float b) {
    if (!std::isfinite(a))
        a = 0.0f;
    if (!std::isfinite(b))
        b = 0.0f;

    a = fastClamp(a, -65504.0f, 65504.0f);
    b = fastClamp(b, -65504.0f, 65504.0f);

    uint16_t ha = floatToHalf(a);
    uint16_t hb = floatToHalf(b);
    return static_cast<uint32_t>(ha) | (static_cast<uint32_t>(hb) << 16);
}
