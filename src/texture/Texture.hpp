#pragma once

#include "bfres/Common.hpp"
#include "texture/TextureFormat.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace bfrass::tex {

// Parameters of a BRTI (nn::gfx::ResTexture) needed to undo block-linear layout.
struct NxSurfaceInfo {
    uint32_t tileMode = 0;       // 0 optimal (block linear), 1 linear
    uint32_t textureLayout = 0;  // low 3 bits: block height log2 of mip 0
    uint32_t alignment = 0;
    uint32_t dataSize = 0;
    uint8_t imageDimension = 0;
    uint8_t storageDimension = 0;
    std::vector<uint64_t> mipOffsets; // relative to mip 0
};

// Parameters of a GX2Surface/GX2Texture from an FTEX block.
struct Gx2SurfaceInfo {
    uint32_t dim = 1;
    uint32_t depth = 1; // slices as stored in the surface, including array layers
    uint32_t format = 0;
    uint32_t aa = 0;
    uint32_t use = 0;
    uint32_t tileMode = 0;
    uint32_t swizzle = 0;
    uint32_t alignment = 0;
    uint32_t pitch = 0;
    uint32_t imageSize = 0;
    uint32_t mipSize = 0;
    std::array<uint32_t, 13> mipOffsets{};
    uint32_t viewFirstMip = 0, viewNumMips = 0, viewFirstSlice = 0, viewNumSlices = 0;
    std::array<uint32_t, 5> registers{};
};

struct TextureResource {
    std::string name;
    std::string path;
    bfres::Platform platform = bfres::Platform::Switch;
    std::string container; // e.g. "textures.bntx" or the FRES file name

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 1;
    uint32_t arrayCount = 1;
    uint32_t mipCount = 1;
    PixelFormat format;
    uint32_t rawFormat = 0;
    ChannelMap channels = kIdentityChannels;

    NxSurfaceInfo nx;
    Gx2SurfaceInfo gx2;

    // Views into the owning file buffer. Wii U textures split into .Tex1/.Tex2
    // files may only carry one of the two.
    std::span<const uint8_t> imageData;
    std::span<const uint8_t> mipData;
    std::shared_ptr<const std::vector<uint8_t>> owner;
    std::shared_ptr<const std::vector<uint8_t>> mipOwner;

    std::vector<bfres::UserData> userData;

    uint32_t mipWidth(uint32_t mip) const { return std::max<uint32_t>(1, width >> mip); }
    uint32_t mipHeight(uint32_t mip) const { return std::max<uint32_t>(1, height >> mip); }
    uint32_t availableMips() const;
};

// Returns linear (deswizzled) block data for one array layer / mip of a texture.
std::vector<uint8_t> extractSurface(const TextureResource& texture, uint32_t arrayIndex, uint32_t mip);

} // namespace bfrass::tex
