#include "bfres/Dump.hpp"

#include <cstring>
#include <format>

namespace bfrass::bfres {

namespace {

uint32_t readU32(const std::vector<uint8_t>& bytes, size_t offset, Endian endian) {
    if (offset + 4 > bytes.size()) {
        return 0;
    }
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        const uint32_t b = bytes[offset + i];
        v |= endian == Endian::Little ? b << (8 * i) : b << (8 * (3 - i));
    }
    return v;
}

float readF32(const std::vector<uint8_t>& bytes, size_t offset, Endian endian) {
    const uint32_t v = readU32(bytes, offset, endian);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

std::string number(double v) {
    std::string s = std::format("{}", v);
    if (s.find_first_of(".eEn") == std::string::npos) {
        s += ".0";
    }
    return s;
}

std::string joinFloats(const std::vector<double>& v, size_t begin, size_t count) {
    std::string out;
    for (size_t i = 0; i < count && begin + i < v.size(); ++i) {
        if (i) {
            out += ", ";
        }
        out += number(v[begin + i]);
    }
    return out;
}

const char* axisName(uint32_t mode) {
    switch (mode) {
    case 0: return "Maya";
    case 1: return "3DS Max";
    case 2: return "Softimage";
    default: return "Unknown";
    }
}

} // namespace

std::vector<double> shaderParamValues(const ShaderParam& param, Endian endian) {
    std::vector<double> values;
    const auto type = static_cast<unsigned>(param.type);
    const size_t words = param.value.size() / 4;
    const bool integer = type <= static_cast<unsigned>(ShaderParamType::UInt4);
    const bool isSigned = type >= static_cast<unsigned>(ShaderParamType::Int) &&
                          type <= static_cast<unsigned>(ShaderParamType::Int4);
    for (size_t i = 0; i < words; ++i) {
        if (integer) {
            const uint32_t raw = readU32(param.value, 4 * i, endian);
            values.push_back(isSigned ? double(int32_t(raw)) : double(raw));
        } else if ((param.type == ShaderParamType::TexSrt || param.type == ShaderParamType::TexSrtEx) &&
                   (i == 0 || (param.type == ShaderParamType::TexSrtEx && i == 6))) {
            values.push_back(double(readU32(param.value, 4 * i, endian)));
        } else {
            values.push_back(readF32(param.value, 4 * i, endian));
        }
    }
    return values;
}

std::string formatShaderParam(const ShaderParam& param, Endian endian) {
    const std::vector<double> v = shaderParamValues(param, endian);
    const auto type = static_cast<unsigned>(param.type);
    std::string body;

    if (type <= static_cast<unsigned>(ShaderParamType::Bool4)) {
        for (size_t i = 0; i < v.size() && i <= type; ++i) {
            body += (i ? ", " : "");
            body += v[i] != 0 ? "True" : "False";
        }
    } else if (type <= static_cast<unsigned>(ShaderParamType::UInt4)) {
        const size_t n = (type - static_cast<unsigned>(ShaderParamType::Int)) % 4 + 1;
        for (size_t i = 0; i < n && i < v.size(); ++i) {
            body += (i ? ", " : "") + std::format("{}", static_cast<int64_t>(v[i]));
        }
    } else if (type <= static_cast<unsigned>(ShaderParamType::Float4)) {
        body = joinFloats(v, 0, type - static_cast<unsigned>(ShaderParamType::Float) + 1);
    } else if (type <= static_cast<unsigned>(ShaderParamType::Float4x4)) {
        // Float{C}x{R}: R groups of C values; the Reserved slots hold C x 1.
        const unsigned rel = type - static_cast<unsigned>(ShaderParamType::Reserved2);
        const unsigned columns = rel / 4 + 2;
        const unsigned rows = rel % 4 == 0 ? 1 : rel % 4 + 1;
        for (unsigned r = 0; r < rows; ++r) {
            body += (r ? " | " : "") + joinFloats(v, r * columns, columns);
        }
    } else if (param.type == ShaderParamType::Srt2D && v.size() >= 5) {
        body = std::format("2D Scale X = {}, Y = {} | Rotate = {} | Translation X = {}, Y = {}", number(v[0]),
                           number(v[1]), number(v[2]), number(v[3]), number(v[4]));
    } else if (param.type == ShaderParamType::Srt3D && v.size() >= 9) {
        body = std::format("3D Scale X = {}, Y = {}, Z = {} | Rotate X = {}, Y = {}, Z = {} | Translation X = {}, "
                           "Y = {}, Z = {}",
                           number(v[0]), number(v[1]), number(v[2]), number(v[3]), number(v[4]), number(v[5]),
                           number(v[6]), number(v[7]), number(v[8]));
    } else if ((param.type == ShaderParamType::TexSrt || param.type == ShaderParamType::TexSrtEx) && v.size() >= 6) {
        body = std::format("Scale X = {}, Y = {} | Rotate = {} | Translation X = {}, Y = {} | Axis: {}", number(v[1]),
                           number(v[2]), number(v[3]), number(v[4]), number(v[5]), axisName(uint32_t(v[0])));
        if (param.type == ShaderParamType::TexSrtEx && v.size() >= 7) {
            body += std::format(" | Matrix Pointer: {}", static_cast<uint64_t>(v[6]));
        }
    } else {
        body = joinFloats(v, 0, v.size());
    }
    return param.name + ": " + body;
}

std::string formatRenderInfo(const RenderInfo& info) {
    std::string body;
    switch (info.type) {
    case RenderInfo::Type::Int32:
        for (size_t i = 0; i < info.ints.size(); ++i) {
            body += (i ? ", " : "") + std::to_string(info.ints[i]);
        }
        break;
    case RenderInfo::Type::Float:
        for (size_t i = 0; i < info.floats.size(); ++i) {
            body += (i ? ", " : "") + number(info.floats[i]);
        }
        break;
    case RenderInfo::Type::String:
        for (size_t i = 0; i < info.strings.size(); ++i) {
            body += (i ? ", " : "") + info.strings[i];
        }
        break;
    }
    return info.name + ": " + body;
}

std::string formatUserData(const UserData& data) {
    std::string body;
    switch (data.type) {
    case UserData::Type::Int32:
        for (size_t i = 0; i < data.ints.size(); ++i) {
            body += (i ? ", " : "") + std::to_string(data.ints[i]);
        }
        break;
    case UserData::Type::Float:
        for (size_t i = 0; i < data.floats.size(); ++i) {
            body += (i ? ", " : "") + number(data.floats[i]);
        }
        break;
    case UserData::Type::String:
    case UserData::Type::WString:
        for (size_t i = 0; i < data.strings.size(); ++i) {
            body += (i ? ", " : "") + data.strings[i];
        }
        break;
    case UserData::Type::Bytes:
        for (size_t i = 0; i < data.bytes.size(); ++i) {
            body += std::format("{:02X}", data.bytes[i]);
        }
        break;
    }
    return data.name + ": " + body;
}

void printMaterialInfo(const Material& material, Endian endian, const Log& log) {
    if (!log.enabled(LogChannel::Material)) {
        return;
    }
    log.material("--------------------");
    log.material("Texture properties for {}:", material.name);
    log.material("--------------------");
    for (const auto& param : material.shaderParams) {
        log.material("{}", formatShaderParam(param, endian));
    }
    if (!material.renderInfos.empty()) {
        log.material("Render info:");
        for (const auto& info : material.renderInfos) {
            log.material("  {}", formatRenderInfo(info));
        }
    }
    if (material.shaderAssign) {
        const auto& sa = *material.shaderAssign;
        log.material("Shader: {} / {} (revision {})", sa.archiveName, sa.modelName, sa.revision);
        for (const auto& [key, value] : sa.samplerAssigns) {
            log.material("  sampler {} = {}", key, value);
        }
        for (const auto& [key, value] : sa.attribAssigns) {
            log.material("  attribute {} = {}", key, value);
        }
        for (const auto& [key, value] : sa.options) {
            log.material("  option {} = {}", key, value);
        }
    }
    for (const auto& ud : material.userData) {
        log.material("User data {}", formatUserData(ud));
    }
}

std::vector<std::string> describeResFile(const ResFile& file) {
    std::vector<std::string> lines;
    lines.push_back(std::format("{} ({} BFRES {}), {} model(s), {} texture(s)", file.name,
                                file.platform == Platform::WiiU ? "Wii U" : "Switch", file.version.toString(),
                                file.models.size(), file.textures.size()));
    for (size_t i = 0; i < file.models.size(); ++i) {
        const auto& m = file.models[i];
        lines.push_back(std::format("  [{}] {}: {} shape(s), {} material(s), {} bone(s), {} vertex buffer(s)", i,
                                    m.name, m.shapes.size(), m.materials.size(), m.skeleton.bones.size(),
                                    m.vertexBuffers.size()));
    }
    const auto listNames = [&](const char* label, const std::vector<std::string>& names) {
        if (!names.empty()) {
            lines.push_back(std::format("  {}: {}", label, names.size()));
        }
    };
    listNames("Skeletal animations", file.skeletalAnimations);
    listNames("Material animations", file.materialAnimations);
    listNames("Bone visibility animations", file.boneVisibilityAnimations);
    listNames("Shape animations", file.shapeAnimations);
    listNames("Scene animations", file.sceneAnimations);
    for (const auto& ext : file.externalFiles) {
        lines.push_back(std::format("  External file: {} ({} bytes)", ext.name, ext.data.size()));
    }
    return lines;
}

} // namespace bfrass::bfres
