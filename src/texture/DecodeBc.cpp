#include "texture/BlockDecoders.hpp"

#include <algorithm>
#include <cstring>

namespace bfrass::tex::block {

namespace {

uint16_t rd16(const uint8_t* p) {
    return uint16_t(p[0] | (p[1] << 8));
}

uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

void expand565(uint16_t c, uint8_t* out) {
    const uint32_t r = (c >> 11) & 0x1F;
    const uint32_t g = (c >> 5) & 0x3F;
    const uint32_t b = c & 0x1F;
    out[0] = uint8_t((r << 3) | (r >> 2));
    out[1] = uint8_t((g << 2) | (g >> 4));
    out[2] = uint8_t((b << 3) | (b >> 2));
    out[3] = 255;
}

void decodeColorBlock(const uint8_t* block, uint8_t* rgba, bool allowThreeColor) {
    const uint16_t c0 = rd16(block);
    const uint16_t c1 = rd16(block + 2);
    uint8_t palette[4][4];
    expand565(c0, palette[0]);
    expand565(c1, palette[1]);
    if (c0 > c1 || !allowThreeColor) {
        for (int i = 0; i < 3; ++i) {
            palette[2][i] = uint8_t((2 * palette[0][i] + palette[1][i]) / 3);
            palette[3][i] = uint8_t((palette[0][i] + 2 * palette[1][i]) / 3);
        }
        palette[2][3] = palette[3][3] = 255;
    } else {
        for (int i = 0; i < 3; ++i) {
            palette[2][i] = uint8_t((palette[0][i] + palette[1][i]) / 2);
            palette[3][i] = 0;
        }
        palette[2][3] = 255;
        palette[3][3] = 0;
    }
    const uint32_t indices = rd32(block + 4);
    for (int i = 0; i < 16; ++i) {
        std::memcpy(rgba + i * 4, palette[(indices >> (2 * i)) & 3], 4);
    }
}

void decodeUnsignedAlpha(const uint8_t* block, uint8_t alpha[16]) {
    const uint32_t a0 = block[0];
    const uint32_t a1 = block[1];
    uint8_t palette[8];
    palette[0] = uint8_t(a0);
    palette[1] = uint8_t(a1);
    if (a0 > a1) {
        for (int i = 1; i < 7; ++i) {
            palette[i + 1] = uint8_t(((7 - i) * a0 + i * a1) / 7);
        }
    } else {
        for (int i = 1; i < 5; ++i) {
            palette[i + 1] = uint8_t(((5 - i) * a0 + i * a1) / 5);
        }
        palette[6] = 0;
        palette[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= uint64_t(block[2 + i]) << (8 * i);
    }
    for (int i = 0; i < 16; ++i) {
        alpha[i] = palette[(bits >> (3 * i)) & 7];
    }
}

} // namespace

void decodeBc1(const uint8_t* block, uint8_t* rgba) {
    decodeColorBlock(block, rgba, true);
}

void decodeBc2(const uint8_t* block, uint8_t* rgba) {
    decodeColorBlock(block + 8, rgba, false);
    for (int i = 0; i < 16; ++i) {
        const uint8_t a = (block[i / 2] >> ((i & 1) * 4)) & 0xF;
        rgba[i * 4 + 3] = uint8_t(a * 17);
    }
}

void decodeBc3(const uint8_t* block, uint8_t* rgba) {
    decodeColorBlock(block + 8, rgba, false);
    uint8_t alpha[16];
    decodeUnsignedAlpha(block, alpha);
    for (int i = 0; i < 16; ++i) {
        rgba[i * 4 + 3] = alpha[i];
    }
}

void decodeBc4(const uint8_t* block, float* values, bool isSigned) {
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= uint64_t(block[2 + i]) << (8 * i);
    }
    if (!isSigned) {
        uint8_t alpha[16];
        decodeUnsignedAlpha(block, alpha);
        for (int i = 0; i < 16; ++i) {
            values[i] = alpha[i] / 255.0f;
        }
        return;
    }
    const float r0 = std::max(-127, int(int8_t(block[0]))) / 127.0f;
    const float r1 = std::max(-127, int(int8_t(block[1]))) / 127.0f;
    float palette[8];
    palette[0] = r0;
    palette[1] = r1;
    if (r0 > r1) {
        for (int i = 1; i < 7; ++i) {
            palette[i + 1] = ((7 - i) * r0 + i * r1) / 7.0f;
        }
    } else {
        for (int i = 1; i < 5; ++i) {
            palette[i + 1] = ((5 - i) * r0 + i * r1) / 5.0f;
        }
        palette[6] = -1.0f;
        palette[7] = 1.0f;
    }
    for (int i = 0; i < 16; ++i) {
        values[i] = palette[(bits >> (3 * i)) & 7];
    }
}

} // namespace bfrass::tex::block
