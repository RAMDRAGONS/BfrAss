#include "texture/ImageWriter.hpp"

#include "core/BinaryReader.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace bfrass::tex {

namespace {

void putBe32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v >> 24));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

void putLe32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 24));
}

void pngChunk(std::vector<uint8_t>& out, const char type[4], std::span<const uint8_t> payload) {
    putBe32(out, uint32_t(payload.size()));
    const size_t typeStart = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    const uLong crc = crc32(0L, out.data() + typeStart, uInt(4 + payload.size()));
    putBe32(out, uint32_t(crc));
}

uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    const int p = int(a) + int(b) - int(c);
    const int pa = std::abs(p - int(a));
    const int pb = std::abs(p - int(b));
    const int pc = std::abs(p - int(c));
    if (pa <= pb && pa <= pc) {
        return a;
    }
    return pb <= pc ? b : c;
}

} // namespace

std::vector<uint8_t> encodePng(std::span<const uint8_t> rgba, uint32_t width, uint32_t height) {
    const bool opaque = [&] {
        for (size_t i = 3; i < rgba.size(); i += 4) {
            if (rgba[i] != 255) {
                return false;
            }
        }
        return true;
    }();
    const unsigned channels = opaque ? 3 : 4;
    const size_t stride = size_t(width) * channels;

    // Per-row adaptive filtering (smallest sum of absolute residuals), as recommended by the PNG spec.
    std::vector<uint8_t> raw((stride + 1) * height);
    std::vector<uint8_t> previous(stride, 0), current(stride), candidate(stride), best(stride);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            std::memcpy(current.data() + size_t(x) * channels, rgba.data() + (size_t(y) * width + x) * 4, channels);
        }
        uint64_t bestScore = ~uint64_t(0);
        uint8_t bestFilter = 0;
        for (uint8_t filter = 0; filter < 5; ++filter) {
            uint64_t score = 0;
            for (size_t i = 0; i < stride; ++i) {
                const uint8_t left = i >= channels ? current[i - channels] : 0;
                const uint8_t up = previous[i];
                const uint8_t upLeft = i >= channels ? previous[i - channels] : 0;
                uint8_t v = current[i];
                switch (filter) {
                case 1: v = uint8_t(v - left); break;
                case 2: v = uint8_t(v - up); break;
                case 3: v = uint8_t(v - ((int(left) + int(up)) >> 1)); break;
                case 4: v = uint8_t(v - paeth(left, up, upLeft)); break;
                default: break;
                }
                candidate[i] = v;
                score += v < 128 ? v : 256 - v;
            }
            if (score < bestScore) {
                bestScore = score;
                bestFilter = filter;
                best.swap(candidate);
            }
        }
        raw[(stride + 1) * y] = bestFilter;
        std::memcpy(raw.data() + (stride + 1) * y + 1, best.data(), stride);
        previous.swap(current);
    }

    uLongf compressedSize = compressBound(uLong(raw.size()));
    std::vector<uint8_t> compressed(compressedSize);
    if (compress2(compressed.data(), &compressedSize, raw.data(), uLong(raw.size()), 6) != Z_OK) {
        throw FormatError("zlib compression failed");
    }
    compressed.resize(compressedSize);

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    putBe32(ihdr, width);
    putBe32(ihdr, height);
    ihdr.push_back(8);
    ihdr.push_back(opaque ? 2 : 6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    pngChunk(out, "IHDR", ihdr);
    pngChunk(out, "IDAT", compressed);
    pngChunk(out, "IEND", {});
    return out;
}

std::vector<uint8_t> encodeDds(const DdsImage& image) {
    if (image.layers.empty() || image.layers[0].empty()) {
        throw FormatError("DDS image without surfaces");
    }
    const auto& top = image.layers[0][0];
    const uint32_t mipCount = uint32_t(image.layers[0].size());
    const uint32_t layerCount = uint32_t(image.layers.size());

    uint32_t fourCC = 0;
    bool legacyRgba = false;
    switch (image.dxgiFormat) {
    case 71: fourCC = 0x31545844; break; // DXT1
    case 74: fourCC = 0x33545844; break; // DXT3
    case 77: fourCC = 0x35545844; break; // DXT5
    case 80: fourCC = 0x31495441; break; // ATI1
    case 83: fourCC = 0x32495441; break; // ATI2
    case 28: legacyRgba = true; break;
    default: fourCC = 0x30315844; break; // DX10
    }
    const bool dx10 = fourCC == 0x30315844 || (layerCount > 1 && !(image.cubeMap && layerCount == 6));
    if (dx10) {
        legacyRgba = false;
        fourCC = 0x30315844;
    }

    std::vector<uint8_t> out = {'D', 'D', 'S', ' '};
    const bool compressed = !legacyRgba;
    uint32_t flags = 0x1 | 0x2 | 0x4 | 0x1000;
    if (mipCount > 1) {
        flags |= 0x20000;
    }
    flags |= compressed ? 0x80000 : 0x8;
    putLe32(out, 124);
    putLe32(out, flags);
    putLe32(out, top.height);
    putLe32(out, top.width);
    putLe32(out, compressed ? uint32_t(top.data.size()) : top.width * 4);
    putLe32(out, 0);
    putLe32(out, mipCount);
    for (int i = 0; i < 11; ++i) {
        putLe32(out, 0);
    }
    putLe32(out, 32);
    if (legacyRgba) {
        putLe32(out, 0x40 | 0x1);
        putLe32(out, 0);
        putLe32(out, 32);
        putLe32(out, 0x000000FF);
        putLe32(out, 0x0000FF00);
        putLe32(out, 0x00FF0000);
        putLe32(out, 0xFF000000);
    } else {
        putLe32(out, 0x4);
        putLe32(out, fourCC);
        for (int i = 0; i < 5; ++i) {
            putLe32(out, 0);
        }
    }
    uint32_t caps = 0x1000;
    if (mipCount > 1) {
        caps |= 0x400000 | 0x8;
    }
    if (image.cubeMap || layerCount > 1) {
        caps |= 0x8;
    }
    putLe32(out, caps);
    putLe32(out, image.cubeMap ? 0xFE00 : 0);
    putLe32(out, 0);
    putLe32(out, 0);
    putLe32(out, 0);

    if (dx10) {
        putLe32(out, image.dxgiFormat);
        putLe32(out, 3);
        putLe32(out, image.cubeMap ? 0x4 : 0);
        putLe32(out, image.cubeMap ? std::max<uint32_t>(1, layerCount / 6) : layerCount);
        putLe32(out, 0);
    }

    for (const auto& layer : image.layers) {
        for (const auto& surface : layer) {
            out.insert(out.end(), surface.data.begin(), surface.data.end());
        }
    }
    return out;
}

void writeFile(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw FormatError("cannot open " + path.string() + " for writing");
    }
    stream.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!stream) {
        throw FormatError("failed writing " + path.string());
    }
}

} // namespace bfrass::tex
