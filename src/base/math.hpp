#pragma once
// The small math everything above shares: angles, a 2D point and a 4x4 matrix. No dependencies.

#include <cmath>

// === ANGLES === //
constexpr float kPi  = 3.14159265358979f;
constexpr float kTau = 2.0f * kPi;
constexpr float toRadians(float degrees) { return degrees * kPi / 180.0f; }
constexpr float toDegrees(float radians) { return radians * 180.0f / kPi; }

// === VEC2 === //
struct Vec2 {
    float x = 0.0f, y = 0.0f;
    bool operator==(const Vec2&) const = default;
};

// === MAT4 === //
// Column-major 4x4 matrix, laid out like WGSL's mat4x4f: m[column * 4 + row].
// Only what a 2D canvas needs.
struct Mat4 {
    float m[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };

    // Pixel space (origin top-left, y down) -> clip space, for a w x h viewport.
    static Mat4 ortho(float w, float h)
    {
        Mat4 r;
        r.m[0]  =  2.0f / w;
        r.m[5]  = -2.0f / h;
        r.m[12] = -1.0f;
        r.m[13] =  1.0f;
        return r;
    }
    // Scale by (sx, sy), then translate by (tx, ty).
    static Mat4 translateScale(float tx, float ty, float sx, float sy)
    {
        Mat4 r;
        r.m[0]  = sx;
        r.m[5]  = sy;
        r.m[12] = tx;
        r.m[13] = ty;
        return r;
    }
    // Scale by `s`, rotate by `angle` radians (clockwise on a y-down screen), then translate by (tx, ty).
    static Mat4 similarity(float tx, float ty, float s, float angle)
    {
        const float c = std::cos(angle) * s, n = std::sin(angle) * s;
        Mat4 r;
        r.m[0]  = c;   r.m[1] = n;
        r.m[4]  = -n;  r.m[5] = c;
        r.m[12] = tx;
        r.m[13] = ty;
        return r;
    }

    friend Mat4 operator*(const Mat4& a, const Mat4& b)
    {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k)
                    s += a.m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = s;
            }
        return r;
    }
};
