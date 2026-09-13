#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace bfrass::tex::gx2 {

// Result of the GX2 address library surface computation for one mip level.
struct SurfaceInfo {
    uint32_t pitch = 0;
    uint32_t height = 0;
    uint32_t depth = 0;
    uint64_t surfSize = 0;
    uint32_t tileMode = 0;
    uint32_t baseAlign = 0;
    uint32_t pitchAlign = 0;
    uint32_t heightAlign = 0;
    uint32_t depthAlign = 0;
    uint32_t bpp = 0; // bits per element
    uint32_t pixelPitch = 0;
    uint32_t pixelHeight = 0;
    uint32_t pixelBits = 0;
    uint32_t sliceSize = 0;
};

bool isBcn(uint32_t surfaceFormat);

SurfaceInfo getSurfaceInfo(uint32_t surfaceFormat, uint32_t width, uint32_t height, uint32_t depth, uint32_t dim,
                           uint32_t tileMode, uint32_t aa, uint32_t level);

// Mip offsets as stored in GX2Surface::mipOffset when the file leaves them empty:
// entry 0 is the base image size, later entries are relative to the mip data.
std::vector<uint32_t> generateMipOffsets(uint32_t surfaceFormat, uint32_t width, uint32_t height, uint32_t tileMode,
                                         uint32_t mipCount);

// Reorders a tiled GX2 surface slice into row-major block order.
std::vector<uint8_t> deswizzle(std::span<const uint8_t> data, uint32_t width, uint32_t height, uint32_t surfaceFormat,
                               uint32_t use, uint32_t tileMode, uint32_t swizzle, uint32_t pitch,
                               uint32_t bitsPerPixel, uint32_t slice, uint32_t aa);

} // namespace bfrass::tex::gx2
