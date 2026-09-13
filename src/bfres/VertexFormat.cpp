#include "bfres/VertexFormat.hpp"

#include "core/Math.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace bfrass::bfres {

using Storage = AttributeFormat::Storage;
using Interp = AttributeFormat::Interpretation;

unsigned AttributeFormat::byteSize() const {
    switch (storage) {
    case Storage::U4x2: return 1;
    case Storage::U8: return components;
    case Storage::U16:
    case Storage::Half: return 2u * components;
    case Storage::U32:
    case Storage::Float: return 4u * components;
    case Storage::Packed10_10_10_2:
    case Storage::Packed10_11_11F: return 4;
    default: return 0;
    }
}

std::string AttributeFormat::name() const {
    const char* s = "invalid";
    switch (storage) {
    case Storage::U4x2: s = "4_4"; break;
    case Storage::U8: s = "8"; break;
    case Storage::U16: s = "16"; break;
    case Storage::U32: s = "32"; break;
    case Storage::Half: s = "16F"; break;
    case Storage::Float: s = "32F"; break;
    case Storage::Packed10_10_10_2: s = "10_10_10_2"; break;
    case Storage::Packed10_11_11F: s = "10_11_11F"; break;
    default: break;
    }
    const char* i = "";
    switch (interpretation) {
    case Interp::UNorm: i = "UNorm"; break;
    case Interp::SNorm: i = "SNorm"; break;
    case Interp::UInt: i = "UInt"; break;
    case Interp::SInt: i = "SInt"; break;
    case Interp::UIntToFloat: i = "UIntToFloat"; break;
    case Interp::SIntToFloat: i = "SIntToFloat"; break;
    case Interp::Float: i = "Float"; break;
    }
    return std::format("{}x{} {}", s, components, i);
}

AttributeFormat fromGx2AttribFormat(uint32_t format) {
    AttributeFormat f;
    bool floatStorage = false;
    switch (format & 0xFF) {
    case 0x00: f.storage = Storage::U8; f.components = 1; break;
    case 0x01: f.storage = Storage::U4x2; f.components = 2; break;
    case 0x02: f.storage = Storage::U16; f.components = 1; break;
    case 0x03: f.storage = Storage::Half; f.components = 1; floatStorage = true; break;
    case 0x04: f.storage = Storage::U8; f.components = 2; break;
    case 0x05: f.storage = Storage::U32; f.components = 1; break;
    case 0x06: f.storage = Storage::Float; f.components = 1; floatStorage = true; break;
    case 0x07: f.storage = Storage::U16; f.components = 2; break;
    case 0x08: f.storage = Storage::Half; f.components = 2; floatStorage = true; break;
    case 0x09: f.storage = Storage::Packed10_11_11F; f.components = 3; floatStorage = true; break;
    case 0x0A: f.storage = Storage::U8; f.components = 4; break;
    case 0x0B: f.storage = Storage::Packed10_10_10_2; f.components = 4; break;
    case 0x0C: f.storage = Storage::U32; f.components = 2; break;
    case 0x0D: f.storage = Storage::Float; f.components = 2; floatStorage = true; break;
    case 0x0E: f.storage = Storage::U16; f.components = 4; break;
    case 0x0F: f.storage = Storage::Half; f.components = 4; floatStorage = true; break;
    case 0x10: f.storage = Storage::U32; f.components = 3; break;
    case 0x11: f.storage = Storage::Float; f.components = 3; floatStorage = true; break;
    case 0x12: f.storage = Storage::U32; f.components = 4; break;
    case 0x13: f.storage = Storage::Float; f.components = 4; floatStorage = true; break;
    default: return {};
    }
    switch (format >> 8) {
    case 0x0: f.interpretation = Interp::UNorm; break;
    case 0x1: f.interpretation = Interp::UInt; break;
    case 0x2: f.interpretation = Interp::SNorm; break;
    case 0x3: f.interpretation = Interp::SInt; break;
    case 0x8: f.interpretation = floatStorage ? Interp::Float : Interp::UIntToFloat; break;
    case 0xA: f.interpretation = Interp::SIntToFloat; break;
    default: return {};
    }
    if (floatStorage && f.interpretation != Interp::Float) {
        return {};
    }
    return f;
}

AttributeFormat fromGfxAttributeFormat(uint32_t format) {
    AttributeFormat f;
    const uint32_t channel = format >> 8;
    const uint32_t type = format & 0xFF;
    const bool isFloat = type == 5;
    switch (channel) {
    case 1: f.storage = Storage::U4x2; f.components = 2; break;
    case 2: f.storage = Storage::U8; f.components = 1; break;
    case 9: f.storage = Storage::U8; f.components = 2; break;
    case 10: f.storage = isFloat ? Storage::Half : Storage::U16; f.components = 1; break;
    case 11: f.storage = Storage::U8; f.components = 4; break;
    case 14: f.storage = Storage::Packed10_10_10_2; f.components = 4; break;
    case 15: f.storage = Storage::Packed10_11_11F; f.components = 3; break;
    case 18: f.storage = isFloat ? Storage::Half : Storage::U16; f.components = 2; break;
    case 20: f.storage = isFloat ? Storage::Float : Storage::U32; f.components = 1; break;
    case 21: f.storage = isFloat ? Storage::Half : Storage::U16; f.components = 4; break;
    case 23: f.storage = isFloat ? Storage::Float : Storage::U32; f.components = 2; break;
    case 24: f.storage = isFloat ? Storage::Float : Storage::U32; f.components = 3; break;
    case 25: f.storage = isFloat ? Storage::Float : Storage::U32; f.components = 4; break;
    default: return {};
    }
    switch (type) {
    case 1: f.interpretation = Interp::UNorm; break;
    case 2: f.interpretation = Interp::SNorm; break;
    case 3: f.interpretation = Interp::UInt; break;
    case 4: f.interpretation = Interp::SInt; break;
    case 5: f.interpretation = Interp::Float; break;
    case 8: f.interpretation = Interp::UIntToFloat; break;
    case 9: f.interpretation = Interp::SIntToFloat; break;
    default: return {};
    }
    return f;
}

namespace {

float convertInteger(uint64_t raw, unsigned bits, Interp interp) {
    const uint64_t mask = bits >= 64 ? ~uint64_t(0) : (uint64_t(1) << bits) - 1;
    raw &= mask;
    const bool isSigned = interp == Interp::SNorm || interp == Interp::SInt || interp == Interp::SIntToFloat;
    double value;
    if (isSigned) {
        int64_t v = int64_t(raw);
        if (v & (int64_t(1) << (bits - 1))) {
            v -= int64_t(1) << bits;
        }
        value = double(v);
    } else {
        value = double(raw);
    }
    switch (interp) {
    case Interp::UNorm: return float(value / double(mask));
    case Interp::SNorm: return float(std::max(-1.0, value / double((uint64_t(1) << (bits - 1)) - 1)));
    default: return float(value);
    }
}

float packedFloat(uint32_t raw, unsigned mantissaBits) {
    const uint32_t mantissa = raw & ((1u << mantissaBits) - 1);
    const uint32_t exponent = (raw >> mantissaBits) & 0x1F;
    if (exponent == 0) {
        return mantissa ? std::ldexp(float(mantissa), -14 - int(mantissaBits)) : 0.0f;
    }
    if (exponent == 31) {
        return mantissa ? 0.0f : HUGE_VALF;
    }
    return std::ldexp(1.0f + float(mantissa) / float(1u << mantissaBits), int(exponent) - 15);
}

} // namespace

std::array<float, 4> readAttribute(const BinaryReader& r, uint64_t o, const AttributeFormat& f) {
    std::array<float, 4> out{0, 0, 0, 1};
    switch (f.storage) {
    case Storage::U4x2: {
        const uint8_t v = r.u8(o);
        out[0] = convertInteger(v & 0xF, 4, f.interpretation);
        out[1] = convertInteger(v >> 4, 4, f.interpretation);
        break;
    }
    case Storage::U8:
        for (unsigned i = 0; i < f.components; ++i) {
            out[i] = convertInteger(r.u8(o + i), 8, f.interpretation);
        }
        break;
    case Storage::U16:
        for (unsigned i = 0; i < f.components; ++i) {
            out[i] = convertInteger(r.u16(o + 2 * i), 16, f.interpretation);
        }
        break;
    case Storage::U32:
        for (unsigned i = 0; i < f.components; ++i) {
            out[i] = convertInteger(r.u32(o + 4 * i), 32, f.interpretation);
        }
        break;
    case Storage::Half:
        for (unsigned i = 0; i < f.components; ++i) {
            out[i] = halfToFloat(r.u16(o + 2 * i));
        }
        break;
    case Storage::Float:
        for (unsigned i = 0; i < f.components; ++i) {
            out[i] = r.f32(o + 4 * i);
        }
        break;
    case Storage::Packed10_10_10_2: {
        const uint32_t v = r.u32(o);
        for (unsigned i = 0; i < 3; ++i) {
            out[i] = convertInteger((v >> (10 * i)) & 0x3FF, 10, f.interpretation);
        }
        // The two alpha bits are unsigned even in the SNORM variant.
        const Interp alphaInterp = f.interpretation == Interp::SNorm ? Interp::UNorm : f.interpretation;
        out[3] = convertInteger(v >> 30, 2, alphaInterp);
        break;
    }
    case Storage::Packed10_11_11F: {
        const uint32_t v = r.u32(o);
        out[0] = packedFloat(v & 0x7FF, 6);
        out[1] = packedFloat((v >> 11) & 0x7FF, 6);
        out[2] = packedFloat(v >> 22, 5);
        break;
    }
    default:
        break;
    }
    return out;
}

} // namespace bfrass::bfres
