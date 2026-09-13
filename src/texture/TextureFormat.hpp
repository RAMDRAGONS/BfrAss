#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace bfrass::tex {

// Channel layouts. Values up to B5G5R5A1 mirror nn::gfx::ChannelFormat so Switch
// image formats convert with a shift; the remainder only exist on GX2 surfaces.
enum class Layout : uint8_t {
    Undefined = 0,
    R4G4, R8, R4G4B4A4, A4B4G4R4, R5G5B5A1, A1B5G5R5, R5G6B5, B5G6R5, R8G8, R16,
    R8G8B8A8, B8G8R8A8, R9G9B9E5, R10G10B10A2, R11G11B10, B10G11R11, R10G11B11,
    R16G16, R24G8, R32, R16G16B16A16, R32G8X24, R32G32, R32G32B32, R32G32B32A32,
    BC1, BC2, BC3, BC4, BC5, BC6H, BC7,
    EacR11, EacR11G11, Etc1, Etc2, Etc2Mask, Etc2Alpha,
    Pvrtc1_2Bpp, Pvrtc1_4Bpp, Pvrtc1Alpha2Bpp, Pvrtc1Alpha4Bpp, Pvrtc2Alpha2Bpp, Pvrtc2Alpha4Bpp,
    Astc4x4, Astc5x4, Astc5x5, Astc6x5, Astc6x6, Astc8x5, Astc8x6, Astc8x8,
    Astc10x5, Astc10x6, Astc10x8, Astc10x10, Astc12x10, Astc12x12,
    B5G5R5A1,
    A2B10G10R10,
    NV12,
    Count
};

// Mirrors nn::gfx::TypeFormat.
enum class DataType : uint8_t {
    Undefined = 0, UNorm, SNorm, UInt, SInt, Float, UNormSrgb, Depth, UIntToFloat, SIntToFloat, UFloat
};

enum class ChannelSource : uint8_t { Red, Green, Blue, Alpha, Zero, One };
using ChannelMap = std::array<ChannelSource, 4>;
inline constexpr ChannelMap kIdentityChannels{ChannelSource::Red, ChannelSource::Green, ChannelSource::Blue,
                                              ChannelSource::Alpha};

struct LayoutInfo {
    const char* name;
    uint8_t blockWidth;
    uint8_t blockHeight;
    uint8_t bytesPerBlock; // bytes per pixel for uncompressed layouts
    uint8_t channels;
    bool compressed;
};

const LayoutInfo& layoutInfo(Layout layout);

struct PixelFormat {
    Layout layout = Layout::Undefined;
    DataType type = DataType::Undefined;

    bool isSrgb() const { return type == DataType::UNormSrgb; }
    bool isSigned() const { return type == DataType::SNorm || type == DataType::SInt || type == DataType::Float; }
    bool isAstc() const { return layout >= Layout::Astc4x4 && layout <= Layout::Astc12x12; }
    const LayoutInfo& info() const { return layoutInfo(layout); }
    std::string name() const;
};

PixelFormat fromGfxImageFormat(uint32_t imageFormat);
PixelFormat fromGx2SurfaceFormat(uint32_t surfaceFormat);
ChannelSource fromGfxChannelMapping(uint8_t mapping);
ChannelSource fromGx2Component(uint8_t component);

// DXGI_FORMAT value for DDS output, or 0 when the data must be decoded first.
uint32_t dxgiFormat(const PixelFormat& format);

uint64_t surfaceByteSize(const PixelFormat& format, uint32_t width, uint32_t height, uint32_t depth = 1);

} // namespace bfrass::tex
