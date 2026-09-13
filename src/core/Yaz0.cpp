#include "core/Yaz0.hpp"

#include "core/BinaryReader.hpp"

namespace bfrass {

bool isYaz0(std::span<const uint8_t> data) {
    return data.size() >= 16 && data[0] == 'Y' && data[1] == 'a' && data[2] == 'z' && data[3] == '0';
}

std::vector<uint8_t> decompressYaz0(std::span<const uint8_t> data) {
    if (!isYaz0(data)) {
        throw FormatError("not a Yaz0 stream");
    }
    const uint32_t outSize = (uint32_t(data[4]) << 24) | (uint32_t(data[5]) << 16) | (uint32_t(data[6]) << 8) | data[7];
    std::vector<uint8_t> out(outSize);

    size_t src = 16;
    size_t dst = 0;
    uint8_t groupHeader = 0;
    int groupBits = 0;
    while (dst < outSize) {
        if (groupBits == 0) {
            if (src >= data.size()) {
                throw FormatError("truncated Yaz0 stream");
            }
            groupHeader = data[src++];
            groupBits = 8;
        }
        if (groupHeader & 0x80) {
            if (src >= data.size()) {
                throw FormatError("truncated Yaz0 stream");
            }
            out[dst++] = data[src++];
        } else {
            if (src + 1 >= data.size()) {
                throw FormatError("truncated Yaz0 stream");
            }
            const uint8_t b1 = data[src++];
            const uint8_t b2 = data[src++];
            const size_t distance = ((size_t(b1 & 0x0F) << 8) | b2) + 1;
            size_t length = b1 >> 4;
            if (length == 0) {
                if (src >= data.size()) {
                    throw FormatError("truncated Yaz0 stream");
                }
                length = size_t(data[src++]) + 0x12;
            } else {
                length += 2;
            }
            if (distance > dst) {
                throw FormatError("corrupt Yaz0 back-reference");
            }
            // Overlapping copies are intentional in LZ77 runs, so copy byte by byte.
            for (size_t i = 0; i < length && dst < outSize; ++i, ++dst) {
                out[dst] = out[dst - distance];
            }
        }
        groupHeader <<= 1;
        --groupBits;
    }
    return out;
}

} // namespace bfrass
