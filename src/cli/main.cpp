#include "bfres/Dump.hpp"
#include "bfres/ResFile.hpp"
#include "importer/BfresImporter.hpp"
#include "importer/ImportConfig.hpp"
#include "texture/TextureExporter.hpp"

#include <assimp/DefaultLogger.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <format>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace bfrass;

namespace {

const char* kUsage = R"(bfrass - NintendoWare BFRES importer built on Assimp

Usage:
  bfrass info <file.bfres> [options]
  bfrass convert <file.bfres> -o <output.ext> [options]
  bfrass textures <file.bfres> -o <directory> [options]
  bfrass formats

Commands:
  info       List models, shapes, materials and textures ("Load BFRES...").
  convert    Import models through Assimp and export them in any Assimp export format.
  textures   Decode and write every texture of the file.
  formats    List the export format ids Assimp was built with.

Model selection:
  -m, --model <name|index>   Import only this model (repeatable, default: all models).

Import options (mirroring the original 3ds Max importer):
  --tex-format <png|dds>             Texture file format (default png).
  --uv-layers <merge|split|none>     UV layering (default merge).
  --rig <reset|skin|none>            Import rigging / reset XForms: Yes/Yes, Yes/No, No (default reset).
  --lods                             Import LOD meshes.
  --no-vertex-colors                 Skip vertex colours.
  --tex-path <absolute|relative|sub> Texture path written to materials (default relative).
  --mat-info                         Print material properties.
  --debug                            Print structure debug information.

Texture options:
  --tex-dir <dir>          Where texture files are written (default: next to the output).
  --embed-textures         Embed textures in the scene instead of writing files (default for .glb).
  --no-texture-files       Neither write nor embed textures; only reference their paths.
  --all-textures           Also export textures no material references.
  --textures <file>        Extra BFRES/BNTX archive to search for textures (repeatable).
  --no-auto-textures       Do not load sibling .Tex/.Tex1/.Tex2 archives automatically.
  --raw-channels           Keep stored channels instead of applying the texture swizzle.
  --no-normal-z            Do not rebuild the blue channel of two-channel normal maps.

Scene options:
  --morphs-as-meshes       Emit morph targets as separate meshes instead of Assimp anim meshes.
  -f, --format <id>        Assimp export format id (default: derived from the output extension).
  --pp <step>              Run an Assimp post-process step before export (repeatable), e.g.
                           JoinIdenticalVertices, ValidateDataStructure, OptimizeMeshes, OptimizeGraph,
                           LimitBoneWeights, FlipUVs, MakeLeftHanded, FlipWindingOrder,
                           GenSmoothNormals, CalcTangentSpace, SortByPType, ImproveCacheLocality,
                           RemoveRedundantMaterials, PopulateArmatureData.
  -v, --verbose            Show Assimp's own log output.
  -q, --quiet              Only print warnings and errors.
)";

const std::map<std::string, unsigned> kPostProcess = {
    {"joinidenticalvertices", aiProcess_JoinIdenticalVertices},
    {"validatedatastructure", aiProcess_ValidateDataStructure},
    {"optimizemeshes", aiProcess_OptimizeMeshes},
    {"optimizegraph", aiProcess_OptimizeGraph},
    {"limitboneweights", aiProcess_LimitBoneWeights},
    {"flipuvs", aiProcess_FlipUVs},
    {"makelefthanded", aiProcess_MakeLeftHanded},
    {"flipwindingorder", aiProcess_FlipWindingOrder},
    {"gensmoothnormals", aiProcess_GenSmoothNormals},
    {"calctangentspace", aiProcess_CalcTangentSpace},
    {"sortbyptype", aiProcess_SortByPType},
    {"improvecachelocality", aiProcess_ImproveCacheLocality},
    {"removeredundantmaterials", aiProcess_RemoveRedundantMaterials},
    {"populatearmaturedata", aiProcess_PopulateArmatureData},
    {"triangulate", aiProcess_Triangulate},
};

struct Arguments {
    std::string command;
    fs::path input;
    fs::path output;
    std::string exportFormat;
    importer::ImportConfig config;
    bool embedRequested = false;
    bool noTextureFiles = false;
    bool texDirGiven = false;
    unsigned postProcess = 0;
    bool verbose = false;
    bool quiet = false;
};

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "bfrass: " << message << "\n";
    std::exit(1);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

Arguments parseArguments(int argc, char** argv) {
    Arguments args;
    std::vector<std::string> positional;
    const auto value = [&](int& i, const std::string& flag) -> std::string {
        if (i + 1 >= argc) {
            fail("missing value for " + flag);
        }
        return argv[++i];
    };
    auto& c = args.config;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            std::cout << kUsage;
            std::exit(0);
        } else if (a == "-o" || a == "--output") {
            args.output = value(i, a);
        } else if (a == "-f" || a == "--format") {
            args.exportFormat = value(i, a);
        } else if (a == "-m" || a == "--model") {
            c.models.push_back(value(i, a));
        } else if (a == "--tex-format") {
            const std::string v = lower(value(i, a));
            if (v != "png" && v != "dds") {
                fail("--tex-format expects png or dds");
            }
            c.textureFormat = v == "dds" ? importer::TextureFileFormat::Dds : importer::TextureFileFormat::Png;
        } else if (a == "--uv-layers") {
            const std::string v = lower(value(i, a));
            if (v == "merge") {
                c.uvLayers = importer::UvLayerMode::Merge;
            } else if (v == "split") {
                c.uvLayers = importer::UvLayerMode::Split;
            } else if (v == "none" || v == "no") {
                c.uvLayers = importer::UvLayerMode::None;
            } else {
                fail("--uv-layers expects merge, split or none");
            }
        } else if (a == "--rig") {
            const std::string v = lower(value(i, a));
            if (v == "reset") {
                c.rigging = importer::RiggingMode::SkinAndReset;
            } else if (v == "skin") {
                c.rigging = importer::RiggingMode::SkinOnly;
            } else if (v == "none" || v == "no") {
                c.rigging = importer::RiggingMode::None;
            } else {
                fail("--rig expects reset, skin or none");
            }
        } else if (a == "--lods") {
            c.importLods = true;
        } else if (a == "--no-vertex-colors") {
            c.vertexColors = false;
        } else if (a == "--tex-path") {
            const std::string v = lower(value(i, a));
            if (v == "absolute") {
                c.texturePath = importer::TexturePathMode::Absolute;
            } else if (v == "relative") {
                c.texturePath = importer::TexturePathMode::Relative;
            } else if (v == "sub") {
                c.texturePath = importer::TexturePathMode::Subfolder;
            } else {
                fail("--tex-path expects absolute, relative or sub");
            }
        } else if (a == "--mat-info") {
            c.printMaterialInfo = true;
        } else if (a == "--debug") {
            c.printDebugInfo = true;
        } else if (a == "--tex-dir") {
            c.textureDirectory = value(i, a);
            args.texDirGiven = true;
        } else if (a == "--embed-textures") {
            args.embedRequested = true;
        } else if (a == "--no-texture-files") {
            args.noTextureFiles = true;
        } else if (a == "--all-textures") {
            c.exportUnreferencedTextures = true;
        } else if (a == "--textures") {
            c.textureSources.emplace_back(value(i, a));
        } else if (a == "--no-auto-textures") {
            c.autoTextureSiblings = false;
        } else if (a == "--raw-channels") {
            c.applyChannelMap = false;
        } else if (a == "--no-normal-z") {
            c.reconstructNormalZ = false;
        } else if (a == "--morphs-as-meshes") {
            c.morphTargetsAsMeshes = true;
        } else if (a == "--pp") {
            const auto it = kPostProcess.find(lower(value(i, a)));
            if (it == kPostProcess.end()) {
                fail("unknown post-process step " + std::string(argv[i]));
            }
            args.postProcess |= it->second;
        } else if (a == "-v" || a == "--verbose") {
            args.verbose = true;
        } else if (a == "-q" || a == "--quiet") {
            args.quiet = true;
        } else if (!a.empty() && a[0] == '-') {
            fail("unknown option " + a + " (see --help)");
        } else {
            positional.push_back(a);
        }
    }
    if (positional.empty()) {
        std::cout << kUsage;
        std::exit(1);
    }
    args.command = positional[0];
    if (positional.size() > 1) {
        args.input = positional[1];
    }
    if (positional.size() > 2) {
        fail("unexpected argument " + positional[2]);
    }
    return args;
}

Log makeLog(const Arguments& args) {
    Log log([](LogChannel channel, std::string_view text) {
        if (channel == LogChannel::Warning) {
            std::cerr << "warning: " << text << "\n";
        } else {
            std::cout << text << "\n";
        }
    });
    log.enable(LogChannel::Info, !args.quiet);
    log.enable(LogChannel::Debug, args.config.printDebugInfo);
    log.enable(LogChannel::Material, args.config.printMaterialInfo);
    return log;
}

bfres::ResFile loadForInspection(const Arguments& args, const Log& log) {
    bfres::LoadOptions options;
    options.textureSources = args.config.textureSources;
    options.autoTextureSiblings = args.config.autoTextureSiblings;
    return bfres::loadResFile(args.input, options, log);
}

int commandInfo(const Arguments& args) {
    const Log log = makeLog(args);
    const bfres::ResFile file = loadForInspection(args, log);
    for (const auto& line : bfres::describeResFile(file)) {
        std::cout << line << "\n";
    }
    for (const auto& model : file.models) {
        std::cout << "\nModel " << model.name << "\n";
        const auto& sk = model.skeleton;
        std::cout << std::format("  Skeleton: {} bones, {} smooth / {} rigid matrices, {} rotation, scale mode {}\n",
                                 sk.bones.size(), sk.smoothMatrixCount, sk.rigidMatrixCount,
                                 sk.usesQuaternions() ? "quaternion" : "euler XYZ", (sk.scaleMode() >> 8));
        for (const auto& shape : model.shapes) {
            const auto* vb = shape.vertexBufferIndex < model.vertexBuffers.size()
                                 ? &model.vertexBuffers[shape.vertexBufferIndex]
                                 : nullptr;
            std::string attributes;
            if (vb) {
                for (const auto& a : vb->attributes) {
                    attributes += (attributes.empty() ? "" : " ") + a.name + "(" + a.format.name() + ")";
                }
            }
            const std::string material =
                shape.materialIndex < model.materials.size() ? model.materials[shape.materialIndex].name : "?";
            std::cout << std::format("  Shape {}: {} vertices, skin {}, {} LOD(s), bone {}, material {}\n    {}\n",
                                     shape.name, vb ? vb->vertexCount : 0, shape.skinCount, shape.meshes.size(),
                                     shape.boneIndex, material, attributes);
        }
        for (const auto& mat : model.materials) {
            std::cout << "  Material " << mat.name << "\n";
            const size_t count = std::min(mat.samplers.size(), mat.textureNames.size());
            for (size_t i = 0; i < count; ++i) {
                std::cout << "    " << mat.samplers[i].name << " -> " << mat.textureNames[i] << "\n";
            }
            if (args.config.printMaterialInfo) {
                bfres::printMaterialInfo(mat, file.endian, log);
            }
        }
    }
    if (!file.textures.empty()) {
        std::cout << "\nTextures\n";
        for (const auto& t : file.textures) {
            std::cout << std::format("  {}: {} {}x{} layers {} mips {} ({})\n", t.name, t.format.name(), t.width,
                                     t.height, t.arrayCount, t.mipCount, t.container);
        }
    }
    return 0;
}

int commandTextures(const Arguments& args) {
    if (args.output.empty()) {
        fail("textures requires -o <directory>");
    }
    const Log log = makeLog(args);
    const bfres::ResFile file = loadForInspection(args, log);
    tex::ExportOptions options;
    options.format =
        args.config.textureFormat == importer::TextureFileFormat::Dds ? tex::ExportFormat::Dds : tex::ExportFormat::Png;
    options.decode.applyChannelMap = args.config.applyChannelMap;
    options.decode.reconstructNormalZ = args.config.reconstructNormalZ;
    std::atomic<size_t> failures{0};
    std::mutex mutex;
    tex::parallelFor(file.textures.size(), [&](size_t i) {
        const auto& t = file.textures[i];
        try {
            const fs::path path = tex::exportTexture(t, args.output, options);
            if (!args.quiet) {
                std::lock_guard lock(mutex);
                std::cout << std::format("{} ({} {}x{}) -> {}\n", t.name, t.format.name(), t.width, t.height,
                                         path.string());
            }
        } catch (const std::exception& e) {
            std::lock_guard lock(mutex);
            std::cerr << "warning: " << t.name << ": " << e.what() << "\n";
            ++failures;
        }
    });
    std::cout << std::format("Exported {} of {} textures\n", file.textures.size() - failures, file.textures.size());
    return failures ? 2 : 0;
}

std::string exportFormatFor(const Assimp::Exporter& exporter, const Arguments& args) {
    if (!args.exportFormat.empty()) {
        return args.exportFormat;
    }
    const std::string ext = lower(args.output.extension().string());
    if (ext == ".glb") {
        return "glb2";
    }
    if (ext == ".gltf") {
        return "gltf2";
    }
    for (size_t i = 0; i < exporter.GetExportFormatCount(); ++i) {
        const aiExportFormatDesc* desc = exporter.GetExportFormatDescription(i);
        if (ext.size() > 1 && lower(desc->fileExtension) == ext.substr(1)) {
            return desc->id;
        }
    }
    fail("cannot derive an export format from '" + ext + "'; pass --format (see `bfrass formats`)");
}

int commandConvert(Arguments args) {
    if (args.output.empty()) {
        fail("convert requires -o <output file>");
    }
    Assimp::Exporter exporter;
    const std::string formatId = exportFormatFor(exporter, args);

    auto& c = args.config;
    const fs::path outputDir = args.output.has_parent_path() ? args.output.parent_path() : fs::path(".");
    const bool binaryContainer = formatId == "glb2" || formatId == "glb" || formatId == "assbin";
    c.embedTextures = !args.noTextureFiles && (args.embedRequested || binaryContainer);
    c.exportTextures = !args.noTextureFiles && !c.embedTextures;
    if (!args.texDirGiven) {
        c.textureDirectory = outputDir;
    }
    c.referenceDirectory = outputDir;

    Log log = makeLog(args);
    if (args.verbose) {
        Assimp::DefaultLogger::create(nullptr, Assimp::Logger::VERBOSE, aiDefaultLogStream_STDOUT);
    }

    Assimp::Importer importer;
    importer::registerImporter(importer);
    importer.SetPropertyPointer(importer::keys::LogPointer, &log);
    std::string models;
    for (const auto& m : c.models) {
        models += (models.empty() ? "" : ";") + m;
    }
    std::string sources;
    for (const auto& s : c.textureSources) {
        sources += (sources.empty() ? "" : ";") + s.string();
    }
    importer.SetPropertyString(importer::keys::Models, models);
    importer.SetPropertyString(importer::keys::TextureFormat,
                               c.textureFormat == importer::TextureFileFormat::Dds ? "dds" : "png");
    importer.SetPropertyString(importer::keys::UvLayers, c.uvLayers == importer::UvLayerMode::Split  ? "split"
                                                         : c.uvLayers == importer::UvLayerMode::None ? "none"
                                                                                                     : "merge");
    importer.SetPropertyString(importer::keys::Rigging, c.rigging == importer::RiggingMode::SkinOnly ? "skin"
                                                        : c.rigging == importer::RiggingMode::None   ? "none"
                                                                                                     : "reset");
    importer.SetPropertyString(importer::keys::TexturePath,
                               c.texturePath == importer::TexturePathMode::Absolute    ? "absolute"
                               : c.texturePath == importer::TexturePathMode::Subfolder ? "sub"
                                                                                       : "relative");
    importer.SetPropertyBool(importer::keys::ImportLods, c.importLods);
    importer.SetPropertyBool(importer::keys::VertexColors, c.vertexColors);
    importer.SetPropertyBool(importer::keys::PrintMaterialInfo, c.printMaterialInfo);
    importer.SetPropertyBool(importer::keys::PrintDebugInfo, c.printDebugInfo);
    importer.SetPropertyBool(importer::keys::ExportTextures, c.exportTextures);
    importer.SetPropertyBool(importer::keys::EmbedTextures, c.embedTextures);
    importer.SetPropertyString(importer::keys::TextureDirectory, c.textureDirectory.string());
    importer.SetPropertyString(importer::keys::ReferenceDirectory, c.referenceDirectory.string());
    importer.SetPropertyBool(importer::keys::UnreferencedTextures, c.exportUnreferencedTextures);
    importer.SetPropertyBool(importer::keys::ChannelMap, c.applyChannelMap);
    importer.SetPropertyBool(importer::keys::NormalZ, c.reconstructNormalZ);
    importer.SetPropertyBool(importer::keys::MorphsAsMeshes, c.morphTargetsAsMeshes);
    importer.SetPropertyString(importer::keys::TextureSources, sources);
    importer.SetPropertyBool(importer::keys::AutoTextureSiblings, c.autoTextureSiblings);

    const aiScene* scene = importer.ReadFile(args.input.string(), args.postProcess);
    if (!scene) {
        std::cerr << "bfrass: import failed: " << importer.GetErrorString() << "\n";
        if (args.verbose) {
            Assimp::DefaultLogger::kill();
        }
        return 1;
    }
    log.info("Scene: {} meshes, {} materials, {} embedded textures", scene->mNumMeshes, scene->mNumMaterials,
             scene->mNumTextures);

    if (args.output.has_parent_path()) {
        fs::create_directories(args.output.parent_path());
    }
    const aiReturn result = exporter.Export(scene, formatId, args.output.string());
    if (args.verbose) {
        Assimp::DefaultLogger::kill();
    }
    if (result != aiReturn_SUCCESS) {
        std::cerr << "bfrass: export failed: " << exporter.GetErrorString() << "\n";
        return 1;
    }
    log.info("Wrote {} ({})", args.output.string(), formatId);
    return 0;
}

int commandFormats() {
    Assimp::Exporter exporter;
    for (size_t i = 0; i < exporter.GetExportFormatCount(); ++i) {
        const aiExportFormatDesc* desc = exporter.GetExportFormatDescription(i);
        std::cout << std::format("{:<12} .{:<8} {}\n", desc->id, desc->fileExtension, desc->description);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const Arguments args = parseArguments(argc, argv);
    try {
        if (args.command == "formats") {
            return commandFormats();
        }
        if (args.input.empty()) {
            fail(args.command + " requires an input file");
        }
        if (args.command == "info") {
            return commandInfo(args);
        }
        if (args.command == "textures") {
            return commandTextures(args);
        }
        if (args.command == "convert") {
            return commandConvert(args);
        }
        fail("unknown command " + args.command + " (see --help)");
    } catch (const std::exception& e) {
        std::cerr << "bfrass: " << e.what() << "\n";
        return 1;
    }
}
