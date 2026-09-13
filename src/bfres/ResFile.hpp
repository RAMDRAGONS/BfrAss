#pragma once

#include "bfres/Common.hpp"
#include "bfres/VertexFormat.hpp"
#include "core/Log.hpp"
#include "texture/Texture.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bfrass::bfres {

using SharedBuffer = std::shared_ptr<const std::vector<uint8_t>>;

struct Bone {
    std::string name;
    uint16_t index = 0;
    int32_t parentIndex = -1;
    int16_t smoothMatrixIndex = -1;
    int16_t rigidMatrixIndex = -1;
    uint16_t billboardIndex = 0xFFFF;
    uint32_t flags = 0;
    std::array<float, 3> scale{1, 1, 1};
    std::array<float, 4> rotation{0, 0, 0, 1}; // quaternion xyzw, or euler xyz with w unused
    std::array<float, 3> translation{0, 0, 0};
    std::optional<std::array<float, 12>> inverseMatrix; // Wii U < 3.4 stores one per bone
    std::vector<UserData> userData;
};

struct Skeleton {
    enum : uint32_t {
        ScaleModeMask = 0x3 << 8,
        ScaleNone = 0x0 << 8,
        ScaleStandard = 0x1 << 8,
        ScaleMaya = 0x2 << 8,
        ScaleSoftimage = 0x3 << 8,
        RotateModeMask = 0x7 << 12,
        RotateQuaternion = 0x0 << 12,
        RotateEulerXYZ = 0x1 << 12,
    };

    uint32_t flags = 0;
    std::vector<Bone> bones;
    std::vector<uint16_t> matrixToBone; // smooth matrices first, then rigid
    uint16_t smoothMatrixCount = 0;
    uint16_t rigidMatrixCount = 0;
    std::vector<std::array<float, 12>> inverseModelMatrices; // row-major 3x4, one per smooth matrix
    std::vector<uint16_t> mirroringTable;

    uint32_t scaleMode() const { return flags & ScaleModeMask; }
    bool usesQuaternions() const { return (flags & RotateModeMask) == RotateQuaternion; }
};

namespace BoneFlag {
constexpr uint32_t Visible = 1u << 0;
constexpr uint32_t SegmentScaleCompensate = 1u << 23;
constexpr uint32_t ScaleUniform = 1u << 24;
constexpr uint32_t ScaleVolumeOne = 1u << 25;
constexpr uint32_t RotateZero = 1u << 26;
constexpr uint32_t TranslateZero = 1u << 27;
constexpr uint32_t ScaleOne = ScaleUniform | ScaleVolumeOne;
constexpr uint32_t RotTransZero = RotateZero | TranslateZero;
} // namespace BoneFlag

struct VertexAttribute {
    std::string name;
    uint32_t rawFormat = 0;
    AttributeFormat format;
    uint8_t bufferIndex = 0;
    uint16_t offset = 0;
};

struct VertexBufferData {
    uint64_t fileOffset = 0;
    uint32_t size = 0;
    uint32_t stride = 0;
    std::span<const uint8_t> data;
};

struct VertexBuffer {
    uint64_t fileOffset = 0;
    uint16_t index = 0;
    uint32_t vertexCount = 0;
    uint8_t skinCount = 0;
    uint32_t alignment = 0;
    std::vector<VertexAttribute> attributes;
    std::vector<VertexBufferData> buffers;

    const VertexAttribute* findAttribute(std::string_view name) const {
        for (const auto& a : attributes) {
            if (a.name == name) {
                return &a;
            }
        }
        return nullptr;
    }
};

enum class PrimitiveType { Points, Lines, LineStrip, LineLoop, Triangles, TriangleStrip, TriangleFan, Quads,
                           QuadStrip, Unknown };

struct SubMesh {
    uint32_t offset = 0;
    uint32_t count = 0;
};

struct Mesh {
    uint64_t fileOffset = 0;
    PrimitiveType primitive = PrimitiveType::Triangles;
    uint32_t rawPrimitive = 0;
    uint32_t rawIndexFormat = 0;
    uint32_t indexCount = 0;
    uint32_t firstVertex = 0;
    uint64_t indexBufferOffset = 0;
    std::vector<uint32_t> indices;
    std::vector<SubMesh> subMeshes;
};

struct KeyShape {
    std::string name;
    std::array<uint8_t, 20> targetAttributeIndices{};
};

struct Shape {
    uint64_t fileOffset = 0;
    std::string name;
    uint32_t flags = 0;
    uint16_t index = 0;
    uint16_t materialIndex = 0;
    uint16_t boneIndex = 0;
    uint16_t vertexBufferIndex = 0;
    uint8_t skinCount = 0;
    uint8_t targetAttributeCount = 0;
    std::vector<uint16_t> skinBoneIndices;
    std::vector<Mesh> meshes; // index 0 is the full detail level
    std::vector<KeyShape> keyShapes;
    std::vector<float> radius;
};

struct RenderInfo {
    enum class Type : uint8_t { Int32, Float, String };
    std::string name;
    Type type = Type::Int32;
    std::vector<int32_t> ints;
    std::vector<float> floats;
    std::vector<std::string> strings;
};

enum class ShaderParamType : uint8_t {
    Bool, Bool2, Bool3, Bool4, Int, Int2, Int3, Int4, UInt, UInt2, UInt3, UInt4,
    Float, Float2, Float3, Float4, Reserved2, Float2x2, Float2x3, Float2x4,
    Reserved3, Float3x2, Float3x3, Float3x4, Reserved4, Float4x2, Float4x3, Float4x4,
    Srt2D, Srt3D, TexSrt, TexSrtEx, Count
};

struct ShaderParam {
    std::string name;
    ShaderParamType type = ShaderParamType::Float;
    uint8_t sourceSize = 0;
    uint16_t sourceOffset = 0;
    int32_t uniformOffset = -1;
    uint16_t dependedIndex = 0;
    uint16_t dependIndex = 0;
    std::vector<uint8_t> value; // raw bytes in file byte order
};

struct ShaderAssign {
    std::string archiveName;
    std::string modelName;
    uint32_t revision = 0;
    std::vector<std::pair<std::string, std::string>> attribAssigns;
    std::vector<std::pair<std::string, std::string>> samplerAssigns;
    std::vector<std::pair<std::string, std::string>> options;
};

struct Sampler {
    std::string name;
    // Wii U GX2Sampler registers (clamp/filter, LOD, border).
    std::array<uint32_t, 3> gx2Registers{};
    uint8_t index = 0;
    // Switch nn::gfx::SamplerInfo.
    uint8_t wrapU = 0, wrapV = 0, wrapW = 0;
    uint8_t compareFunction = 0;
    uint8_t borderColor = 0;
    uint8_t maxAnisotropy = 0;
    uint16_t filter = 0;
    float minLod = 0, maxLod = 0, lodBias = 0;
    // The raw fields the original MaxScript printed as Unk1..Unk4 for each platform.
    std::array<uint32_t, 4> legacyFields{};
};

struct Material {
    uint64_t fileOffset = 0;
    std::string name;
    uint32_t flags = 0;
    uint16_t index = 0;
    std::vector<RenderInfo> renderInfos;
    std::optional<ShaderAssign> shaderAssign;
    std::vector<std::string> textureNames;
    std::vector<Sampler> samplers;
    std::vector<ShaderParam> shaderParams;
    std::vector<UserData> userData;
};

struct Model {
    uint64_t fileOffset = 0;
    std::string name;
    std::string path;
    Skeleton skeleton;
    std::vector<VertexBuffer> vertexBuffers;
    std::vector<Shape> shapes;
    std::vector<Material> materials;
    std::vector<UserData> userData;
};

struct ExternalFile {
    std::string name;
    std::span<const uint8_t> data;
};

struct ResFile {
    Platform platform = Platform::Switch;
    Endian endian = Endian::Little;
    Version version;
    std::string name;
    std::filesystem::path sourcePath;
    std::vector<Model> models;
    std::vector<tex::TextureResource> textures;
    std::vector<ExternalFile> externalFiles;
    std::vector<std::string> skeletalAnimations, materialAnimations, boneVisibilityAnimations, shapeAnimations,
        sceneAnimations;
    SharedBuffer buffer;

    unsigned major() const { return version.major; }
    const tex::TextureResource* findTexture(std::string_view textureName) const;
};

struct LoadOptions {
    // Additional BFRES/BNTX files searched for textures referenced by materials.
    std::vector<std::filesystem::path> textureSources;
    // Look for sibling .Tex/.Tex1/.Tex2 archives next to the model file.
    bool autoTextureSiblings = true;
};

bool isResFile(std::span<const uint8_t> data);

// Loads a BFRES from memory (Yaz0-compressed input is decompressed transparently).
ResFile loadResFile(std::vector<uint8_t> data, const std::string& displayName, const Log& log);
ResFile loadResFile(const std::filesystem::path& path, const LoadOptions& options, const Log& log);

// Loads textures from LoadOptions::textureSources and, when enabled, sibling
// .Tex/.Tex1/.Tex2 archives of `modelPath`.
void attachTextureSources(ResFile& file, const std::filesystem::path& modelPath, const LoadOptions& options,
                          const Log& log);

namespace detail {
void loadSwitch(ResFile& file, const Log& log);
void loadWiiU(ResFile& file, const Log& log, bool loadImages, bool loadMips);
} // namespace detail

} // namespace bfrass::bfres
