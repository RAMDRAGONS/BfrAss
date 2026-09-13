#include "importer/BfresImporter.hpp"

#include "bfres/ResFile.hpp"
#include "importer/SceneBuilder.hpp"

#include <assimp/DefaultLogger.hpp>
#include <assimp/Exceptional.h>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/Importer.hpp>
#include <assimp/importerdesc.h>
#include <assimp/scene.h>

#include <chrono>
#include <cstring>
#include <memory>

namespace bfrass::importer {

namespace {

const aiImporterDesc kDescription = {
    "NintendoWare BFRES Importer",
    "",
    "",
    "Wii U (FRES 3.x/4.x) and Switch (FRES 0.x-10.x) models with BNTX/FTEX textures",
    aiImporterFlags_SupportBinaryFlavour | aiImporterFlags_SupportCompressedFlavour,
    0,
    0,
    0,
    0,
    "bfres sbfres fmdb",
};

std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t end = s.find(';', start);
        const std::string item = s.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!item.empty()) {
            out.push_back(item);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return out;
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

Log makeAssimpLog(const ImportConfig& config) {
    Log log([](LogChannel channel, std::string_view text) {
        const std::string message(text);
        switch (channel) {
        case LogChannel::Warning: ASSIMP_LOG_WARN(message); break;
        case LogChannel::Info: ASSIMP_LOG_INFO(message); break;
        default: ASSIMP_LOG_DEBUG(message); break;
        }
    });
    log.enable(LogChannel::Debug, config.printDebugInfo);
    log.enable(LogChannel::Material, config.printMaterialInfo);
    return log;
}

} // namespace

bool BfresImporter::CanRead(const std::string& file, Assimp::IOSystem* io, bool) const {
    if (!io) {
        return false;
    }
    std::unique_ptr<Assimp::IOStream> stream(io->Open(file, "rb"));
    if (!stream) {
        return false;
    }
    uint8_t header[0x18] = {};
    const size_t read = stream->Read(header, 1, sizeof(header));
    if (read >= 8 && std::memcmp(header, "FRES", 4) == 0) {
        return true;
    }
    // Yaz0 streams usually start with a literal run, so the FRES signature shows up
    // right after the first group header.
    return read >= 0x15 && std::memcmp(header, "Yaz0", 4) == 0 && std::memcmp(header + 0x11, "FRES", 4) == 0;
}

const aiImporterDesc* BfresImporter::GetInfo() const {
    return &kDescription;
}

void BfresImporter::SetupProperties(const Assimp::Importer* importer) {
    ImportConfig c;
    c.models = splitList(importer->GetPropertyString(keys::Models, ""));

    const std::string format = lower(importer->GetPropertyString(keys::TextureFormat, "png"));
    c.textureFormat = format == "dds" ? TextureFileFormat::Dds : TextureFileFormat::Png;

    const std::string uv = lower(importer->GetPropertyString(keys::UvLayers, "merge"));
    c.uvLayers = uv == "split" ? UvLayerMode::Split : (uv == "none" || uv == "no") ? UvLayerMode::None : UvLayerMode::Merge;

    const std::string rig = lower(importer->GetPropertyString(keys::Rigging, "reset"));
    c.rigging = (rig == "skin" || rig == "noreset") ? RiggingMode::SkinOnly
                : (rig == "none" || rig == "no")    ? RiggingMode::None
                                                    : RiggingMode::SkinAndReset;

    const std::string path = lower(importer->GetPropertyString(keys::TexturePath, "relative"));
    c.texturePath = path == "absolute" ? TexturePathMode::Absolute
                    : (path == "sub" || path == "subfolder") ? TexturePathMode::Subfolder
                                                             : TexturePathMode::Relative;

    c.importLods = importer->GetPropertyBool(keys::ImportLods, false);
    c.vertexColors = importer->GetPropertyBool(keys::VertexColors, true);
    c.printMaterialInfo = importer->GetPropertyBool(keys::PrintMaterialInfo, false);
    c.printDebugInfo = importer->GetPropertyBool(keys::PrintDebugInfo, false);
    c.exportTextures = importer->GetPropertyBool(keys::ExportTextures, false);
    c.embedTextures = importer->GetPropertyBool(keys::EmbedTextures, !c.exportTextures);
    c.textureDirectory = importer->GetPropertyString(keys::TextureDirectory, "");
    c.referenceDirectory = importer->GetPropertyString(keys::ReferenceDirectory, "");
    c.exportUnreferencedTextures = importer->GetPropertyBool(keys::UnreferencedTextures, false);
    c.applyChannelMap = importer->GetPropertyBool(keys::ChannelMap, true);
    c.reconstructNormalZ = importer->GetPropertyBool(keys::NormalZ, true);
    c.morphTargetsAsMeshes = importer->GetPropertyBool(keys::MorphsAsMeshes, false);
    for (const auto& source : splitList(importer->GetPropertyString(keys::TextureSources, ""))) {
        c.textureSources.emplace_back(source);
    }
    c.autoTextureSiblings = importer->GetPropertyBool(keys::AutoTextureSiblings, true);
    config_ = std::move(c);
    logPointer_ = importer->GetPropertyPointer(keys::LogPointer, nullptr);
}

void BfresImporter::InternReadFile(const std::string& file, aiScene* scene, Assimp::IOSystem* io) {
    const auto start = std::chrono::steady_clock::now();
    const Log fallbackLog = makeAssimpLog(config_);
    const Log& log = logPointer_ ? *static_cast<const Log*>(logPointer_) : fallbackLog;

    std::unique_ptr<Assimp::IOStream> stream(io->Open(file, "rb"));
    if (!stream) {
        throw DeadlyImportError("BFRES: failed to open ", file);
    }
    std::vector<uint8_t> data(stream->FileSize());
    if (!data.empty() && stream->Read(data.data(), 1, data.size()) != data.size()) {
        throw DeadlyImportError("BFRES: failed to read ", file);
    }
    stream.reset();

    try {
        const std::filesystem::path path(file);
        bfres::ResFile res = bfres::loadResFile(std::move(data), path.filename().string(), log);
        res.sourcePath = path;
        bfres::LoadOptions options;
        options.textureSources = config_.textureSources;
        options.autoTextureSiblings = config_.autoTextureSiblings;
        bfres::attachTextureSources(res, path, options, log);

        SceneBuilder(res, config_, log).build(scene);
    } catch (const DeadlyImportError&) {
        throw;
    } catch (const std::exception& e) {
        throw DeadlyImportError("BFRES: ", e.what());
    }

    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    log.info("Done! ({:.3f} Seconds)", seconds);
}

void registerImporter(Assimp::Importer& importer) {
    importer.RegisterLoader(new BfresImporter());
}

} // namespace bfrass::importer
