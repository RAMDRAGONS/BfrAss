#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace bfrass::tex {

std::vector<uint8_t> encodePng(std::span<const uint8_t> rgba, uint32_t width, uint32_t height);

struct DdsSurface {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> data;
};

// Mip chains per array layer, laid out as layers[array][mip].
struct DdsImage {
    uint32_t dxgiFormat = 0;
    bool cubeMap = false;
    std::vector<std::vector<DdsSurface>> layers;
};

std::vector<uint8_t> encodeDds(const DdsImage& image);

void writeFile(const std::filesystem::path& path, std::span<const uint8_t> bytes);

} // namespace bfrass::tex
