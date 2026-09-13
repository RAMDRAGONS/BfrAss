#include "importer/SceneBuilder.hpp"

#include "bfres/Dump.hpp"
#include "texture/TextureExporter.hpp"

#include <assimp/material.h>
#include <assimp/texture.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <utility>

namespace bfrass::importer {

namespace {

std::string fileStem(const std::filesystem::path& path) {
    return path.stem().string();
}

void addTexture(aiMaterial* mat, aiTextureType type, unsigned index, const std::string& path, int flags = -1,
                int op = -1) {
    const aiString s(path);
    mat->AddProperty(&s, AI_MATKEY_TEXTURE(type, index));
    const int uv = 0;
    mat->AddProperty(&uv, 1, AI_MATKEY_UVWSRC(type, index));
    if (flags >= 0) {
        mat->AddProperty(&flags, 1, AI_MATKEY_TEXFLAGS(type, index));
    }
    if (op >= 0) {
        mat->AddProperty(&op, 1, AI_MATKEY_TEXOP(type, index));
    }
}

void addString(aiMaterial* mat, const std::string& key, const std::string& value) {
    const aiString s(value);
    mat->AddProperty(&s, key.c_str(), 0, 0);
}

unsigned nextIndex(const aiMaterial* mat, aiTextureType type) {
    return mat->GetTextureCount(type);
}

const char* finishingName(uint32_t value, std::string& storage) {
    switch (value) {
    case 3328: return "Glossy";
    case 3580: return "Matte";
    default:
        storage = std::to_string(value);
        return storage.c_str();
    }
}

} // namespace

TextureTable::TextureTable(const bfres::ResFile& file, const ImportConfig& config, const Log& log)
    : file_(file), config_(config), log_(log) {}

void TextureTable::prepare(const std::vector<std::string>& referencedNames) {
    std::vector<std::string> names = referencedNames;
    if (config_.exportUnreferencedTextures) {
        for (const auto& t : file_.textures) {
            names.push_back(t.name);
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());

    const tex::ExportFormat format =
        config_.textureFormat == TextureFileFormat::Dds ? tex::ExportFormat::Dds : tex::ExportFormat::Png;
    const char* extension = tex::extensionFor(format);
    std::filesystem::path baseDirectory = config_.textureDirectory;
    if (baseDirectory.empty()) {
        baseDirectory = file_.sourcePath.has_parent_path() ? file_.sourcePath.parent_path() : std::filesystem::path(".");
    }
    std::filesystem::path directory = baseDirectory;
    if (config_.texturePath == TexturePathMode::Subfolder) {
        directory /= fileStem(file_.sourcePath.empty() ? std::filesystem::path(file_.name) : file_.sourcePath);
    }
    const std::filesystem::path referenceBase =
        config_.referenceDirectory.empty() ? baseDirectory : config_.referenceDirectory;

    struct Job {
        std::string name;
        const tex::TextureResource* texture;
    };
    std::vector<Job> jobs;
    for (const auto& name : names) {
        const tex::TextureResource* texture = file_.findTexture(name);
        const std::filesystem::path filePath = directory / (name + "." + extension);
        std::string reference;
        switch (config_.texturePath) {
        case TexturePathMode::Absolute:
            reference = std::filesystem::absolute(filePath).lexically_normal().generic_string();
            break;
        default:
            reference = std::filesystem::absolute(filePath)
                            .lexically_normal()
                            .lexically_relative(std::filesystem::absolute(referenceBase).lexically_normal())
                            .generic_string();
            if (reference.empty()) {
                reference = filePath.filename().generic_string();
            }
            break;
        }
        paths_[name] = reference;
        if (!texture) {
            log_.warn("Texture {} is not stored in {} or the supplied texture archives", name, file_.name);
            continue;
        }
        if (!tex::canDecode(texture->format) && (format == tex::ExportFormat::Png || !tex::dxgiFormat(texture->format))) {
            log_.warn("Texture {} uses unsupported format {}", name, texture->format.name());
            continue;
        }
        if (texture->availableMips() == 0) {
            log_.warn("Texture {} has no base level data (is the matching .Tex1 archive missing?)", name);
            continue;
        }
        jobs.push_back({name, texture});
    }

    tex::ExportOptions options;
    options.format = format;
    options.decode.applyChannelMap = config_.applyChannelMap;
    options.decode.reconstructNormalZ = config_.reconstructNormalZ;

    std::vector<std::vector<uint8_t>> embeddedBytes(config_.embedTextures ? jobs.size() : 0);
    std::mutex logMutex;
    tex::parallelFor(jobs.size(), [&](size_t i) {
        const Job& job = jobs[i];
        try {
            if (config_.exportTextures) {
                tex::exportTexture(*job.texture, directory, options);
            }
            if (config_.embedTextures) {
                embeddedBytes[i] = tex::encodeTexture(*job.texture, options).bytes;
            }
        } catch (const std::exception& e) {
            std::lock_guard lock(logMutex);
            log_.warn("Failed to convert texture {}: {}", job.name, e.what());
        }
    });
    if (config_.exportTextures && !jobs.empty()) {
        log_.info("Wrote {} texture(s) to {}", jobs.size(), directory.string());
    }

    if (config_.embedTextures) {
        for (size_t i = 0; i < jobs.size(); ++i) {
            auto& bytes = embeddedBytes[i];
            if (bytes.empty()) {
                continue;
            }
            auto* texture = new aiTexture();
            texture->mWidth = static_cast<unsigned>(bytes.size());
            texture->mHeight = 0;
            texture->pcData = new aiTexel[(bytes.size() + sizeof(aiTexel) - 1) / sizeof(aiTexel)];
            std::memcpy(texture->pcData, bytes.data(), bytes.size());
            std::strncpy(texture->achFormatHint, extension, HINTMAXTEXTURELEN - 1);
            texture->mFilename = aiString(jobs[i].name + "." + extension);
            paths_[jobs[i].name] = "*" + std::to_string(embedded_.size());
            embedded_.push_back(texture);
        }
    }
}

std::string TextureTable::pathFor(const std::string& textureName) const {
    const auto it = paths_.find(textureName);
    return it == paths_.end() ? textureName : it->second;
}

std::vector<aiTexture*> TextureTable::releaseEmbedded() {
    return std::exchange(embedded_, {});
}

aiMaterial* buildMaterial(const bfres::Material& material, const bfres::ResFile& file, const TextureTable& textures,
                          const ImportConfig& config, const Log& log) {
    auto* mat = new aiMaterial();
    const aiString name(material.name);
    mat->AddProperty(&name, AI_MATKEY_NAME);
    const aiColor3D white(1, 1, 1);
    mat->AddProperty(&white, 1, AI_MATKEY_COLOR_DIFFUSE);
    const int twoSided = std::any_of(material.renderInfos.begin(), material.renderInfos.end(),
                                     [](const bfres::RenderInfo& r) {
                                         return r.name == "display_face" && !r.strings.empty() &&
                                                r.strings[0] == "both";
                                     })
                             ? 1
                             : 0;
    mat->AddProperty(&twoSided, 1, AI_MATKEY_TWOSIDED);

    const size_t count = std::min(material.samplers.size(), material.textureNames.size());
    log.debug("{} uses the following textures:", material.name);

    // First pass assigns the albedo map, which later layers composite onto.
    bool matte = false;
    std::string albedoPath;
    for (size_t t = 0; t < count; ++t) {
        const auto& sampler = material.samplers[t];
        const std::string& textureName = material.textureNames[t];
        if (log.enabled(LogChannel::Debug)) {
            std::string storage;
            log.debug("#{}: {}, {}, Unk1= 0x{:X}, Finishing={}, Unk3=0x{:X}, Unk4={}", t + 1, textureName,
                      sampler.name, sampler.legacyFields[0], finishingName(sampler.legacyFields[1], storage),
                      sampler.legacyFields[2], sampler.legacyFields[3]);
        }
        if (sampler.name == "_a0" || sampler.name == "_albedo0") {
            albedoPath = textures.pathFor(textureName);
            addTexture(mat, aiTextureType_DIFFUSE, nextIndex(mat, aiTextureType_DIFFUSE), albedoPath);
            addTexture(mat, aiTextureType_BASE_COLOR, nextIndex(mat, aiTextureType_BASE_COLOR), albedoPath);
            addTexture(mat, aiTextureType_OPACITY, nextIndex(mat, aiTextureType_OPACITY), albedoPath,
                       aiTextureFlags_UseAlpha);
            matte = sampler.legacyFields[1] == 3580;
        }
    }
    if (matte && !albedoPath.empty()) {
        addTexture(mat, aiTextureType_SPECULAR, nextIndex(mat, aiTextureType_SPECULAR), albedoPath);
    }

    for (size_t t = 0; t < count; ++t) {
        const std::string& s = material.samplers[t].name;
        const std::string path = textures.pathFor(material.textureNames[t]);
        if (s == "sampler0" || s == "_a0" || s == "_albedo0") {
            continue;
        }
        if (s == "_sd0") {
            // Shadow maps multiply over the albedo, as the original composite map did.
            addTexture(mat, aiTextureType_DIFFUSE, nextIndex(mat, aiTextureType_DIFFUSE), path, -1,
                       aiTextureOp_Multiply);
        } else if (s == "_ao0") {
            addTexture(mat, aiTextureType_AMBIENT_OCCLUSION, nextIndex(mat, aiTextureType_AMBIENT_OCCLUSION), path);
        } else if (s == "_em0" || s == "_e0") {
            addTexture(mat, aiTextureType_EMISSIVE, nextIndex(mat, aiTextureType_EMISSIVE), path);
        } else if (s == "_n0" || s == "_normal0") {
            addTexture(mat, aiTextureType_NORMALS, nextIndex(mat, aiTextureType_NORMALS), path);
        } else if (s == "_s0") {
            addTexture(mat, aiTextureType_SPECULAR, nextIndex(mat, aiTextureType_SPECULAR), path);
        } else if (s == "_r0") {
            addTexture(mat, aiTextureType_SHININESS, nextIndex(mat, aiTextureType_SHININESS), path);
        } else if (s == "_rn0") {
            addTexture(mat, aiTextureType_SHININESS, nextIndex(mat, aiTextureType_SHININESS), path);
            addTexture(mat, aiTextureType_DIFFUSE_ROUGHNESS, nextIndex(mat, aiTextureType_DIFFUSE_ROUGHNESS), path);
        } else if (s == "_x0") {
            addTexture(mat, aiTextureType_REFLECTION, nextIndex(mat, aiTextureType_REFLECTION), path);
        } else {
            static const std::map<std::string, const char*> kKnown = {
                {"_b0", "Bake Map"},        {"_g0", "Alt. Bake? Map"}, {"_l0", "Light Map"},
                {"_p0", "Paper Map"},       {"_rt0", "Phong Warp? Map"}, {"_t0", "TRM? Map"},
                {"_tc0", "Team Color Map"},
            };
            const auto known = kKnown.find(s);
            if (known != kKnown.end()) {
                log.debug("{} ({}) not applied!", s, known->second);
            } else {
                log.debug("{} not applied!", s);
            }
            // Keep the binding reachable for consumers that understand the shader.
            addTexture(mat, aiTextureType_UNKNOWN, nextIndex(mat, aiTextureType_UNKNOWN), path);
        }
    }

    for (size_t t = 0; t < count; ++t) {
        addString(mat, "$bfres.sampler." + material.samplers[t].name, material.textureNames[t]);
    }
    for (const auto& param : material.shaderParams) {
        addString(mat, "$bfres.param." + param.name, bfres::formatShaderParam(param, file.endian).substr(param.name.size() + 2));
    }
    for (const auto& info : material.renderInfos) {
        addString(mat, "$bfres.renderinfo." + info.name, bfres::formatRenderInfo(info).substr(info.name.size() + 2));
    }
    if (material.shaderAssign) {
        const auto& sa = *material.shaderAssign;
        addString(mat, "$bfres.shader.archive", sa.archiveName);
        addString(mat, "$bfres.shader.model", sa.modelName);
        for (const auto& [key, value] : sa.samplerAssigns) {
            addString(mat, "$bfres.shader.sampler." + key, value);
        }
        for (const auto& [key, value] : sa.attribAssigns) {
            addString(mat, "$bfres.shader.attribute." + key, value);
        }
        for (const auto& [key, value] : sa.options) {
            addString(mat, "$bfres.shader.option." + key, value);
        }
    }
    for (const auto& ud : material.userData) {
        addString(mat, "$bfres.userdata." + ud.name, bfres::formatUserData(ud).substr(ud.name.size() + 2));
    }

    if (config.printMaterialInfo) {
        bfres::printMaterialInfo(material, file.endian, log);
    }
    return mat;
}

} // namespace bfrass::importer
