#include "core/BinaryReader.hpp"

#include <cstdio>

namespace bfrass {

std::string BinaryReader::toHex(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(v));
    return buf;
}

} // namespace bfrass
