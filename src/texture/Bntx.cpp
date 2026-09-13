#include "texture/TextureContainers.hpp"

#include <cstring>

namespace bfrass::tex {

namespace {

std::string readNxString(const BinaryReader& r, uint64_t pointerOffset) {
    const uint64_t target = r.u64(pointerOffset);
    if (target == 0) {
        return {};
    }
    const uint16_t length = r.u16(target);
    return r.fixedString(target + 2, length);
}

std::vector<bfres::UserData> readNxUserData(const BinaryReader& r, uint64_t arrayOffset, uint32_t count) {
    std::vector<bfres::UserData> result;
    for (uint32_t i = 0; i < count; ++i) {
        const uint64_t base = arrayOffset + uint64_t(i) * 0x40;
        bfres::UserData ud;
        ud.name = readNxString(r, base);
        const uint64_t data = r.u64(base + 8);
        const uint32_t n = r.u32(base + 0x10);
        const uint8_t type = r.u8(base + 0x14);
        switch (type) {
        case 0:
            ud.type = bfres::UserData::Type::Int32;
            for (uint32_t k = 0; k < n; ++k) {
                ud.ints.push_back(r.s32(data + 4 * k));
            }
            break;
        case 1:
            ud.type = bfres::UserData::Type::Float;
            for (uint32_t k = 0; k < n; ++k) {
                ud.floats.push_back(r.f32(data + 4 * k));
            }
            break;
        case 2:
            ud.type = bfres::UserData::Type::String;
            for (uint32_t k = 0; k < n; ++k) {
                ud.strings.push_back(readNxString(r, data + 8 * k));
            }
            break;
        default: {
            ud.type = bfres::UserData::Type::Bytes;
            const auto bytes = r.bytes(data, n);
            ud.bytes.assign(bytes.begin(), bytes.end());
            break;
        }
        }
        result.push_back(std::move(ud));
    }
    return result;
}

} // namespace

bool isBntx(std::span<const uint8_t> data) {
    return data.size() >= 0x58 && std::memcmp(data.data(), "BNTX\0\0\0\0", 8) == 0;
}

std::vector<TextureResource> parseBntx(const SharedBuffer& owner, std::span<const uint8_t> bntx,
                                       const std::string& containerName, const Log& log) {
    if (!isBntx(bntx)) {
        throw FormatError("'" + containerName + "' is not a BNTX archive");
    }
    const Endian endian = (bntx[0x0C] == 0xFF && bntx[0x0D] == 0xFE) ? Endian::Little : Endian::Big;
    const BinaryReader r(bntx, endian);
    if (r.fixedString(0x20, 4) != "NX  ") {
        log.warn("BNTX '{}' targets platform '{}', decoding as NX", containerName, r.fixedString(0x20, 4));
    }
    const uint32_t count = r.u32(0x24);
    const uint64_t pointerArray = r.u64(0x28);
    log.debug("BNTX {}: version 0x{:08X}, {} textures", containerName, r.u32(0x08), count);

    std::vector<TextureResource> textures;
    textures.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint64_t o = r.u64(pointerArray + 8 * uint64_t(i));
        if (r.fixedString(o, 4) != "BRTI") {
            throw FormatError("BNTX texture " + std::to_string(i) + " has no BRTI signature");
        }
        TextureResource t;
        t.platform = bfres::Platform::Switch;
        t.container = containerName;
        t.owner = owner;
        t.nx.storageDimension = r.u8(o + 0x11);
        t.nx.tileMode = r.u16(o + 0x12);
        t.mipCount = std::max<uint32_t>(1, r.u16(o + 0x16));
        t.rawFormat = r.u32(o + 0x1C);
        t.format = fromGfxImageFormat(t.rawFormat);
        t.width = r.u32(o + 0x24);
        t.height = r.u32(o + 0x28);
        t.depth = std::max<uint32_t>(1, r.u32(o + 0x2C));
        t.arrayCount = std::max<uint32_t>(1, r.u32(o + 0x30));
        t.nx.textureLayout = r.u32(o + 0x34);
        t.nx.dataSize = r.u32(o + 0x50);
        t.nx.alignment = r.u32(o + 0x54);
        for (int c = 0; c < 4; ++c) {
            t.channels[c] = fromGfxChannelMapping(r.u8(o + 0x58 + c));
        }
        t.nx.imageDimension = r.u8(o + 0x5C);
        t.name = readNxString(r, o + 0x60);

        const uint64_t mipArray = r.u64(o + 0x70);
        uint64_t firstMip = 0;
        if (mipArray) {
            firstMip = r.u64(mipArray);
            for (uint32_t m = 0; m < t.mipCount; ++m) {
                t.nx.mipOffsets.push_back(r.u64(mipArray + 8 * uint64_t(m)) - firstMip);
            }
        }
        const uint64_t dataSize = t.nx.dataSize ? t.nx.dataSize : (r.size() > firstMip ? r.size() - firstMip : 0);
        t.imageData = r.bytes(firstMip, std::min<uint64_t>(dataSize, r.size() - firstMip));

        const uint64_t userDataArray = r.u64(o + 0x78);
        // BRTI has no user data count field; the dictionary header carries it.
        const uint64_t userDataDic = r.u64(o + 0x98);
        if (userDataArray && userDataDic) {
            t.userData = readNxUserData(r, userDataArray, r.u32(userDataDic + 4));
        }

        log.debug("  BRTI #{} {}: {} {}x{}x{} array {} mips {} tile {} layout 0x{:X} size 0x{:X}", i, t.name,
                  t.format.name(), t.width, t.height, t.depth, t.arrayCount, t.mipCount, t.nx.tileMode,
                  t.nx.textureLayout, t.nx.dataSize);
        textures.push_back(std::move(t));
    }
    return textures;
}

} // namespace bfrass::tex
