#pragma once

#include "core/BinaryReader.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace bfrass::bfres {

// Storage layout of one vertex attribute, unified across GX2AttribFormat and
// nn::gfx::AttributeFormat.
struct AttributeFormat {
    enum class Storage : uint8_t { Invalid, U4x2, U8, U16, U32, Half, Float, Packed10_10_10_2, Packed10_11_11F };
    enum class Interpretation : uint8_t { UNorm, SNorm, UInt, SInt, UIntToFloat, SIntToFloat, Float };

    Storage storage = Storage::Invalid;
    Interpretation interpretation = Interpretation::Float;
    uint8_t components = 0;

    bool valid() const { return storage != Storage::Invalid; }
    unsigned byteSize() const;
    bool isInteger() const { return interpretation == Interpretation::UInt || interpretation == Interpretation::SInt; }
    std::string name() const;
};

AttributeFormat fromGx2AttribFormat(uint32_t format);
AttributeFormat fromGfxAttributeFormat(uint32_t format);

// Reads one attribute value; missing components are 0 except w which is 1 for
// normalised data. Integer formats return raw values (e.g. bone indices).
std::array<float, 4> readAttribute(const BinaryReader& reader, uint64_t offset, const AttributeFormat& format);

} // namespace bfrass::bfres
