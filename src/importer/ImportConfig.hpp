#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace bfrass::importer {

// Options mirror the original 3ds Max importer's dialog; each has an Assimp
// property key so the importer can be driven through Assimp::Importer.
enum class TextureFileFormat { Png, Dds };
enum class UvLayerMode { Merge, Split, None };
// "Yes/Yes", "Yes/No" and "No" of "Import rigging / reset XForms".
enum class RiggingMode { SkinAndReset, SkinOnly, None };
enum class TexturePathMode { Absolute, Relative, Subfolder };

struct ImportConfig {
    // Model names or zero-based indices to import; empty imports every model.
    std::vector<std::string> models;

    TextureFileFormat textureFormat = TextureFileFormat::Png;
    UvLayerMode uvLayers = UvLayerMode::Merge;
    RiggingMode rigging = RiggingMode::SkinAndReset;
    bool importLods = false;
    bool vertexColors = true;
    TexturePathMode texturePath = TexturePathMode::Relative;
    bool printMaterialInfo = false;
    bool printDebugInfo = false;

    // Write decoded textures to disk next to the referencing paths.
    bool exportTextures = false;
    // Store textures inside the aiScene (aiTexture) and reference them as "*N".
    bool embedTextures = true;
    // Directory textures are written to; defaults to the BFRES file's directory.
    std::filesystem::path textureDirectory;
    // Directory relative texture paths are expressed against (usually the output file's).
    std::filesystem::path referenceDirectory;
    // Also export textures no material references (always true for Wii U/Switch archives without models).
    bool exportUnreferencedTextures = false;

    bool applyChannelMap = true;
    bool reconstructNormalZ = true;
    bool morphTargetsAsMeshes = false;

    std::vector<std::filesystem::path> textureSources;
    bool autoTextureSiblings = true;
};

namespace keys {
inline constexpr const char* Models = "BFRES_MODELS";                        // string, ';' separated
inline constexpr const char* TextureFormat = "BFRES_TEXTURE_FORMAT";         // "png" | "dds"
inline constexpr const char* UvLayers = "BFRES_UV_LAYERS";                   // "merge" | "split" | "none"
inline constexpr const char* Rigging = "BFRES_RIGGING";                      // "reset" | "skin" | "none"
inline constexpr const char* ImportLods = "BFRES_IMPORT_LODS";               // bool
inline constexpr const char* VertexColors = "BFRES_VERTEX_COLORS";           // bool
inline constexpr const char* TexturePath = "BFRES_TEXTURE_PATH";             // "absolute" | "relative" | "sub"
inline constexpr const char* PrintMaterialInfo = "BFRES_PRINT_MATERIAL_INFO"; // bool
inline constexpr const char* PrintDebugInfo = "BFRES_PRINT_DEBUG_INFO";      // bool
inline constexpr const char* ExportTextures = "BFRES_EXPORT_TEXTURES";       // bool
inline constexpr const char* EmbedTextures = "BFRES_EMBED_TEXTURES";         // bool
inline constexpr const char* TextureDirectory = "BFRES_TEXTURE_DIRECTORY";   // string
inline constexpr const char* ReferenceDirectory = "BFRES_REFERENCE_DIRECTORY"; // string
inline constexpr const char* UnreferencedTextures = "BFRES_EXPORT_UNREFERENCED_TEXTURES"; // bool
inline constexpr const char* ChannelMap = "BFRES_APPLY_CHANNEL_MAP";         // bool
inline constexpr const char* NormalZ = "BFRES_RECONSTRUCT_NORMAL_Z";         // bool
inline constexpr const char* MorphsAsMeshes = "BFRES_MORPHS_AS_MESHES";      // bool
inline constexpr const char* TextureSources = "BFRES_TEXTURE_SOURCES";       // string, ';' separated paths
inline constexpr const char* AutoTextureSiblings = "BFRES_AUTO_TEXTURE_SIBLINGS"; // bool
inline constexpr const char* LogPointer = "BFRES_LOG";                       // pointer to bfrass::Log
} // namespace keys

} // namespace bfrass::importer
