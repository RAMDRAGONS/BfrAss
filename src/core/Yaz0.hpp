#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace bfrass {

// Nintendo's Yaz0 LZ container, used for .sbfres/.szs wrapped resources.
bool isYaz0(std::span<const uint8_t> data);
std::vector<uint8_t> decompressYaz0(std::span<const uint8_t> data);

} // namespace bfrass
