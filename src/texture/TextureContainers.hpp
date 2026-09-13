#pragma once

#include "core/BinaryReader.hpp"
#include "core/Log.hpp"
#include "texture/Texture.hpp"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace bfrass::tex {

using SharedBuffer = std::shared_ptr<const std::vector<uint8_t>>;

bool isBntx(std::span<const uint8_t> data);

// Parses every BRTI texture of a BNTX archive. `bntx` must be a view into `owner`.
std::vector<TextureResource> parseBntx(const SharedBuffer& owner, std::span<const uint8_t> bntx,
                                       const std::string& containerName, const Log& log);

struct FtexContext {
    SharedBuffer owner;
    bfres::Version version;
    std::string containerName;
    // Wii U .Tex1 archives carry only base levels and .Tex2 archives only mips.
    bool loadImage = true;
    bool loadMips = true;
};

// Parses an FTEX block located at `offset` of a big-endian Wii U resource file.
TextureResource parseFtex(const BinaryReader& reader, uint64_t offset, const FtexContext& context, const Log& log);

} // namespace bfrass::tex
