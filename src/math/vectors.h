#pragma once
// This code is mostly based on the excelent video Javidx9 made

/*
	Operator Overloading
	"Yes, ok, the video had a bug..." - javidx9

	License (OLC-3)
	~~~~~~~~~~~~~~~

	Copyright 2018-2019 OneLoneCoder.com

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions
	are met:

	1. Redistributions or derivations of source code must retain the above
	copyright notice, this list of conditions and the following disclaimer.

	2. Redistributions or derivative works in binary form must reproduce
	the above copyright notice. This list of conditions and the following
	disclaimer must be reproduced in the documentation and/or other
	materials provided with the distribution.

	3. Neither the name of the copyright holder nor the names of its
	contributors may be used to endorse or promote products derived
	from this software without specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
	"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
	LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
	A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
	HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
	SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
	LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
	DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
	THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
	(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

	Relevant Video: https://youtu.be/4FyeBUPrwKY

	Links
	~~~~~
	YouTube:	https://www.youtube.com/javidx9
	Discord:	https://discord.gg/WhwHUMV
	Twitter:	https://www.twitter.com/javidx9
	Twitch:		https://www.twitch.tv/javidx9
	GitHub:		https://www.github.com/onelonecoder
	Patreon:	https://www.patreon.com/javidx9
	Homepage:	https://www.onelonecoder.com

	Author
	~~~~~~
	David Barr, aka javidx9, ©OneLoneCoder 2019
*/

#include <cstdint>
#include <cmath>
#include <string>
#include <algorithm>
#include <sstream>
#include <cstring>
#include <cstdarg>
#include <cstdio>

template<class T>
union rect_generic;

#if __has_include("box2d/box2d.h")
#include "box2d/box2d.h"
#endif

#if __has_include("glm/vec2.hpp")
#include "glm/vec2.hpp"
#endif

#if __has_include("imgui.h")
#include "imgui.h"
#endif

[[maybe_unused]] static const char *doTextFormat(const char *text, ...) {
#define MAX_TEXT_BUFFER_LENGTH              1024
#ifndef MAX_TEXTFORMAT_BUFFERS
#define MAX_TEXTFORMAT_BUFFERS 4        // Maximum number of static buffers for text formatting
#endif

    // We create an array of buffers so strings don't expire until MAX_TEXTFORMAT_BUFFERS invocations
    static char buffers[MAX_TEXTFORMAT_BUFFERS][MAX_TEXT_BUFFER_LENGTH] = {{0}};
    static int index = 0;

    char *currentBuffer = buffers[index];
    memset(currentBuffer, 0, MAX_TEXT_BUFFER_LENGTH);   // Clear buffer before using

    va_list args;
    va_start(args, text);
    vsnprintf(currentBuffer, MAX_TEXT_BUFFER_LENGTH, text, args);
    va_end(args);

    index += 1;     // Move to next buffer for next function call
    if (index >= MAX_TEXTFORMAT_BUFFERS) index = 0;

    return currentBuffer;
}

/**
 * @brief A generic 2D vector of component type T, with the usual arithmetic and geometry helpers.
 *
 * Aliased as vi2d/vu2d/vf2d/vd2d for int/uint/float/double. Based on olcPixelGameEngine's v2d
 * (OneLoneCoder, OLC-3 license).
 */
template<class T>
struct v2d_generic {
    T x = 0;  ///< X component.
    T y = 0;  ///< Y component.

    /// @brief Constructs a zero vector.
    v2d_generic() : x(0), y(0) {}

    /// @brief Constructs a vector from x and y components.
    v2d_generic(T _x, T _y) : x(_x), y(_y) {}

    /// @brief Copy constructor.
    v2d_generic(const v2d_generic &v) : x(v.x), y(v.y) {}

    /// @brief Copy assignment.
    v2d_generic &operator=(const v2d_generic &v) = default;

    /// @brief Returns the vector's length (magnitude).
    T mag() const { return T(std::sqrt(x * x + y * y)); }

    /// @brief Returns the squared length (cheaper than mag()).
    T mag2() const { return x * x + y * y; }

    /// @brief Returns a unit-length copy in the same direction (zero vector stays zero).
    v2d_generic norm() const {
        T m = mag();
        if (m == 0) return v2d_generic(0, 0);  // Handle zero vector
        T r = 1 / m;
        return v2d_generic(x * r, y * r);
    }

    /// @brief Returns the perpendicular vector (rotated 90° counter-clockwise).
    v2d_generic perp() const { return v2d_generic(-y, x); }

    /// @brief Returns a copy with both components rounded down.
    v2d_generic floor() const { return v2d_generic(std::floor(x), std::floor(y)); }

    /// @brief Returns a copy with both components rounded up.
    v2d_generic ceil() const { return v2d_generic(std::ceil(x), std::ceil(y)); }

    /// @brief Returns a copy clamped inside the given rectangle.
    v2d_generic clamp(const rect_generic<T> &target) const;

    /// @brief Returns the Euclidean distance to another vector.
    float distanceTo(const v2d_generic other) {
        return sqrtf(
                ((float) this->x - (float) other.x) *
                ((float) this->x - (float) other.x) +
                ((float) this->y - (float) other.y) *
                ((float) this->y - (float) other.y));
    }

    /// @brief Reflects this vector off a surface with the given normal.
    v2d_generic reflectOn(const v2d_generic normal) {
        v2d_generic result;
        float dotProduct = this->dot(normal);
        result.x = this->x - (2.0f * normal.x) * dotProduct;
        result.y = this->y - (2.0f * normal.y) * dotProduct;
        return result;
    }

#if __has_include("box2d/box2d.h")
    /// @brief Converts to a Box2D b2Vec2 (when Box2D is available).
    operator b2Vec2() { return b2Vec2(x, y); }
    /// @brief Constructs from a Box2D b2Vec2 (when Box2D is available).
    v2d_generic(const b2Vec2& v) : x(v.x), y(v.y) {}
#endif

#if __has_include("glm/vec2.hpp")
    /// @brief Converts to a glm::vec2 (when GLM is available).
    operator glm::vec2() { return glm::vec2(x, y); }
    /// @brief Constructs from a glm::vec2 (when GLM is available).
    v2d_generic(const glm::vec2& v) : x(v.x), y(v.y) {}
#endif

#if __has_include("imgui.h")
    /// @brief Converts to an ImGui ImVec2 (when ImGui is available).
    operator ImVec2() { return ImVec2(x, y); }
    /// @brief Constructs from an ImGui ImVec2 (when ImGui is available).
    v2d_generic(const ImVec2& v) : x(v.x), y(v.y) {}
#endif

    /// @brief Returns the angle of the vector in radians (atan2(y, x)).
    T getAngle() const { return atan2(y, x); }

    /// @brief Rotates the vector in place by l radians.
    void rotateBy(float l) {
        const float angle = getAngle();
        const float length = sqrt(x * x + y * y);
        x = cos(l + angle) * length;
        y = sin(l + angle) * length;
    }

    /// @brief Returns the component-wise maximum of this and v.
    v2d_generic max(const v2d_generic &v) const { return v2d_generic(std::max(x, v.x), std::max(y, v.y)); }

    /// @brief Returns the component-wise minimum of this and v.
    v2d_generic min(const v2d_generic &v) const { return v2d_generic(std::min(x, v.x), std::min(y, v.y)); }

    /// @brief Interprets (x=radius, y=angle) as polar and returns the cartesian vector.
    v2d_generic cart() { return {std::cos(y) * x, std::sin(y) * x}; }

    /// @brief Returns this cartesian vector as polar (x=radius, y=angle).
    v2d_generic polar() { return {mag(), std::atan2(y, x)}; }

    /// @brief Returns the dot product with rhs.
    T dot(const v2d_generic &rhs) const { return this->x * rhs.x + this->y * rhs.y; }

    /// @brief Returns the 2D cross product (scalar z-component) with rhs.
    T cross(const v2d_generic &rhs) const { return this->x * rhs.y - this->y * rhs.x; }

    /// @brief Component-wise vector addition.
    v2d_generic operator+(const v2d_generic &rhs) const { return v2d_generic(this->x + rhs.x, this->y + rhs.y); }

    /// @brief Adds a scalar to both components.
    v2d_generic operator+(const T &rhs) const { return v2d_generic(this->x + rhs, this->y + rhs); }

    /// @brief Component-wise vector subtraction.
    v2d_generic operator-(const v2d_generic &rhs) const { return v2d_generic(this->x - rhs.x, this->y - rhs.y); }

    /// @brief Scales both components by a scalar.
    v2d_generic operator*(const T &rhs) const { return v2d_generic(this->x * rhs, this->y * rhs); }

    /// @brief Component-wise vector multiplication.
    v2d_generic operator*(const v2d_generic &rhs) const { return v2d_generic(this->x * rhs.x, this->y * rhs.y); }

    /// @brief Divides both components by a scalar.
    v2d_generic operator/(const T &rhs) const { return v2d_generic(this->x / rhs, this->y / rhs); }

    /// @brief Component-wise vector division.
    v2d_generic operator/(const v2d_generic &rhs) const { return v2d_generic(this->x / rhs.x, this->y / rhs.y); }

    /// @brief Component-wise add-assign.
    v2d_generic &operator+=(const v2d_generic &rhs) {
        this->x += rhs.x;
        this->y += rhs.y;
        return *this;
    }

    /// @brief Component-wise subtract-assign.
    v2d_generic &operator-=(const v2d_generic &rhs) {
        this->x -= rhs.x;
        this->y -= rhs.y;
        return *this;
    }

    /// @brief Scalar multiply-assign.
    v2d_generic &operator*=(const T &rhs) {
        this->x *= rhs;
        this->y *= rhs;
        return *this;
    }

    /// @brief Scalar divide-assign.
    v2d_generic &operator/=(const T &rhs) {
        this->x /= rhs;
        this->y /= rhs;
        return *this;
    }

    /// @brief Component-wise multiply-assign.
    v2d_generic &operator*=(const v2d_generic &rhs) {
        this->x *= rhs.x;
        this->y *= rhs.y;
        return *this;
    }

    /// @brief Component-wise divide-assign.
    v2d_generic &operator/=(const v2d_generic &rhs) {
        this->x /= rhs.x;
        this->y /= rhs.y;
        return *this;
    }

    /// @brief Unary plus (returns a copy).
    v2d_generic operator+() const { return {+x, +y}; }

    /// @brief Unary negation.
    v2d_generic operator-() const { return {-x, -y}; }

    /// @brief Equality comparison.
    bool operator==(const v2d_generic &rhs) const { return (this->x == rhs.x && this->y == rhs.y); }

    /// @brief Inequality comparison.
    bool operator!=(const v2d_generic &rhs) const { return (this->x != rhs.x || this->y != rhs.y); }

    /// @brief Returns a "(x,y)" string with 2-decimal formatting.
    const std::string str() const { return std::string("(") + doTextFormat("%.2f", this->x) + "," + doTextFormat("%.2f", this->y) + ")"; }

    /// @brief Returns str() as a C string (points to a rotating static buffer).
    const char *c_str() const { return str().c_str(); }

    /// @brief Stream-insertion operator, writing str().
    friend std::ostream &operator<<(std::ostream &os, const v2d_generic &rhs) {
        os << rhs.str();
        return os;
    }

    /// @brief Converts to an int32 vector.
    operator v2d_generic<int32_t>() const { return {static_cast<int32_t>(this->x), static_cast<int32_t>(this->y)}; }

    /// @brief Converts to a float vector.
    operator v2d_generic<float>() const { return {static_cast<float>(this->x), static_cast<float>(this->y)}; }

    /// @brief Converts to a double vector.
    operator v2d_generic<double>() const { return {static_cast<double>(this->x), static_cast<double>(this->y)}; }
};

template<class T>
inline v2d_generic<T> operator*(const float &lhs, const v2d_generic<T> &rhs) {
    return v2d_generic<T>((T) (lhs * (float) rhs.x), (T) (lhs * (float) rhs.y));
}

template<class T>
inline v2d_generic<T> operator*(const double &lhs, const v2d_generic<T> &rhs) {
    return v2d_generic<T>((T) (lhs * (double) rhs.x), (T) (lhs * (double) rhs.y));
}

template<class T>
inline v2d_generic<T> operator*(const int &lhs, const v2d_generic<T> &rhs) {
    return v2d_generic<T>((T) (lhs * (int) rhs.x), (T) (lhs * (int) rhs.y));
}

template<class T>
inline v2d_generic<T> operator/(const float &lhs, const v2d_generic<T> &rhs) {
    return v2d_generic<T>((T) (lhs / (float) rhs.x), (T) (lhs / (float) rhs.y));
}

template<class T>
inline v2d_generic<T> operator/(const double &lhs, const v2d_generic<T> &rhs) {
    return v2d_generic<T>((T) (lhs / (double) rhs.x), (T) (lhs / (double) rhs.y));
}

template<class T>
inline v2d_generic<T> operator/(const int &lhs, const v2d_generic<T> &rhs) {
    return v2d_generic<T>((T) (lhs / (int) rhs.x), (T) (lhs / (int) rhs.y));
}

template<class T, class U>
inline bool operator<(const v2d_generic<T> &lhs, const v2d_generic<U> &rhs) {
    return lhs.y < rhs.y || (lhs.y == rhs.y && lhs.x < rhs.x);
}

template<class T, class U>
inline bool operator>(const v2d_generic<T> &lhs, const v2d_generic<U> &rhs) {
    return lhs.y > rhs.y || (lhs.y == rhs.y && lhs.x > rhs.x);
}

typedef v2d_generic<int32_t> vi2d;
typedef v2d_generic<uint32_t> vu2d;
typedef v2d_generic<float> vf2d;
typedef v2d_generic<double> vd2d;

/**
 * @brief A generic 3D vector of component type T, with arithmetic and geometry helpers.
 *
 * Aliased as vi3d/vu3d/vf3d/vd3d for int/uint/float/double.
 */
template<class T>
struct v3d_generic {
    T x = 0;  ///< X component.
    T y = 0;  ///< Y component.
    T z = 0;  ///< Z component.

    /// @brief Constructs a zero vector.
    v3d_generic() : x(0), y(0), z(0) {}
    /// @brief Constructs a vector from x, y and z components.
    v3d_generic(T _x, T _y, T _z) : x(_x), y(_y), z(_z) {}
    /// @brief Copy constructor.
    v3d_generic(const v3d_generic &v) : x(v.x), y(v.y), z(v.z) {}
    /// @brief Copy assignment.
    v3d_generic &operator=(const v3d_generic &v) = default;

    /// @brief Returns the vector's length (magnitude).
    T mag() const { return T(std::sqrt(x * x + y * y + z * z)); }
    /// @brief Returns the squared length (cheaper than mag()).
    T mag2() const { return x * x + y * y + z * z; }

    /// @brief Returns a unit-length copy in the same direction (zero vector stays zero).
    v3d_generic norm() const {
        T m = mag();
        if (m == 0) return v3d_generic(0, 0, 0);  // Handle zero vector
        T r = 1 / m;
        return v3d_generic(x * r, y * r, z * r);
    }

    /// @brief Returns the dot product with rhs.
    T dot(const v3d_generic &rhs) const { return this->x * rhs.x + this->y * rhs.y + this->z * rhs.z; }

    /// @brief Returns the cross product with rhs.
    v3d_generic cross(const v3d_generic &rhs) const {
        return v3d_generic(
            this->y * rhs.z - this->z * rhs.y,
            this->z * rhs.x - this->x * rhs.z,
            this->x * rhs.y - this->y * rhs.x
        );
    }

    /// @brief Component-wise vector addition.
    v3d_generic operator+(const v3d_generic &rhs) const { return v3d_generic(this->x + rhs.x, this->y + rhs.y, this->z + rhs.z); }
    /// @brief Component-wise vector subtraction.
    v3d_generic operator-(const v3d_generic &rhs) const { return v3d_generic(this->x - rhs.x, this->y - rhs.y, this->z - rhs.z); }
    /// @brief Scales all components by a scalar.
    v3d_generic operator*(const T &rhs) const { return v3d_generic(this->x * rhs, this->y * rhs, this->z * rhs); }
    /// @brief Component-wise vector multiplication.
    v3d_generic operator*(const v3d_generic &rhs) const { return v3d_generic(this->x * rhs.x, this->y * rhs.y, this->z * rhs.z); }
    /// @brief Divides all components by a scalar.
    v3d_generic operator/(const T &rhs) const { return v3d_generic(this->x / rhs, this->y / rhs, this->z / rhs); }
    /// @brief Component-wise vector division.
    v3d_generic operator/(const v3d_generic &rhs) const { return v3d_generic(this->x / rhs.x, this->y / rhs.y, this->z / rhs.z); }

    /// @brief Component-wise add-assign.
    v3d_generic &operator+=(const v3d_generic &rhs) {
        this->x += rhs.x; this->y += rhs.y; this->z += rhs.z;
        return *this;
    }
    /// @brief Component-wise subtract-assign.
    v3d_generic &operator-=(const v3d_generic &rhs) {
        this->x -= rhs.x; this->y -= rhs.y; this->z -= rhs.z;
        return *this;
    }
    /// @brief Scalar multiply-assign.
    v3d_generic &operator*=(const T &rhs) {
        this->x *= rhs; this->y *= rhs; this->z *= rhs;
        return *this;
    }
    /// @brief Scalar divide-assign.
    v3d_generic &operator/=(const T &rhs) {
        this->x /= rhs; this->y /= rhs; this->z /= rhs;
        return *this;
    }

    /// @brief Unary plus (returns a copy).
    v3d_generic operator+() const { return {+x, +y, +z}; }
    /// @brief Unary negation.
    v3d_generic operator-() const { return {-x, -y, -z}; }

    /// @brief Equality comparison.
    bool operator==(const v3d_generic &rhs) const { return (this->x == rhs.x && this->y == rhs.y && this->z == rhs.z); }
    /// @brief Inequality comparison.
    bool operator!=(const v3d_generic &rhs) const { return !(*this == rhs); }

    /// @brief Returns a "(x,y,z)" string with 2-decimal formatting.
    const std::string str() const {
        return std::string("(") + doTextFormat("%.2f", this->x) + "," +
               doTextFormat("%.2f", this->y) + "," + doTextFormat("%.2f", this->z) + ")";
    }

    /// @brief Converts to an int32 vector.
    operator v3d_generic<int32_t>() const { return {static_cast<int32_t>(this->x), static_cast<int32_t>(this->y), static_cast<int32_t>(this->z)}; }
    /// @brief Converts to a float vector.
    operator v3d_generic<float>() const { return {static_cast<float>(this->x), static_cast<float>(this->y), static_cast<float>(this->z)}; }
    /// @brief Converts to a double vector.
    operator v3d_generic<double>() const { return {static_cast<double>(this->x), static_cast<double>(this->y), static_cast<double>(this->z)}; }
};

typedef v3d_generic<int32_t> vi3d;
typedef v3d_generic<uint32_t> vu3d;
typedef v3d_generic<float> vf3d;
typedef v3d_generic<double> vd3d;