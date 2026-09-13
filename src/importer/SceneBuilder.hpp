#pragma once

#include "bfres/ResFile.hpp"
#include "core/Log.hpp"
#include "importer/ImportConfig.hpp"

#include <map>
#include <string>
#include <vector>

struct aiMaterial;
struct aiMesh;
struct aiNode;
struct aiScene;
struct aiTexture;

namespace bfrass::importer {

// Resolves texture names used by materials to the path strings stored in
// aiMaterial texture slots, exporting or embedding the images as configured.
class TextureTable {
public:
    TextureTable(const bfres::ResFile& file, const ImportConfig& config, const Log& log);

    void prepare(const std::vector<std::string>& referencedNames);
    std::string pathFor(const std::string& textureName) const;
    std::vector<aiTexture*> releaseEmbedded();

private:
    const bfres::ResFile& file_;
    const ImportConfig& config_;
    const Log& log_;
    std::map<std::string, std::string> paths_;
    std::vector<aiTexture*> embedded_;
};

aiMaterial* buildMaterial(const bfres::Material& material, const bfres::ResFile& file, const TextureTable& textures,
                          const ImportConfig& config, const Log& log);

class SceneBuilder {
public:
    SceneBuilder(const bfres::ResFile& file, const ImportConfig& config, const Log& log);

    // Fills an empty scene; the scene takes ownership of every allocation.
    void build(aiScene* scene);

private:
    std::vector<size_t> selectModels() const;

    const bfres::ResFile& file_;
    const ImportConfig& config_;
    const Log& log_;
};

} // namespace bfrass::importer
