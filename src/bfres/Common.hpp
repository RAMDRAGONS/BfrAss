#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace bfrass::bfres {

enum class Platform { WiiU, Switch };

// Wii U headers store nw::g3d major.minor.micro.binaryBugfix as four bytes. Switch
// headers store nn::util::BinVersion: u8 micro, u8 minor, u16 major, which has no
// bugfix component.
struct Version {
    uint16_t major = 0;
    uint8_t minor = 0;
    uint8_t micro = 0;
    uint8_t binaryBugfix = 0;
    bool hasBinaryBugfix = false;

    static Version fromCafe(const uint8_t* bytes) {
        return {bytes[0], bytes[1], bytes[2], bytes[3], true};
    }
    static Version fromNx(const uint8_t* bytes) {
        return {uint16_t(bytes[2] | (bytes[3] << 8)), bytes[1], bytes[0], 0, false};
    }

    bool atLeast(unsigned maj, unsigned min = 0, unsigned mic = 0, unsigned bugfix = 0) const {
        const auto key = [](unsigned a, unsigned b, unsigned c, unsigned d) {
            return (uint64_t(a) << 24) | (uint64_t(b) << 16) | (uint64_t(c) << 8) | d;
        };
        return key(major, minor, micro, binaryBugfix) >= key(maj, min, mic, bugfix);
    }

    std::string toString() const {
        return hasBinaryBugfix ? std::format("{}.{}.{}.{}", major, minor, micro, binaryBugfix)
                               : std::format("{}.{}.{}", major, minor, micro);
    }
};

struct UserData {
    enum class Type : uint8_t { Int32, Float, String, WString, Bytes };

    std::string name;
    Type type = Type::Int32;
    std::vector<int32_t> ints;
    std::vector<float> floats;
    std::vector<std::string> strings;
    std::vector<uint8_t> bytes;
};

} // namespace bfrass::bfres
