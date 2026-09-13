#include "texture/BlockDecoders.hpp"
#include "texture/BptcTables.hpp"

#include <cstring>
#include <utility>

namespace bfrass::tex::block {

namespace {

struct Bc7Mode {
    uint8_t subsets;
    uint8_t partitionBits;
    uint8_t rotationBits;
    uint8_t indexSelectionBits;
    uint8_t colorBits;
    uint8_t alphaBits;
    uint8_t endpointPBits;
    uint8_t sharedPBits;
    uint8_t indexBits;
    uint8_t secondaryIndexBits;
};

constexpr Bc7Mode kModes[8] = {
    {3, 4, 0, 0, 4, 0, 1, 0, 3, 0},
    {2, 6, 0, 0, 6, 0, 0, 1, 3, 0},
    {3, 6, 0, 0, 5, 0, 0, 0, 2, 0},
    {2, 6, 0, 0, 7, 0, 1, 0, 2, 0},
    {1, 0, 2, 1, 5, 6, 0, 0, 2, 3},
    {1, 0, 2, 0, 7, 8, 0, 0, 2, 2},
    {1, 0, 0, 0, 7, 7, 1, 0, 4, 0},
    {2, 6, 0, 0, 5, 5, 1, 0, 2, 0},
};

unsigned subsetOf(unsigned subsets, unsigned partition, unsigned texel) {
    if (subsets == 2) {
        return (bptc::kPartitions2[partition] >> texel) & 1;
    }
    if (subsets == 3) {
        return (bptc::kPartitions3[partition] >> (2 * texel)) & 3;
    }
    return 0;
}

bool isAnchor(unsigned subsets, unsigned partition, unsigned texel) {
    if (texel == 0) {
        return true;
    }
    if (subsets == 2) {
        return texel == bptc::kAnchor2[partition];
    }
    if (subsets == 3) {
        return texel == bptc::kAnchor3a[partition] || texel == bptc::kAnchor3b[partition];
    }
    return false;
}

uint8_t expandBits(uint32_t value, unsigned bits) {
    if (bits >= 8) {
        return uint8_t(value);
    }
    value <<= (8 - bits);
    return uint8_t(value | (value >> bits));
}

uint8_t interpolate(uint8_t a, uint8_t b, unsigned weight) {
    return uint8_t(((64 - weight) * a + weight * b + 32) >> 6);
}

} // namespace

void decodeBc7(const uint8_t* block, uint8_t* rgba) {
    unsigned modeIndex = 0;
    while (modeIndex < 8 && ((block[0] >> modeIndex) & 1) == 0) {
        ++modeIndex;
    }
    if (modeIndex == 8) {
        std::memset(rgba, 0, 64);
        return;
    }
    const Bc7Mode& mode = kModes[modeIndex];
    bptc::BitReader bits(block);
    bits.read(modeIndex + 1);

    const unsigned partition = bits.read(mode.partitionBits);
    const unsigned rotation = bits.read(mode.rotationBits);
    const unsigned indexSelection = bits.read(mode.indexSelectionBits);
    const unsigned endpointCount = mode.subsets * 2u;

    uint32_t endpoints[6][4] = {};
    for (unsigned c = 0; c < 3; ++c) {
        for (unsigned e = 0; e < endpointCount; ++e) {
            endpoints[e][c] = bits.read(mode.colorBits);
        }
    }
    if (mode.alphaBits) {
        for (unsigned e = 0; e < endpointCount; ++e) {
            endpoints[e][3] = bits.read(mode.alphaBits);
        }
    }

    unsigned colorPrecision = mode.colorBits;
    unsigned alphaPrecision = mode.alphaBits;
    if (mode.endpointPBits) {
        for (unsigned e = 0; e < endpointCount; ++e) {
            const uint32_t p = bits.read(1);
            for (unsigned c = 0; c < 4; ++c) {
                endpoints[e][c] = (endpoints[e][c] << 1) | p;
            }
        }
        ++colorPrecision;
        if (alphaPrecision) {
            ++alphaPrecision;
        }
    } else if (mode.sharedPBits) {
        for (unsigned s = 0; s < mode.subsets; ++s) {
            const uint32_t p = bits.read(1);
            for (unsigned e = s * 2; e < s * 2 + 2; ++e) {
                for (unsigned c = 0; c < 4; ++c) {
                    endpoints[e][c] = (endpoints[e][c] << 1) | p;
                }
            }
        }
        ++colorPrecision;
        if (alphaPrecision) {
            ++alphaPrecision;
        }
    }

    uint8_t expanded[6][4];
    for (unsigned e = 0; e < endpointCount; ++e) {
        for (unsigned c = 0; c < 3; ++c) {
            expanded[e][c] = expandBits(endpoints[e][c], colorPrecision);
        }
        expanded[e][3] = alphaPrecision ? expandBits(endpoints[e][3], alphaPrecision) : 255;
    }

    unsigned primary[16];
    for (unsigned t = 0; t < 16; ++t) {
        const unsigned count = isAnchor(mode.subsets, partition, t) ? mode.indexBits - 1u : mode.indexBits;
        primary[t] = bits.read(count);
    }
    unsigned secondary[16] = {};
    if (mode.secondaryIndexBits) {
        for (unsigned t = 0; t < 16; ++t) {
            const unsigned count = t == 0 ? mode.secondaryIndexBits - 1u : mode.secondaryIndexBits;
            secondary[t] = bits.read(count);
        }
    }

    for (unsigned t = 0; t < 16; ++t) {
        const unsigned subset = subsetOf(mode.subsets, partition, t);
        const uint8_t* e0 = expanded[subset * 2];
        const uint8_t* e1 = expanded[subset * 2 + 1];
        uint8_t* out = rgba + t * 4;

        if (mode.secondaryIndexBits) {
            unsigned colorIndex = primary[t];
            unsigned colorBitsUsed = mode.indexBits;
            unsigned alphaIndex = secondary[t];
            unsigned alphaBitsUsed = mode.secondaryIndexBits;
            if (indexSelection) {
                std::swap(colorIndex, alphaIndex);
                std::swap(colorBitsUsed, alphaBitsUsed);
            }
            const unsigned cw = bptc::weightsFor(colorBitsUsed)[colorIndex];
            const unsigned aw = bptc::weightsFor(alphaBitsUsed)[alphaIndex];
            for (unsigned c = 0; c < 3; ++c) {
                out[c] = interpolate(e0[c], e1[c], cw);
            }
            out[3] = interpolate(e0[3], e1[3], aw);
        } else {
            const unsigned w = bptc::weightsFor(mode.indexBits)[primary[t]];
            for (unsigned c = 0; c < 4; ++c) {
                out[c] = interpolate(e0[c], e1[c], w);
            }
        }

        switch (rotation) {
        case 1: std::swap(out[0], out[3]); break;
        case 2: std::swap(out[1], out[3]); break;
        case 3: std::swap(out[2], out[3]); break;
        default: break;
        }
    }
}

} // namespace bfrass::tex::block
