#include "texture/TextureFormat.hpp"

#include <format>

namespace bfrass::tex {

namespace {

constexpr LayoutInfo kLayouts[] = {
    {"Undefined", 1, 1, 0, 0, false},
    {"R4_G4", 1, 1, 1, 2, false},
    {"R8", 1, 1, 1, 1, false},
    {"R4_G4_B4_A4", 1, 1, 2, 4, false},
    {"A4_B4_G4_R4", 1, 1, 2, 4, false},
    {"R5_G5_B5_A1", 1, 1, 2, 4, false},
    {"A1_B5_G5_R5", 1, 1, 2, 4, false},
    {"R5_G6_B5", 1, 1, 2, 3, false},
    {"B5_G6_R5", 1, 1, 2, 3, false},
    {"R8_G8", 1, 1, 2, 2, false},
    {"R16", 1, 1, 2, 1, false},
    {"R8_G8_B8_A8", 1, 1, 4, 4, false},
    {"B8_G8_R8_A8", 1, 1, 4, 4, false},
    {"R9_G9_B9_E5", 1, 1, 4, 3, false},
    {"R10_G10_B10_A2", 1, 1, 4, 4, false},
    {"R11_G11_B10", 1, 1, 4, 3, false},
    {"B10_G11_R11", 1, 1, 4, 3, false},
    {"R10_G11_B11", 1, 1, 4, 3, false},
    {"R16_G16", 1, 1, 4, 2, false},
    {"R24_G8", 1, 1, 4, 2, false},
    {"R32", 1, 1, 4, 1, false},
    {"R16_G16_B16_A16", 1, 1, 8, 4, false},
    {"R32_G8_X24", 1, 1, 8, 2, false},
    {"R32_G32", 1, 1, 8, 2, false},
    {"R32_G32_B32", 1, 1, 12, 3, false},
    {"R32_G32_B32_A32", 1, 1, 16, 4, false},
    {"BC1", 4, 4, 8, 4, true},
    {"BC2", 4, 4, 16, 4, true},
    {"BC3", 4, 4, 16, 4, true},
    {"BC4", 4, 4, 8, 1, true},
    {"BC5", 4, 4, 16, 2, true},
    {"BC6H", 4, 4, 16, 3, true},
    {"BC7", 4, 4, 16, 4, true},
    {"EAC_R11", 4, 4, 8, 1, true},
    {"EAC_R11_G11", 4, 4, 16, 2, true},
    {"ETC1", 4, 4, 8, 3, true},
    {"ETC2", 4, 4, 8, 3, true},
    {"ETC2_MASK", 4, 4, 8, 4, true},
    {"ETC2_ALPHA", 4, 4, 16, 4, true},
    {"PVRTC1_2BPP", 8, 4, 8, 3, true},
    {"PVRTC1_4BPP", 4, 4, 8, 3, true},
    {"PVRTC1_ALPHA_2BPP", 8, 4, 8, 4, true},
    {"PVRTC1_ALPHA_4BPP", 4, 4, 8, 4, true},
    {"PVRTC2_ALPHA_2BPP", 8, 4, 8, 4, true},
    {"PVRTC2_ALPHA_4BPP", 4, 4, 8, 4, true},
    {"ASTC_4x4", 4, 4, 16, 4, true},
    {"ASTC_5x4", 5, 4, 16, 4, true},
    {"ASTC_5x5", 5, 5, 16, 4, true},
    {"ASTC_6x5", 6, 5, 16, 4, true},
    {"ASTC_6x6", 6, 6, 16, 4, true},
    {"ASTC_8x5", 8, 5, 16, 4, true},
    {"ASTC_8x6", 8, 6, 16, 4, true},
    {"ASTC_8x8", 8, 8, 16, 4, true},
    {"ASTC_10x5", 10, 5, 16, 4, true},
    {"ASTC_10x6", 10, 6, 16, 4, true},
    {"ASTC_10x8", 10, 8, 16, 4, true},
    {"ASTC_10x10", 10, 10, 16, 4, true},
    {"ASTC_12x10", 12, 10, 16, 4, true},
    {"ASTC_12x12", 12, 12, 16, 4, true},
    {"B5_G5_R5_A1", 1, 1, 2, 4, false},
    {"A2_B10_G10_R10", 1, 1, 4, 4, false},
    {"NV12", 1, 1, 0, 3, false},
};

const char* typeName(DataType t) {
    switch (t) {
    case DataType::UNorm: return "UNORM";
    case DataType::SNorm: return "SNORM";
    case DataType::UInt: return "UINT";
    case DataType::SInt: return "SINT";
    case DataType::Float: return "FLOAT";
    case DataType::UNormSrgb: return "SRGB";
    case DataType::Depth: return "DEPTH";
    case DataType::UIntToFloat: return "UINT_TO_FLOAT";
    case DataType::SIntToFloat: return "SINT_TO_FLOAT";
    case DataType::UFloat: return "UFLOAT";
    default: return "UNDEFINED";
    }
}

} // namespace

const LayoutInfo& layoutInfo(Layout layout) {
    const auto index = static_cast<size_t>(layout);
    if (index >= std::size(kLayouts)) {
        return kLayouts[0];
    }
    return kLayouts[index];
}

std::string PixelFormat::name() const {
    return std::format("{}_{}", info().name, typeName(type));
}

PixelFormat fromGfxImageFormat(uint32_t imageFormat) {
    PixelFormat f;
    const uint32_t channel = imageFormat >> 8;
    const uint32_t type = imageFormat & 0xFF;
    if (channel <= static_cast<uint32_t>(Layout::B5G5R5A1)) {
        f.layout = static_cast<Layout>(channel);
    }
    if (type <= static_cast<uint32_t>(DataType::UFloat)) {
        f.type = static_cast<DataType>(type);
    }
    return f;
}

PixelFormat fromGx2SurfaceFormat(uint32_t surfaceFormat) {
    PixelFormat f;
    if (surfaceFormat == 0x81) {
        f.layout = Layout::NV12;
        f.type = DataType::UNorm;
        return f;
    }
    switch (surfaceFormat & 0x3F) {
    case 0x01: f.layout = Layout::R8; break;
    case 0x02: f.layout = Layout::R4G4; break;
    case 0x05: case 0x06: f.layout = Layout::R16; break;
    case 0x07: f.layout = Layout::R8G8; break;
    case 0x08: f.layout = Layout::R5G6B5; break;
    case 0x0A: f.layout = Layout::R5G5B5A1; break;
    case 0x0B: f.layout = Layout::R4G4B4A4; break;
    case 0x0C: f.layout = Layout::A1B5G5R5; break;
    case 0x0D: case 0x0E: f.layout = Layout::R32; break;
    case 0x0F: case 0x10: f.layout = Layout::R16G16; break;
    case 0x11: f.layout = Layout::R24G8; break;
    case 0x16: f.layout = Layout::R11G11B10; break;
    case 0x19: f.layout = Layout::R10G10B10A2; break;
    case 0x1A: f.layout = Layout::R8G8B8A8; break;
    case 0x1B: f.layout = Layout::A2B10G10R10; break;
    case 0x1C: f.layout = Layout::R32G8X24; break;
    case 0x1D: case 0x1E: f.layout = Layout::R32G32; break;
    case 0x1F: case 0x20: f.layout = Layout::R16G16B16A16; break;
    case 0x22: case 0x23: f.layout = Layout::R32G32B32A32; break;
    case 0x31: f.layout = Layout::BC1; break;
    case 0x32: f.layout = Layout::BC2; break;
    case 0x33: f.layout = Layout::BC3; break;
    case 0x34: f.layout = Layout::BC4; break;
    case 0x35: f.layout = Layout::BC5; break;
    default: f.layout = Layout::Undefined; break;
    }
    switch (surfaceFormat & 0xF00) {
    case 0x000: f.type = DataType::UNorm; break;
    case 0x100: f.type = DataType::UInt; break;
    case 0x200: f.type = DataType::SNorm; break;
    case 0x300: f.type = DataType::SInt; break;
    case 0x400: f.type = DataType::UNormSrgb; break;
    case 0x800: f.type = DataType::Float; break;
    default: f.type = DataType::Undefined; break;
    }
    return f;
}

ChannelSource fromGfxChannelMapping(uint8_t mapping) {
    switch (mapping) {
    case 0: return ChannelSource::Zero;
    case 1: return ChannelSource::One;
    case 2: return ChannelSource::Red;
    case 3: return ChannelSource::Green;
    case 4: return ChannelSource::Blue;
    case 5: return ChannelSource::Alpha;
    default: return ChannelSource::Zero;
    }
}

ChannelSource fromGx2Component(uint8_t component) {
    switch (component) {
    case 0: return ChannelSource::Red;
    case 1: return ChannelSource::Green;
    case 2: return ChannelSource::Blue;
    case 3: return ChannelSource::Alpha;
    case 4: return ChannelSource::Zero;
    case 5: return ChannelSource::One;
    default: return ChannelSource::Zero;
    }
}

uint32_t dxgiFormat(const PixelFormat& f) {
    const bool srgb = f.isSrgb();
    switch (f.layout) {
    case Layout::R8G8B8A8:
        switch (f.type) {
        case DataType::UNorm: return 28;
        case DataType::UNormSrgb: return 29;
        case DataType::UInt: return 30;
        case DataType::SNorm: return 31;
        case DataType::SInt: return 32;
        default: return 0;
        }
    case Layout::B8G8R8A8:
        return srgb ? 91 : (f.type == DataType::UNorm ? 87 : 0);
    case Layout::R8:
        switch (f.type) {
        case DataType::UNorm: return 61;
        case DataType::UInt: return 62;
        case DataType::SNorm: return 63;
        case DataType::SInt: return 64;
        default: return 0;
        }
    case Layout::R8G8:
        switch (f.type) {
        case DataType::UNorm: return 49;
        case DataType::UInt: return 50;
        case DataType::SNorm: return 51;
        case DataType::SInt: return 52;
        default: return 0;
        }
    case Layout::R16:
        switch (f.type) {
        case DataType::Float: return 54;
        case DataType::UNorm: return 56;
        case DataType::UInt: return 57;
        case DataType::SNorm: return 58;
        case DataType::SInt: return 59;
        default: return 0;
        }
    case Layout::R16G16:
        switch (f.type) {
        case DataType::Float: return 34;
        case DataType::UNorm: return 35;
        case DataType::UInt: return 36;
        case DataType::SNorm: return 37;
        case DataType::SInt: return 38;
        default: return 0;
        }
    case Layout::R16G16B16A16:
        switch (f.type) {
        case DataType::Float: return 10;
        case DataType::UNorm: return 11;
        case DataType::UInt: return 12;
        case DataType::SNorm: return 13;
        case DataType::SInt: return 14;
        default: return 0;
        }
    case Layout::R32:
        switch (f.type) {
        case DataType::Float: return 41;
        case DataType::UInt: return 42;
        case DataType::SInt: return 43;
        default: return 0;
        }
    case Layout::R32G32:
        switch (f.type) {
        case DataType::Float: return 16;
        case DataType::UInt: return 17;
        case DataType::SInt: return 18;
        default: return 0;
        }
    case Layout::R32G32B32:
        switch (f.type) {
        case DataType::Float: return 6;
        case DataType::UInt: return 7;
        case DataType::SInt: return 8;
        default: return 0;
        }
    case Layout::R32G32B32A32:
        switch (f.type) {
        case DataType::Float: return 2;
        case DataType::UInt: return 3;
        case DataType::SInt: return 4;
        default: return 0;
        }
    case Layout::R10G10B10A2:
        return f.type == DataType::UInt ? 25 : (f.type == DataType::UNorm ? 24 : 0);
    case Layout::R11G11B10: return f.type == DataType::Float ? 26 : 0;
    case Layout::R9G9B9E5: return 67;
    case Layout::B5G6R5: return f.type == DataType::UNorm ? 85 : 0;
    case Layout::B5G5R5A1: return f.type == DataType::UNorm ? 86 : 0;
    case Layout::BC1: return srgb ? 72 : 71;
    case Layout::BC2: return srgb ? 75 : 74;
    case Layout::BC3: return srgb ? 78 : 77;
    case Layout::BC4: return f.type == DataType::SNorm ? 81 : 80;
    case Layout::BC5: return f.type == DataType::SNorm ? 84 : 83;
    case Layout::BC6H: return f.type == DataType::Float ? 96 : 95;
    case Layout::BC7: return srgb ? 99 : 98;
    default: return 0;
    }
}

uint64_t surfaceByteSize(const PixelFormat& format, uint32_t width, uint32_t height, uint32_t depth) {
    const auto& info = format.info();
    const uint64_t bw = (uint64_t(width) + info.blockWidth - 1) / info.blockWidth;
    const uint64_t bh = (uint64_t(height) + info.blockHeight - 1) / info.blockHeight;
    return bw * bh * depth * info.bytesPerBlock;
}

} // namespace bfrass::tex
