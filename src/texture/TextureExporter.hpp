#pragma once

#include "texture/Texture.hpp"
#include "texture/TextureDecoder.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace bfrass::tex {

enum class ExportFormat { Png, Dds };

struct ExportOptions {
    ExportFormat format = ExportFormat::Png;
    DecodeOptions decode;
};

struct EncodedImage {
    std::vector<uint8_t> bytes;
    const char* extension = "png";
};

const char* extensionFor(ExportFormat format);

// PNG holds one decoded layer; DDS keeps every layer and mip, preserving block
// compression whenever DXGI has an equivalent format.
EncodedImage encodeTexture(const TextureResource& texture, const ExportOptions& options, uint32_t arrayIndex = 0);

// Writes `<directory>/<name>.<ext>` (plus `<name>_layerN.png` for extra PNG layers)
// and returns the primary file path.
std::filesystem::path exportTexture(const TextureResource& texture, const std::filesystem::path& directory,
                                    const ExportOptions& options);

// Runs fn(0..count-1) on a small thread pool sized to the hardware.
void parallelFor(size_t count, const std::function<void(size_t)>& fn);

} // namespace bfrass::tex
