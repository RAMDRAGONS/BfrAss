#include "texture/BlockDecoders.hpp"
#include "texture/BptcTables.hpp"

#include "core/Math.hpp"

#include <vector>

namespace bfrass::tex::block {

namespace {

// Endpoint field identifiers: channel * 4 + endpoint (W, X, Y, Z = 0..3).
enum Field : uint8_t {
    RW = 0, RX, RY, RZ,
    GW, GX, GY, GZ,
    BW, BX, BY, BZ,
    D = 12, // partition shape
    M = 13, // mode bits, already consumed
    End = 14,
};

struct Bit {
    uint8_t field;
    uint8_t bit;
};

// Field sequences range-compressed as {field, first bit, last bit}; bit order follows
// the listing, so descending ranges encode reversed bit order.
struct Run {
    uint8_t field;
    uint8_t first;
    uint8_t last;
};

struct ModeInfo {
    uint8_t modeValue;
    bool transformed;
    uint8_t regions;
    uint8_t precision[4][3]; // per endpoint W, X, Y, Z
    std::vector<Run> runs;
};

const std::vector<ModeInfo>& modes() {
    static const std::vector<ModeInfo> table = {
        {0x00, true, 2, {{10, 10, 10}, {5, 5, 5}, {5, 5, 5}, {5, 5, 5}},
         {{GY, 4, 4}, {BY, 4, 4}, {BZ, 4, 4}, {RW, 0, 9}, {GW, 0, 9}, {BW, 0, 9}, {RX, 0, 4}, {GZ, 4, 4},
          {GY, 0, 3}, {GX, 0, 4}, {BZ, 0, 0}, {GZ, 0, 3}, {BX, 0, 4}, {BZ, 1, 1}, {BY, 0, 3}, {RY, 0, 4},
          {BZ, 2, 2}, {RZ, 0, 4}, {BZ, 3, 3}, {D, 0, 4}}},
        {0x01, true, 2, {{7, 7, 7}, {6, 6, 6}, {6, 6, 6}, {6, 6, 6}},
         {{GY, 5, 5}, {GZ, 4, 4}, {GZ, 5, 5}, {RW, 0, 6}, {BZ, 0, 0}, {BZ, 1, 1}, {BY, 4, 4}, {GW, 0, 6},
          {BY, 5, 5}, {BZ, 2, 2}, {GY, 4, 4}, {BW, 0, 6}, {BZ, 3, 3}, {BZ, 5, 5}, {BZ, 4, 4}, {RX, 0, 5},
          {GY, 0, 3}, {GX, 0, 5}, {GZ, 0, 3}, {BX, 0, 5}, {BY, 0, 3}, {RY, 0, 5}, {RZ, 0, 5}, {D, 0, 4}}},
        {0x02, true, 2, {{11, 11, 11}, {5, 4, 4}, {5, 4, 4}, {5, 4, 4}},
         {{RW, 0, 9}, {GW, 0, 9}, {BW, 0, 9}, {RX, 0, 4}, {RW, 10, 10}, {GY, 0, 3}, {GX, 0, 3}, {GW, 10, 10},
          {BZ, 0, 0}, {GZ, 0, 3}, {BX, 0, 3}, {BW, 10, 10}, {BZ, 1, 1}, {BY, 0, 3}, {RY, 0, 4}, {BZ, 2, 2},
          {RZ, 0, 4}, {BZ, 3, 3}, {D, 0, 4}}},
        {0x06, true, 2, {{11, 11, 11}, {4, 5, 4}, {4, 5, 4}, {4, 5, 4}},
         {{RW, 0, 9}, {GW, 0, 9}, {BW, 0, 9}, {RX, 0, 3}, {RW, 10, 10}, {GZ, 4, 4}, {GY, 0, 3}, {GX, 0, 4},
          {GW, 10, 10}, {GZ, 0, 3}, {BX, 0, 3}, {BW, 10, 10}, {BZ, 1, 1}, {BY, 0, 3}, {RY, 0, 3}, {BZ, 0, 0},
          {BZ, 2, 2}, {RZ, 0, 3}, {GY, 4, 4}, {BZ, 3, 3}, {D, 0, 4}}},
        {0x0A, true, 2, {{11, 11, 11}, {4, 4, 5}, {4, 4, 5}, {4, 4, 5}},
         {{RW, 0, 9}, {GW, 0, 9}, {BW, 0, 9}, {RX, 0, 3}, {RW, 10, 10}, {BY, 4, 4}, {GY, 0, 3}, {GX, 0, 3},
          {GW, 10, 10}, {BZ, 0, 0}, {GZ, 0, 3}, {BX, 0, 4}, {BW, 10, 10}, {BY, 0, 3}, {RY, 0, 3}, {BZ, 1, 1},
          {BZ, 2, 2}, {RZ, 0, 3}, {BZ, 4, 4}, {BZ, 3, 3}, {D, 0, 4}}},
        {0x0E, true, 2, {{9, 9, 9}, {5, 5, 5}, {5, 5, 5}, {5, 5, 5}},
         {{RW, 0, 8}, {BY, 4, 4}, {GW, 0, 8}, {GY, 4, 4}, {BW, 0, 8}, {BZ, 4, 4}, {RX, 0, 4}, {GZ, 4, 4},
          {GY, 0, 3}, {GX, 0, 4}, {BZ, 0, 0}, {GZ, 0, 3}, {BX, 0, 4}, {BZ, 1, 1}, {BY, 0, 3}, {RY, 0, 4},
          {BZ, 2, 2}, {RZ, 0, 4}, {BZ, 3, 3}, {D, 0, 4}}},
        {0x12, true, 2, {{8, 8, 8}, {6, 5, 5}, {6, 5, 5}, {6, 5, 5}},
         {{RW, 0, 7}, {GZ, 4, 4}, {BY, 4, 4}, {GW, 0, 7}, {BZ, 2, 2}, {GY, 4, 4}, {BW, 0, 7}, {BZ, 3, 3},
          {BZ, 4, 4}, {RX, 0, 5}, {GY, 0, 3}, {GX, 0, 4}, {BZ, 0, 0}, {GZ, 0, 3}, {BX, 0, 4}, {BZ, 1, 1},
          {BY, 0, 3}, {RY, 0, 5}, {RZ, 0, 5}, {D, 0, 4}}},
        {0x16, true, 2, {{8, 8, 8}, {5, 6, 5}, {5, 6, 5}, {5, 6, 5}},
         {{RW, 0, 7}, {BZ, 0, 0}, {BY, 4, 4}, {GW, 0, 7}, {GY, 5, 5}, {GY, 4, 4}, {BW, 0, 7}, {GZ, 5, 5},
          {BZ, 4, 4}, {RX, 0, 4}, {GZ, 4, 4}, {GY, 0, 3}, {GX, 0, 5}, {GZ, 0, 3}, {BX, 0, 4}, {BZ, 1, 1},
          {BY, 0, 3}, {RY, 0, 4}, {BZ, 2, 2}, {RZ, 0, 4}, {BZ, 3, 3}, {D, 0, 4}}},
        {0x1A, true, 2, {{8, 8, 8}, {5, 5, 6}, {5, 5, 6}, {5, 5, 6}},
         {{RW, 0, 7}, {BZ, 1, 1}, {BY, 4, 4}, {GW, 0, 7}, {BY, 5, 5}, {GY, 4, 4}, {BW, 0, 7}, {BZ, 5, 5},
          {BZ, 4, 4}, {RX, 0, 4}, {GZ, 4, 4}, {GY, 0, 3}, {GX, 0, 4}, {BZ, 0, 0}, {GZ, 0, 3}, {BX, 0, 5},
          {BY, 0, 3}, {RY, 0, 4}, {BZ, 2, 2}, {RZ, 0, 4}, {BZ, 3, 3}, {D, 0, 4}}},
        {0x1E, false, 2, {{6, 6, 6}, {6, 6, 6}, {6, 6, 6}, {6, 6, 6}},
         {{RW, 0, 5}, {GZ, 4, 4}, {BZ, 0, 0}, {BZ, 1, 1}, {BY, 4, 4}, {GW, 0, 5}, {GY, 5, 5}, {BY, 5, 5},
          {BZ, 2, 2}, {GY, 4, 4}, {BW, 0, 5}, {GZ, 5, 5}, {BZ, 3, 3}, {BZ, 5, 5}, {BZ, 4, 4}, {RX, 0, 5},
          {GY, 0, 3}, {GX, 0, 5}, {GZ, 0, 3}, {BX, 0, 5}, {BY, 0, 3}, {RY, 0, 5}, {RZ, 0, 5}, {D, 0, 4}}},
        {0x03, false, 1, {{10, 10, 10}, {10, 10, 10}, {0, 0, 0}, {0, 0, 0}},
         {{RW, 0, 9}, {GW, 0, 9}, {BW, 0, 9}, {RX, 0, 9}, {GX, 0, 9}, {BX, 0, 9}}},
        {0x07, true, 1, {{11, 11, 11}, {9, 9, 9}, {0, 0, 0}, {0, 0, 0}},
         {{RW, 0, 9}, {GW, 0, 9}, {BW, 0, 9}, {RX, 0, 8}, {RW, 10, 10}, {GX, 0, 8}, {GW, 10, 10}, {BX, 0, 8},
          {BW, 10, 10}}},
        {0x0B, true, 1, {{12, 12, 12}, {8, 8, 8}, {0, 0, 0}, {0, 0, 0}},
         {{RW, 0, 9}, {GW, 0, 9}, {BW, 0, 9}, {RX, 0, 7}, {RW, 11, 10}, {GX, 0, 7}, {GW, 11, 10}, {BX, 0, 7},
          {BW, 11, 10}}},
        {0x0F, true, 1, {{16, 16, 16}, {4, 4, 4}, {0, 0, 0}, {0, 0, 0}},
         {{RW, 0, 9}, {GW, 0, 9}, {BW, 0, 9}, {RX, 0, 3}, {RW, 15, 10}, {GX, 0, 3}, {GW, 15, 10}, {BX, 0, 3},
          {BW, 15, 10}}},
    };
    return table;
}

int32_t signExtend(int32_t value, unsigned bits) {
    if (bits == 0 || bits >= 32) {
        return value;
    }
    const int32_t shift = 32 - int32_t(bits);
    return int32_t(uint32_t(value) << shift) >> shift;
}

int32_t unquantize(int32_t value, unsigned bits, bool isSigned) {
    if (!isSigned) {
        if (bits >= 15) {
            return value;
        }
        if (value == 0) {
            return 0;
        }
        if (value == (1 << bits) - 1) {
            return 0xFFFF;
        }
        return ((value << 15) + 0x4000) >> (bits - 1);
    }
    if (bits >= 16) {
        return value;
    }
    bool negative = false;
    if (value < 0) {
        negative = true;
        value = -value;
    }
    int32_t result;
    if (value == 0) {
        result = 0;
    } else if (value >= (1 << (bits - 1)) - 1) {
        result = 0x7FFF;
    } else {
        result = ((value << 15) + 0x4000) >> (bits - 1);
    }
    return negative ? -result : result;
}

float finishUnquantize(int32_t value, bool isSigned) {
    if (!isSigned) {
        const uint16_t half = uint16_t((value * 31) >> 6);
        return halfToFloat(half);
    }
    uint16_t half;
    if (value < 0) {
        half = uint16_t((((-value) * 31) >> 5) | 0x8000);
    } else {
        half = uint16_t((value * 31) >> 5);
    }
    return halfToFloat(half);
}

} // namespace

void decodeBc6h(const uint8_t* block, float* rgb, bool isSigned) {
    bptc::BitReader bits(block);
    uint32_t modeValue = bits.read(2);
    if (modeValue > 1) {
        modeValue |= bits.read(3) << 2;
    }
    const ModeInfo* mode = nullptr;
    for (const auto& m : modes()) {
        if (m.modeValue == modeValue) {
            mode = &m;
            break;
        }
    }
    if (!mode) {
        for (int i = 0; i < 48; ++i) {
            rgb[i] = 0.0f;
        }
        return;
    }

    int32_t endpoints[4][3] = {};
    uint32_t shape = 0;
    for (const Run& run : mode->runs) {
        const int step = run.last >= run.first ? 1 : -1;
        for (int b = run.first;; b += step) {
            const uint32_t v = bits.read(1);
            if (run.field == D) {
                shape |= v << b;
            } else {
                endpoints[run.field % 4][run.field / 4] |= int32_t(v << b);
            }
            if (b == run.last) {
                break;
            }
        }
    }

    const unsigned endpointCount = mode->regions * 2u;
    for (unsigned c = 0; c < 3; ++c) {
        const unsigned baseBits = mode->precision[0][c];
        if (isSigned) {
            endpoints[0][c] = signExtend(endpoints[0][c], baseBits);
        }
        for (unsigned e = 1; e < endpointCount; ++e) {
            const unsigned eBits = mode->precision[e][c];
            if (mode->transformed) {
                const int32_t delta = signExtend(endpoints[e][c], eBits);
                int32_t v = (endpoints[0][c] + delta) & ((1 << baseBits) - 1);
                if (isSigned) {
                    v = signExtend(v, baseBits);
                }
                endpoints[e][c] = v;
            } else if (isSigned) {
                endpoints[e][c] = signExtend(endpoints[e][c], eBits);
            }
        }
    }
    for (unsigned e = 0; e < endpointCount; ++e) {
        for (unsigned c = 0; c < 3; ++c) {
            endpoints[e][c] = unquantize(endpoints[e][c], mode->precision[0][c], isSigned);
        }
    }

    const unsigned indexBits = mode->regions == 2 ? 3 : 4;
    const uint8_t* weights = bptc::weightsFor(indexBits);
    for (unsigned t = 0; t < 16; ++t) {
        unsigned subset = 0;
        bool anchor = t == 0;
        if (mode->regions == 2) {
            subset = (bptc::kPartitions2[shape] >> t) & 1;
            anchor = anchor || t == bptc::kAnchor2[shape];
        }
        const unsigned index = bits.read(anchor ? indexBits - 1 : indexBits);
        const unsigned w = weights[index];
        for (unsigned c = 0; c < 3; ++c) {
            const int32_t a = endpoints[subset * 2][c];
            const int32_t b = endpoints[subset * 2 + 1][c];
            const int32_t v = ((64 - int32_t(w)) * a + int32_t(w) * b + 32) >> 6;
            rgb[t * 3 + c] = finishUnquantize(v, isSigned);
        }
    }
}

} // namespace bfrass::tex::block
