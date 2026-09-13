#include "texture/TegraSwizzle.hpp"

#include "core/BinaryReader.hpp"

#include <algorithm>
#include <cstring>

namespace bfrass::tex {

uint32_t defaultBlockHeight(uint32_t heightInBlocks) {
    const uint32_t heightAndHalf = heightInBlocks + heightInBlocks / 2;
    if (heightAndHalf >= 128) {
        return 16;
    }
    if (heightAndHalf >= 64) {
        return 8;
    }
    if (heightAndHalf >= 32) {
        return 4;
    }
    if (heightAndHalf >= 16) {
        return 2;
    }
    return 1;
}

uint32_t mipBlockHeight(uint32_t mipHeightInBlocks, uint32_t blockHeightMip0) {
    uint32_t blockHeight = std::max<uint32_t>(1, blockHeightMip0);
    while (blockHeight > 1 && mipHeightInBlocks <= (blockHeight / 2) * 8) {
        blockHeight /= 2;
    }
    return blockHeight;
}

uint64_t blockLinearSurfaceSize(uint32_t widthInBlocks, uint32_t heightInBlocks, uint32_t depth,
                                uint32_t bytesPerBlock, uint32_t blockHeight) {
    const uint64_t pitch = alignUp(uint64_t(widthInBlocks) * bytesPerBlock, 64);
    const uint64_t rows = alignUp(heightInBlocks, uint64_t(blockHeight) * 8);
    return pitch * rows * std::max<uint32_t>(1, depth);
}

namespace {

// Address of a block inside block-linear memory as described by the Tegra X1 TRM:
// blocks are grouped into 64 byte wide, 8 row tall "GOBs" which are themselves
// stacked block_height at a time.
uint64_t blockLinearAddress(uint32_t x, uint32_t y, uint32_t widthInBlocks, uint32_t bytesPerBlock,
                            uint32_t blockHeight) {
    const uint64_t gobsPerRow = divRoundUp(widthInBlocks * bytesPerBlock, 64);
    const uint64_t gobAddress = uint64_t(y / (8 * blockHeight)) * 512 * blockHeight * gobsPerRow +
                                uint64_t(x * bytesPerBlock / 64) * 512 * blockHeight +
                                uint64_t((y % (8 * blockHeight)) / 8) * 512;
    const uint64_t xb = uint64_t(x) * bytesPerBlock;
    return gobAddress + ((xb % 64) / 32) * 256 + ((y % 8) / 2) * 64 + ((xb % 32) / 16) * 32 + (y % 2) * 16 +
           (xb % 16);
}

} // namespace

std::vector<uint8_t> deswizzleBlockLinear(std::span<const uint8_t> source, uint32_t widthInBlocks,
                                          uint32_t heightInBlocks, uint32_t depth, uint32_t bytesPerBlock,
                                          uint32_t blockHeight) {
    depth = std::max<uint32_t>(1, depth);
    const uint64_t sliceSize = blockLinearSurfaceSize(widthInBlocks, heightInBlocks, 1, bytesPerBlock, blockHeight);
    const uint64_t outSlice = uint64_t(widthInBlocks) * heightInBlocks * bytesPerBlock;
    std::vector<uint8_t> out(outSlice * depth);
    for (uint32_t z = 0; z < depth; ++z) {
        const uint64_t base = z * sliceSize;
        for (uint32_t y = 0; y < heightInBlocks; ++y) {
            for (uint32_t x = 0; x < widthInBlocks; ++x) {
                const uint64_t src = base + blockLinearAddress(x, y, widthInBlocks, bytesPerBlock, blockHeight);
                const uint64_t dst = z * outSlice + (uint64_t(y) * widthInBlocks + x) * bytesPerBlock;
                if (src + bytesPerBlock <= source.size()) {
                    std::memcpy(out.data() + dst, source.data() + src, bytesPerBlock);
                }
            }
        }
    }
    return out;
}

std::vector<uint8_t> deswizzleLinear(std::span<const uint8_t> source, uint32_t widthInBlocks,
                                     uint32_t heightInBlocks, uint32_t depth, uint32_t bytesPerBlock) {
    depth = std::max<uint32_t>(1, depth);
    const uint64_t pitch = alignUp(uint64_t(widthInBlocks) * bytesPerBlock, 32);
    const uint64_t rowBytes = uint64_t(widthInBlocks) * bytesPerBlock;
    std::vector<uint8_t> out(rowBytes * heightInBlocks * depth);
    for (uint32_t z = 0; z < depth; ++z) {
        for (uint32_t y = 0; y < heightInBlocks; ++y) {
            const uint64_t src = (uint64_t(z) * heightInBlocks + y) * pitch;
            const uint64_t dst = (uint64_t(z) * heightInBlocks + y) * rowBytes;
            if (src < source.size()) {
                std::memcpy(out.data() + dst, source.data() + src, std::min<uint64_t>(rowBytes, source.size() - src));
            }
        }
    }
    return out;
}

} // namespace bfrass::tex
