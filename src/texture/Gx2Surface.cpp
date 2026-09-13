#include "texture/Gx2Surface.hpp"

#include <algorithm>
#include <cstring>

// Port of the Radeon R7xx address library logic used by GX2, following the
// reverse-engineered implementation shipped with BfresLibrary/Switch Toolbox
// (itself derived from AboodXD's GTX Extractor).

namespace bfrass::tex::gx2 {

namespace {

constexpr uint8_t kFormatHwInfo[] = {
    0x00, 0x00, 0x00, 0x01, 0x08, 0x03, 0x00, 0x01, 0x08, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x01, 0x10, 0x07, 0x00, 0x00, 0x10, 0x03, 0x00, 0x01, 0x10, 0x03, 0x00, 0x01,
    0x10, 0x0B, 0x00, 0x01, 0x10, 0x01, 0x00, 0x01, 0x10, 0x03, 0x00, 0x01, 0x10, 0x03, 0x00, 0x01,
    0x10, 0x03, 0x00, 0x01, 0x20, 0x03, 0x00, 0x00, 0x20, 0x07, 0x00, 0x00, 0x20, 0x03, 0x00, 0x00,
    0x20, 0x03, 0x00, 0x01, 0x20, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x20, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x01, 0x20, 0x0B, 0x00, 0x01, 0x20, 0x0B, 0x00, 0x01, 0x20, 0x0B, 0x00, 0x01,
    0x40, 0x05, 0x00, 0x00, 0x40, 0x03, 0x00, 0x00, 0x40, 0x03, 0x00, 0x00, 0x40, 0x03, 0x00, 0x00,
    0x40, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x80, 0x03, 0x00, 0x00, 0x80, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x10, 0x01, 0x00, 0x00,
    0x10, 0x01, 0x00, 0x00, 0x20, 0x01, 0x00, 0x00, 0x20, 0x01, 0x00, 0x00, 0x20, 0x01, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x60, 0x01, 0x00, 0x00,
    0x60, 0x01, 0x00, 0x00, 0x40, 0x01, 0x00, 0x01, 0x80, 0x01, 0x00, 0x01, 0x80, 0x01, 0x00, 0x01,
    0x40, 0x01, 0x00, 0x01, 0x80, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr uint8_t kFormatExInfo[] = {
    0x00, 0x01, 0x01, 0x03, 0x08, 0x01, 0x01, 0x03, 0x08, 0x01, 0x01, 0x03, 0x08, 0x01, 0x01, 0x03,
    0x00, 0x01, 0x01, 0x03, 0x10, 0x01, 0x01, 0x03, 0x10, 0x01, 0x01, 0x03, 0x10, 0x01, 0x01, 0x03,
    0x10, 0x01, 0x01, 0x03, 0x10, 0x01, 0x01, 0x03, 0x10, 0x01, 0x01, 0x03, 0x10, 0x01, 0x01, 0x03,
    0x10, 0x01, 0x01, 0x03, 0x20, 0x01, 0x01, 0x03, 0x20, 0x01, 0x01, 0x03, 0x20, 0x01, 0x01, 0x03,
    0x20, 0x01, 0x01, 0x03, 0x20, 0x01, 0x01, 0x03, 0x20, 0x01, 0x01, 0x03, 0x20, 0x01, 0x01, 0x03,
    0x20, 0x01, 0x01, 0x03, 0x20, 0x01, 0x01, 0x03, 0x20, 0x01, 0x01, 0x03, 0x20, 0x01, 0x01, 0x03,
    0x20, 0x01, 0x01, 0x03, 0x20, 0x01, 0x01, 0x03, 0x20, 0x01, 0x01, 0x03, 0x20, 0x01, 0x01, 0x03,
    0x40, 0x01, 0x01, 0x03, 0x40, 0x01, 0x01, 0x03, 0x40, 0x01, 0x01, 0x03, 0x40, 0x01, 0x01, 0x03,
    0x40, 0x01, 0x01, 0x03, 0x00, 0x01, 0x01, 0x03, 0x80, 0x01, 0x01, 0x03, 0x80, 0x01, 0x01, 0x03,
    0x00, 0x01, 0x01, 0x03, 0x01, 0x08, 0x01, 0x05, 0x01, 0x08, 0x01, 0x06, 0x10, 0x01, 0x01, 0x07,
    0x10, 0x01, 0x01, 0x08, 0x20, 0x01, 0x01, 0x03, 0x20, 0x01, 0x01, 0x03, 0x20, 0x01, 0x01, 0x03,
    0x18, 0x03, 0x01, 0x04, 0x30, 0x03, 0x01, 0x04, 0x30, 0x03, 0x01, 0x04, 0x60, 0x03, 0x01, 0x04,
    0x60, 0x03, 0x01, 0x04, 0x40, 0x04, 0x04, 0x09, 0x80, 0x04, 0x04, 0x0A, 0x80, 0x04, 0x04, 0x0B,
    0x40, 0x04, 0x04, 0x0C, 0x40, 0x04, 0x04, 0x0D, 0x40, 0x04, 0x04, 0x0D, 0x40, 0x04, 0x04, 0x0D,
    0x00, 0x01, 0x01, 0x03, 0x00, 0x01, 0x01, 0x03, 0x00, 0x01, 0x01, 0x03, 0x00, 0x01, 0x01, 0x03,
    0x00, 0x01, 0x01, 0x03, 0x00, 0x01, 0x01, 0x03, 0x40, 0x01, 0x01, 0x03, 0x00, 0x01, 0x01, 0x03,
};

enum AddrTileMode : uint32_t {
    LinearGeneral = 0,
    LinearAligned = 1,
    Tiled1DThin1 = 2,
    Tiled1DThick = 3,
    Tiled2DThin1 = 4,
    Tiled2DThin2 = 5,
    Tiled2DThin4 = 6,
    Tiled2DThick = 7,
    Tiled2BThin1 = 8,
    Tiled2BThin2 = 9,
    Tiled2BThin4 = 10,
    Tiled2BThick = 11,
    Tiled3DThin1 = 12,
    Tiled3DThick = 13,
    Tiled3BThin1 = 14,
    Tiled3BThick = 15,
    Tiled2DXThick = 16,
    Tiled3DXThick = 17,
};

struct SurfaceIn {
    uint32_t tileMode = 0;
    uint32_t format = 0;
    uint32_t bpp = 0;
    uint32_t numSamples = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t numSlices = 0;
    uint32_t slice = 0;
    uint32_t mipLevel = 0;
    uint32_t flags = 0;
};

struct Expanded {
    uint32_t pitch = 0;
    uint32_t height = 0;
    uint32_t slices = 0;
};

struct Layout {
    uint32_t valid = 0;
    uint32_t pitch = 0;
    uint32_t height = 0;
    uint32_t slices = 0;
    uint32_t surfSize = 0;
    uint32_t tileMode = 0;
    uint32_t baseAlign = 0;
    uint32_t pitchAlign = 0;
    uint32_t heightAlign = 0;
    uint32_t depthAlign = 0;
};

uint32_t powTwoAlign(uint32_t x, uint32_t align) {
    return ~(align - 1) & (x + align - 1);
}

uint32_t nextPow2(uint32_t dim) {
    uint32_t newDim = 1;
    if (dim < 0x7FFFFFFF) {
        while (newDim < dim) {
            newDim *= 2;
        }
    } else {
        newDim = 0x80000000;
    }
    return newDim;
}

uint32_t surfaceThickness(uint32_t tileMode) {
    switch (tileMode) {
    case Tiled1DThick:
    case Tiled2DThick:
    case Tiled2BThick:
    case Tiled3DThick:
    case Tiled3BThick:
        return 4;
    case Tiled2DXThick:
    case Tiled3DXThick:
        return 8;
    default:
        return 1;
    }
}

uint32_t pixelIndexWithinMicroTile(uint32_t x, uint32_t y, uint32_t z, uint32_t bpp, uint32_t tileMode, bool isDepth) {
    uint32_t b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, b7 = 0, b8 = 0;
    const uint32_t thickness = surfaceThickness(tileMode);
    if (isDepth) {
        b0 = x & 1;
        b1 = y & 1;
        b2 = (x & 2) >> 1;
        b3 = (y & 2) >> 1;
        b4 = (x & 4) >> 2;
        b5 = (y & 4) >> 2;
    } else {
        switch (bpp) {
        case 8:
            b0 = x & 1;
            b1 = (x & 2) >> 1;
            b2 = (x & 4) >> 2;
            b3 = (y & 2) >> 1;
            b4 = y & 1;
            b5 = (y & 4) >> 2;
            break;
        case 0x10:
            b0 = x & 1;
            b1 = (x & 2) >> 1;
            b2 = (x & 4) >> 2;
            b3 = y & 1;
            b4 = (y & 2) >> 1;
            b5 = (y & 4) >> 2;
            break;
        case 0x40:
            b0 = x & 1;
            b1 = y & 1;
            b2 = (x & 2) >> 1;
            b3 = (x & 4) >> 2;
            b4 = (y & 2) >> 1;
            b5 = (y & 4) >> 2;
            break;
        case 0x80:
            b0 = y & 1;
            b1 = x & 1;
            b2 = (x & 2) >> 1;
            b3 = (x & 4) >> 2;
            b4 = (y & 2) >> 1;
            b5 = (y & 4) >> 2;
            break;
        default: // 0x20, 0x60 and anything else
            b0 = x & 1;
            b1 = (x & 2) >> 1;
            b2 = y & 1;
            b3 = (x & 4) >> 2;
            b4 = (y & 2) >> 1;
            b5 = (y & 4) >> 2;
            break;
        }
    }
    if (thickness > 1) {
        b6 = z & 1;
        b7 = (z & 2) >> 1;
    }
    if (thickness == 8) {
        b8 = (z & 4) >> 2;
    }
    return (b8 << 8) | (b7 << 7) | (b6 << 6) | 32 * b5 | 16 * b4 | 8 * b3 | 4 * b2 | b0 | 2 * b1;
}

uint32_t pipeFromCoordWoRotation(uint32_t x, uint32_t y) {
    return ((y >> 3) ^ (x >> 3)) & 1;
}

uint32_t bankFromCoordWoRotation(uint32_t x, uint32_t y) {
    return (((y >> 5) ^ (x >> 3)) & 1) | (2 * (((y >> 4) ^ (x >> 4)) & 1));
}

uint32_t surfaceRotationFromTileMode(uint32_t tileMode) {
    switch (tileMode) {
    case Tiled2DThin1: case Tiled2DThin2: case Tiled2DThin4: case Tiled2DThick:
    case Tiled2BThin1: case Tiled2BThin2: case Tiled2BThin4: case Tiled2BThick:
        return 2;
    case Tiled3DThin1: case Tiled3DThick: case Tiled3BThin1: case Tiled3BThick:
        return 1;
    default:
        return 0;
    }
}

bool isThickMacroTiled(uint32_t tileMode) {
    return tileMode == Tiled2DThick || tileMode == Tiled2BThick || tileMode == Tiled3DThick ||
           tileMode == Tiled3BThick;
}

bool isBankSwappedTileMode(uint32_t tileMode) {
    switch (tileMode) {
    case Tiled2BThin1: case Tiled2BThin2: case Tiled2BThin4: case Tiled2BThick:
    case Tiled3BThin1: case Tiled3BThick:
        return true;
    default:
        return false;
    }
}

uint32_t macroTileAspectRatio(uint32_t tileMode) {
    switch (tileMode) {
    case Tiled2DThin2: case Tiled2BThin2: return 2;
    case Tiled2DThin4: case Tiled2BThin4: return 4;
    default: return 1;
    }
}

uint32_t surfaceBankSwappedWidth(uint32_t tileMode, uint32_t bpp, uint32_t numSamples, uint32_t pitch) {
    if (!isBankSwappedTileMode(tileMode)) {
        return 0;
    }
    const uint32_t bytesPerSample = 8 * bpp;
    uint32_t slicesPerTile = 1;
    if (bytesPerSample != 0) {
        const uint32_t samplesPerTile = 2048 / bytesPerSample;
        slicesPerTile = std::max<uint32_t>(1, samplesPerTile ? numSamples / samplesPerTile : 1);
    }
    if (isThickMacroTiled(tileMode)) {
        numSamples = 4;
    }
    const uint32_t bytesPerTileSlice = numSamples * bytesPerSample / slicesPerTile;
    const uint32_t factor = macroTileAspectRatio(tileMode);
    const uint32_t swapTiles = std::max<uint32_t>(1, 128 / bpp);
    const uint32_t swapWidth = swapTiles * 32;
    const uint32_t heightBytes = numSamples * factor * bpp * 2 / slicesPerTile;
    const uint32_t swapMax = heightBytes ? 0x4000 / heightBytes : 0;
    const uint32_t swapMin = bytesPerTileSlice ? 256 / bytesPerTileSlice : 0;
    uint32_t bankSwapWidth = std::min(swapMax, std::max(swapMin, swapWidth));
    while (bankSwapWidth >= 2 * pitch && bankSwapWidth > 0) {
        bankSwapWidth >>= 1;
    }
    return bankSwapWidth;
}

uint64_t addrFromCoordLinear(uint32_t x, uint32_t y, uint32_t slice, uint32_t sample, uint32_t bytesPerPixel,
                             uint32_t pitch, uint32_t height, uint32_t numSlices) {
    const uint64_t sliceOffset = uint64_t(pitch) * height * (slice + sample * numSlices);
    return (uint64_t(y) * pitch + x + sliceOffset) * bytesPerPixel;
}

uint64_t addrFromCoordMicroTiled(uint32_t x, uint32_t y, uint32_t slice, uint32_t bpp, uint32_t pitch, uint32_t height,
                                 uint32_t tileMode, bool isDepth) {
    uint32_t thickness = tileMode == Tiled1DThick ? 4 : 1;
    const uint64_t microTileBytes = (64 * thickness * bpp + 7) / 8;
    const uint64_t microTilesPerRow = pitch >> 3;
    const uint64_t tileX = x >> 3;
    const uint64_t tileY = y >> 3;
    const uint64_t tileZ = slice / thickness;
    const uint64_t microTileOffset = microTileBytes * (tileX + tileY * microTilesPerRow);
    const uint64_t sliceBytes = (uint64_t(pitch) * height * thickness * bpp + 7) / 8;
    const uint64_t sliceOffset = tileZ * sliceBytes;
    const uint32_t pixelIndex = pixelIndexWithinMicroTile(x, y, slice, bpp, tileMode, isDepth);
    const uint64_t pixelOffset = (uint64_t(bpp) * pixelIndex) >> 3;
    return pixelOffset + microTileOffset + sliceOffset;
}

constexpr uint8_t kBankSwapOrder[] = {0, 1, 3, 2, 6, 7, 5, 4, 0, 0};

uint64_t addrFromCoordMacroTiled(uint32_t x, uint32_t y, uint32_t slice, uint32_t sample, uint32_t bpp, uint32_t pitch,
                                 uint32_t height, uint32_t numSamples, uint32_t tileMode, bool isDepth,
                                 uint32_t pipeSwizzle, uint32_t bankSwizzle) {
    const uint32_t thickness = surfaceThickness(tileMode);
    const uint32_t microTileBits = numSamples * bpp * (thickness * 64);
    const uint32_t microTileBytes = (microTileBits + 7) / 8;
    const uint32_t pixelIndex = pixelIndexWithinMicroTile(x, y, slice, bpp, tileMode, isDepth);
    const uint32_t bytesPerSample = microTileBytes / numSamples;

    uint32_t sampleOffset;
    uint32_t pixelOffset;
    if (isDepth) {
        sampleOffset = bpp * sample;
        pixelOffset = numSamples * bpp * pixelIndex;
    } else {
        sampleOffset = sample * (microTileBits / numSamples);
        pixelOffset = bpp * pixelIndex;
    }

    uint32_t elemOffset = pixelOffset + sampleOffset;
    uint32_t numSampleSplits;
    uint32_t sampleSlice;
    if (numSamples <= 1 || microTileBytes <= 2048) {
        numSampleSplits = 1;
        sampleSlice = 0;
    } else {
        const uint32_t samplesPerSlice = 2048 / bytesPerSample;
        numSampleSplits = numSamples / samplesPerSlice;
        numSamples = samplesPerSlice;
        const uint32_t tileSliceBits = microTileBits / numSampleSplits;
        sampleSlice = elemOffset / tileSliceBits;
        elemOffset %= tileSliceBits;
    }
    elemOffset = (elemOffset + 7) / 8;

    uint32_t pipe = pipeFromCoordWoRotation(x, y);
    uint32_t bank = bankFromCoordWoRotation(x, y);
    const uint32_t swizzle = pipeSwizzle + 2 * bankSwizzle;
    uint32_t bankPipe = pipe + 2 * bank;
    const uint32_t rotation = surfaceRotationFromTileMode(tileMode);
    uint32_t sliceIn = slice;
    if (isThickMacroTiled(tileMode)) {
        sliceIn >>= 2;
    }
    bankPipe ^= (2 * sampleSlice * 3) ^ (swizzle + sliceIn * rotation);
    bankPipe %= 8;
    pipe = bankPipe % 2;
    bank = bankPipe / 2;

    const uint32_t sliceBytes = (height * pitch * thickness * bpp * numSamples + 7) / 8;
    const uint32_t sliceOffset = sliceBytes * (sampleSlice + numSampleSplits * slice) / thickness;

    uint32_t macroTilePitch = 32;
    uint32_t macroTileHeight = 16;
    if (tileMode == Tiled2DThin2 || tileMode == Tiled2BThin2) {
        macroTilePitch = 16;
        macroTileHeight = 32;
    } else if (tileMode == Tiled2DThin4 || tileMode == Tiled2BThin4) {
        macroTilePitch = 8;
        macroTileHeight = 64;
    }

    const uint32_t macroTilesPerRow = pitch / macroTilePitch;
    const uint32_t macroTileBytes = (numSamples * thickness * bpp * macroTileHeight * macroTilePitch + 7) / 8;
    const uint32_t macroTileIndexX = x / macroTilePitch;
    const uint32_t macroTileIndexY = y / macroTileHeight;
    const uint64_t macroTileOffset = uint64_t(macroTileIndexX + macroTilesPerRow * macroTileIndexY) * macroTileBytes;

    if (isBankSwappedTileMode(tileMode)) {
        const uint32_t bankSwapWidth = surfaceBankSwappedWidth(tileMode, bpp, 1, pitch);
        if (bankSwapWidth) {
            const uint32_t swapIndex = macroTilePitch * macroTileIndexX / bankSwapWidth;
            bank ^= kBankSwapOrder[swapIndex & 3];
        }
    }

    const uint64_t totalOffset = elemOffset + ((macroTileOffset + sliceOffset) >> 3);
    const int64_t highPart = static_cast<int64_t>(static_cast<int32_t>(totalOffset) & -256);
    return (uint64_t(bank) << 9) | (uint64_t(pipe) << 8) | (totalOffset & 255) | (uint64_t(highPart) << 3);
}

uint32_t surfaceTileSlices(uint32_t tileMode, uint32_t bpp, uint32_t numSamples) {
    const uint32_t bytesPerSample = ((bpp << 6) + 7) >> 3;
    uint32_t tileSlices = 1;
    if (surfaceThickness(tileMode) > 1) {
        numSamples = 4;
    }
    if (bytesPerSample != 0) {
        const uint32_t samplesPerTile = 2048 / bytesPerSample;
        if (samplesPerTile < numSamples) {
            tileSlices = std::max<uint32_t>(1, numSamples / samplesPerTile);
        }
    }
    return tileSlices;
}

uint32_t convertToNonBankSwappedMode(uint32_t tileMode) {
    switch (tileMode) {
    case 8: return 4;
    case 9: return 5;
    case 10: return 6;
    case 11: return 7;
    case 14: return 12;
    case 15: return 13;
    default: return tileMode;
    }
}

uint32_t surfaceMipLevelTileMode(uint32_t baseTileMode, uint32_t bpp, uint32_t level, uint32_t width, uint32_t height,
                                 uint32_t numSlices, uint32_t numSamples, uint32_t isDepth, bool noRecursive) {
    uint32_t widthAlignFactor = 1;
    uint32_t macroTileWidth = 32;
    uint32_t macroTileHeight = 16;
    const uint32_t tileSlices = surfaceTileSlices(baseTileMode, bpp, numSamples);
    uint32_t expTileMode = baseTileMode;

    if (numSamples > 1 || tileSlices > 1 || isDepth != 0) {
        if (baseTileMode == 7) {
            expTileMode = 4;
        } else if (baseTileMode == 13) {
            expTileMode = 12;
        } else if (baseTileMode == 11) {
            expTileMode = 8;
        } else if (baseTileMode == 15) {
            expTileMode = 14;
        }
    }

    if (baseTileMode == 2 && numSamples > 1) {
        expTileMode = 4;
    } else if (baseTileMode == 3) {
        if (numSamples > 1 || isDepth != 0) {
            expTileMode = 2;
        }
        if (numSamples == 2 || numSamples == 4) {
            expTileMode = 7;
        }
    } else {
        expTileMode = baseTileMode;
    }

    if (noRecursive || level == 0) {
        return expTileMode;
    }

    if (bpp == 24 || bpp == 48 || bpp == 96) {
        bpp /= 3;
    }

    const uint32_t widtha = nextPow2(width);
    const uint32_t heighta = nextPow2(height);
    const uint32_t numSlicesa = nextPow2(numSlices);

    expTileMode = convertToNonBankSwappedMode(expTileMode);
    const uint32_t thickness = surfaceThickness(expTileMode);
    const uint32_t microTileBytes = (numSamples * bpp * (thickness << 6) + 7) >> 3;

    if (microTileBytes < 256) {
        widthAlignFactor = std::max<uint32_t>(1, 256 / microTileBytes);
    }
    if (expTileMode == 4 || expTileMode == 12) {
        if (widtha < widthAlignFactor * macroTileWidth || heighta < macroTileHeight) {
            expTileMode = 2;
        }
    } else if (expTileMode == 5) {
        macroTileWidth = 16;
        macroTileHeight = 32;
        if (widtha < widthAlignFactor * macroTileWidth || heighta < macroTileHeight) {
            expTileMode = 2;
        }
    } else if (expTileMode == 6) {
        macroTileWidth = 8;
        macroTileHeight = 64;
        if (widtha < widthAlignFactor * macroTileWidth || heighta < macroTileHeight) {
            expTileMode = 2;
        }
    } else if (expTileMode == 7 || expTileMode == 13) {
        if (widtha < widthAlignFactor * macroTileWidth || heighta < macroTileHeight) {
            expTileMode = 3;
        }
    }

    if (numSlicesa < 4) {
        if (expTileMode == 3) {
            expTileMode = 2;
        } else if (expTileMode == 7) {
            expTileMode = 4;
        } else if (expTileMode == 13) {
            expTileMode = 12;
        }
    }

    return surfaceMipLevelTileMode(expTileMode, bpp, level, widtha, heighta, numSlicesa, numSamples, isDepth, true);
}

uint32_t adjustPitchAlignment(uint32_t flags, uint32_t pitchAlign) {
    if ((flags >> 13) & 1) {
        pitchAlign = powTwoAlign(pitchAlign, 0x20);
    }
    return pitchAlign;
}

void padDimensions(Expanded& exp, uint32_t tileMode, uint32_t padDims, uint32_t isCube, uint32_t pitchAlign,
                   uint32_t heightAlign, uint32_t sliceAlign) {
    const uint32_t thickness = surfaceThickness(tileMode);
    if (padDims == 0) {
        padDims = 3;
    }
    if ((pitchAlign & (pitchAlign - 1)) == 0) {
        exp.pitch = powTwoAlign(exp.pitch, pitchAlign);
    } else {
        exp.pitch += pitchAlign - 1;
        exp.pitch /= pitchAlign;
        exp.pitch *= pitchAlign;
    }
    if (padDims > 1) {
        exp.height = powTwoAlign(exp.height, heightAlign);
    }
    if (padDims > 2 || thickness > 1) {
        if (isCube != 0) {
            exp.slices = nextPow2(exp.slices);
        }
        if (thickness > 1) {
            exp.slices = powTwoAlign(exp.slices, sliceAlign);
        }
    }
}

void alignmentsLinear(uint32_t tileMode, uint32_t bpp, uint32_t flags, uint32_t& baseAlign, uint32_t& pitchAlign,
                      uint32_t& heightAlign) {
    if (tileMode == 0) {
        baseAlign = 1;
        pitchAlign = bpp != 1 ? 1 : 8;
        heightAlign = 1;
    } else if (tileMode == 1) {
        const uint32_t pixelsPerPipeInterleave = 2048 / bpp;
        baseAlign = 256;
        pitchAlign = std::max<uint32_t>(0x40, pixelsPerPipeInterleave);
        heightAlign = 1;
    } else {
        baseAlign = 1;
        pitchAlign = 1;
        heightAlign = 1;
    }
    pitchAlign = adjustPitchAlignment(flags, pitchAlign);
}

void alignmentsMicroTiled(uint32_t tileMode, uint32_t bpp, uint32_t flags, uint32_t numSamples, uint32_t& baseAlign,
                          uint32_t& pitchAlign, uint32_t& heightAlign) {
    if (bpp == 24 || bpp == 48 || bpp == 96) {
        bpp /= 3;
    }
    const uint32_t thickness = surfaceThickness(tileMode);
    baseAlign = 256;
    pitchAlign = std::max<uint32_t>(8, 256 / bpp / numSamples / thickness);
    heightAlign = 8;
    pitchAlign = adjustPitchAlignment(flags, pitchAlign);
}

void alignmentsMacroTiled(uint32_t tileMode, uint32_t bpp, uint32_t flags, uint32_t numSamples, uint32_t& baseAlign,
                          uint32_t& pitchAlign, uint32_t& heightAlign) {
    const uint32_t aspectRatio = macroTileAspectRatio(tileMode);
    const uint32_t thickness = surfaceThickness(tileMode);
    if (bpp == 24 || bpp == 48 || bpp == 96) {
        bpp /= 3;
    } else if (bpp == 3) {
        bpp = 1;
    }
    const uint32_t macroTileWidth = 32 / aspectRatio;
    const uint32_t macroTileHeight = aspectRatio * 16;
    pitchAlign = std::max(macroTileWidth, macroTileWidth * (256 / bpp / (8 * thickness) / numSamples));
    pitchAlign = adjustPitchAlignment(flags, pitchAlign);
    heightAlign = macroTileHeight;
    const uint32_t macroTileBytes = numSamples * ((bpp * macroTileHeight * macroTileWidth + 7) >> 3);
    if (thickness == 1) {
        baseAlign = std::max(macroTileBytes, (numSamples * heightAlign * bpp * pitchAlign + 7) >> 3);
    } else {
        baseAlign = std::max<uint32_t>(256, (4 * heightAlign * bpp * pitchAlign + 7) >> 3);
    }
    const uint32_t microTileBytes = (thickness * numSamples * (bpp << 6) + 7) >> 3;
    const uint32_t numSlicesPerMicroTile = microTileBytes < 2048 ? 1 : microTileBytes / 2048;
    baseAlign /= numSlicesPerMicroTile;
}

Layout surfaceInfoLinear(uint32_t tileMode, uint32_t bpp, uint32_t numSamples, uint32_t pitch, uint32_t height,
                         uint32_t numSlices, uint32_t mipLevel, uint32_t padDims, uint32_t flags) {
    Expanded exp{pitch, height, numSlices};
    const uint32_t thickness = surfaceThickness(tileMode);
    uint32_t baseAlign, pitchAlign, heightAlign;
    alignmentsLinear(tileMode, bpp, flags, baseAlign, pitchAlign, heightAlign);

    if (((flags >> 9) & 1) && mipLevel == 0) {
        exp.pitch /= 3;
        exp.pitch = nextPow2(exp.pitch);
    }
    if (mipLevel != 0) {
        exp.pitch = nextPow2(exp.pitch);
        exp.height = nextPow2(exp.height);
        if ((flags >> 4) & 1) {
            exp.slices = numSlices;
            padDims = numSlices <= 1 ? 2 : 0;
        } else {
            exp.slices = nextPow2(numSlices);
        }
    }

    padDimensions(exp, tileMode, padDims, (flags >> 4) & 1, pitchAlign, heightAlign, thickness);

    if (((flags >> 9) & 1) && mipLevel == 0) {
        exp.pitch *= 3;
    }

    const uint32_t slices = exp.slices * numSamples / thickness;
    Layout out;
    out.valid = 1;
    out.pitch = exp.pitch;
    out.height = exp.height;
    out.slices = exp.slices;
    out.surfSize = (exp.height * exp.pitch * slices * bpp * numSamples + 7) / 8;
    out.tileMode = tileMode;
    out.baseAlign = baseAlign;
    out.pitchAlign = pitchAlign;
    out.heightAlign = heightAlign;
    out.depthAlign = thickness;
    return out;
}

Layout surfaceInfoMicroTiled(uint32_t tileMode, uint32_t bpp, uint32_t numSamples, uint32_t pitch, uint32_t height,
                             uint32_t numSlices, uint32_t mipLevel, uint32_t padDims, uint32_t flags) {
    Expanded exp{pitch, height, numSlices};
    uint32_t expTileMode = tileMode;
    uint32_t thickness = surfaceThickness(tileMode);

    if (mipLevel != 0) {
        exp.pitch = nextPow2(pitch);
        exp.height = nextPow2(height);
        if ((flags >> 4) & 1) {
            exp.slices = numSlices;
            padDims = numSlices <= 1 ? 2 : 0;
        } else {
            exp.slices = nextPow2(numSlices);
        }
        if (expTileMode == 3 && exp.slices < 4) {
            expTileMode = 2;
            thickness = 1;
        }
    }

    uint32_t baseAlign, pitchAlign, heightAlign;
    alignmentsMicroTiled(expTileMode, bpp, flags, numSamples, baseAlign, pitchAlign, heightAlign);
    padDimensions(exp, expTileMode, padDims, (flags >> 4) & 1, pitchAlign, heightAlign, thickness);

    Layout out;
    out.valid = 1;
    out.pitch = exp.pitch;
    out.height = exp.height;
    out.slices = exp.slices;
    out.surfSize = (exp.height * exp.pitch * exp.slices * bpp * numSamples + 7) / 8;
    out.tileMode = expTileMode;
    out.baseAlign = baseAlign;
    out.pitchAlign = pitchAlign;
    out.heightAlign = heightAlign;
    out.depthAlign = thickness;
    return out;
}

Layout surfaceInfoMacroTiled(uint32_t tileMode, uint32_t baseTileMode, uint32_t bpp, uint32_t numSamples,
                             uint32_t pitch, uint32_t height, uint32_t numSlices, uint32_t mipLevel, uint32_t padDims,
                             uint32_t flags) {
    Expanded exp{pitch, height, numSlices};
    uint32_t expTileMode = tileMode;
    uint32_t thickness = surfaceThickness(tileMode);

    if (mipLevel != 0) {
        exp.pitch = nextPow2(pitch);
        exp.height = nextPow2(height);
        if ((flags >> 4) & 1) {
            exp.slices = numSlices;
            padDims = numSlices <= 1 ? 2 : 0;
        } else {
            exp.slices = nextPow2(numSlices);
        }
        if (expTileMode == 7 && exp.slices < 4) {
            expTileMode = 4;
            thickness = 1;
        }
    }

    uint32_t baseAlign, pitchAlign, heightAlign;
    const auto macroLayout = [&](uint32_t mode) {
        alignmentsMacroTiled(mode, bpp, flags, numSamples, baseAlign, pitchAlign, heightAlign);
        const uint32_t bankSwappedWidth = surfaceBankSwappedWidth(mode, bpp, numSamples, pitch);
        if (bankSwappedWidth > pitchAlign) {
            pitchAlign = bankSwappedWidth;
        }
        padDimensions(exp, mode, padDims, (flags >> 4) & 1, pitchAlign, heightAlign, thickness);
        Layout out;
        out.valid = 1;
        out.pitch = exp.pitch;
        out.height = exp.height;
        out.slices = exp.slices;
        out.surfSize = (exp.height * exp.pitch * exp.slices * bpp * numSamples + 7) / 8;
        out.tileMode = expTileMode;
        out.baseAlign = baseAlign;
        out.pitchAlign = pitchAlign;
        out.heightAlign = heightAlign;
        out.depthAlign = thickness;
        return out;
    };

    if (tileMode == baseTileMode || mipLevel == 0 || !isThickMacroTiled(baseTileMode) || isThickMacroTiled(tileMode)) {
        return macroLayout(tileMode);
    }

    alignmentsMacroTiled(baseTileMode, bpp, flags, numSamples, baseAlign, pitchAlign, heightAlign);
    const uint32_t pitchAlignFactor = std::max<uint32_t>(1, 32 / bpp);
    if (exp.pitch < pitchAlign * pitchAlignFactor || exp.height < heightAlign) {
        return surfaceInfoMicroTiled(2, bpp, numSamples, pitch, height, numSlices, mipLevel, padDims, flags);
    }
    return macroLayout(tileMode);
}

struct Computation {
    SurfaceIn in;
    SurfaceInfo out;
};

uint32_t hwlComputeMipLevel(SurfaceIn& in) {
    if (49 <= in.format && in.format <= 55) {
        if (in.mipLevel != 0) {
            uint32_t width = in.width;
            uint32_t height = in.height;
            uint32_t slices = in.numSlices;
            if ((in.flags >> 12) & 1) {
                const uint32_t widtha = width >> in.mipLevel;
                const uint32_t heighta = height >> in.mipLevel;
                if (((in.flags >> 4) & 1) == 0) {
                    slices >>= in.mipLevel;
                }
                width = std::max<uint32_t>(1, widtha);
                height = std::max<uint32_t>(1, heighta);
                slices = std::max<uint32_t>(1, slices);
            }
            in.width = nextPow2(width);
            in.height = nextPow2(height);
            in.numSlices = slices;
        }
        return 1;
    }
    return 0;
}

void computeMipLevel(SurfaceIn& in) {
    if (49 <= in.format && in.format <= 55 && (in.mipLevel == 0 || ((in.flags >> 12) & 1))) {
        in.width = powTwoAlign(in.width, 4);
        in.height = powTwoAlign(in.height, 4);
    }
    const uint32_t handled = hwlComputeMipLevel(in);
    if (!handled && in.mipLevel != 0 && ((in.flags >> 12) & 1)) {
        uint32_t width = std::max<uint32_t>(1, in.width >> in.mipLevel);
        uint32_t height = std::max<uint32_t>(1, in.height >> in.mipLevel);
        uint32_t slices = std::max<uint32_t>(1, in.numSlices);
        if (((in.flags >> 4) & 1) == 0) {
            slices = std::max<uint32_t>(1, slices >> in.mipLevel);
        }
        if (in.format != 47 && in.format != 48) {
            width = nextPow2(width);
            height = nextPow2(height);
            slices = nextPow2(slices);
        }
        in.width = width;
        in.height = height;
        in.numSlices = slices;
    }
}

uint32_t adjustSurfaceInfo(SurfaceIn& in, uint32_t elemMode, uint32_t expandX, uint32_t expandY, uint32_t bpp,
                           uint32_t width, uint32_t height) {
    bool bcn = false;
    if (elemMode >= 9 && elemMode <= 13 && bpp != 0) {
        bcn = true;
    }
    if (width != 0 && height != 0 && (expandX > 1 || expandY > 1)) {
        uint32_t widtha, heighta;
        if (elemMode == 4) {
            widtha = expandX * width;
            heighta = expandY * height;
        } else if (bcn) {
            widtha = width / expandX;
            heighta = height / expandY;
        } else {
            widtha = (width + expandX - 1) / expandX;
            heighta = (height + expandY - 1) / expandY;
        }
        in.width = std::max<uint32_t>(1, widtha);
        in.height = std::max<uint32_t>(1, heighta);
    }
    if (bpp != 0) {
        switch (elemMode) {
        case 4: in.bpp = bpp / expandX / expandY; break;
        case 5: case 6: in.bpp = expandY * expandX * bpp; break;
        case 9: case 12: in.bpp = 64; break;
        case 10: case 11: case 13: in.bpp = 128; break;
        default: in.bpp = bpp; break;
        }
        return in.bpp;
    }
    return 0;
}

void restoreSurfaceInfo(SurfaceInfo& out, uint32_t elemMode, uint32_t expandX, uint32_t expandY) {
    if (out.pixelPitch != 0 && out.pixelHeight != 0) {
        uint32_t width = out.pixelPitch;
        uint32_t height = out.pixelHeight;
        if (expandX > 1 || expandY > 1) {
            if (elemMode == 4) {
                width /= expandX;
                height /= expandY;
            } else {
                width *= expandX;
                height *= expandY;
            }
        }
        out.pixelPitch = std::max<uint32_t>(1, width);
        out.pixelHeight = std::max<uint32_t>(1, height);
    }
}

bool computeSurfaceInfoEx(SurfaceIn& in, SurfaceInfo& out) {
    uint32_t tileMode = in.tileMode;
    const uint32_t bpp = in.bpp;
    const uint32_t numSamples = std::max<uint32_t>(1, in.numSamples);
    const uint32_t pitch = in.width;
    const uint32_t height = in.height;
    const uint32_t numSlices = in.numSlices;
    const uint32_t mipLevel = in.mipLevel;
    const uint32_t flags = in.flags;
    uint32_t padDims = 0;
    const uint32_t baseTileMode = tileMode;

    if (((flags >> 4) & 1) && mipLevel == 0) {
        padDims = 2;
    }

    if ((flags >> 6) & 1) {
        tileMode = convertToNonBankSwappedMode(tileMode);
    } else {
        tileMode = surfaceMipLevelTileMode(tileMode, bpp, mipLevel, pitch, height, numSlices, numSamples,
                                           (flags >> 1) & 1, false);
    }

    Layout layout;
    switch (tileMode) {
    case 0:
    case 1:
        layout = surfaceInfoLinear(tileMode, bpp, numSamples, pitch, height, numSlices, mipLevel, padDims, flags);
        layout.tileMode = tileMode;
        break;
    case 2:
    case 3:
        layout = surfaceInfoMicroTiled(tileMode, bpp, numSamples, pitch, height, numSlices, mipLevel, padDims, flags);
        break;
    default:
        if (tileMode >= 4 && tileMode <= 15) {
            layout = surfaceInfoMacroTiled(tileMode, baseTileMode, bpp, numSamples, pitch, height, numSlices, mipLevel,
                                           padDims, flags);
        }
        break;
    }

    out.pitch = layout.pitch;
    out.height = layout.height;
    out.depth = layout.slices;
    out.tileMode = layout.tileMode;
    out.surfSize = layout.surfSize;
    out.baseAlign = layout.baseAlign;
    out.pitchAlign = layout.pitchAlign;
    out.heightAlign = layout.heightAlign;
    out.depthAlign = layout.depthAlign;
    return layout.valid != 0;
}

void computeSurfaceInfo(SurfaceIn& in, SurfaceInfo& out) {
    if (in.bpp > 0x80) {
        return;
    }
    computeMipLevel(in);

    const uint32_t width = in.width;
    const uint32_t height = in.height;
    uint32_t bpp = in.bpp;
    uint32_t expandX = 1;
    uint32_t expandY = 1;
    uint32_t elemMode = 0;
    out.pixelBits = in.bpp;

    if (in.format != 0) {
        bpp = kFormatExInfo[in.format * 4];
        expandX = kFormatExInfo[in.format * 4 + 1];
        expandY = kFormatExInfo[in.format * 4 + 2];
        elemMode = kFormatExInfo[in.format * 4 + 3];
        if (elemMode == 4 && expandX == 3 && in.tileMode == 1) {
            in.flags |= 0x200;
        }
        adjustSurfaceInfo(in, elemMode, expandX, expandY, bpp, width, height);
    } else if (in.bpp != 0) {
        in.width = std::max<uint32_t>(1, in.width);
        in.height = std::max<uint32_t>(1, in.height);
    } else {
        return;
    }

    if (!computeSurfaceInfoEx(in, out)) {
        return;
    }

    out.bpp = in.bpp;
    out.pixelPitch = out.pitch;
    out.pixelHeight = out.height;
    if (in.format != 0 && (((in.flags >> 9) & 1) == 0 || in.mipLevel == 0)) {
        restoreSurfaceInfo(out, elemMode, expandX, expandY);
    }

    if ((in.flags >> 5) & 1) {
        out.sliceSize = static_cast<uint32_t>(out.surfSize);
    } else if (out.depth != 0) {
        out.sliceSize = static_cast<uint32_t>(out.surfSize / out.depth);
        if (in.slice == in.numSlices - 1 && in.numSlices > 1) {
            out.sliceSize += out.sliceSize * (out.depth - in.numSlices);
        }
    }
}

} // namespace

bool isBcn(uint32_t surfaceFormat) {
    const uint32_t hw = surfaceFormat & 0x3F;
    return hw >= 0x31 && hw <= 0x35;
}

SurfaceInfo getSurfaceInfo(uint32_t surfaceFormat, uint32_t width, uint32_t height, uint32_t depth, uint32_t dim,
                           uint32_t tileMode, uint32_t aa, uint32_t level) {
    SurfaceInfo out;
    const uint32_t hwFormat = surfaceFormat & 0x3F;

    if (tileMode == 16) {
        const uint32_t numSamples = 1u << aa;
        const uint32_t blockSize = (hwFormat < 0x31 || hwFormat > 0x35) ? 1 : 4;
        const uint32_t alignedWidth = ~(blockSize - 1) & (std::max<uint32_t>(1, width >> level) + blockSize - 1);
        out.bpp = kFormatHwInfo[hwFormat * 4];
        out.pitch = alignedWidth / blockSize;
        out.pixelBits = kFormatHwInfo[hwFormat * 4];
        out.baseAlign = 1;
        out.pitchAlign = 1;
        out.heightAlign = 1;
        out.depthAlign = 1;
        switch (dim) {
        case 0: out.height = 1; out.depth = 1; break;
        case 1: case 6: out.height = std::max<uint32_t>(1, height >> level); out.depth = 1; break;
        case 2:
            out.height = std::max<uint32_t>(1, height >> level);
            out.depth = std::max<uint32_t>(1, depth >> level);
            break;
        case 3: out.height = std::max<uint32_t>(1, height >> level); out.depth = std::max<uint32_t>(6, depth); break;
        case 4: out.height = 1; out.depth = depth; break;
        case 5: case 7: out.height = std::max<uint32_t>(1, height >> level); out.depth = depth; break;
        default: break;
        }
        out.pixelPitch = alignedWidth;
        out.pixelHeight = ~(blockSize - 1) & (out.height + blockSize - 1);
        out.height = out.pixelHeight / blockSize;
        out.surfSize = (uint64_t(out.bpp) * numSamples * out.depth * out.height * out.pitch) >> 3;
        out.sliceSize = dim == 2 ? uint32_t(out.surfSize) : uint32_t(out.depth ? out.surfSize / out.depth : 0);
    } else {
        SurfaceIn in;
        in.tileMode = tileMode & 0x0F;
        in.format = hwFormat;
        in.bpp = kFormatHwInfo[hwFormat * 4];
        in.numSamples = 1u << aa;
        in.width = std::max<uint32_t>(1, width >> level);
        switch (dim) {
        case 0: in.height = 1; in.numSlices = 1; break;
        case 1: case 6: in.height = std::max<uint32_t>(1, height >> level); in.numSlices = 1; break;
        case 2:
            in.height = std::max<uint32_t>(1, height >> level);
            in.numSlices = std::max<uint32_t>(1, depth >> level);
            break;
        case 3:
            in.height = std::max<uint32_t>(1, height >> level);
            in.numSlices = std::max<uint32_t>(6, depth);
            in.flags |= 0x10;
            break;
        case 4: in.height = 1; in.numSlices = depth; break;
        case 5: case 7: in.height = std::max<uint32_t>(1, height >> level); in.numSlices = depth; break;
        default: break;
        }
        in.slice = 0;
        in.mipLevel = level;
        if (dim == 2) {
            in.flags |= 0x20;
        }
        if (level == 0) {
            in.flags = (1u << 12) | (in.flags & 0xFFFFEFFF);
        } else {
            in.flags &= 0xFFFFEFFF;
        }
        computeSurfaceInfo(in, out);
    }
    if (out.tileMode == 0) {
        out.tileMode = 16;
    }
    return out;
}

std::vector<uint32_t> generateMipOffsets(uint32_t surfaceFormat, uint32_t width, uint32_t height, uint32_t tileMode,
                                         uint32_t mipCount) {
    std::vector<uint32_t> offsets;
    const SurfaceInfo base = getSurfaceInfo(surfaceFormat, width, height, 1, 1, tileMode, 0, 0);
    uint32_t mipSize = 0;
    for (uint32_t level = 1; level < mipCount; ++level) {
        const SurfaceInfo info = getSurfaceInfo(surfaceFormat, width, height, 1, 1, tileMode, 0, level);
        offsets.push_back(level == 1 ? uint32_t(base.surfSize) : mipSize);
        const uint32_t align = std::max<uint32_t>(1, info.baseAlign);
        mipSize = ((mipSize + align - 1) / align) * align;
        mipSize += uint32_t(info.surfSize);
    }
    return offsets;
}

std::vector<uint8_t> deswizzle(std::span<const uint8_t> data, uint32_t width, uint32_t height, uint32_t surfaceFormat,
                               uint32_t use, uint32_t tileMode, uint32_t swizzle, uint32_t pitch,
                               uint32_t bitsPerPixel, uint32_t slice, uint32_t aa) {
    const uint32_t bytesPerPixel = bitsPerPixel / 8;
    if (isBcn(surfaceFormat)) {
        width = (width + 3) / 4;
        height = (height + 3) / 4;
    }
    std::vector<uint8_t> result(uint64_t(width) * height * bytesPerPixel);

    const uint32_t pipeSwizzle = (swizzle >> 8) & 1;
    const uint32_t bankSwizzle = (swizzle >> 9) & 3;
    const uint32_t addrTileMode = tileMode == 16 ? 0 : tileMode;
    const bool isDepth = (use & 4) != 0;
    const uint32_t numSamples = 1u << aa;

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint64_t pos;
            if (addrTileMode == 0 || addrTileMode == 1) {
                pos = addrFromCoordLinear(x, y, slice, 0, bytesPerPixel, pitch, height, 1);
            } else if (addrTileMode == 2 || addrTileMode == 3) {
                pos = addrFromCoordMicroTiled(x, y, slice, bitsPerPixel, pitch, height, addrTileMode, isDepth);
            } else {
                pos = addrFromCoordMacroTiled(x, y, slice, 0, bitsPerPixel, pitch, height, numSamples, addrTileMode,
                                              isDepth, pipeSwizzle, bankSwizzle);
            }
            const uint64_t dst = (uint64_t(y) * width + x) * bytesPerPixel;
            if (pos + bytesPerPixel <= data.size()) {
                std::memcpy(result.data() + dst, data.data() + pos, bytesPerPixel);
            }
        }
    }
    return result;
}

} // namespace bfrass::tex::gx2
