#include "bfres/Readers.hpp"
#include "bfres/ResFile.hpp"
#include "texture/TextureContainers.hpp"

#include <algorithm>

namespace bfrass::bfres::detail {

namespace {

constexpr uint8_t kExternalFlagHoldsExternalStrings = 1 << 2;
constexpr uint8_t kExternalFlagHasExternalGpu = 1 << 3;

class SwitchLoader {
public:
    SwitchLoader(ResFile& file, const Log& log)
        : file_(file), log_(log), reader_(std::span<const uint8_t>(*file.buffer), Endian::Little), nx_{reader_} {}

    void load();

private:
    unsigned major() const { return file_.version.major; }

    std::vector<UserData> loadUserData(uint64_t array, uint32_t count) const;
    void loadModel(Model& model, uint64_t offset);
    void loadSkeleton(Skeleton& skeleton, uint64_t offset);
    void loadVertexBuffer(VertexBuffer& vb, uint64_t offset, uint16_t index);
    void loadShape(Shape& shape, uint64_t offset, const Model& model);
    void loadMaterial(Material& material, uint64_t offset);
    void loadMaterialV10(Material& material, uint64_t offset);
    void loadShaderAssign(Material& material, uint64_t offset);
    void loadRenderInfos(Material& material, uint64_t array, uint64_t dict, uint16_t count);
    void loadShaderParams(Material& material, uint64_t array, uint16_t count, uint64_t source);
    void loadSamplers(Material& material, uint64_t infoArray, uint64_t dict, uint8_t count);
    uint64_t poolBase(uint64_t pool) const;

    ResFile& file_;
    const Log& log_;
    BinaryReader reader_;
    NxReader nx_;
    uint64_t bufferBase_ = 0;
    std::vector<std::pair<uint64_t, uint64_t>> poolBases_;
};

size_t shaderParamValueSize(ShaderParamType type) {
    switch (type) {
    case ShaderParamType::Bool:
    case ShaderParamType::Int:
    case ShaderParamType::UInt:
    case ShaderParamType::Float: return 4;
    case ShaderParamType::Bool2:
    case ShaderParamType::Int2:
    case ShaderParamType::UInt2:
    case ShaderParamType::Float2: return 8;
    case ShaderParamType::Bool3:
    case ShaderParamType::Int3:
    case ShaderParamType::UInt3:
    case ShaderParamType::Float3: return 12;
    case ShaderParamType::Bool4:
    case ShaderParamType::Int4:
    case ShaderParamType::UInt4:
    case ShaderParamType::Float4:
    case ShaderParamType::Float2x2: return 16;
    case ShaderParamType::Float2x3: return 24;
    case ShaderParamType::Float2x4: return 32;
    case ShaderParamType::Float3x2: return 24;
    case ShaderParamType::Float3x3: return 36;
    case ShaderParamType::Float3x4: return 48;
    case ShaderParamType::Float4x2: return 32;
    case ShaderParamType::Float4x3: return 48;
    case ShaderParamType::Float4x4: return 64;
    case ShaderParamType::Srt2D: return 20;
    case ShaderParamType::Srt3D: return 36;
    case ShaderParamType::TexSrt: return 24;
    case ShaderParamType::TexSrtEx: return 28;
    default: return 0;
    }
}

PrimitiveType switchPrimitive(uint32_t v) {
    switch (v) {
    case 0: return PrimitiveType::Points;
    case 1: return PrimitiveType::Lines;
    case 2: return PrimitiveType::LineStrip;
    case 3: return PrimitiveType::Triangles;
    case 4: return PrimitiveType::TriangleStrip;
    default: return PrimitiveType::Unknown;
    }
}

} // namespace

std::vector<UserData> SwitchLoader::loadUserData(uint64_t array, uint32_t count) const {
    std::vector<UserData> result;
    if (array == 0) {
        return result;
    }
    // Before 5.x user data was a 32 byte g3d structure with a 16 bit count and a wide
    // string type, and its byte stream kept the size in the union after the pointer.
    // From 5.x it is nn::gfx::ResUserData, which has no wide strings and stores the
    // stream size in its count.
    const bool compact = major() < 5;
    for (uint32_t i = 0; i < count; ++i) {
        const uint64_t base = array + uint64_t(i) * (compact ? 0x20 : 0x40);
        UserData ud;
        ud.name = nx_.string(base);
        const uint64_t data = nx_.ptr(base + 8);
        const uint8_t rawType = compact ? reader_.u8(base + 0x1A) : reader_.u8(base + 0x14);
        switch (rawType) {
        case 0: ud.type = UserData::Type::Int32; break;
        case 1: ud.type = UserData::Type::Float; break;
        case 2: ud.type = UserData::Type::String; break;
        case 3: ud.type = compact ? UserData::Type::WString : UserData::Type::Bytes; break;
        default: ud.type = UserData::Type::Bytes; break;
        }
        const uint32_t n = !compact                              ? reader_.u32(base + 0x10)
                           : ud.type == UserData::Type::Bytes ? reader_.u32(base + 0x10)
                                                              : reader_.u16(base + 0x18);
        switch (ud.type) {
        case UserData::Type::Int32:
            for (uint32_t k = 0; k < n; ++k) {
                ud.ints.push_back(reader_.s32(data + 4 * k));
            }
            break;
        case UserData::Type::Float:
            for (uint32_t k = 0; k < n; ++k) {
                ud.floats.push_back(reader_.f32(data + 4 * k));
            }
            break;
        case UserData::Type::String: ud.strings = nx_.stringArray(data, n); break;
        case UserData::Type::WString:
            for (uint32_t k = 0; k < n; ++k) {
                const uint64_t s = nx_.ptr(data + 8 * k);
                // Wide strings carry the same length prefix as narrow ones.
                ud.strings.push_back(s ? utf16String(reader_, s + 2) : std::string());
            }
            break;
        case UserData::Type::Bytes: {
            const auto bytes = reader_.bytes(data, n);
            ud.bytes.assign(bytes.begin(), bytes.end());
            break;
        }
        }
        result.push_back(std::move(ud));
    }
    return result;
}

uint64_t SwitchLoader::poolBase(uint64_t pool) const {
    for (const auto& [object, base] : poolBases_) {
        if (object == pool) {
            return base;
        }
    }
    return bufferBase_;
}

void SwitchLoader::load() {
    const BinaryReader& r = reader_;
    file_.name = nx_.string(0x20);
    const uint64_t modelArray = nx_.ptr(0x28);
    const uint64_t modelDic = nx_.ptr(0x30);
    uint64_t o = 0x38;
    if (major() >= 9) {
        o += 32;
    }
    const uint64_t skelAnimDic = nx_.ptr(o + 0x08);
    const uint64_t matAnimDic = nx_.ptr(o + 0x18);
    const uint64_t boneVisDic = nx_.ptr(o + 0x28);
    const uint64_t shapeAnimDic = nx_.ptr(o + 0x38);
    const uint64_t sceneAnimDic = nx_.ptr(o + 0x48);
    // Before 3.x vertex and index buffers lived in two memory pools, each with its own
    // pool object and info pointer.
    const bool splitPools = major() < 3;
    const uint64_t poolShift = splitPools ? 0x10 : 0;
    const uint64_t memoryPoolInfo = splitPools ? 0 : nx_.ptr(o + 0x58);
    const uint64_t externalArray = nx_.ptr(o + 0x60 + poolShift);
    const uint64_t externalDic = nx_.ptr(o + 0x68 + poolShift);
    uint64_t countsOffset = o + 0x84 + poolShift;
    const uint16_t modelCount = r.u16(countsOffset);
    countsOffset += major() >= 9 ? 6 : 2;
    const uint16_t externalCount = r.u16(countsOffset + 10);
    const uint8_t externalFlags = major() >= 9 ? r.u8(countsOffset + 12) : 0;

    file_.skeletalAnimations = nx_.dictKeys(skelAnimDic);
    file_.materialAnimations = nx_.dictKeys(matAnimDic);
    file_.boneVisibilityAnimations = nx_.dictKeys(boneVisDic);
    file_.shapeAnimations = nx_.dictKeys(shapeAnimDic);
    file_.sceneAnimations = nx_.dictKeys(sceneAnimDic);

    if (memoryPoolInfo) {
        bufferBase_ = r.u64(memoryPoolInfo + 8);
    }
    for (uint64_t k = 0; splitPools && k < 2; ++k) {
        const uint64_t pool = nx_.ptr(o + 0x50 + 8 * k);
        const uint64_t info = nx_.ptr(o + 0x60 + 8 * k);
        if (pool && info) {
            poolBases_.emplace_back(pool, r.u64(info + 8));
        }
    }
    if (major() >= 10 && (externalFlags & kExternalFlagHasExternalGpu)) {
        // The GPU buffer section is appended after the CPU-side file image.
        bufferBase_ = r.u32(0x1C) + 288;
    }
    if (major() >= 10 && (externalFlags & kExternalFlagHoldsExternalStrings)) {
        log_.warn("{} is a string table resource for another BFRES and holds no models", file_.name);
        return;
    }

    log_.debug("FRES: name={} version={} models={} externalFiles={} bufferBase=0x{:X} flags=0x{:X}", file_.name,
               file_.version.toString(), modelCount, externalCount, bufferBase_, externalFlags);

    const auto externalNames = nx_.dictKeys(externalDic);
    for (uint16_t i = 0; i < externalCount && externalArray; ++i) {
        const uint64_t e = externalArray + uint64_t(i) * 16;
        ExternalFile ext;
        ext.name = i < externalNames.size() ? externalNames[i] : std::string();
        const uint64_t data = nx_.ptr(e);
        const uint32_t size = r.u32(e + 8);
        ext.data = r.bytes(data, size);
        log_.debug("External file #{}: {} at 0x{:X}, 0x{:X} bytes", i, ext.name, data, size);
        if (tex::isBntx(ext.data)) {
            auto textures = tex::parseBntx(file_.buffer, ext.data, ext.name, log_);
            for (auto& t : textures) {
                file_.textures.push_back(std::move(t));
            }
        }
        file_.externalFiles.push_back(ext);
    }

    const auto modelNames = nx_.dictKeys(modelDic);
    const uint32_t count = modelDic ? uint32_t(modelNames.size()) : modelCount;
    for (uint32_t i = 0; i < count; ++i) {
        Model model;
        loadModel(model, modelArray + uint64_t(i) * 0x78);
        file_.models.push_back(std::move(model));
    }
}

void SwitchLoader::loadModel(Model& model, uint64_t o) {
    const BinaryReader& r = reader_;
    if (r.fixedString(o, 4) != "FMDL") {
        throw FormatError("expected FMDL at 0x" + BinaryReader::toHex(o));
    }
    model.fileOffset = o;
    const uint64_t base = major() >= 9 ? o + 0x08 : o + 0x10;
    model.name = nx_.string(base);
    model.path = nx_.string(base + 0x08);
    const uint64_t skeleton = nx_.ptr(base + 0x10);
    const uint64_t vertexArray = nx_.ptr(base + 0x18);
    const uint64_t shapeArray = nx_.ptr(base + 0x20);
    const uint64_t materialArray = nx_.ptr(base + 0x30);
    const uint64_t userDataArray = nx_.ptr(o + 0x50);
    const uint16_t vertexCount = r.u16(o + 0x68);
    const uint16_t shapeCount = r.u16(o + 0x6A);
    const uint16_t materialCount = r.u16(o + 0x6C);
    const uint16_t userDataCount = major() >= 9 ? r.u16(o + 0x70) : r.u16(o + 0x6E);

    log_.debug("FMDL: name={} path={} fskl=0x{:X} fvtxArray=0x{:X} fshpArray=0x{:X} fmatArray=0x{:X} "
               "fvtxCount={} fshpCount={} fmatCount={} userDataCount={}",
               model.name, model.path, skeleton, vertexArray, shapeArray, materialArray, vertexCount, shapeCount,
               materialCount, userDataCount);

    model.userData = loadUserData(userDataArray, userDataCount);
    loadSkeleton(model.skeleton, skeleton);
    for (uint16_t i = 0; i < vertexCount; ++i) {
        VertexBuffer vb;
        const uint64_t vertexSize = (major() >= 9 || major() < 3) ? 0x58 : 0x60;
        loadVertexBuffer(vb, vertexArray + uint64_t(i) * vertexSize, i);
        model.vertexBuffers.push_back(std::move(vb));
    }
    const uint64_t materialSize = major() >= 10 ? 0xB0 : major() == 9 ? 0xA8 : major() < 3 ? 0xB0 : 0xB8;
    for (uint16_t i = 0; i < materialCount; ++i) {
        Material mat;
        if (major() >= 10) {
            loadMaterialV10(mat, materialArray + i * materialSize);
        } else {
            loadMaterial(mat, materialArray + i * materialSize);
        }
        model.materials.push_back(std::move(mat));
    }
    const uint64_t shapeSize = major() >= 9 ? 0x60 : major() < 5 ? 0x68 : 0x70;
    for (uint16_t i = 0; i < shapeCount; ++i) {
        Shape shape;
        loadShape(shape, shapeArray + i * shapeSize, model);
        model.shapes.push_back(std::move(shape));
    }
}

void SwitchLoader::loadSkeleton(Skeleton& s, uint64_t o) {
    const BinaryReader& r = reader_;
    if (r.fixedString(o, 4) != "FSKL") {
        throw FormatError("expected FSKL at 0x" + BinaryReader::toHex(o));
    }
    uint64_t boneArray, mtxToBone, invMatrices, mirroring = 0;
    uint16_t boneCount;
    if (major() >= 9) {
        s.flags = r.u32(o + 0x04);
        boneArray = nx_.ptr(o + 0x10);
        mtxToBone = nx_.ptr(o + 0x18);
        invMatrices = nx_.ptr(o + 0x20);
        mirroring = nx_.ptr(o + 0x30);
        boneCount = r.u16(o + 0x38);
        s.smoothMatrixCount = r.u16(o + 0x3A);
        s.rigidMatrixCount = r.u16(o + 0x3C);
    } else {
        boneArray = nx_.ptr(o + 0x18);
        mtxToBone = nx_.ptr(o + 0x20);
        invMatrices = nx_.ptr(o + 0x28);
        if (major() >= 8) {
            mirroring = nx_.ptr(o + 0x38);
        }
        const uint64_t tail = major() >= 8 ? o + 0x48 : o + 0x38;
        s.flags = r.u32(tail);
        boneCount = r.u16(tail + 4);
        s.smoothMatrixCount = r.u16(tail + 6);
        s.rigidMatrixCount = r.u16(tail + 8);
    }
    log_.debug("FSKL: flags=0x{:08X} boneArrCount={} invIndxArrCount={} exIndxCount={} boneArr=0x{:X} "
               "invIndxArr=0x{:X} invMatrArr=0x{:X}",
               s.flags, boneCount, s.smoothMatrixCount, s.rigidMatrixCount, boneArray, mtxToBone, invMatrices);

    const uint32_t matrixCount = uint32_t(s.smoothMatrixCount) + s.rigidMatrixCount;
    for (uint32_t i = 0; i < matrixCount && mtxToBone; ++i) {
        s.matrixToBone.push_back(r.u16(mtxToBone + 2 * i));
    }
    for (uint32_t i = 0; i < s.smoothMatrixCount && invMatrices; ++i) {
        std::array<float, 12> m{};
        for (int k = 0; k < 12; ++k) {
            m[k] = r.f32(invMatrices + uint64_t(i) * 48 + 4 * k);
        }
        s.inverseModelMatrices.push_back(m);
    }
    if (mirroring) {
        for (uint32_t i = 0; i < boneCount; ++i) {
            s.mirroringTable.push_back(r.u16(mirroring + 2 * i));
        }
    }

    const uint64_t boneSize = major() >= 10 ? 0x58 : (major() >= 8 ? 0x60 : 0x50);
    const uint64_t fieldBase = major() >= 10 ? 0x20 : (major() >= 8 ? 0x28 : 0x18);
    for (uint32_t i = 0; i < boneCount; ++i) {
        const uint64_t b = boneArray + i * boneSize;
        const uint64_t f = b + fieldBase;
        Bone bone;
        bone.name = nx_.string(b);
        bone.index = r.u16(f);
        const uint16_t parent = r.u16(f + 2);
        bone.parentIndex = parent == 0xFFFF ? -1 : parent;
        bone.smoothMatrixIndex = r.s16(f + 4);
        bone.rigidMatrixIndex = r.s16(f + 6);
        bone.billboardIndex = r.u16(f + 8);
        const uint16_t userDataCount = r.u16(f + 10);
        bone.flags = r.u32(f + 12);
        for (int k = 0; k < 3; ++k) {
            bone.scale[k] = r.f32(f + 16 + 4 * k);
        }
        for (int k = 0; k < 4; ++k) {
            bone.rotation[k] = r.f32(f + 28 + 4 * k);
        }
        for (int k = 0; k < 3; ++k) {
            bone.translation[k] = r.f32(f + 44 + 4 * k);
        }
        bone.userData = loadUserData(nx_.ptr(b + 8), userDataCount);
        log_.debug("  bone #{} {}: parent={} smooth={} rigid={} flags=0x{:08X} scale=({}, {}, {}) "
                   "rotation=({}, {}, {}, {}) translation=({}, {}, {})",
                   bone.index, bone.name, bone.parentIndex, bone.smoothMatrixIndex, bone.rigidMatrixIndex,
                   bone.flags, bone.scale[0], bone.scale[1], bone.scale[2], bone.rotation[0], bone.rotation[1],
                   bone.rotation[2], bone.rotation[3], bone.translation[0], bone.translation[1],
                   bone.translation[2]);
        s.bones.push_back(std::move(bone));
    }
}

void SwitchLoader::loadVertexBuffer(VertexBuffer& vb, uint64_t o, uint16_t index) {
    const BinaryReader& r = reader_;
    if (r.fixedString(o, 4) != "FVTX") {
        throw FormatError("expected FVTX at 0x" + BinaryReader::toHex(o));
    }
    vb.fileOffset = o;
    // Version 9 replaced the 16 byte block header with a signature and flags.
    const uint64_t f = major() >= 9 ? o + 0x08 : o + 0x10;
    const uint64_t attributeArray = nx_.ptr(f);
    const uint64_t attributeDic = nx_.ptr(f + 0x08);
    const uint64_t memoryPool = nx_.ptr(f + 0x10);
    // Before 3.x there is no vertex buffer pointer array ahead of the buffer infos.
    const uint64_t t = major() < 3 ? f - 0x08 : f;
    const uint64_t sizeArray = nx_.ptr(t + 0x28);
    const uint64_t strideArray = nx_.ptr(t + 0x30);
    const uint32_t memoryPoolOffset = r.u32(t + 0x40);
    const uint8_t attributeCount = r.u8(t + 0x44);
    const uint8_t bufferCount = r.u8(t + 0x45);
    vb.index = index;
    vb.vertexCount = r.u32(t + 0x48);
    vb.skinCount = r.u8(t + 0x4C);
    // Runtimes before 5.x packed vertex buffers on 64 byte boundaries and later ones on
    // 8; from 9.1 the file may override it, with 0 still meaning 8.
    constexpr uint16_t kEarlyVertexBufferAlignment = 64;
    constexpr uint16_t kDefaultVertexBufferAlignment = 8;
    vb.alignment = file_.version.atLeast(9, 1) ? r.u16(t + 0x4E) : 0;
    if (vb.alignment == 0) {
        vb.alignment = major() < 5 ? kEarlyVertexBufferAlignment : kDefaultVertexBufferAlignment;
    }
    log_.debug("FVTX #{}: attCount={} buffCount={} vertCount={} vertSkinCount={} attArr=0x{:X} "
               "vtxBuffSizeOff=0x{:X} vtxStrideSizeOff=0x{:X} buffOff=0x{:X} DataAlign={}",
               index, attributeCount, bufferCount, vb.vertexCount, vb.skinCount, attributeArray, sizeArray,
               strideArray, memoryPoolOffset, vb.alignment);

    const auto names = nx_.dictKeys(attributeDic);
    for (uint8_t i = 0; i < attributeCount; ++i) {
        const uint64_t a = attributeArray + uint64_t(i) * 16;
        VertexAttribute attr;
        attr.name = nx_.string(a);
        if (attr.name.empty() && i < names.size()) {
            attr.name = names[i];
        }
        attr.rawFormat = r.u32(a + 8);
        attr.format = fromGfxAttributeFormat(attr.rawFormat);
        attr.offset = r.u16(a + 12);
        attr.bufferIndex = r.u8(a + 14);
        log_.debug("  attribute {}: format=0x{:04X} ({}) buffIndx={} buffOff={}", attr.name, attr.rawFormat,
                   attr.format.name(), attr.bufferIndex, attr.offset);
        vb.attributes.push_back(std::move(attr));
    }

    uint64_t poolOffset = memoryPoolOffset;
    for (uint8_t i = 0; i < bufferCount; ++i) {
        VertexBufferData buf;
        buf.size = r.u32(sizeArray + uint64_t(i) * 16);
        buf.stride = r.u32(strideArray + uint64_t(i) * 16);
        buf.fileOffset = poolBase(memoryPool) + poolOffset;
        buf.data = r.bytes(buf.fileOffset, buf.size);
        poolOffset += alignUp(buf.size, vb.alignment);
        log_.debug("  buffer #{}: buffSize=0x{:X} strideSize={} dataOffset=0x{:X}", i, buf.size, buf.stride,
                   buf.fileOffset);
        vb.buffers.push_back(buf);
    }
}

void SwitchLoader::loadShape(Shape& shape, uint64_t o, const Model&) {
    const BinaryReader& r = reader_;
    if (r.fixedString(o, 4) != "FSHP") {
        throw FormatError("expected FSHP at 0x" + BinaryReader::toHex(o));
    }
    shape.fileOffset = o;
    const uint64_t base = major() >= 9 ? o + 0x08 : o + 0x10;
    shape.name = nx_.string(base);
    const uint64_t meshArray = nx_.ptr(base + 0x10);
    const uint64_t skinBoneArray = nx_.ptr(base + 0x18);
    const uint64_t keyShapeArray = nx_.ptr(base + 0x20);
    const uint64_t keyShapeDic = nx_.ptr(base + 0x28);
    // Before 5.x shapes stored a single bounding radius in place of the per mesh array.
    const bool scalarRadius = major() < 5;
    const uint64_t radiusArray = scalarRadius ? 0 : nx_.ptr(base + 0x38);
    const uint64_t counts = major() >= 9 ? o + 0x50 : scalarRadius ? o + 0x58 : o + 0x5C;
    shape.flags = major() >= 9 ? r.u32(o + 0x04) : scalarRadius ? r.u32(o + 0x54) : r.u32(o + 0x58);
    if (scalarRadius) {
        shape.radius.push_back(r.f32(o + 0x50));
    }
    shape.index = r.u16(counts);
    shape.materialIndex = r.u16(counts + 2);
    shape.boneIndex = r.u16(counts + 4);
    shape.vertexBufferIndex = r.u16(counts + 6);
    const uint16_t skinBoneCount = r.u16(counts + 8);
    shape.skinCount = r.u8(counts + 10);
    const uint8_t meshCount = r.u8(counts + 11);
    const uint8_t keyShapeCount = r.u8(counts + 12);
    shape.targetAttributeCount = r.u8(counts + 13);

    log_.debug("FSHP #{} {}: fvtxIndx={} fmatIndx={} fsklIndx={} fsklIndxArrCount={} matrFlag={} lodMdlCount={} "
               "keyShapes={} lodMdlOff=0x{:X}",
               shape.index, shape.name, shape.vertexBufferIndex, shape.materialIndex, shape.boneIndex, skinBoneCount,
               shape.skinCount, meshCount, keyShapeCount, meshArray);

    for (uint16_t i = 0; i < skinBoneCount && skinBoneArray; ++i) {
        shape.skinBoneIndices.push_back(r.u16(skinBoneArray + 2 * i));
    }
    if (radiusArray && meshCount) {
        const uint32_t n = major() >= 10 ? (skinBoneCount ? skinBoneCount : meshCount) : meshCount;
        for (uint32_t i = 0; i < n; ++i) {
            shape.radius.push_back(major() >= 10 ? r.f32(radiusArray + 16 * i + 12) : r.f32(radiusArray + 4 * i));
        }
    }

    const auto keyNames = nx_.dictKeys(keyShapeDic);
    for (uint8_t i = 0; i < keyShapeCount; ++i) {
        KeyShape key;
        key.name = i < keyNames.size() ? keyNames[i] : std::string();
        if (keyShapeArray) {
            for (int k = 0; k < 20; ++k) {
                key.targetAttributeIndices[k] = r.u8(keyShapeArray + uint64_t(i) * 20 + k);
            }
        }
        shape.keyShapes.push_back(std::move(key));
    }

    for (uint8_t i = 0; i < meshCount; ++i) {
        const uint64_t m = meshArray + uint64_t(i) * 0x38;
        Mesh mesh;
        mesh.fileOffset = m;
        const uint64_t subMeshArray = nx_.ptr(m);
        const uint64_t indexMemoryPool = nx_.ptr(m + 0x08);
        const uint64_t indexBufferInfo = nx_.ptr(m + 0x18);
        const uint32_t memoryPoolOffset = r.u32(m + 0x20);
        mesh.rawPrimitive = r.u32(m + 0x24);
        mesh.primitive = switchPrimitive(mesh.rawPrimitive);
        mesh.rawIndexFormat = r.u32(m + 0x28);
        mesh.indexCount = r.u32(m + 0x2C);
        mesh.firstVertex = r.u32(m + 0x30);
        const uint16_t subMeshCount = r.u16(m + 0x34);
        for (uint16_t k = 0; k < subMeshCount && subMeshArray; ++k) {
            mesh.subMeshes.push_back({r.u32(subMeshArray + 8 * k), r.u32(subMeshArray + 8 * k + 4)});
        }
        const uint32_t bufferSize = indexBufferInfo ? r.u32(indexBufferInfo) : 0;
        mesh.indexBufferOffset = poolBase(indexMemoryPool) + memoryPoolOffset;
        const unsigned width = mesh.rawIndexFormat == 0 ? 1 : (mesh.rawIndexFormat == 1 ? 2 : 4);
        const uint32_t available = bufferSize ? bufferSize / width : mesh.indexCount;
        const uint32_t count = std::min(mesh.indexCount, available);
        mesh.indices.resize(count);
        for (uint32_t k = 0; k < count; ++k) {
            const uint64_t p = mesh.indexBufferOffset + uint64_t(k) * width;
            mesh.indices[k] = width == 1 ? r.u8(p) : (width == 2 ? r.u16(p) : r.u32(p));
        }
        log_.debug("  LOD #{}: faceType={} primitive={} FaceCount={} PolyStart={} FaceBuffer=0x{:X} subMeshes={}", i,
                   mesh.rawIndexFormat, mesh.rawPrimitive, mesh.indexCount, mesh.firstVertex, mesh.indexBufferOffset,
                   subMeshCount);
        shape.meshes.push_back(std::move(mesh));
    }
}

void SwitchLoader::loadSamplers(Material& material, uint64_t infoArray, uint64_t dict, uint8_t count) {
    const BinaryReader& r = reader_;
    const auto names = nx_.dictKeys(dict);
    for (uint8_t i = 0; i < count; ++i) {
        Sampler s;
        s.name = i < names.size() ? names[i] : std::string();
        s.index = i;
        if (infoArray) {
            const uint64_t p = infoArray + uint64_t(i) * 32;
            s.wrapU = r.u8(p);
            s.wrapV = r.u8(p + 1);
            s.wrapW = r.u8(p + 2);
            s.compareFunction = r.u8(p + 3);
            s.borderColor = r.u8(p + 4);
            s.maxAnisotropy = r.u8(p + 5);
            s.filter = r.u16(p + 6);
            s.minLod = r.f32(p + 8);
            s.maxLod = r.f32(p + 12);
            s.lodBias = r.f32(p + 16);
        }
        if (dict) {
            // The original importer read the dictionary node as four shorts.
            const uint64_t node = dict + 8 + uint64_t(i + 1) * 16;
            s.legacyFields = {r.u16(node), r.u16(node + 2), r.u16(node + 4), r.u16(node + 6)};
        }
        material.samplers.push_back(std::move(s));
    }
}

void SwitchLoader::loadRenderInfos(Material& material, uint64_t array, uint64_t dict, uint16_t count) {
    const BinaryReader& r = reader_;
    (void)dict;
    for (uint16_t i = 0; i < count && array; ++i) {
        const uint64_t p = array + uint64_t(i) * 24;
        RenderInfo info;
        info.name = nx_.string(p);
        const uint64_t data = nx_.ptr(p + 8);
        const uint16_t n = r.u16(p + 16);
        info.type = RenderInfo::Type(r.u8(p + 18));
        for (uint16_t k = 0; k < n && data; ++k) {
            switch (info.type) {
            case RenderInfo::Type::Int32: info.ints.push_back(r.s32(data + 4 * k)); break;
            case RenderInfo::Type::Float: info.floats.push_back(r.f32(data + 4 * k)); break;
            case RenderInfo::Type::String: info.strings.push_back(nx_.string(data + 8 * k)); break;
            }
        }
        material.renderInfos.push_back(std::move(info));
    }
}

void SwitchLoader::loadShaderParams(Material& material, uint64_t array, uint16_t count, uint64_t source) {
    const BinaryReader& r = reader_;
    for (uint16_t i = 0; i < count && array; ++i) {
        const uint64_t p = array + uint64_t(i) * 32;
        ShaderParam param;
        param.name = nx_.string(p + 8);
        param.type = ShaderParamType(std::min<uint8_t>(r.u8(p + 16), uint8_t(ShaderParamType::Count)));
        param.sourceSize = r.u8(p + 17);
        param.sourceOffset = r.u16(p + 18);
        param.uniformOffset = r.s32(p + 20);
        param.dependedIndex = r.u16(p + 24);
        param.dependIndex = r.u16(p + 26);
        const size_t size = std::max<size_t>(param.sourceSize, shaderParamValueSize(param.type));
        if (source && size) {
            const auto bytes = r.bytes(source + param.sourceOffset, size);
            param.value.assign(bytes.begin(), bytes.end());
        }
        material.shaderParams.push_back(std::move(param));
    }
}

void SwitchLoader::loadShaderAssign(Material& material, uint64_t o) {
    if (o == 0) {
        return;
    }
    const BinaryReader& r = reader_;
    ShaderAssign sa;
    sa.archiveName = nx_.string(o);
    sa.modelName = nx_.string(o + 0x08);
    const uint64_t attribArray = nx_.ptr(o + 0x10);
    const uint64_t attribDic = nx_.ptr(o + 0x18);
    const uint64_t samplerArray = nx_.ptr(o + 0x20);
    const uint64_t samplerDic = nx_.ptr(o + 0x28);
    const uint64_t optionArray = nx_.ptr(o + 0x30);
    const uint64_t optionDic = nx_.ptr(o + 0x38);
    sa.revision = r.u32(o + 0x40);
    const uint8_t attribCount = r.u8(o + 0x44);
    const uint8_t samplerCount = r.u8(o + 0x45);
    const uint16_t optionCount = r.u16(o + 0x46);
    const auto pairUp = [&](uint64_t array, uint64_t dic, size_t n) {
        std::vector<std::pair<std::string, std::string>> out;
        const auto keys = nx_.dictKeys(dic);
        const auto values = nx_.stringArray(array, n);
        for (size_t i = 0; i < n; ++i) {
            out.emplace_back(i < keys.size() ? keys[i] : std::string(), i < values.size() ? values[i] : std::string());
        }
        return out;
    };
    sa.attribAssigns = pairUp(attribArray, attribDic, attribCount);
    sa.samplerAssigns = pairUp(samplerArray, samplerDic, samplerCount);
    sa.options = pairUp(optionArray, optionDic, optionCount);
    material.shaderAssign = std::move(sa);
}

void SwitchLoader::loadMaterial(Material& material, uint64_t o) {
    const BinaryReader& r = reader_;
    if (r.fixedString(o, 4) != "FMAT") {
        throw FormatError("expected FMAT at 0x" + BinaryReader::toHex(o));
    }
    material.fileOffset = o;
    const uint64_t base = major() >= 9 ? o + 0x08 : o + 0x10;
    material.name = nx_.string(base);
    const uint64_t renderInfoArray = nx_.ptr(base + 0x08);
    const uint64_t renderInfoDic = nx_.ptr(base + 0x10);
    const uint64_t shaderAssign = nx_.ptr(base + 0x18);
    // Before 3.x textures were a single array of name and texture view pairs instead of
    // separate view and name arrays, so every later field sits 8 bytes earlier.
    const bool textureRefs = major() < 3;
    const uint64_t t = textureRefs ? base - 0x08 : base;
    const uint64_t textureNameArray = nx_.ptr(textureRefs ? base + 0x20 : base + 0x28);
    const uint64_t samplerInfoArray = nx_.ptr(t + 0x38);
    const uint64_t samplerInfoDic = nx_.ptr(t + 0x40);
    const uint64_t shaderParamArray = nx_.ptr(t + 0x48);
    const uint64_t sourceParam = nx_.ptr(t + 0x58);
    const uint64_t userDataArray = nx_.ptr(t + 0x60);
    const uint64_t counts = major() >= 9 ? o + 0x98 : textureRefs ? o + 0x9C : o + 0xA4;
    material.flags = major() >= 9 ? r.u32(o + 0x04) : textureRefs ? r.u32(o + 0x98) : r.u32(o + 0xA0);
    material.index = r.u16(counts);
    const uint16_t renderInfoCount = r.u16(counts + 2);
    const uint8_t samplerCount = r.u8(counts + 4);
    const uint8_t textureCount = r.u8(counts + 5);
    const uint16_t shaderParamCount = r.u16(counts + 6);
    const uint16_t userDataCount = r.u16(counts + 14);

    log_.debug("FMAT #{} {}: rendParamCount={} texSelCount={} texAttSelCount={} matParamCount={} texSelOff=0x{:X} "
               "texAttSelOff=0x{:X} texAttIndxOff=0x{:X} matParamArrOff=0x{:X} matParamOff=0x{:X}",
               material.index, material.name, renderInfoCount, samplerCount, textureCount, shaderParamCount,
               textureNameArray, samplerInfoArray, samplerInfoDic, shaderParamArray, sourceParam);

    if (textureRefs) {
        for (uint8_t i = 0; i < textureCount && textureNameArray; ++i) {
            material.textureNames.push_back(nx_.string(textureNameArray + uint64_t(i) * 16));
        }
    } else {
        material.textureNames = nx_.stringArray(textureNameArray, textureCount);
    }
    loadSamplers(material, samplerInfoArray, samplerInfoDic, samplerCount);
    loadRenderInfos(material, renderInfoArray, renderInfoDic, renderInfoCount);
    loadShaderParams(material, shaderParamArray, shaderParamCount, sourceParam);
    loadShaderAssign(material, shaderAssign);
    material.userData = loadUserData(userDataArray, userDataCount);
}

void SwitchLoader::loadMaterialV10(Material& material, uint64_t o) {
    const BinaryReader& r = reader_;
    if (r.fixedString(o, 4) != "FMAT") {
        throw FormatError("expected FMAT at 0x" + BinaryReader::toHex(o));
    }
    material.fileOffset = o;
    material.flags = r.u32(o + 0x04);
    material.name = nx_.string(o + 0x08);
    const uint64_t shaderInfo = nx_.ptr(o + 0x10);
    const uint64_t textureNameArray = nx_.ptr(o + 0x20);
    const uint64_t samplerInfoArray = nx_.ptr(o + 0x30);
    const uint64_t samplerInfoDic = nx_.ptr(o + 0x38);
    const uint64_t renderInfoData = nx_.ptr(o + 0x40);
    const uint64_t renderInfoCounts = nx_.ptr(o + 0x48);
    const uint64_t renderInfoOffsets = nx_.ptr(o + 0x50);
    const uint64_t sourceParam = nx_.ptr(o + 0x58);
    const uint64_t userDataArray = nx_.ptr(o + 0x70);
    material.index = r.u16(o + 0xA0);
    const uint8_t samplerCount = r.u8(o + 0xA2);
    const uint8_t textureCount = r.u8(o + 0xA3);
    const uint16_t userDataCount = r.u16(o + 0xA6);

    log_.debug("FMAT #{} {} (v10): texSelCount={} texAttSelCount={} shaderInfo=0x{:X} texSelOff=0x{:X} "
               "texAttIndxOff=0x{:X} sourceParam=0x{:X}",
               material.index, material.name, samplerCount, textureCount, shaderInfo, textureNameArray,
               samplerInfoDic, sourceParam);

    material.textureNames = nx_.stringArray(textureNameArray, textureCount);
    loadSamplers(material, samplerInfoArray, samplerInfoDic, samplerCount);
    material.userData = loadUserData(userDataArray, userDataCount);

    if (shaderInfo == 0) {
        return;
    }
    const uint64_t assign = nx_.ptr(shaderInfo);
    if (assign == 0) {
        return;
    }
    ShaderAssign sa;
    sa.archiveName = nx_.string(assign);
    sa.modelName = nx_.string(assign + 0x08);
    const uint64_t renderInfoList = nx_.ptr(assign + 0x10);
    const uint64_t renderInfoDic = nx_.ptr(assign + 0x18);
    const uint64_t paramList = nx_.ptr(assign + 0x20);
    const uint64_t paramDic = nx_.ptr(assign + 0x28);
    const auto attribKeys = nx_.dictKeys(nx_.ptr(assign + 0x30));
    const auto samplerKeys = nx_.dictKeys(nx_.ptr(assign + 0x38));
    const auto optionKeys = nx_.dictKeys(nx_.ptr(assign + 0x40));
    const size_t renderInfoCount = nx_.dictKeys(renderInfoDic).size();
    const size_t paramCount = nx_.dictKeys(paramDic).size();

    // Render info: names and types live in the shared shader assign, per-material
    // counts, data offsets and payload live in three tables on the material.
    for (size_t i = 0; i < renderInfoCount && renderInfoList; ++i) {
        RenderInfo info;
        info.name = nx_.string(renderInfoList + 16 * i);
        info.type = RenderInfo::Type(r.u8(renderInfoList + 16 * i + 8));
        const uint16_t n = renderInfoCounts ? r.u16(renderInfoCounts + 2 * i) : 0;
        const uint16_t offset = renderInfoOffsets ? r.u16(renderInfoOffsets + 2 * i) : 0;
        const uint64_t data = renderInfoData + offset;
        for (uint16_t k = 0; k < n && renderInfoData; ++k) {
            switch (info.type) {
            case RenderInfo::Type::Int32: info.ints.push_back(r.s32(data + 4 * k)); break;
            case RenderInfo::Type::Float: info.floats.push_back(r.f32(data + 4 * k)); break;
            case RenderInfo::Type::String: info.strings.push_back(nx_.string(data + 8 * k)); break;
            }
        }
        material.renderInfos.push_back(std::move(info));
    }

    for (size_t i = 0; i < paramCount && paramList; ++i) {
        const uint64_t p = paramList + 24 * i;
        ShaderParam param;
        param.name = nx_.string(p + 8);
        param.sourceOffset = r.u16(p + 16);
        param.type = ShaderParamType(std::min<uint16_t>(r.u16(p + 18), uint16_t(ShaderParamType::Count)));
        const size_t size = shaderParamValueSize(param.type);
        param.sourceSize = uint8_t(size);
        if (sourceParam && size) {
            const auto bytes = r.bytes(sourceParam + param.sourceOffset, size);
            param.value.assign(bytes.begin(), bytes.end());
        }
        material.shaderParams.push_back(std::move(param));
    }

    const uint64_t attribValues = nx_.ptr(shaderInfo + 0x08);
    const uint64_t attribIndices = nx_.ptr(shaderInfo + 0x10);
    const uint64_t samplerValues = nx_.ptr(shaderInfo + 0x18);
    const uint64_t samplerIndices = nx_.ptr(shaderInfo + 0x20);
    const uint64_t optionToggles = nx_.ptr(shaderInfo + 0x28);
    const uint64_t optionStrings = nx_.ptr(shaderInfo + 0x30);
    const uint64_t optionIndices = nx_.ptr(shaderInfo + 0x38);
    const uint8_t attribCount = r.u8(shaderInfo + 0x44);
    const uint8_t samplerAssignCount = r.u8(shaderInfo + 0x45);
    const uint16_t boolCount = r.u16(shaderInfo + 0x46);
    const uint16_t choiceCount = r.u16(shaderInfo + 0x48);

    // Index tables start with the used indices, followed by one entry per key (-1 = default).
    const auto assigned = [&](uint64_t values, uint64_t indices, size_t usedCount, const std::vector<std::string>& keys,
                              bool wide) {
        std::vector<std::pair<std::string, std::string>> out;
        const auto strings = nx_.stringArray(values, usedCount);
        for (size_t i = 0; i < keys.size(); ++i) {
            int32_t idx = int32_t(i);
            if (indices) {
                idx = wide ? r.s16(indices + 2 * (usedCount + i)) : r.s8(indices + usedCount + i);
            }
            const std::string value =
                (idx < 0 || size_t(idx) >= strings.size()) ? std::string("<Default Value>") : strings[idx];
            out.emplace_back(keys[i], value);
        }
        return out;
    };
    sa.attribAssigns = assigned(attribValues, attribIndices, attribCount, attribKeys, false);
    sa.samplerAssigns = assigned(samplerValues, samplerIndices, samplerAssignCount, samplerKeys, false);

    std::vector<std::string> choices;
    for (uint16_t i = 0; i < boolCount; ++i) {
        const uint64_t word = optionToggles ? r.u64(optionToggles + 8 * (i / 64)) : 0;
        choices.emplace_back(((word >> (i % 64)) & 1) ? "True" : "False");
    }
    const uint16_t stringChoices = choiceCount > boolCount ? uint16_t(choiceCount - boolCount) : 0;
    for (const auto& s : nx_.stringArray(optionStrings, stringChoices)) {
        choices.push_back(s);
    }
    for (size_t i = 0; i < optionKeys.size(); ++i) {
        int32_t idx = int32_t(i);
        if (optionIndices) {
            idx = r.s16(optionIndices + 2 * (choiceCount + i));
        }
        const std::string value =
            (idx < 0 || size_t(idx) >= choices.size()) ? std::string("<Default Value>") : choices[idx];
        sa.options.emplace_back(optionKeys[i], value);
    }
    material.shaderAssign = std::move(sa);
}

void loadSwitch(ResFile& file, const Log& log) {
    SwitchLoader(file, log).load();
}

} // namespace bfrass::bfres::detail
