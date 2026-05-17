#pragma once

#include "constants.h"

/**
 * @brief Convert degrees to radians.
 * 
 * @param degrees Angle in degrees
 * @return Angle in radians
 * 
 * @example
 * Draw::RotatedTexture(texture, pos, size, deg(90));  // 90 degrees = PI/2 radians
 */
constexpr float deg(float degrees) {
    return degrees * PI / 180.0f;
}

/**
 * @brief Identity function for radians (for explicit clarity).
 * 
 * @param radians Angle in radians
 * @return Same angle in radians
 * 
 * @example
 * Draw::RotatedTexture(texture, pos, size, rad(M_PI_2));  // Explicit radians
 */
constexpr float rad(float radians) {
    return radians;
}
