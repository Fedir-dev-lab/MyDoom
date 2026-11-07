#pragma once

#include <cmath>

struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;

    Vector2() = default;
    Vector2(float x_, float y_) : x(x_), y(y_) {}

    Vector2 operator+(const Vector2& o) const { return {x + o.x, y + o.y}; }
    Vector2 operator-(const Vector2& o) const { return {x - o.x, y - o.y}; }
    Vector2 operator*(float s) const { return {x * s, y * s}; }

    Vector2& operator+=(const Vector2& o) { x += o.x; y += o.y; return *this; }
    Vector2& operator-=(const Vector2& o) { x -= o.x; y -= o.y; return *this; }

    float Length() const { return std::sqrt(x * x + y * y); }
    float LengthSquared() const { return x * x + y * y; }

    float DistanceTo(const Vector2& o) const { return (o - *this).Length(); }

    // Angle of this vector relative to +X axis, in radians.
    float Angle() const { return std::atan2(y, x); }

    Vector2 Normalized() const {
        float len = Length();
        if (len < 1e-6f) return {0.0f, 0.0f};
        return {x / len, y / len};
    }

    static float Dot(const Vector2& a, const Vector2& b) {
        return a.x * b.x + a.y * b.y;
    }
};