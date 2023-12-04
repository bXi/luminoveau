#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <algorithm>
#include <sstream>
#include <cstring>
#include <cstdarg>
#include <cstdio>

#include "rectangles.h"

#if __has_include("box2d/box2d.h")
#include "box2d/box2d.h"
#endif


[[maybe_unused]] static const char* doTextFormat(const char *text, ...) {
    #define MAX_TEXT_BUFFER_LENGTH              1024
    #ifndef MAX_TEXTFORMAT_BUFFERS
    #define MAX_TEXTFORMAT_BUFFERS 4        // Maximum number of static buffers for text formatting
#endif

    // We create an array of buffers so strings don't expire until MAX_TEXTFORMAT_BUFFERS invocations
    static char buffers[MAX_TEXTFORMAT_BUFFERS][MAX_TEXT_BUFFER_LENGTH] = { {0} };
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
template <class T>
struct v2d_generic
{
	T x = 0;
	T y = 0;
	v2d_generic() : x(0), y(0) {}
	v2d_generic(T _x, T _y) : x(_x), y(_y) {}
	v2d_generic(const v2d_generic& v) : x(v.x), y(v.y) {}
	v2d_generic& operator=(const v2d_generic& v) = default;
	T mag() const { return T(std::sqrt(x * x + y * y)); }
	T mag2() const { return x * x + y * y; }
	v2d_generic  norm() const { T r = 1 / mag(); return v2d_generic(x * r, y * r); }
	v2d_generic  perp() const { return v2d_generic(-y, x); }
	v2d_generic  floor() const { return v2d_generic(std::floor(x), std::floor(y)); }
	v2d_generic  ceil() const { return v2d_generic(std::ceil(x), std::ceil(y)); }
	v2d_generic  clamp(Rectangle target) const { return v2d_generic(std::clamp(x, target.x, target.x + target.width), std::clamp(y, target.y, target.y + target.height)); }
    float  distanceTo(const v2d_generic other) { return sqrtf(
            ((float)this->x - (float)other.x) *
            ((float)this->x - (float)other.x) +
            ((float)this->y - (float)other.y) *
            ((float)this->y - (float)other.y)); }
    v2d_generic reflectOn(const v2d_generic normal) {
            v2d_generic result;
            float dotProduct = this->dot(normal);
            result.x = this->x - (2.0f*normal.x)*dotProduct;
            result.y = this->y - (2.0f*normal.y)*dotProduct;
            return result;
    }
    //	operator Vector2() { return Vector2(static_cast<float>(x), static_cast<float>(y)); }
//	v2d_generic(const Vector2& v) : x(static_cast<T>(v.x)), y(static_cast<T>(v.y)) {}

#if __has_include("box2d/box2d.h")
	operator b2Vec2() { return b2Vec2(x, y); }
	v2d_generic(const b2Vec2& v) : x(v.x), y(v.y) {}
#endif

	T  getAngle() const { return atan2(y, x); }
	void rotateBy(float l) {
		const float angle = getAngle();
		const float length = sqrt(x * x + y * y);
		x = cos(l + angle) * length;
		y = sin(l + angle) * length;
	}
	v2d_generic  max(const v2d_generic& v) const { return v2d_generic(std::max(x, v.x), std::max(y, v.y)); }
	v2d_generic  min(const v2d_generic& v) const { return v2d_generic(std::min(x, v.x), std::min(y, v.y)); }
	v2d_generic  cart() { return { std::cos(y) * x, std::sin(y) * x }; }
	v2d_generic  polar() { return { mag(), std::atan2(y, x) }; }
	T dot(const v2d_generic& rhs) const { return this->x * rhs.x + this->y * rhs.y; }
	T cross(const v2d_generic& rhs) const { return this->x * rhs.y - this->y * rhs.x; }
	v2d_generic  operator +  (const v2d_generic& rhs) const { return v2d_generic(this->x + rhs.x, this->y + rhs.y); }
	v2d_generic  operator +  (const T& rhs)			  const { return v2d_generic(this->x + rhs, this->y + rhs); } 
	v2d_generic  operator -  (const v2d_generic& rhs) const { return v2d_generic(this->x - rhs.x, this->y - rhs.y); }
	v2d_generic  operator *  (const T& rhs)           const { return v2d_generic(this->x * rhs, this->y * rhs); }
	v2d_generic  operator *  (const v2d_generic& rhs) const { return v2d_generic(this->x * rhs.x, this->y * rhs.y); }
	v2d_generic  operator /  (const T& rhs)           const { return v2d_generic(this->x / rhs, this->y / rhs); }
	v2d_generic  operator /  (const v2d_generic& rhs) const { return v2d_generic(this->x / rhs.x, this->y / rhs.y); }
	v2d_generic& operator += (const v2d_generic& rhs) { this->x += rhs.x; this->y += rhs.y; return *this; }
	v2d_generic& operator -= (const v2d_generic& rhs) { this->x -= rhs.x; this->y -= rhs.y; return *this; }
	v2d_generic& operator *= (const T& rhs) { this->x *= rhs; this->y *= rhs; return *this; }
	v2d_generic& operator /= (const T& rhs) { this->x /= rhs; this->y /= rhs; return *this; }
	v2d_generic& operator *= (const v2d_generic& rhs) { this->x *= rhs.x; this->y *= rhs.y; return *this; }
	v2d_generic& operator /= (const v2d_generic& rhs) { this->x /= rhs.x; this->y /= rhs.y; return *this; }
	v2d_generic  operator +  () const { return { +x, +y }; }
	v2d_generic  operator -  () const { return { -x, -y }; }
	bool operator == (const v2d_generic& rhs) const { return (this->x == rhs.x && this->y == rhs.y); }
	bool operator != (const v2d_generic& rhs) const { return (this->x != rhs.x || this->y != rhs.y); }
    const std::string str() const { return std::string("(") + doTextFormat("%.2f", this->x) + "," + doTextFormat("%.2f", this->y) + ")"; }
    const char* c_str() const { return str().c_str(); }
    friend std::ostream& operator << (std::ostream& os, const v2d_generic& rhs) { os << rhs.str(); return os; }
	operator v2d_generic<int32_t>() const { return { static_cast<int32_t>(this->x), static_cast<int32_t>(this->y) }; }
	operator v2d_generic<float>() const { return { static_cast<float>(this->x), static_cast<float>(this->y) }; }
	operator v2d_generic<double>() const { return { static_cast<double>(this->x), static_cast<double>(this->y) }; }
};

template<class T> inline v2d_generic<T> operator * (const float& lhs, const v2d_generic<T>& rhs)
{
	return v2d_generic<T>((T)(lhs * (float)rhs.x), (T)(lhs * (float)rhs.y));
}
template<class T> inline v2d_generic<T> operator * (const double& lhs, const v2d_generic<T>& rhs)
{
	return v2d_generic<T>((T)(lhs * (double)rhs.x), (T)(lhs * (double)rhs.y));
}
template<class T> inline v2d_generic<T> operator * (const int& lhs, const v2d_generic<T>& rhs)
{
	return v2d_generic<T>((T)(lhs * (int)rhs.x), (T)(lhs * (int)rhs.y));
}
template<class T> inline v2d_generic<T> operator / (const float& lhs, const v2d_generic<T>& rhs)
{
	return v2d_generic<T>((T)(lhs / (float)rhs.x), (T)(lhs / (float)rhs.y));
}
template<class T> inline v2d_generic<T> operator / (const double& lhs, const v2d_generic<T>& rhs)
{
	return v2d_generic<T>((T)(lhs / (double)rhs.x), (T)(lhs / (double)rhs.y));
}
template<class T> inline v2d_generic<T> operator / (const int& lhs, const v2d_generic<T>& rhs)
{
	return v2d_generic<T>((T)(lhs / (int)rhs.x), (T)(lhs / (int)rhs.y));
}

template<class T, class U> inline bool operator < (const v2d_generic<T>& lhs, const v2d_generic<U>& rhs)
{
	return lhs.y < rhs.y || (lhs.y == rhs.y && lhs.x < rhs.x);
}
template<class T, class U> inline bool operator > (const v2d_generic<T>& lhs, const v2d_generic<U>& rhs)
{
	return lhs.y > rhs.y || (lhs.y == rhs.y && lhs.x > rhs.x);
}

typedef v2d_generic<int32_t> vi2d;
typedef v2d_generic<uint32_t> vu2d;
typedef v2d_generic<float> vf2d;
typedef v2d_generic<double> vd2d;