#include "bfres/ResFile.hpp"

#include "core/Yaz0.hpp"
#include "texture/TextureContainers.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace bfrass::bfres {

namespace {

std::vector<uint8_t> readWholeFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw FormatError("cannot open " + path.string());
    }
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    stream.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (size > 0 && !stream.read(reinterpret_cast<char*>(data.data()), size)) {
        throw FormatError("failed reading " + path.string());
    }
    return data;
}

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// Adds textures from a secondary archive, joining Wii U .Tex1 base levels with
// the mip chains that live in the matching .Tex2 archive.
void mergeTextures(ResFile& target, std::vector<tex::TextureResource> textures, const Log& log) {
    for (auto& t : textures) {
        auto existing = std::find_if(target.textures.begin(), target.textures.end(),
                                     [&](const tex::TextureResource& e) { return e.name == t.name; });
        if (existing == target.textures.end()) {
            target.textures.push_back(std::move(t));
            continue;
        }
        if (existing->mipData.empty() && !t.mipData.empty()) {
            existing->mipData = t.mipData;
            existing->mipOwner = t.mipOwner;
        } else if (existing->imageData.empty() && !t.imageData.empty()) {
            const auto mips = existing->mipData;
            const auto mipOwner = existing->mipOwner;
            *existing = std::move(t);
            if (existing->mipData.empty()) {
                existing->mipData = mips;
                existing->mipOwner = mipOwner;
            }
        } else {
            log.debug("Texture {} from {} already provided by {}", t.name, t.container, existing->container);
        }
    }
}

void loadTextureSource(ResFile& target, const std::filesystem::path& path, const Log& log) {
    std::vector<uint8_t> raw = readWholeFile(path);
    if (isYaz0(raw)) {
        raw = decompressYaz0(raw);
    }
    auto owner = std::make_shared<const std::vector<uint8_t>>(std::move(raw));
    const std::span<const uint8_t> view(*owner);
    const std::string name = path.filename().string();
    if (tex::isBntx(view)) {
        mergeTextures(target, tex::parseBntx(owner, view, name, log), log);
        return;
    }
    if (!isResFile(view)) {
        throw FormatError(path.string() + " is neither a BFRES nor a BNTX file");
    }
    const std::string lowered = lower(name);
    const bool tex2 = lowered.find(".tex2") != std::string::npos;
    const bool tex1 = lowered.find(".tex1") != std::string::npos;

    ResFile source;
    source.buffer = owner;
    source.sourcePath = path;
    if (std::memcmp(view.data() + 4, "    ", 4) == 0) {
        source.platform = Platform::Switch;
        source.version = Version::fromNx(view.data() + 0x08);
        detail::loadSwitch(source, log);
    } else {
        source.platform = Platform::WiiU;
        source.endian = Endian::Big;
        source.version = Version::fromCafe(view.data() + 4);
        detail::loadWiiU(source, log, !tex2, !tex1);
    }
    log.info("Loaded {} textures from {}", source.textures.size(), name);
    mergeTextures(target, std::move(source.textures), log);
}

} // namespace

const tex::TextureResource* ResFile::findTexture(std::string_view textureName) const {
    for (const auto& t : textures) {
        if (t.name == textureName) {
            return &t;
        }
    }
    for (const auto& t : textures) {
        if (iequals(t.name, textureName)) {
            return &t;
        }
    }
    return nullptr;
}

bool isResFile(std::span<const uint8_t> data) {
    return data.size() >= 0x20 && std::memcmp(data.data(), "FRES", 4) == 0;
}

ResFile loadResFile(std::vector<uint8_t> data, const std::string& displayName, const Log& log) {
    if (isYaz0(data)) {
        data = decompressYaz0(data);
    }
    if (!isResFile(data)) {
        throw FormatError(displayName + " is not a BFRES file");
    }
    ResFile file;
    file.buffer = std::make_shared<const std::vector<uint8_t>>(std::move(data));
    const auto& bytes = *file.buffer;

    // Switch files pad the signature with spaces; Wii U files put the version there.
    if (std::memcmp(bytes.data() + 4, "    ", 4) == 0) {
        file.platform = Platform::Switch;
        file.endian = Endian::Little;
        file.version = Version::fromNx(bytes.data() + 0x08);
        log.info("FRES Version: {}", file.version.toString());
        const unsigned major = file.version.major;
        if (major > 10 || major == 1 || major == 6 || major == 7) {
            log.warn("Switch BFRES version {} has no verified layout; reading it with the nearest known one",
                     file.version.toString());
        }
        detail::loadSwitch(file, log);
    } else {
        file.platform = Platform::WiiU;
        file.endian = Endian::Big;
        file.version = Version::fromCafe(bytes.data() + 4);
        log.info("FRES Version: {}", file.version.toString());
        if (file.version.major < 3 || file.version.major > 4) {
            log.warn("Wii U BFRES version {} is outside the tested range (3.x to 4.x)", file.version.toString());
        }
        detail::loadWiiU(file, log, true, true);
    }
    return file;
}

ResFile loadResFile(const std::filesystem::path& path, const LoadOptions& options, const Log& log) {
    ResFile file = loadResFile(readWholeFile(path), path.filename().string(), log);
    file.sourcePath = path;
    attachTextureSources(file, path, options, log);
    return file;
}

void attachTextureSources(ResFile& file, const std::filesystem::path& path, const LoadOptions& options,
                          const Log& log) {
    std::vector<std::filesystem::path> sources = options.textureSources;
    if (options.autoTextureSiblings) {
        std::string stem = path.filename().string();
        const std::string loweredStem = lower(stem);
        for (const char* ext : {".sbfres", ".bfres"}) {
            if (loweredStem.size() > std::strlen(ext) && loweredStem.ends_with(ext)) {
                stem.resize(stem.size() - std::strlen(ext));
                break;
            }
        }
        if (lower(stem).find(".tex") == std::string::npos && std::filesystem::exists(path)) {
            for (const char* suffix : {".Tex", ".Tex1", ".Tex2"}) {
                for (const char* ext : {".bfres", ".sbfres"}) {
                    const auto candidate = path.parent_path() / (stem + suffix + ext);
                    if (std::filesystem::exists(candidate) &&
                        std::find(sources.begin(), sources.end(), candidate) == sources.end()) {
                        sources.push_back(candidate);
                    }
                }
            }
        }
    }
    for (const auto& source : sources) {
        try {
            loadTextureSource(file, source, log);
        } catch (const std::exception& e) {
            log.warn("Could not load textures from {}: {}", source.string(), e.what());
        }
    }
}

} // namespace bfrass::bfres
