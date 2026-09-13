#include "texture/BlockDecoders.hpp"

#include <algorithm>

namespace bfrass::tex::block {

namespace {

constexpr int kEtcModifiers[8][2] = {{2, 8}, {5, 17}, {9, 29}, {13, 42}, {18, 60}, {24, 80}, {33, 106}, {47, 183}};
constexpr int kEtcDistances[8] = {3, 6, 11, 16, 23, 32, 41, 64};

constexpr int kEacModifiers[16][8] = {
    {-3, -6, -9, -15, 2, 5, 8, 14}, {-3, -7, -10, -13, 2, 6, 9, 12}, {-2, -5, -8, -13, 1, 4, 7, 12},
    {-2, -4, -6, -13, 1, 3, 5, 12}, {-3, -6, -8, -12, 2, 5, 7, 11}, {-3, -7, -9, -11, 2, 6, 8, 10},
    {-4, -7, -8, -11, 3, 6, 7, 10}, {-3, -5, -8, -11, 2, 4, 7, 10}, {-2, -6, -8, -10, 1, 5, 7, 9},
    {-2, -5, -8, -10, 1, 4, 7, 9},  {-2, -4, -8, -10, 1, 3, 7, 9},  {-2, -5, -7, -10, 1, 4, 6, 9},
    {-3, -4, -7, -10, 2, 3, 6, 9},  {-1, -2, -3, -10, 0, 1, 2, 9},  {-4, -6, -8, -9, 3, 5, 7, 8},
    {-3, -5, -7, -9, 2, 4, 6, 8},
};

uint8_t clamp8(int v) {
    return uint8_t(std::clamp(v, 0, 255));
}

uint8_t expand4(int v) {
    return uint8_t((v << 4) | v);
}
uint8_t expand5(int v) {
    return uint8_t((v << 3) | (v >> 2));
}
uint8_t expand6(int v) {
    return uint8_t((v << 2) | (v >> 4));
}
uint8_t expand7(int v) {
    return uint8_t((v << 1) | (v >> 6));
}

int signExtend3(int v) {
    return (v & 4) ? v - 8 : v;
}

// Pixel indices are stored column major: texel (x, y) uses bit x * 4 + y.
unsigned pixelIndex(const uint8_t* b, unsigned x, unsigned y) {
    const unsigned bit = x * 4 + y;
    const unsigned msbWord = (unsigned(b[4]) << 8) | b[5];
    const unsigned lsbWord = (unsigned(b[6]) << 8) | b[7];
    return (((msbWord >> bit) & 1) << 1) | ((lsbWord >> bit) & 1);
}

void writeTexel(uint8_t* rgba, unsigned x, unsigned y, int r, int g, int bl, int a) {
    uint8_t* p = rgba + (y * 4 + x) * 4;
    p[0] = clamp8(r);
    p[1] = clamp8(g);
    p[2] = clamp8(bl);
    p[3] = clamp8(a);
}

void decodePaintMode(const uint8_t* b, uint8_t* rgba, const int paint[4][3], bool punchThrough, bool opaque) {
    for (unsigned y = 0; y < 4; ++y) {
        for (unsigned x = 0; x < 4; ++x) {
            const unsigned idx = pixelIndex(b, x, y);
            if (punchThrough && !opaque && idx == 2) {
                writeTexel(rgba, x, y, 0, 0, 0, 0);
            } else {
                writeTexel(rgba, x, y, paint[idx][0], paint[idx][1], paint[idx][2], 255);
            }
        }
    }
}

} // namespace

void decodeEtc2Rgb(const uint8_t* b, uint8_t* rgba, bool punchThrough) {
    const bool diffBit = (b[3] & 2) != 0;
    const bool flip = (b[3] & 1) != 0;
    const bool opaque = punchThrough ? diffBit : true;
    const bool differential = punchThrough ? true : diffBit;

    int base[2][3];
    if (!differential) {
        base[0][0] = expand4(b[0] >> 4);
        base[1][0] = expand4(b[0] & 0xF);
        base[0][1] = expand4(b[1] >> 4);
        base[1][1] = expand4(b[1] & 0xF);
        base[0][2] = expand4(b[2] >> 4);
        base[1][2] = expand4(b[2] & 0xF);
    } else {
        const int r = b[0] >> 3, dr = signExtend3(b[0] & 7);
        const int g = b[1] >> 3, dg = signExtend3(b[1] & 7);
        const int bb = b[2] >> 3, db = signExtend3(b[2] & 7);

        if (r + dr < 0 || r + dr > 31) {
            // T mode
            const int r1 = (((b[0] >> 3) & 3) << 2) | (b[0] & 3);
            const int g1 = b[1] >> 4, b1 = b[1] & 0xF;
            const int r2 = b[2] >> 4, g2 = b[2] & 0xF, b2 = b[3] >> 4;
            const int d = kEtcDistances[(((b[3] >> 2) & 3) << 1) | (b[3] & 1)];
            const int c1[3] = {expand4(r1), expand4(g1), expand4(b1)};
            const int c2[3] = {expand4(r2), expand4(g2), expand4(b2)};
            int paint[4][3];
            for (int c = 0; c < 3; ++c) {
                paint[0][c] = c1[c];
                paint[1][c] = c2[c] + d;
                paint[2][c] = c2[c];
                paint[3][c] = c2[c] - d;
            }
            decodePaintMode(b, rgba, paint, punchThrough, opaque);
            return;
        }
        if (g + dg < 0 || g + dg > 31) {
            // H mode
            const int r1 = (b[0] >> 3) & 0xF;
            const int g1 = ((b[0] & 7) << 1) | ((b[1] >> 4) & 1);
            const int b1 = (((b[1] >> 3) & 1) << 3) | ((b[1] & 3) << 1) | (b[2] >> 7);
            const int r2 = (b[2] >> 3) & 0xF;
            const int g2 = ((b[2] & 7) << 1) | (b[3] >> 7);
            const int b2 = (b[3] >> 3) & 0xF;
            const int packed1 = (r1 << 8) | (g1 << 4) | b1;
            const int packed2 = (r2 << 8) | (g2 << 4) | b2;
            const int di = (((b[3] >> 2) & 1) << 2) | ((b[3] & 1) << 1) | (packed1 >= packed2 ? 1 : 0);
            const int d = kEtcDistances[di];
            const int c1[3] = {expand4(r1), expand4(g1), expand4(b1)};
            const int c2[3] = {expand4(r2), expand4(g2), expand4(b2)};
            int paint[4][3];
            for (int c = 0; c < 3; ++c) {
                paint[0][c] = c1[c] + d;
                paint[1][c] = c1[c] - d;
                paint[2][c] = c2[c] + d;
                paint[3][c] = c2[c] - d;
            }
            decodePaintMode(b, rgba, paint, punchThrough, opaque);
            return;
        }
        if (bb + db < 0 || bb + db > 31) {
            // Planar mode
            const int ro = (b[0] >> 1) & 0x3F;
            const int go = ((b[0] & 1) << 6) | ((b[1] >> 1) & 0x3F);
            const int bo = ((b[1] & 1) << 5) | (((b[2] >> 3) & 3) << 3) | ((b[2] & 3) << 1) | (b[3] >> 7);
            const int rh = (((b[3] >> 2) & 0x1F) << 1) | (b[3] & 1);
            const int gh = b[4] >> 1;
            const int bh = ((b[4] & 1) << 5) | (b[5] >> 3);
            const int rv = ((b[5] & 7) << 3) | (b[6] >> 5);
            const int gv = ((b[6] & 0x1F) << 2) | (b[7] >> 6);
            const int bv = b[7] & 0x3F;
            const int O[3] = {expand6(ro), expand7(go), expand6(bo)};
            const int H[3] = {expand6(rh), expand7(gh), expand6(bh)};
            const int V[3] = {expand6(rv), expand7(gv), expand6(bv)};
            for (unsigned y = 0; y < 4; ++y) {
                for (unsigned x = 0; x < 4; ++x) {
                    int c[3];
                    for (int k = 0; k < 3; ++k) {
                        c[k] = (int(x) * (H[k] - O[k]) + int(y) * (V[k] - O[k]) + 4 * O[k] + 2) >> 2;
                    }
                    writeTexel(rgba, x, y, c[0], c[1], c[2], 255);
                }
            }
            return;
        }
        base[0][0] = expand5(r);
        base[1][0] = expand5(r + dr);
        base[0][1] = expand5(g);
        base[1][1] = expand5(g + dg);
        base[0][2] = expand5(bb);
        base[1][2] = expand5(bb + db);
    }

    const int table[2] = {b[3] >> 5, (b[3] >> 2) & 7};
    for (unsigned y = 0; y < 4; ++y) {
        for (unsigned x = 0; x < 4; ++x) {
            const unsigned sub = flip ? (y >= 2 ? 1 : 0) : (x >= 2 ? 1 : 0);
            const unsigned idx = pixelIndex(b, x, y);
            int a = kEtcModifiers[table[sub]][0];
            const int bm = kEtcModifiers[table[sub]][1];
            if (punchThrough && !opaque) {
                if (idx == 2) {
                    writeTexel(rgba, x, y, 0, 0, 0, 0);
                    continue;
                }
                a = 0;
            }
            const int modifier = idx == 0 ? a : idx == 1 ? bm : idx == 2 ? -a : -bm;
            writeTexel(rgba, x, y, base[sub][0] + modifier, base[sub][1] + modifier, base[sub][2] + modifier, 255);
        }
    }
}

void decodeEacAlpha(const uint8_t* b, uint8_t* rgba) {
    const int base = b[0];
    const int multiplier = b[1] >> 4;
    const int table = b[1] & 0xF;
    uint64_t bits = 0;
    for (int i = 2; i < 8; ++i) {
        bits = (bits << 8) | b[i];
    }
    for (unsigned x = 0; x < 4; ++x) {
        for (unsigned y = 0; y < 4; ++y) {
            const unsigned shift = 45 - 3 * (x * 4 + y);
            const unsigned idx = unsigned(bits >> shift) & 7;
            rgba[(y * 4 + x) * 4 + 3] = clamp8(base + kEacModifiers[table][idx] * multiplier);
        }
    }
}

void decodeEacR11(const uint8_t* b, float* values, bool isSigned) {
    const int multiplier = b[1] >> 4;
    const int table = b[1] & 0xF;
    uint64_t bits = 0;
    for (int i = 2; i < 8; ++i) {
        bits = (bits << 8) | b[i];
    }
    for (unsigned x = 0; x < 4; ++x) {
        for (unsigned y = 0; y < 4; ++y) {
            const unsigned shift = 45 - 3 * (x * 4 + y);
            const unsigned idx = unsigned(bits >> shift) & 7;
            const int mod = kEacModifiers[table][idx];
            const int scaled = multiplier == 0 ? mod : mod * multiplier * 8;
            float v;
            if (isSigned) {
                const int base = std::max(-127, int(int8_t(b[0])));
                v = std::clamp(base * 8 + scaled, -1023, 1023) / 1023.0f;
            } else {
                v = std::clamp(b[0] * 8 + 4 + scaled, 0, 2047) / 2047.0f;
            }
            values[y * 4 + x] = v;
        }
    }
}

} // namespace bfrass::tex::block
