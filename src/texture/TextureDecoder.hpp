#pragma once

#include "texture/Texture.hpp"
#include "texture/TextureFormat.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace bfrass::tex {

struct DecodeOptions {
    // Route channels through the texture's swizzle (BNTX channel mapping / GX2 compSel)
    // so the image matches what the shader samples.
    bool applyChannelMap = true;
    // Two-channel signed textures are tangent space normal maps; derive the Z
    // component so the exported image is a conventional RGB normal map.
    bool reconstructNormalZ = true;
};

bool canDecode(const PixelFormat& format);

// Decodes tightly packed block data (already deswizzled) into RGBA8.
std::vector<uint8_t> decodeToRgba8(const PixelFormat& format, std::span<const uint8_t> data, uint32_t width,
                                   uint32_t height);

// Decodes one surface of a texture and applies the requested post-processing.
std::vector<uint8_t> decodeTextureSurface(const TextureResource& texture, uint32_t arrayIndex, uint32_t mip,
                                          const DecodeOptions& options);

void applyChannelMap(std::vector<uint8_t>& rgba, const ChannelMap& map);
void reconstructNormalZ(std::vector<uint8_t>& rgba);

bool isTwoChannelSigned(const PixelFormat& format);

namespace detail {
// Uncompressed layouts; returns false when the layout is not a raw pixel format.
bool decodeRawPixels(const PixelFormat& format, std::span<const uint8_t> data, uint32_t width, uint32_t height,
                     std::vector<uint8_t>& rgba);
} // namespace detail

} // namespace bfrass::tex
