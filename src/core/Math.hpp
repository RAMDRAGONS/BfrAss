#pragma once

#include <array>
#include <cstdint>

namespace bfrass {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

// Affine transform stored as three rows of four; points are transformed as
// column vectors (p' = M * p) with the translation in the last column.
struct Mat34 {
    std::array<std::array<float, 4>, 3> m{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}};

    static Mat34 identity() { return {}; }
    static Mat34 scale(const Vec3& s);
    static Mat34 translation(const Vec3& t);
    static Mat34 fromQuaternion(float x, float y, float z, float w);
    // Rotation order used by NintendoWare EulerXYZ bones: X first, then Y, then Z.
    static Mat34 fromEulerXYZ(float x, float y, float z);

    Mat34 operator*(const Mat34& rhs) const;
    Vec3 transformPoint(const Vec3& p) const;
    Vec3 transformVector(const Vec3& v) const;
    Mat34 inverse() const;
    // Inverse-transpose of the linear part, used to carry normals through non-uniform scale.
    Mat34 normalMatrix() const;
    bool isIdentity(float epsilon = 1e-6f) const;

    Vec3 translationPart() const { return {m[0][3], m[1][3], m[2][3]}; }
    void setTranslation(const Vec3& t) {
        m[0][3] = t.x;
        m[1][3] = t.y;
        m[2][3] = t.z;
    }
};

Vec3 normalize(const Vec3& v);

float halfToFloat(uint16_t h);

} // namespace bfrass
