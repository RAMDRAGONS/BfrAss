#include "texture/TextureDecoder.hpp"

#include "core/Math.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bfrass::tex::detail {

namespace {

uint64_t readLe(const uint8_t* p, unsigned bytes) {
    uint64_t v = 0;
    for (unsigned i = 0; i < bytes; ++i) {
        v |= uint64_t(p[i]) << (8 * i);
    }
    return v;
}

// Normalises an integer channel of the given width according to the data type.
float normalizeChannel(uint64_t raw, unsigned bits, DataType type) {
    const uint64_t maxValue = bits >= 64 ? ~uint64_t(0) : ((uint64_t(1) << bits) - 1);
    raw &= maxValue;
    switch (type) {
    case DataType::SNorm:
    case DataType::SInt:
    case DataType::SIntToFloat: {
        const int64_t signBit = int64_t(1) << (bits - 1);
        int64_t v = int64_t(raw);
        if (v & signBit) {
            v -= int64_t(1) << bits;
        }
        return std::max(-1.0f, float(v) / float(signBit - 1));
    }
    default:
        return float(double(raw) / double(maxValue));
    }
}

float smallFloat(uint32_t raw, unsigned mantissaBits, unsigned exponentBits) {
    const uint32_t mantissa = raw & ((1u << mantissaBits) - 1);
    const uint32_t exponent = (raw >> mantissaBits) & ((1u << exponentBits) - 1);
    const int bias = (1 << (exponentBits - 1)) - 1;
    if (exponent == 0) {
        return mantissa ? std::ldexp(float(mantissa), 1 - bias - int(mantissaBits)) : 0.0f;
    }
    if (exponent == (1u << exponentBits) - 1) {
        return mantissa ? 0.0f : 1.0f;
    }
    return std::ldexp(1.0f + float(mantissa) / float(1u << mantissaBits), int(exponent) - bias);
}

uint8_t toByte(float v, bool isSigned) {
    if (std::isnan(v)) {
        return 0;
    }
    if (isSigned) {
        v = v * 0.5f + 0.5f;
    }
    return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

struct Pixel {
    float c[4] = {0, 0, 0, 1};
};

} // namespace

bool decodeRawPixels(const PixelFormat& format, std::span<const uint8_t> data, uint32_t width, uint32_t height,
                     std::vector<uint8_t>& rgba) {
    const auto& info = format.info();
    if (info.compressed || info.bytesPerBlock == 0) {
        return false;
    }
    const uint64_t pixelCount = uint64_t(width) * height;
    if (data.size() < pixelCount * info.bytesPerBlock) {
        return false;
    }
    rgba.assign(pixelCount * 4, 0);

    const DataType type = format.type;
    const bool isFloat = type == DataType::Float || type == DataType::UFloat;
    const bool isSigned = type == DataType::SNorm || type == DataType::SInt || type == DataType::SIntToFloat;

    if ((format.layout == Layout::R8G8B8A8 || format.layout == Layout::B8G8R8A8) && !isSigned) {
        const bool bgra = format.layout == Layout::B8G8R8A8;
        for (uint64_t i = 0; i < pixelCount; ++i) {
            const uint8_t* s = data.data() + i * 4;
            uint8_t* d = rgba.data() + i * 4;
            d[0] = bgra ? s[2] : s[0];
            d[1] = s[1];
            d[2] = bgra ? s[0] : s[2];
            d[3] = s[3];
        }
        return true;
    }

    for (uint64_t i = 0; i < pixelCount; ++i) {
        const uint8_t* s = data.data() + i * info.bytesPerBlock;
        Pixel px;
        auto& c = px.c;
        auto norm = [&](uint64_t raw, unsigned bits) { return normalizeChannel(raw, bits, type); };
        auto unorm = [&](uint64_t raw, unsigned bits) { return normalizeChannel(raw, bits, DataType::UNorm); };

        switch (format.layout) {
        case Layout::R4G4: {
            const uint64_t v = s[0];
            c[0] = unorm(v, 4);
            c[1] = unorm(v >> 4, 4);
            c[2] = 0;
            break;
        }
        case Layout::R8:
            c[0] = norm(s[0], 8);
            c[1] = c[2] = 0;
            break;
        case Layout::R8G8:
            c[0] = norm(s[0], 8);
            c[1] = norm(s[1], 8);
            c[2] = 0;
            break;
        case Layout::R8G8B8A8:
        case Layout::B8G8R8A8: {
            const bool bgra = format.layout == Layout::B8G8R8A8;
            c[0] = norm(s[bgra ? 2 : 0], 8);
            c[1] = norm(s[1], 8);
            c[2] = norm(s[bgra ? 0 : 2], 8);
            c[3] = norm(s[3], 8);
            break;
        }
        case Layout::R4G4B4A4: {
            const uint64_t v = readLe(s, 2);
            c[0] = unorm(v, 4); c[1] = unorm(v >> 4, 4); c[2] = unorm(v >> 8, 4); c[3] = unorm(v >> 12, 4);
            break;
        }
        case Layout::A4B4G4R4: {
            const uint64_t v = readLe(s, 2);
            c[3] = unorm(v, 4); c[2] = unorm(v >> 4, 4); c[1] = unorm(v >> 8, 4); c[0] = unorm(v >> 12, 4);
            break;
        }
        case Layout::R5G5B5A1: {
            const uint64_t v = readLe(s, 2);
            c[0] = unorm(v, 5); c[1] = unorm(v >> 5, 5); c[2] = unorm(v >> 10, 5); c[3] = unorm(v >> 15, 1);
            break;
        }
        case Layout::A1B5G5R5: {
            const uint64_t v = readLe(s, 2);
            c[3] = unorm(v, 1); c[2] = unorm(v >> 1, 5); c[1] = unorm(v >> 6, 5); c[0] = unorm(v >> 11, 5);
            break;
        }
        case Layout::B5G5R5A1: {
            const uint64_t v = readLe(s, 2);
            c[2] = unorm(v, 5); c[1] = unorm(v >> 5, 5); c[0] = unorm(v >> 10, 5); c[3] = unorm(v >> 15, 1);
            break;
        }
        case Layout::R5G6B5: {
            const uint64_t v = readLe(s, 2);
            c[0] = unorm(v, 5); c[1] = unorm(v >> 5, 6); c[2] = unorm(v >> 11, 5);
            break;
        }
        case Layout::B5G6R5: {
            const uint64_t v = readLe(s, 2);
            c[2] = unorm(v, 5); c[1] = unorm(v >> 5, 6); c[0] = unorm(v >> 11, 5);
            break;
        }
        case Layout::R16: {
            const uint64_t v = readLe(s, 2);
            c[0] = isFloat ? halfToFloat(uint16_t(v)) : norm(v, 16);
            c[1] = c[2] = 0;
            break;
        }
        case Layout::R16G16: {
            for (int k = 0; k < 2; ++k) {
                const uint64_t v = readLe(s + 2 * k, 2);
                c[k] = isFloat ? halfToFloat(uint16_t(v)) : norm(v, 16);
            }
            c[2] = 0;
            break;
        }
        case Layout::R16G16B16A16: {
            for (int k = 0; k < 4; ++k) {
                const uint64_t v = readLe(s + 2 * k, 2);
                c[k] = isFloat ? halfToFloat(uint16_t(v)) : norm(v, 16);
            }
            break;
        }
        case Layout::R32:
        case Layout::R32G32:
        case Layout::R32G32B32:
        case Layout::R32G32B32A32: {
            const unsigned channels = info.channels;
            for (unsigned k = 0; k < channels; ++k) {
                const uint32_t v = uint32_t(readLe(s + 4 * k, 4));
                float f;
                std::memcpy(&f, &v, 4);
                c[k] = isFloat ? f : norm(v, 32);
            }
            if (channels < 3) {
                c[2] = 0;
            }
            if (channels < 2) {
                c[1] = 0;
            }
            break;
        }
        case Layout::R24G8: {
            const uint64_t v = readLe(s, 4);
            c[0] = unorm(v, 24);
            c[1] = unorm(v >> 24, 8);
            c[2] = 0;
            break;
        }
        case Layout::R32G8X24: {
            const uint32_t v = uint32_t(readLe(s, 4));
            std::memcpy(&c[0], &v, 4);
            c[1] = unorm(s[4], 8);
            c[2] = 0;
            break;
        }
        case Layout::R10G10B10A2: {
            const uint64_t v = readLe(s, 4);
            c[0] = norm(v, 10); c[1] = norm(v >> 10, 10); c[2] = norm(v >> 20, 10); c[3] = unorm(v >> 30, 2);
            break;
        }
        case Layout::A2B10G10R10: {
            const uint64_t v = readLe(s, 4);
            c[3] = unorm(v, 2); c[2] = norm(v >> 2, 10); c[1] = norm(v >> 12, 10); c[0] = norm(v >> 22, 10);
            break;
        }
        case Layout::R11G11B10: {
            const uint32_t v = uint32_t(readLe(s, 4));
            c[0] = smallFloat(v & 0x7FF, 6, 5);
            c[1] = smallFloat((v >> 11) & 0x7FF, 6, 5);
            c[2] = smallFloat(v >> 22, 5, 5);
            break;
        }
        case Layout::B10G11R11: {
            const uint32_t v = uint32_t(readLe(s, 4));
            c[2] = smallFloat(v & 0x3FF, 5, 5);
            c[1] = smallFloat((v >> 10) & 0x7FF, 6, 5);
            c[0] = smallFloat(v >> 21, 6, 5);
            break;
        }
        case Layout::R10G11B11: {
            const uint32_t v = uint32_t(readLe(s, 4));
            c[0] = smallFloat(v & 0x3FF, 5, 5);
            c[1] = smallFloat((v >> 10) & 0x7FF, 6, 5);
            c[2] = smallFloat(v >> 21, 6, 5);
            break;
        }
        case Layout::R9G9B9E5: {
            const uint32_t v = uint32_t(readLe(s, 4));
            const int exponent = int(v >> 27) - 15 - 9;
            c[0] = std::ldexp(float(v & 0x1FF), exponent);
            c[1] = std::ldexp(float((v >> 9) & 0x1FF), exponent);
            c[2] = std::ldexp(float((v >> 18) & 0x1FF), exponent);
            break;
        }
        default:
            return false;
        }

        const bool signedOut = isSigned;
        uint8_t* d = rgba.data() + i * 4;
        for (int k = 0; k < 3; ++k) {
            d[k] = toByte(c[k], signedOut);
        }
        d[3] = toByte(c[3], false);
    }
    return true;
}

} // namespace bfrass::tex::detail
