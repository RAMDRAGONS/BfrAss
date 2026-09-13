#include "texture/TextureContainers.hpp"

namespace bfrass::tex {

namespace {

uint64_t relative(const BinaryReader& r, uint64_t field) {
    const int32_t value = r.s32(field);
    return value == 0 ? 0 : uint64_t(int64_t(field) + value);
}

std::string relativeString(const BinaryReader& r, uint64_t field) {
    const uint64_t target = relative(r, field);
    return target ? r.cString(target) : std::string();
}

} // namespace

TextureResource parseFtex(const BinaryReader& r, uint64_t o, const FtexContext& context, const Log& log) {
    if (r.fixedString(o, 4) != "FTEX") {
        throw FormatError("expected FTEX block at 0x" + BinaryReader::toHex(o));
    }
    TextureResource t;
    t.platform = bfres::Platform::WiiU;
    t.container = context.containerName;
    t.owner = context.owner;
    t.mipOwner = context.owner;

    auto& g = t.gx2;
    g.dim = r.u32(o + 0x04);
    t.width = r.u32(o + 0x08);
    t.height = r.u32(o + 0x0C);
    const uint32_t depth = std::max<uint32_t>(1, r.u32(o + 0x10));
    g.depth = depth;
    t.mipCount = std::max<uint32_t>(1, r.u32(o + 0x14));
    g.format = r.u32(o + 0x18);
    g.aa = r.u32(o + 0x1C);
    g.use = r.u32(o + 0x20);
    g.imageSize = r.u32(o + 0x24);
    g.mipSize = r.u32(o + 0x2C);
    g.tileMode = r.u32(o + 0x34);
    g.swizzle = r.u32(o + 0x38);
    g.alignment = r.u32(o + 0x3C);
    g.pitch = r.u32(o + 0x40);
    for (int i = 0; i < 13; ++i) {
        g.mipOffsets[i] = r.u32(o + 0x44 + 4 * i);
    }
    g.viewFirstMip = r.u32(o + 0x78);
    g.viewNumMips = r.u32(o + 0x7C);
    g.viewFirstSlice = r.u32(o + 0x80);
    g.viewNumSlices = r.u32(o + 0x84);
    for (int c = 0; c < 4; ++c) {
        t.channels[c] = fromGx2Component(r.u8(o + 0x88 + c));
    }
    for (int i = 0; i < 5; ++i) {
        g.registers[i] = r.u32(o + 0x8C + 4 * i);
    }

    t.rawFormat = g.format;
    t.format = fromGx2SurfaceFormat(g.format);
    const bool layered = g.dim == 3 || g.dim == 4 || g.dim == 5 || g.dim == 7;
    t.arrayCount = layered ? depth : 1;
    t.depth = layered ? 1 : depth;

    t.name = relativeString(r, o + 0xA8);
    t.path = relativeString(r, o + 0xAC);

    if (context.loadImage) {
        const uint64_t data = relative(r, o + 0xB0);
        if (data && g.imageSize) {
            t.imageData = r.bytes(data, g.imageSize);
        }
    }
    if (context.loadMips) {
        const uint64_t mips = relative(r, o + 0xB4);
        if (mips && g.mipSize) {
            t.mipData = r.bytes(mips, g.mipSize);
        }
    }

    const uint64_t userDataDic = relative(r, o + 0xB8);
    if (userDataDic) {
        const int32_t count = r.s32(userDataDic + 4);
        for (int32_t i = 1; i <= count; ++i) {
            const uint64_t node = userDataDic + 8 + uint64_t(i) * 16;
            const uint64_t entry = relative(r, node + 12);
            if (!entry) {
                continue;
            }
            bfres::UserData ud;
            ud.name = relativeString(r, entry);
            const uint16_t n = r.u16(entry + 4);
            const uint8_t type = r.u8(entry + 6);
            const uint64_t payload = entry + 8;
            switch (type) {
            case 0:
                ud.type = bfres::UserData::Type::Int32;
                for (uint16_t k = 0; k < n; ++k) {
                    ud.ints.push_back(r.s32(payload + 4 * k));
                }
                break;
            case 1:
                ud.type = bfres::UserData::Type::Float;
                for (uint16_t k = 0; k < n; ++k) {
                    ud.floats.push_back(r.f32(payload + 4 * k));
                }
                break;
            case 2:
                ud.type = bfres::UserData::Type::String;
                for (uint16_t k = 0; k < n; ++k) {
                    ud.strings.push_back(relativeString(r, payload + 4 * k));
                }
                break;
            default: {
                ud.type = bfres::UserData::Type::Bytes;
                const auto bytes = r.bytes(payload, n);
                ud.bytes.assign(bytes.begin(), bytes.end());
                break;
            }
            }
            t.userData.push_back(std::move(ud));
        }
    }

    log.debug("FTEX {}: {} {}x{} depth {} mips {} tile {} swizzle 0x{:X} image 0x{:X} mip 0x{:X}", t.name,
              t.format.name(), t.width, t.height, depth, t.mipCount, g.tileMode, g.swizzle, g.imageSize, g.mipSize);
    return t;
}

} // namespace bfrass::tex
