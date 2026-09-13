#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>

namespace bfrass {

class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class Endian { Little, Big };

// Random-access reader over an in-memory file. Every read is absolute and bounds
// checked, which matches how BFRES structures are traversed through offsets.
class BinaryReader {
public:
    BinaryReader() = default;
    BinaryReader(std::span<const uint8_t> data, Endian endian) : data_(data), endian_(endian) {}

    std::span<const uint8_t> data() const { return data_; }
    size_t size() const { return data_.size(); }
    Endian endian() const { return endian_; }
    void setEndian(Endian e) { endian_ = e; }

    void require(uint64_t offset, uint64_t count) const {
        if (offset > data_.size() || count > data_.size() - offset) {
            throw FormatError("read of " + std::to_string(count) + " bytes at 0x" + toHex(offset) +
                              " exceeds file size 0x" + toHex(data_.size()));
        }
    }

    uint8_t u8(uint64_t o) const {
        require(o, 1);
        return data_[o];
    }
    int8_t s8(uint64_t o) const { return static_cast<int8_t>(u8(o)); }
    uint16_t u16(uint64_t o) const { return load<uint16_t>(o); }
    int16_t s16(uint64_t o) const { return static_cast<int16_t>(u16(o)); }
    uint32_t u32(uint64_t o) const { return load<uint32_t>(o); }
    int32_t s32(uint64_t o) const { return static_cast<int32_t>(u32(o)); }
    uint64_t u64(uint64_t o) const { return load<uint64_t>(o); }
    int64_t s64(uint64_t o) const { return static_cast<int64_t>(u64(o)); }
    float f32(uint64_t o) const { return std::bit_cast<float>(u32(o)); }

    uint16_t u16le(uint64_t o) const { return loadAs<uint16_t>(o, Endian::Little); }
    uint32_t u32le(uint64_t o) const { return loadAs<uint32_t>(o, Endian::Little); }
    uint16_t u16be(uint64_t o) const { return loadAs<uint16_t>(o, Endian::Big); }
    uint32_t u32be(uint64_t o) const { return loadAs<uint32_t>(o, Endian::Big); }

    std::string fixedString(uint64_t o, size_t len) const {
        require(o, len);
        return std::string(reinterpret_cast<const char*>(data_.data() + o), len);
    }

    std::string cString(uint64_t o) const {
        require(o, 1);
        const auto* begin = reinterpret_cast<const char*>(data_.data() + o);
        const auto* end = static_cast<const char*>(std::memchr(begin, 0, data_.size() - o));
        return end ? std::string(begin, end) : std::string(begin, data_.size() - o);
    }

    std::span<const uint8_t> bytes(uint64_t o, uint64_t len) const {
        require(o, len);
        return data_.subspan(static_cast<size_t>(o), static_cast<size_t>(len));
    }

    static std::string toHex(uint64_t v);

private:
    template <typename T>
    T load(uint64_t o) const { return loadAs<T>(o, endian_); }

    template <typename T>
    T loadAs(uint64_t o, Endian e) const {
        require(o, sizeof(T));
        T v;
        std::memcpy(&v, data_.data() + o, sizeof(T));
        const bool hostLittle = std::endian::native == std::endian::little;
        if ((e == Endian::Little) != hostLittle) {
            v = swapBytes(v);
        }
        return v;
    }

    template <typename T>
    static T swapBytes(T v) {
        T out{};
        auto* src = reinterpret_cast<const uint8_t*>(&v);
        auto* dst = reinterpret_cast<uint8_t*>(&out);
        for (size_t i = 0; i < sizeof(T); ++i) {
            dst[i] = src[sizeof(T) - 1 - i];
        }
        return out;
    }

    std::span<const uint8_t> data_;
    Endian endian_ = Endian::Little;
};

inline uint64_t alignUp(uint64_t value, uint64_t alignment) {
    if (alignment <= 1) {
        return value;
    }
    return (value + alignment - 1) & ~(alignment - 1);
}

inline uint32_t divRoundUp(uint32_t n, uint32_t d) {
    return (n + d - 1) / d;
}

} // namespace bfrass
