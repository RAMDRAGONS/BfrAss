#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace bfrass::tex {

// Block height (in multiples of 8 block rows) the Tegra X1 driver picks for mip 0
// when a texture does not carry an explicit layout.
uint32_t defaultBlockHeight(uint32_t heightInBlocks);

// Block height used by a given mip level, derived from the mip 0 block height.
uint32_t mipBlockHeight(uint32_t mipHeightInBlocks, uint32_t blockHeightMip0);

// Size in bytes a block-linear surface occupies, including its GPU padding.
uint64_t blockLinearSurfaceSize(uint32_t widthInBlocks, uint32_t heightInBlocks, uint32_t depth,
                                uint32_t bytesPerBlock, uint32_t blockHeight);

// Converts one mip level from block-linear GPU layout to tightly packed rows.
std::vector<uint8_t> deswizzleBlockLinear(std::span<const uint8_t> source, uint32_t widthInBlocks,
                                          uint32_t heightInBlocks, uint32_t depth, uint32_t bytesPerBlock,
                                          uint32_t blockHeight);

// Converts a linear-tiled surface whose rows are padded to a 32 byte pitch.
std::vector<uint8_t> deswizzleLinear(std::span<const uint8_t> source, uint32_t widthInBlocks,
                                     uint32_t heightInBlocks, uint32_t depth, uint32_t bytesPerBlock);

} // namespace bfrass::tex
