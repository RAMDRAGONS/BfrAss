#include "texture/BlockDecoders.hpp"

#include <algorithm>
#include <array>
#include <cstring>

// ASTC LDR decoder following the Khronos ASTC specification (block mode,
// integer sequence encoding, endpoint and weight unquantisation, infill).

namespace bfrass::tex::block {

namespace {

constexpr unsigned kMaxWeights = 64;
constexpr unsigned kMaxTexels = 144;

struct IseLevel {
    uint16_t levels;
    uint8_t trits;
    uint8_t quints;
    uint8_t bits;
};

constexpr IseLevel kLevels[] = {
    {2, 0, 0, 1},   {3, 1, 0, 0},   {4, 0, 0, 2},   {5, 0, 1, 0},   {6, 1, 0, 1},   {8, 0, 0, 3},
    {10, 0, 1, 1},  {12, 1, 0, 2},  {16, 0, 0, 4},  {20, 0, 1, 2},  {24, 1, 0, 3},  {32, 0, 0, 5},
    {40, 0, 1, 3},  {48, 1, 0, 4},  {64, 0, 0, 6},  {80, 0, 1, 4},  {96, 1, 0, 5},  {128, 0, 0, 7},
    {160, 0, 1, 5}, {192, 1, 0, 6}, {256, 0, 0, 8},
};
constexpr unsigned kLevelCount = sizeof(kLevels) / sizeof(kLevels[0]);

unsigned iseBitCount(unsigned count, const IseLevel& level) {
    if (level.trits) {
        return count * level.bits + (8 * count + 4) / 5;
    }
    if (level.quints) {
        return count * level.bits + (7 * count + 2) / 3;
    }
    return count * level.bits;
}

class Bits128 {
public:
    explicit Bits128(const uint8_t* block) { std::memcpy(bytes_.data(), block, 16); }

    unsigned get(unsigned position, unsigned count) const {
        unsigned v = 0;
        for (unsigned i = 0; i < count; ++i) {
            const unsigned bit = position + i;
            if (bit < 128) {
                v |= unsigned((bytes_[bit >> 3] >> (bit & 7)) & 1) << i;
            }
        }
        return v;
    }

    Bits128 reversed() const {
        Bits128 r = *this;
        for (unsigned i = 0; i < 128; ++i) {
            const unsigned src = 127 - i;
            const uint8_t bit = (bytes_[src >> 3] >> (src & 7)) & 1;
            if (bit) {
                r.bytes_[i >> 3] |= uint8_t(1u << (i & 7));
            } else {
                r.bytes_[i >> 3] &= uint8_t(~(1u << (i & 7)));
            }
        }
        return r;
    }

private:
    std::array<uint8_t, 16> bytes_{};
};

struct IseValue {
    unsigned bits;  // low bits
    unsigned value; // trit or quint digit
};

void decodeTritBlock(unsigned T, unsigned out[5]) {
    auto bit = [&](unsigned i) { return (T >> i) & 1; };
    unsigned C;
    unsigned t0, t1, t2, t3, t4;
    if (((T >> 2) & 7) == 7) {
        C = (((T >> 5) & 7) << 2) | (T & 3);
        t4 = 2;
        t3 = 2;
    } else {
        C = T & 0x1F;
        if (((T >> 5) & 3) == 3) {
            t4 = 2;
            t3 = bit(7);
        } else {
            t4 = bit(7);
            t3 = (T >> 5) & 3;
        }
    }
    auto cbit = [&](unsigned i) { return (C >> i) & 1; };
    if ((C & 3) == 3) {
        t2 = 2;
        t1 = cbit(4);
        t0 = (cbit(3) << 1) | (cbit(2) & ~cbit(3) & 1);
    } else if (((C >> 2) & 3) == 3) {
        t2 = 2;
        t1 = 2;
        t0 = C & 3;
    } else {
        t2 = cbit(4);
        t1 = (C >> 2) & 3;
        t0 = (cbit(1) << 1) | (cbit(0) & ~cbit(1) & 1);
    }
    out[0] = t0;
    out[1] = t1;
    out[2] = t2;
    out[3] = t3;
    out[4] = t4;
}

void decodeQuintBlock(unsigned Q, unsigned out[3]) {
    auto bit = [&](unsigned i) { return (Q >> i) & 1; };
    unsigned q0, q1, q2;
    if (((Q >> 1) & 3) == 3 && ((Q >> 5) & 3) == 0) {
        q2 = (bit(0) << 2) | ((bit(4) & ~bit(0) & 1) << 1) | (bit(3) & ~bit(0) & 1);
        q1 = 4;
        q0 = 4;
    } else {
        unsigned C;
        if (((Q >> 1) & 3) == 3) {
            q2 = 4;
            C = (((Q >> 3) & 3) << 3) | ((~(Q >> 5) & 3) << 1) | bit(0);
        } else {
            q2 = (Q >> 5) & 3;
            C = Q & 0x1F;
        }
        if ((C & 7) == 5) {
            q1 = 4;
            q0 = (C >> 3) & 3;
        } else {
            q1 = (C >> 3) & 3;
            q0 = C & 7;
        }
    }
    out[0] = q0;
    out[1] = q1;
    out[2] = q2;
}

void decodeIse(const Bits128& source, unsigned start, unsigned count, const IseLevel& level, IseValue* out) {
    unsigned pos = start;
    unsigned produced = 0;
    const unsigned b = level.bits;
    // A trailing partial trit/quint group only stores the bits it needs; the
    // missing high bits of the packed digit block are defined as zero.
    const unsigned end = start + iseBitCount(count, level);
    struct {
        const Bits128& bits;
        unsigned end;
        unsigned get(unsigned position, unsigned n) const {
            if (position >= end) {
                return 0;
            }
            return bits.get(position, std::min(n, end - position));
        }
    } bits{source, end};
    if (level.trits) {
        while (produced < count) {
            unsigned m[5] = {};
            unsigned T = 0;
            m[0] = bits.get(pos, b); pos += b;
            T |= bits.get(pos, 2); pos += 2;
            m[1] = bits.get(pos, b); pos += b;
            T |= bits.get(pos, 2) << 2; pos += 2;
            m[2] = bits.get(pos, b); pos += b;
            T |= bits.get(pos, 1) << 4; pos += 1;
            m[3] = bits.get(pos, b); pos += b;
            T |= bits.get(pos, 2) << 5; pos += 2;
            m[4] = bits.get(pos, b); pos += b;
            T |= bits.get(pos, 1) << 7; pos += 1;
            unsigned t[5];
            decodeTritBlock(T, t);
            for (unsigned i = 0; i < 5 && produced < count; ++i, ++produced) {
                out[produced] = {m[i], t[i]};
            }
        }
    } else if (level.quints) {
        while (produced < count) {
            unsigned m[3] = {};
            unsigned Q = 0;
            m[0] = bits.get(pos, b); pos += b;
            Q |= bits.get(pos, 3); pos += 3;
            m[1] = bits.get(pos, b); pos += b;
            Q |= bits.get(pos, 2) << 3; pos += 2;
            m[2] = bits.get(pos, b); pos += b;
            Q |= bits.get(pos, 2) << 5; pos += 2;
            unsigned q[3];
            decodeQuintBlock(Q, q);
            for (unsigned i = 0; i < 3 && produced < count; ++i, ++produced) {
                out[produced] = {m[i], q[i]};
            }
        }
    } else {
        for (; produced < count; ++produced) {
            out[produced] = {bits.get(pos, b), 0};
            pos += b;
        }
    }
}

unsigned replicate(unsigned value, unsigned fromBits, unsigned toBits) {
    if (fromBits == 0) {
        return 0;
    }
    unsigned result = 0;
    int shift = int(toBits) - int(fromBits);
    while (shift > -int(fromBits)) {
        result |= shift >= 0 ? (value << shift) : (value >> -shift);
        shift -= int(fromBits);
    }
    return result & ((1u << toBits) - 1);
}

unsigned unquantizeColor(const IseValue& v, const IseLevel& level) {
    const unsigned b = level.bits;
    if (!level.trits && !level.quints) {
        return replicate(v.bits, b, 8);
    }
    if (b == 0) {
        const unsigned levels = level.levels;
        return (v.value * 255 + (levels - 1) / 2) / (levels - 1);
    }
    const unsigned A = (v.bits & 1) ? 0x1FF : 0;
    const unsigned bb = (v.bits >> 1) & 1, c = (v.bits >> 2) & 1, d = (v.bits >> 3) & 1, e = (v.bits >> 4) & 1,
                   f = (v.bits >> 5) & 1;
    unsigned B = 0, C = 0;
    if (level.trits) {
        switch (b) {
        case 1: C = 204; B = 0; break;
        case 2: C = 93; B = bb * 0x116; break;
        case 3: C = 44; B = c * 0x10A + bb * 0x085; break;
        case 4: C = 22; B = d * 0x104 + c * 0x082 + bb * 0x041; break;
        case 5: C = 11; B = e * 0x102 + d * 0x081 + c * 0x040 + bb * 0x020; break;
        case 6: C = 5; B = f * 0x101 + e * 0x080 + d * 0x040 + c * 0x020 + bb * 0x010; break;
        default: break;
        }
    } else {
        switch (b) {
        case 1: C = 113; B = 0; break;
        case 2: C = 54; B = bb * 0x10C; break;
        case 3: C = 26; B = c * 0x105 + bb * 0x082; break;
        case 4: C = 13; B = d * 0x102 + c * 0x081 + bb * 0x040; break;
        case 5: C = 6; B = e * 0x101 + d * 0x080 + c * 0x040 + bb * 0x020; break;
        default: break;
        }
    }
    unsigned T = v.value * C + B;
    T ^= A;
    return (A & 0x80) | (T >> 2);
}

unsigned unquantizeWeight(const IseValue& v, const IseLevel& level) {
    const unsigned b = level.bits;
    unsigned w;
    if (!level.trits && !level.quints) {
        w = replicate(v.bits, b, 6);
    } else if (b == 0) {
        if (level.trits) {
            static constexpr unsigned kTable[3] = {0, 32, 63};
            w = kTable[v.value];
        } else {
            static constexpr unsigned kTable[5] = {0, 16, 32, 47, 63};
            w = kTable[v.value];
        }
    } else {
        const unsigned A = (v.bits & 1) ? 0x7F : 0;
        const unsigned bb = (v.bits >> 1) & 1, c = (v.bits >> 2) & 1;
        unsigned B = 0, C = 0;
        if (level.trits) {
            switch (b) {
            case 1: C = 50; B = 0; break;
            case 2: C = 23; B = bb * 0x45; break;
            case 3: C = 11; B = c * 0x42 + bb * 0x21; break;
            default: break;
            }
        } else {
            switch (b) {
            case 1: C = 28; B = 0; break;
            case 2: C = 13; B = bb * 0x42; break;
            default: break;
            }
        }
        unsigned T = v.value * C + B;
        T ^= A;
        w = (A & 0x20) | (T >> 2);
    }
    if (w > 32) {
        ++w;
    }
    return w;
}

uint32_t hash52(uint32_t p) {
    p ^= p >> 15;
    p *= 0xEEDE0891u;
    p ^= p >> 5;
    p += p << 16;
    p ^= p >> 7;
    p ^= p >> 3;
    p ^= p << 6;
    p ^= p >> 17;
    return p;
}

unsigned selectPartition(int seed, int x, int y, int z, int partitionCount, bool smallBlock) {
    if (smallBlock) {
        x <<= 1;
        y <<= 1;
        z <<= 1;
    }
    seed += (partitionCount - 1) * 1024;
    const uint32_t rnum = hash52(uint32_t(seed));
    uint8_t s[13];
    s[1] = rnum & 0xF;
    s[2] = (rnum >> 4) & 0xF;
    s[3] = (rnum >> 8) & 0xF;
    s[4] = (rnum >> 12) & 0xF;
    s[5] = (rnum >> 16) & 0xF;
    s[6] = (rnum >> 20) & 0xF;
    s[7] = (rnum >> 24) & 0xF;
    s[8] = (rnum >> 28) & 0xF;
    s[9] = (rnum >> 18) & 0xF;
    s[10] = (rnum >> 22) & 0xF;
    s[11] = (rnum >> 26) & 0xF;
    s[12] = ((rnum >> 30) | (rnum << 2)) & 0xF;
    for (int i = 1; i <= 12; ++i) {
        s[i] = uint8_t(s[i] * s[i]);
    }
    int sh1, sh2;
    if (seed & 1) {
        sh1 = (seed & 2) ? 4 : 5;
        sh2 = partitionCount == 3 ? 6 : 5;
    } else {
        sh1 = partitionCount == 3 ? 6 : 5;
        sh2 = (seed & 2) ? 4 : 5;
    }
    const int sh3 = (seed & 0x10) ? sh1 : sh2;
    s[1] >>= sh1; s[2] >>= sh2; s[3] >>= sh1; s[4] >>= sh2;
    s[5] >>= sh1; s[6] >>= sh2; s[7] >>= sh1; s[8] >>= sh2;
    s[9] >>= sh3; s[10] >>= sh3; s[11] >>= sh3; s[12] >>= sh3;

    int a = s[1] * x + s[2] * y + s[11] * z + int(rnum >> 14);
    int b = s[3] * x + s[4] * y + s[12] * z + int(rnum >> 10);
    int c = s[5] * x + s[6] * y + s[9] * z + int(rnum >> 6);
    int d = s[7] * x + s[8] * y + s[10] * z + int(rnum >> 2);
    a &= 0x3F;
    b &= 0x3F;
    c &= 0x3F;
    d &= 0x3F;
    if (partitionCount < 4) {
        d = 0;
    }
    if (partitionCount < 3) {
        c = 0;
    }
    if (a >= b && a >= c && a >= d) {
        return 0;
    }
    if (b >= c && b >= d) {
        return 1;
    }
    if (c >= d) {
        return 2;
    }
    return 3;
}

void bitTransferSigned(int& a, int& b) {
    b >>= 1;
    b |= a & 0x80;
    a >>= 1;
    a &= 0x3F;
    if (a & 0x20) {
        a -= 0x40;
    }
}

struct Rgba {
    int r, g, b, a;
};

Rgba blueContract(int r, int g, int b, int a) {
    return {(r + b) >> 1, (g + b) >> 1, b, a};
}

int clamp255(int v) {
    return std::clamp(v, 0, 255);
}

void clampEndpoints(Rgba& e0, Rgba& e1) {
    for (Rgba* e : {&e0, &e1}) {
        e->r = clamp255(e->r);
        e->g = clamp255(e->g);
        e->b = clamp255(e->b);
        e->a = clamp255(e->a);
    }
}

bool decodeEndpoints(unsigned cem, const unsigned* v, Rgba& e0, Rgba& e1) {
    int vi[8];
    for (int i = 0; i < 8; ++i) {
        vi[i] = int(v[i]);
    }
    switch (cem) {
    case 0:
        e0 = {vi[0], vi[0], vi[0], 255};
        e1 = {vi[1], vi[1], vi[1], 255};
        return true;
    case 1: {
        const int l0 = (vi[0] >> 2) | (vi[1] & 0xC0);
        const int l1 = std::min(l0 + (vi[1] & 0x3F), 255);
        e0 = {l0, l0, l0, 255};
        e1 = {l1, l1, l1, 255};
        return true;
    }
    case 4:
        e0 = {vi[0], vi[0], vi[0], vi[2]};
        e1 = {vi[1], vi[1], vi[1], vi[3]};
        return true;
    case 5:
        bitTransferSigned(vi[1], vi[0]);
        bitTransferSigned(vi[3], vi[2]);
        e0 = {vi[0], vi[0], vi[0], vi[2]};
        e1 = {vi[0] + vi[1], vi[0] + vi[1], vi[0] + vi[1], vi[2] + vi[3]};
        clampEndpoints(e0, e1);
        return true;
    case 6:
        e0 = {(vi[0] * vi[3]) >> 8, (vi[1] * vi[3]) >> 8, (vi[2] * vi[3]) >> 8, 255};
        e1 = {vi[0], vi[1], vi[2], 255};
        return true;
    case 8:
        if (vi[1] + vi[3] + vi[5] >= vi[0] + vi[2] + vi[4]) {
            e0 = {vi[0], vi[2], vi[4], 255};
            e1 = {vi[1], vi[3], vi[5], 255};
        } else {
            e0 = blueContract(vi[1], vi[3], vi[5], 255);
            e1 = blueContract(vi[0], vi[2], vi[4], 255);
        }
        return true;
    case 9:
        bitTransferSigned(vi[1], vi[0]);
        bitTransferSigned(vi[3], vi[2]);
        bitTransferSigned(vi[5], vi[4]);
        if (vi[1] + vi[3] + vi[5] >= 0) {
            e0 = {vi[0], vi[2], vi[4], 255};
            e1 = {vi[0] + vi[1], vi[2] + vi[3], vi[4] + vi[5], 255};
        } else {
            e0 = blueContract(vi[0] + vi[1], vi[2] + vi[3], vi[4] + vi[5], 255);
            e1 = blueContract(vi[0], vi[2], vi[4], 255);
        }
        clampEndpoints(e0, e1);
        return true;
    case 10:
        e0 = {(vi[0] * vi[3]) >> 8, (vi[1] * vi[3]) >> 8, (vi[2] * vi[3]) >> 8, vi[4]};
        e1 = {vi[0], vi[1], vi[2], vi[5]};
        return true;
    case 12:
        if (vi[1] + vi[3] + vi[5] >= vi[0] + vi[2] + vi[4]) {
            e0 = {vi[0], vi[2], vi[4], vi[6]};
            e1 = {vi[1], vi[3], vi[5], vi[7]};
        } else {
            e0 = blueContract(vi[1], vi[3], vi[5], vi[7]);
            e1 = blueContract(vi[0], vi[2], vi[4], vi[6]);
        }
        return true;
    case 13:
        bitTransferSigned(vi[1], vi[0]);
        bitTransferSigned(vi[3], vi[2]);
        bitTransferSigned(vi[5], vi[4]);
        bitTransferSigned(vi[7], vi[6]);
        if (vi[1] + vi[3] + vi[5] >= 0) {
            e0 = {vi[0], vi[2], vi[4], vi[6]};
            e1 = {vi[0] + vi[1], vi[2] + vi[3], vi[4] + vi[5], vi[6] + vi[7]};
        } else {
            e0 = blueContract(vi[0] + vi[1], vi[2] + vi[3], vi[4] + vi[5], vi[6] + vi[7]);
            e1 = blueContract(vi[0], vi[2], vi[4], vi[6]);
        }
        clampEndpoints(e0, e1);
        return true;
    default:
        return false; // HDR endpoint modes are not representable as LDR output
    }
}

void fillError(uint8_t* rgba, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 0;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = 255;
    }
}

bool decodeBlockMode(unsigned mode, unsigned& gridW, unsigned& gridH, bool& dualPlane, unsigned& quant) {
    unsigned baseQuant = (mode >> 4) & 1;
    unsigned H = (mode >> 9) & 1;
    unsigned D = (mode >> 10) & 1;
    const unsigned A = (mode >> 5) & 3;
    if ((mode & 3) != 0) {
        baseQuant |= (mode & 3) << 1;
        unsigned B = (mode >> 7) & 3;
        switch ((mode >> 2) & 3) {
        case 0: gridW = B + 4; gridH = A + 2; break;
        case 1: gridW = B + 8; gridH = A + 2; break;
        case 2: gridW = A + 2; gridH = B + 8; break;
        default:
            B &= 1;
            if (mode & 0x100) {
                gridW = B + 2;
                gridH = A + 2;
            } else {
                gridW = A + 2;
                gridH = B + 6;
            }
            break;
        }
    } else {
        baseQuant |= ((mode >> 2) & 3) << 1;
        if (((mode >> 2) & 3) == 0) {
            return false;
        }
        const unsigned B = (mode >> 9) & 3;
        switch ((mode >> 7) & 3) {
        case 0: gridW = 12; gridH = A + 2; break;
        case 1: gridW = A + 2; gridH = 12; break;
        case 2:
            gridW = A + 6;
            gridH = B + 6;
            D = 0;
            H = 0;
            break;
        default:
            switch ((mode >> 5) & 3) {
            case 0: gridW = 6; gridH = 10; break;
            case 1: gridW = 10; gridH = 6; break;
            default: return false;
            }
            break;
        }
    }
    dualPlane = D != 0;
    quant = (baseQuant - 2) + 6 * H;
    return quant < 12;
}

} // namespace

bool decodeAstc(const uint8_t* block, uint32_t blockWidth, uint32_t blockHeight, uint8_t* rgba) {
    const unsigned texelCount = blockWidth * blockHeight;
    if (texelCount == 0 || texelCount > kMaxTexels) {
        return false;
    }
    const Bits128 bits(block);
    const unsigned mode = bits.get(0, 11);

    if ((mode & 0x1FF) == 0x1FC) {
        const bool hdr = (mode & 0x200) != 0;
        if (hdr) {
            fillError(rgba, texelCount);
            return false;
        }
        const uint8_t r = uint8_t(bits.get(64, 16) >> 8);
        const uint8_t g = uint8_t(bits.get(80, 16) >> 8);
        const uint8_t b = uint8_t(bits.get(96, 16) >> 8);
        const uint8_t a = uint8_t(bits.get(112, 16) >> 8);
        for (unsigned i = 0; i < texelCount; ++i) {
            rgba[i * 4] = r;
            rgba[i * 4 + 1] = g;
            rgba[i * 4 + 2] = b;
            rgba[i * 4 + 3] = a;
        }
        return true;
    }

    unsigned gridW = 0, gridH = 0, quant = 0;
    bool dualPlane = false;
    if (!decodeBlockMode(mode, gridW, gridH, dualPlane, quant) || gridW > blockWidth || gridH > blockHeight) {
        fillError(rgba, texelCount);
        return false;
    }
    const unsigned weightCount = gridW * gridH * (dualPlane ? 2 : 1);
    const IseLevel& weightLevel = kLevels[quant];
    const unsigned weightBits = iseBitCount(weightCount, weightLevel);
    if (weightCount > kMaxWeights || weightBits < 24 || weightBits > 96) {
        fillError(rgba, texelCount);
        return false;
    }

    const unsigned partitionCount = bits.get(11, 2) + 1;
    if (partitionCount == 4 && dualPlane) {
        fillError(rgba, texelCount);
        return false;
    }

    unsigned cems[4] = {};
    unsigned partitionIndex = 0;
    unsigned colorStart;
    unsigned extraCemBits = 0;
    if (partitionCount == 1) {
        cems[0] = bits.get(13, 4);
        colorStart = 17;
    } else {
        partitionIndex = bits.get(13, 10);
        const unsigned low = bits.get(23, 6);
        colorStart = 29;
        if ((low & 3) == 0) {
            for (unsigned i = 0; i < partitionCount; ++i) {
                cems[i] = low >> 2;
            }
        } else {
            extraCemBits = 3 * partitionCount - 4;
            const unsigned high = bits.get(128 - weightBits - extraCemBits, extraCemBits);
            const unsigned encoded = low | (high << 6);
            const unsigned baseClass = (encoded & 3) - 1;
            for (unsigned i = 0; i < partitionCount; ++i) {
                const unsigned c = (encoded >> (2 + i)) & 1;
                const unsigned m = (encoded >> (2 + partitionCount + 2 * i)) & 3;
                cems[i] = ((baseClass + c) << 2) | m;
            }
        }
    }

    unsigned planeSelector = 0;
    unsigned colorEnd = 128 - weightBits - extraCemBits;
    if (dualPlane) {
        colorEnd -= 2;
        planeSelector = bits.get(colorEnd, 2);
    }

    unsigned colorValueCount = 0;
    for (unsigned i = 0; i < partitionCount; ++i) {
        colorValueCount += ((cems[i] >> 2) + 1) * 2;
    }
    if (colorValueCount > 18 || colorEnd <= colorStart) {
        fillError(rgba, texelCount);
        return false;
    }
    const unsigned colorBits = colorEnd - colorStart;
    int colorLevel = -1;
    // Endpoint ranges below 6 levels are not permitted by the format.
    for (int l = int(kLevelCount) - 1; l >= 4; --l) {
        if (iseBitCount(colorValueCount, kLevels[l]) <= colorBits) {
            colorLevel = l;
            break;
        }
    }
    if (colorLevel < 0) {
        fillError(rgba, texelCount);
        return false;
    }

    IseValue colorValues[20];
    decodeIse(bits, colorStart, colorValueCount, kLevels[colorLevel], colorValues);
    Rgba endpoints[4][2];
    unsigned valueOffset = 0;
    for (unsigned p = 0; p < partitionCount; ++p) {
        unsigned v[8] = {};
        const unsigned count = ((cems[p] >> 2) + 1) * 2;
        for (unsigned i = 0; i < count; ++i) {
            v[i] = unquantizeColor(colorValues[valueOffset + i], kLevels[colorLevel]);
        }
        valueOffset += count;
        if (!decodeEndpoints(cems[p], v, endpoints[p][0], endpoints[p][1])) {
            fillError(rgba, texelCount);
            return false;
        }
    }

    const Bits128 reversedBits = bits.reversed();
    IseValue weightValues[kMaxWeights];
    decodeIse(reversedBits, 0, weightCount, weightLevel, weightValues);
    unsigned gridWeights[2][kMaxWeights] = {};
    const unsigned planes = dualPlane ? 2 : 1;
    for (unsigned i = 0; i < gridW * gridH; ++i) {
        for (unsigned p = 0; p < planes; ++p) {
            gridWeights[p][i] = unquantizeWeight(weightValues[i * planes + p], weightLevel);
        }
    }

    const unsigned ds = (1024 + blockWidth / 2) / std::max<uint32_t>(1, blockWidth - 1);
    const unsigned dt = (1024 + blockHeight / 2) / std::max<uint32_t>(1, blockHeight - 1);
    const bool smallBlock = texelCount < 31;

    for (unsigned t = 0; t < blockHeight; ++t) {
        for (unsigned s = 0; s < blockWidth; ++s) {
            const unsigned gs = (ds * s * (gridW - 1) + 32) >> 6;
            const unsigned gt = (dt * t * (gridH - 1) + 32) >> 6;
            const unsigned js = gs >> 4, fs = gs & 0xF;
            const unsigned jt = gt >> 4, ft = gt & 0xF;
            const unsigned w11 = (fs * ft + 8) >> 4;
            const unsigned w10 = ft - w11;
            const unsigned w01 = fs - w11;
            const unsigned w00 = 16 - fs - ft + w11;
            const unsigned x0 = std::min(js, gridW - 1), x1 = std::min(js + 1, gridW - 1);
            const unsigned y0 = std::min(jt, gridH - 1), y1 = std::min(jt + 1, gridH - 1);

            unsigned texelWeights[2];
            for (unsigned p = 0; p < planes; ++p) {
                const unsigned* g = gridWeights[p];
                texelWeights[p] = (g[y0 * gridW + x0] * w00 + g[y0 * gridW + x1] * w01 + g[y1 * gridW + x0] * w10 +
                                   g[y1 * gridW + x1] * w11 + 8) >> 4;
            }

            const unsigned partition =
                partitionCount > 1 ? selectPartition(int(partitionIndex), int(s), int(t), 0, int(partitionCount), smallBlock)
                                   : 0;
            const Rgba& e0 = endpoints[partition][0];
            const Rgba& e1 = endpoints[partition][1];
            const int c0[4] = {e0.r, e0.g, e0.b, e0.a};
            const int c1[4] = {e1.r, e1.g, e1.b, e1.a};
            uint8_t* out = rgba + (t * blockWidth + s) * 4;
            for (unsigned c = 0; c < 4; ++c) {
                const unsigned w = (dualPlane && c == planeSelector) ? texelWeights[1] : texelWeights[0];
                const int a = c0[c] * 257;
                const int b = c1[c] * 257;
                const int value = (a * (64 - int(w)) + b * int(w) + 32) >> 6;
                out[c] = uint8_t(std::clamp(value >> 8, 0, 255));
            }
        }
    }
    return true;
}

} // namespace bfrass::tex::block
