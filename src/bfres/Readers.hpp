#pragma once

#include "core/BinaryReader.hpp"

#include <string>
#include <vector>

namespace bfrass::bfres {

inline void appendUtf8(std::string& utf8, uint32_t cp) {
    if (cp < 0x80) {
        utf8.push_back(char(cp));
    } else if (cp < 0x800) {
        utf8.push_back(char(0xC0 | (cp >> 6)));
        utf8.push_back(char(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        utf8.push_back(char(0xE0 | (cp >> 12)));
        utf8.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        utf8.push_back(char(0x80 | (cp & 0x3F)));
    } else {
        utf8.push_back(char(0xF0 | (cp >> 18)));
        utf8.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
        utf8.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        utf8.push_back(char(0x80 | (cp & 0x3F)));
    }
}

// Zero-terminated UTF-16 in the reader's byte order, converted to UTF-8.
inline std::string utf16String(const BinaryReader& r, uint64_t target) {
    std::string utf8;
    if (target == 0) {
        return utf8;
    }
    for (uint64_t p = target;; p += 2) {
        uint32_t cp = r.u16(p);
        if (cp == 0) {
            break;
        }
        if (cp >= 0xD800 && cp < 0xDC00) {
            const uint16_t low = r.u16(p + 2);
            if (low >= 0xDC00 && low < 0xE000) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                p += 2;
            }
        }
        appendUtf8(utf8, cp);
    }
    return utf8;
}

// Zero-terminated UTF-32 in the reader's byte order, converted to UTF-8.
inline std::string utf32String(const BinaryReader& r, uint64_t target) {
    std::string utf8;
    for (uint64_t p = target;; p += 4) {
        const uint32_t cp = r.u32(p);
        if (cp == 0) {
            break;
        }
        appendUtf8(utf8, cp);
    }
    return utf8;
}

// Switch resources use absolute 64-bit pointers and length-prefixed strings.
struct NxReader {
    const BinaryReader& r;

    uint64_t ptr(uint64_t field) const { return r.u64(field); }

    std::string stringAt(uint64_t target) const {
        if (target == 0 || target + 2 > r.size()) {
            return {};
        }
        return r.fixedString(target + 2, r.u16(target));
    }

    std::string string(uint64_t field) const { return stringAt(ptr(field)); }

    // Keys of an nn::util::ResDic, excluding the root node.
    std::vector<std::string> dictKeys(uint64_t dict) const {
        std::vector<std::string> keys;
        if (dict == 0) {
            return keys;
        }
        const int32_t count = r.s32(dict + 4);
        for (int32_t i = 1; i <= count; ++i) {
            keys.push_back(string(dict + 8 + uint64_t(i) * 16 + 8));
        }
        return keys;
    }

    std::vector<std::string> stringArray(uint64_t array, size_t count) const {
        std::vector<std::string> out;
        if (array == 0) {
            return out;
        }
        for (size_t i = 0; i < count; ++i) {
            out.push_back(string(array + 8 * i));
        }
        return out;
    }
};

// Wii U resources use self-relative 32-bit offsets and zero-terminated strings.
struct CafeReader {
    const BinaryReader& r;

    uint64_t offset(uint64_t field) const {
        const int32_t v = r.s32(field);
        return v == 0 ? 0 : uint64_t(int64_t(field) + v);
    }

    std::string string(uint64_t field) const {
        const uint64_t target = offset(field);
        return target ? r.cString(target) : std::string();
    }

    struct DictEntry {
        std::string key;
        uint64_t data;
    };

    std::vector<DictEntry> dict(uint64_t dictOffset) const {
        std::vector<DictEntry> entries;
        if (dictOffset == 0) {
            return entries;
        }
        const int32_t count = r.s32(dictOffset + 4);
        for (int32_t i = 1; i <= count; ++i) {
            const uint64_t node = dictOffset + 8 + uint64_t(i) * 16;
            entries.push_back({string(node + 8), offset(node + 12)});
        }
        return entries;
    }

    std::vector<DictEntry> dictAt(uint64_t field) const { return dict(offset(field)); }
};

} // namespace bfrass::bfres
