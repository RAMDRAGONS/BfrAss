#include "core/Math.hpp"

#include <cmath>

namespace bfrass {

Mat34 Mat34::scale(const Vec3& s) {
    Mat34 r;
    r.m[0][0] = s.x;
    r.m[1][1] = s.y;
    r.m[2][2] = s.z;
    return r;
}

Mat34 Mat34::translation(const Vec3& t) {
    Mat34 r;
    r.setTranslation(t);
    return r;
}

Mat34 Mat34::fromQuaternion(float x, float y, float z, float w) {
    Mat34 r;
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float wx = w * x, wy = w * y, wz = w * z;
    r.m[0] = {1 - 2 * (yy + zz), 2 * (xy - wz), 2 * (xz + wy), 0};
    r.m[1] = {2 * (xy + wz), 1 - 2 * (xx + zz), 2 * (yz - wx), 0};
    r.m[2] = {2 * (xz - wy), 2 * (yz + wx), 1 - 2 * (xx + yy), 0};
    return r;
}

Mat34 Mat34::fromEulerXYZ(float x, float y, float z) {
    const float cx = std::cos(x), sx = std::sin(x);
    const float cy = std::cos(y), sy = std::sin(y);
    const float cz = std::cos(z), sz = std::sin(z);
    Mat34 r;
    r.m[0] = {cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz, 0};
    r.m[1] = {cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz, 0};
    r.m[2] = {-sy, sx * cy, cx * cy, 0};
    return r;
}

Mat34 Mat34::operator*(const Mat34& b) const {
    Mat34 r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            float v = m[i][0] * b.m[0][j] + m[i][1] * b.m[1][j] + m[i][2] * b.m[2][j];
            if (j == 3) {
                v += m[i][3];
            }
            r.m[i][j] = v;
        }
    }
    return r;
}

Vec3 Mat34::transformPoint(const Vec3& p) const {
    return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3],
            m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
            m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3]};
}

Vec3 Mat34::transformVector(const Vec3& v) const {
    return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
}

Mat34 Mat34::inverse() const {
    const double a = m[0][0], b = m[0][1], c = m[0][2];
    const double d = m[1][0], e = m[1][1], f = m[1][2];
    const double g = m[2][0], h = m[2][1], i = m[2][2];
    const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const double det = a * A + b * B + c * C;
    Mat34 r;
    if (std::abs(det) < 1e-20) {
        return r;
    }
    const double inv = 1.0 / det;
    r.m[0][0] = float(A * inv);
    r.m[0][1] = float(-(b * i - c * h) * inv);
    r.m[0][2] = float((b * f - c * e) * inv);
    r.m[1][0] = float(B * inv);
    r.m[1][1] = float((a * i - c * g) * inv);
    r.m[1][2] = float(-(a * f - c * d) * inv);
    r.m[2][0] = float(C * inv);
    r.m[2][1] = float(-(a * h - b * g) * inv);
    r.m[2][2] = float((a * e - b * d) * inv);
    const Vec3 t = r.transformVector(translationPart());
    r.setTranslation({-t.x, -t.y, -t.z});
    return r;
}

Mat34 Mat34::normalMatrix() const {
    Mat34 inv = inverse();
    Mat34 r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r.m[i][j] = inv.m[j][i];
        }
    }
    return r;
}

bool Mat34::isIdentity(float epsilon) const {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            const float expected = (i == j) ? 1.0f : 0.0f;
            if (std::abs(m[i][j] - expected) > epsilon) {
                return false;
            }
        }
    }
    return true;
}

Vec3 normalize(const Vec3& v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 0.0f) {
        return v;
    }
    return {v.x / len, v.y / len, v.z / len};
}

float halfToFloat(uint16_t h) {
    const int sign = (h >> 15) & 1;
    const int exponent = (h >> 10) & 0x1F;
    const int mantissa = h & 0x3FF;
    float value;
    if (exponent == 0) {
        value = std::ldexp(float(mantissa), -24);
    } else if (exponent == 31) {
        value = mantissa ? std::nanf("") : HUGE_VALF;
    } else {
        value = std::ldexp(float(mantissa | 0x400), exponent - 25);
    }
    return sign ? -value : value;
}

} // namespace bfrass
