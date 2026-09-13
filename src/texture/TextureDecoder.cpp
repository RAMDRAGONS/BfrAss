#include "texture/TextureDecoder.hpp"

#include "core/BinaryReader.hpp"
#include "texture/BlockDecoders.hpp"
#include "texture/Gx2Surface.hpp"
#include "texture/TegraSwizzle.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bfrass::tex {

uint32_t TextureResource::availableMips() const {
    if (platform == bfres::Platform::WiiU) {
        if (imageData.empty()) {
            return 0;
        }
        return mipData.empty() ? 1 : std::max<uint32_t>(1, mipCount);
    }
    return std::max<uint32_t>(1, mipCount);
}

namespace {

std::vector<uint8_t> extractSwitchSurface(const TextureResource& t, uint32_t arrayIndex, uint32_t mip) {
    const PixelFormat& fmt = t.format;
    const auto& info = fmt.info();
    const uint32_t layers = std::max<uint32_t>(1, t.arrayCount);
    const uint64_t totalSize = t.nx.dataSize ? t.nx.dataSize : t.imageData.size();
    const uint64_t layerSize = totalSize / layers;

    const uint32_t mipW = t.mipWidth(mip);
    const uint32_t mipH = t.mipHeight(mip);
    const uint32_t mipD = t.nx.storageDimension == 3 ? std::max<uint32_t>(1, t.depth >> mip) : 1;
    const uint32_t wBlocks = divRoundUp(mipW, info.blockWidth);
    const uint32_t hBlocks = divRoundUp(mipH, info.blockHeight);
    const uint32_t blockHeight0 =
        t.nx.tileMode == 0 ? (1u << (t.nx.textureLayout & 7)) : 1;

    uint64_t mipOffset = 0;
    uint64_t mipSize = 0;
    if (mip < t.nx.mipOffsets.size()) {
        mipOffset = t.nx.mipOffsets[mip];
        mipSize = (mip + 1 < t.nx.mipOffsets.size()) ? t.nx.mipOffsets[mip + 1] - mipOffset : layerSize - mipOffset;
    } else {
        // Offsets are normally present; rebuild them from the aligned surface sizes otherwise.
        const uint64_t alignment = std::max<uint32_t>(1, t.nx.alignment);
        uint64_t offset = 0;
        for (uint32_t m = 0; m <= mip; ++m) {
            const uint32_t w = divRoundUp(t.mipWidth(m), info.blockWidth);
            const uint32_t h = divRoundUp(t.mipHeight(m), info.blockHeight);
            const uint64_t size = t.nx.tileMode == 0
                                      ? blockLinearSurfaceSize(w, h, 1, info.bytesPerBlock, mipBlockHeight(h, blockHeight0))
                                      : alignUp(uint64_t(w) * info.bytesPerBlock, 32) * h;
            offset = alignUp(offset, alignment);
            if (m == mip) {
                mipOffset = offset;
                mipSize = size;
            }
            offset += size;
        }
    }

    const uint64_t start = arrayIndex * layerSize + mipOffset;
    if (start >= t.imageData.size()) {
        throw FormatError("texture '" + t.name + "' surface lies outside its data");
    }
    const auto source = t.imageData.subspan(start, std::min<uint64_t>(mipSize, t.imageData.size() - start));
    if (t.nx.tileMode == 1) {
        return deswizzleLinear(source, wBlocks, hBlocks, mipD, info.bytesPerBlock);
    }
    return deswizzleBlockLinear(source, wBlocks, hBlocks, mipD, info.bytesPerBlock,
                                mipBlockHeight(hBlocks, blockHeight0));
}

std::vector<uint8_t> extractWiiUSurface(const TextureResource& t, uint32_t arrayIndex, uint32_t mip) {
    const auto& g = t.gx2;
    const uint32_t depth = std::max<uint32_t>(1, g.depth);
    const gx2::SurfaceInfo base = gx2::getSurfaceInfo(g.format, t.width, t.height, depth, g.dim, g.tileMode, g.aa, 0);
    const gx2::SurfaceInfo level = gx2::getSurfaceInfo(g.format, t.width, t.height, depth, g.dim, g.tileMode, g.aa, mip);

    std::span<const uint8_t> source;
    if (mip == 0) {
        source = t.imageData;
    } else {
        std::vector<uint32_t> offsets(g.mipOffsets.begin(), g.mipOffsets.end());
        if (std::all_of(offsets.begin(), offsets.end(), [](uint32_t v) { return v == 0; })) {
            offsets = gx2::generateMipOffsets(g.format, t.width, t.height, g.tileMode, t.mipCount);
        }
        uint64_t offset = mip - 1 < offsets.size() ? offsets[mip - 1] : 0;
        if (mip == 1) {
            offset = offset >= base.surfSize ? offset - base.surfSize : 0;
        }
        if (offset >= t.mipData.size()) {
            throw FormatError("texture '" + t.name + "' mip " + std::to_string(mip) + " lies outside its mip data");
        }
        source = t.mipData.subspan(offset);
    }
    source = source.subspan(0, std::min<uint64_t>(source.size(), level.surfSize));
    return gx2::deswizzle(source, t.mipWidth(mip), t.mipHeight(mip), g.format, g.use, level.tileMode, g.swizzle,
                          level.pitch, level.bpp, arrayIndex, g.aa);
}

void decodeBlocks(const PixelFormat& format, std::span<const uint8_t> data, uint32_t width, uint32_t height,
                  std::vector<uint8_t>& rgba) {
    const auto& info = format.info();
    const uint32_t bw = info.blockWidth, bh = info.blockHeight;
    const uint32_t wBlocks = divRoundUp(width, bw);
    const uint32_t hBlocks = divRoundUp(height, bh);
    if (data.size() < uint64_t(wBlocks) * hBlocks * info.bytesPerBlock) {
        throw FormatError("texture data is shorter than its declared size");
    }
    rgba.assign(uint64_t(width) * height * 4, 0);
    const bool isSigned = format.type == DataType::SNorm || format.type == DataType::Float;

    uint8_t blockRgba[144 * 4];
    float values[16];
    float values2[16];
    float rgbFloat[48];

    for (uint32_t by = 0; by < hBlocks; ++by) {
        for (uint32_t bx = 0; bx < wBlocks; ++bx) {
            const uint8_t* block = data.data() + (uint64_t(by) * wBlocks + bx) * info.bytesPerBlock;
            auto toByte = [&](float v) {
                if (isSigned) {
                    v = v * 0.5f + 0.5f;
                }
                return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            };
            switch (format.layout) {
            case Layout::BC1: block::decodeBc1(block, blockRgba); break;
            case Layout::BC2: block::decodeBc2(block, blockRgba); break;
            case Layout::BC3: block::decodeBc3(block, blockRgba); break;
            case Layout::BC4:
                block::decodeBc4(block, values, isSigned);
                for (int i = 0; i < 16; ++i) {
                    blockRgba[i * 4] = toByte(values[i]);
                    blockRgba[i * 4 + 1] = isSigned ? 128 : 0;
                    blockRgba[i * 4 + 2] = isSigned ? 128 : 0;
                    blockRgba[i * 4 + 3] = 255;
                }
                break;
            case Layout::BC5:
                block::decodeBc4(block, values, isSigned);
                block::decodeBc4(block + 8, values2, isSigned);
                for (int i = 0; i < 16; ++i) {
                    blockRgba[i * 4] = toByte(values[i]);
                    blockRgba[i * 4 + 1] = toByte(values2[i]);
                    blockRgba[i * 4 + 2] = isSigned ? 128 : 0;
                    blockRgba[i * 4 + 3] = 255;
                }
                break;
            case Layout::BC6H:
                block::decodeBc6h(block, rgbFloat, format.type == DataType::Float);
                for (int i = 0; i < 16; ++i) {
                    for (int c = 0; c < 3; ++c) {
                        blockRgba[i * 4 + c] = uint8_t(std::clamp(rgbFloat[i * 3 + c], 0.0f, 1.0f) * 255.0f + 0.5f);
                    }
                    blockRgba[i * 4 + 3] = 255;
                }
                break;
            case Layout::BC7: block::decodeBc7(block, blockRgba); break;
            case Layout::Etc1:
            case Layout::Etc2: block::decodeEtc2Rgb(block, blockRgba, false); break;
            case Layout::Etc2Mask: block::decodeEtc2Rgb(block, blockRgba, true); break;
            case Layout::Etc2Alpha:
                block::decodeEtc2Rgb(block + 8, blockRgba, false);
                block::decodeEacAlpha(block, blockRgba);
                break;
            case Layout::EacR11:
                block::decodeEacR11(block, values, isSigned);
                for (int i = 0; i < 16; ++i) {
                    blockRgba[i * 4] = toByte(values[i]);
                    blockRgba[i * 4 + 1] = isSigned ? 128 : 0;
                    blockRgba[i * 4 + 2] = isSigned ? 128 : 0;
                    blockRgba[i * 4 + 3] = 255;
                }
                break;
            case Layout::EacR11G11:
                block::decodeEacR11(block, values, isSigned);
                block::decodeEacR11(block + 8, values2, isSigned);
                for (int i = 0; i < 16; ++i) {
                    blockRgba[i * 4] = toByte(values[i]);
                    blockRgba[i * 4 + 1] = toByte(values2[i]);
                    blockRgba[i * 4 + 2] = isSigned ? 128 : 0;
                    blockRgba[i * 4 + 3] = 255;
                }
                break;
            default:
                if (format.isAstc()) {
                    block::decodeAstc(block, bw, bh, blockRgba);
                    break;
                }
                throw FormatError("no decoder for " + format.name());
            }

            for (uint32_t y = 0; y < bh; ++y) {
                const uint32_t py = by * bh + y;
                if (py >= height) {
                    break;
                }
                for (uint32_t x = 0; x < bw; ++x) {
                    const uint32_t px = bx * bw + x;
                    if (px >= width) {
                        break;
                    }
                    std::memcpy(rgba.data() + (uint64_t(py) * width + px) * 4, blockRgba + (y * bw + x) * 4, 4);
                }
            }
        }
    }
}

} // namespace

std::vector<uint8_t> extractSurface(const TextureResource& texture, uint32_t arrayIndex, uint32_t mip) {
    if (texture.platform == bfres::Platform::Switch) {
        return extractSwitchSurface(texture, arrayIndex, mip);
    }
    return extractWiiUSurface(texture, arrayIndex, mip);
}

bool canDecode(const PixelFormat& format) {
    switch (format.layout) {
    case Layout::Undefined:
    case Layout::NV12:
    case Layout::Pvrtc1_2Bpp:
    case Layout::Pvrtc1_4Bpp:
    case Layout::Pvrtc1Alpha2Bpp:
    case Layout::Pvrtc1Alpha4Bpp:
    case Layout::Pvrtc2Alpha2Bpp:
    case Layout::Pvrtc2Alpha4Bpp:
        return false;
    default:
        return true;
    }
}

bool isTwoChannelSigned(const PixelFormat& format) {
    const bool twoChannel = format.layout == Layout::BC5 || format.layout == Layout::R8G8 ||
                            format.layout == Layout::R16G16 || format.layout == Layout::EacR11G11;
    return twoChannel && format.type == DataType::SNorm;
}

std::vector<uint8_t> decodeToRgba8(const PixelFormat& format, std::span<const uint8_t> data, uint32_t width,
                                   uint32_t height) {
    if (!canDecode(format)) {
        throw FormatError("unsupported texture format " + format.name());
    }
    std::vector<uint8_t> rgba;
    if (format.info().compressed) {
        decodeBlocks(format, data, width, height, rgba);
    } else if (!detail::decodeRawPixels(format, data, width, height, rgba)) {
        throw FormatError("unsupported texture format " + format.name());
    }
    return rgba;
}

void applyChannelMap(std::vector<uint8_t>& rgba, const ChannelMap& map) {
    if (map == kIdentityChannels) {
        return;
    }
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        const uint8_t src[4] = {rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]};
        for (int c = 0; c < 4; ++c) {
            switch (map[c]) {
            case ChannelSource::Red: rgba[i + c] = src[0]; break;
            case ChannelSource::Green: rgba[i + c] = src[1]; break;
            case ChannelSource::Blue: rgba[i + c] = src[2]; break;
            case ChannelSource::Alpha: rgba[i + c] = src[3]; break;
            case ChannelSource::Zero: rgba[i + c] = 0; break;
            case ChannelSource::One: rgba[i + c] = 255; break;
            }
        }
    }
}

void reconstructNormalZ(std::vector<uint8_t>& rgba) {
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        const float x = rgba[i] / 127.5f - 1.0f;
        const float y = rgba[i + 1] / 127.5f - 1.0f;
        const float z = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y));
        rgba[i + 2] = uint8_t(std::clamp(z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
}

std::vector<uint8_t> decodeTextureSurface(const TextureResource& texture, uint32_t arrayIndex, uint32_t mip,
                                          const DecodeOptions& options) {
    const std::vector<uint8_t> blocks = extractSurface(texture, arrayIndex, mip);
    std::vector<uint8_t> rgba = decodeToRgba8(texture.format, blocks, texture.mipWidth(mip), texture.mipHeight(mip));
    if (options.applyChannelMap) {
        applyChannelMap(rgba, texture.channels);
    }
    if (options.reconstructNormalZ && isTwoChannelSigned(texture.format)) {
        const ChannelSource blue = options.applyChannelMap ? texture.channels[2] : ChannelSource::Blue;
        if (blue == ChannelSource::Zero || blue == ChannelSource::One || blue == ChannelSource::Blue) {
            reconstructNormalZ(rgba);
        }
    }
    return rgba;
}

} // namespace bfrass::tex
