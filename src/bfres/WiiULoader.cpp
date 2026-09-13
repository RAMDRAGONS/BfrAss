#include "bfres/Readers.hpp"
#include "bfres/ResFile.hpp"
#include "texture/TextureContainers.hpp"

#include <algorithm>

namespace bfrass::bfres::detail {

namespace {

PrimitiveType gx2Primitive(uint32_t v) {
    switch (v) {
    case 0x01: return PrimitiveType::Points;
    case 0x02: return PrimitiveType::Lines;
    case 0x03: return PrimitiveType::LineStrip;
    case 0x04: return PrimitiveType::Triangles;
    case 0x05: return PrimitiveType::TriangleFan;
    case 0x06: return PrimitiveType::TriangleStrip;
    case 0x12: return PrimitiveType::LineLoop;
    case 0x13: return PrimitiveType::Quads;
    case 0x14: return PrimitiveType::QuadStrip;
    default: return PrimitiveType::Unknown;
    }
}

class WiiULoader {
public:
    WiiULoader(ResFile& file, const Log& log, bool loadImages, bool loadMips)
        : file_(file), log_(log), reader_(std::span<const uint8_t>(*file.buffer), Endian::Big), cafe_{reader_},
          loadImages_(loadImages), loadMips_(loadMips) {}

    void load();

private:
    const Version& version() const { return file_.version; }

    std::vector<UserData> loadUserData(uint64_t dict) const;
    void loadModel(Model& model, uint64_t offset);
    void loadSkeleton(Skeleton& skeleton, uint64_t offset);
    void loadVertexBuffer(VertexBuffer& vb, uint64_t offset, uint16_t index);
    void loadShape(Shape& shape, uint64_t offset);
    void loadMaterial(Material& material, uint64_t offset);

    ResFile& file_;
    const Log& log_;
    BinaryReader reader_;
    CafeReader cafe_;
    bool loadImages_;
    bool loadMips_;
};

std::vector<UserData> WiiULoader::loadUserData(uint64_t dict) const {
    std::vector<UserData> result;
    for (const auto& entry : cafe_.dict(dict)) {
        const uint64_t e = entry.data;
        if (!e) {
            continue;
        }
        UserData ud;
        ud.name = cafe_.string(e);
        const uint16_t n = reader_.u16(e + 4);
        const uint64_t payload = e + 8;
        switch (reader_.u8(e + 6)) {
        case 0:
            ud.type = UserData::Type::Int32;
            for (uint16_t k = 0; k < n; ++k) {
                ud.ints.push_back(reader_.s32(payload + 4 * k));
            }
            break;
        case 1:
            ud.type = UserData::Type::Float;
            for (uint16_t k = 0; k < n; ++k) {
                ud.floats.push_back(reader_.f32(payload + 4 * k));
            }
            break;
        case 2:
            ud.type = UserData::Type::String;
            for (uint16_t k = 0; k < n; ++k) {
                ud.strings.push_back(cafe_.string(payload + 4 * k));
            }
            break;
        case 3:
            ud.type = UserData::Type::WString;
            for (uint16_t k = 0; k < n; ++k) {
                ud.strings.push_back(utf16String(reader_, cafe_.offset(payload + 4 * k)));
            }
            break;
        default: {
            ud.type = UserData::Type::Bytes;
            const auto bytes = reader_.bytes(payload + 4, reader_.u32(payload));
            ud.bytes.assign(bytes.begin(), bytes.end());
            break;
        }
        }
        result.push_back(std::move(ud));
    }
    return result;
}

void WiiULoader::load() {
    const BinaryReader& r = reader_;
    file_.name = cafe_.string(0x14);
    const auto models = cafe_.dictAt(0x20);
    const auto textures = cafe_.dictAt(0x24);
    for (const auto& e : cafe_.dictAt(0x28)) {
        file_.skeletalAnimations.push_back(e.key);
    }
    for (uint64_t field : {0x2C, 0x30, 0x34, 0x38, 0x40}) {
        for (const auto& e : cafe_.dictAt(field)) {
            file_.materialAnimations.push_back(e.key);
        }
    }
    for (const auto& e : cafe_.dictAt(0x3C)) {
        file_.boneVisibilityAnimations.push_back(e.key);
    }
    for (const auto& e : cafe_.dictAt(0x44)) {
        file_.shapeAnimations.push_back(e.key);
    }
    const uint64_t sceneDicField = version().atLeast(2, 4) ? 0x48 : 0x50;
    for (const auto& e : cafe_.dictAt(sceneDicField)) {
        file_.sceneAnimations.push_back(e.key);
    }
    for (const auto& e : cafe_.dictAt(sceneDicField + 4)) {
        ExternalFile ext;
        ext.name = e.key;
        const uint64_t data = cafe_.offset(e.data);
        ext.data = r.bytes(data, r.u32(e.data + 4));
        file_.externalFiles.push_back(ext);
    }

    log_.debug("FRES: name={} version={} models={} textures={} alignment={}", file_.name, file_.version.toString(),
               models.size(), textures.size(), r.u32(0x10));

    tex::FtexContext context{file_.buffer, file_.version, file_.name, loadImages_, loadMips_};
    for (const auto& e : textures) {
        auto t = tex::parseFtex(r, e.data, context, log_);
        if (t.name.empty()) {
            t.name = e.key;
        }
        file_.textures.push_back(std::move(t));
    }

    for (const auto& e : models) {
        Model model;
        loadModel(model, e.data);
        file_.models.push_back(std::move(model));
    }
}

void WiiULoader::loadModel(Model& model, uint64_t o) {
    const BinaryReader& r = reader_;
    if (r.fixedString(o, 4) != "FMDL") {
        throw FormatError("expected FMDL at 0x" + BinaryReader::toHex(o));
    }
    model.fileOffset = o;
    model.name = cafe_.string(o + 0x04);
    model.path = cafe_.string(o + 0x08);
    const uint64_t skeleton = cafe_.offset(o + 0x0C);
    const uint64_t vertexArray = cafe_.offset(o + 0x10);
    const uint64_t shapeDic = cafe_.offset(o + 0x14);
    const uint64_t materialDic = cafe_.offset(o + 0x18);
    const uint64_t userDataDic = cafe_.offset(o + 0x1C);
    const uint16_t vertexCount = r.u16(o + 0x20);
    const uint16_t shapeCount = r.u16(o + 0x22);
    const uint16_t materialCount = r.u16(o + 0x24);

    log_.debug("FMDL: fmdlName={} eofString={} fsklOff=0x{:X} fvtxArrOff=0x{:X} fshpIndx=0x{:X} fmatIndx=0x{:X} "
               "paramOff=0x{:X} fvtxCount={} fshpCount={} fmatCount={} paramCount={}",
               model.name, model.path, skeleton, vertexArray, shapeDic, materialDic, userDataDic, vertexCount,
               shapeCount, materialCount, r.u16(o + 0x26));

    model.userData = loadUserData(userDataDic);
    loadSkeleton(model.skeleton, skeleton);
    for (uint16_t i = 0; i < vertexCount; ++i) {
        VertexBuffer vb;
        loadVertexBuffer(vb, vertexArray + uint64_t(i) * 0x20, i);
        model.vertexBuffers.push_back(std::move(vb));
    }
    for (const auto& e : cafe_.dict(materialDic)) {
        Material mat;
        loadMaterial(mat, e.data);
        model.materials.push_back(std::move(mat));
    }
    for (const auto& e : cafe_.dict(shapeDic)) {
        Shape shape;
        loadShape(shape, e.data);
        model.shapes.push_back(std::move(shape));
    }
}

void WiiULoader::loadSkeleton(Skeleton& s, uint64_t o) {
    const BinaryReader& r = reader_;
    if (r.fixedString(o, 4) != "FSKL") {
        throw FormatError("expected FSKL at 0x" + BinaryReader::toHex(o));
    }
    s.flags = r.u32(o + 0x04);
    const uint16_t boneCount = r.u16(o + 0x08);
    s.smoothMatrixCount = r.u16(o + 0x0A);
    s.rigidMatrixCount = r.u16(o + 0x0C);
    const uint64_t boneArray = cafe_.offset(o + 0x14);
    const uint64_t mtxToBone = cafe_.offset(o + 0x18);
    const bool hasInverseArray = version().atLeast(3, 4);
    const uint64_t invMatrices = hasInverseArray ? cafe_.offset(o + 0x1C) : 0;

    log_.debug("FSKL: fsklType=0x{:08X} boneArrCount={} invIndxArrCount={} exIndxCount={} boneArrOff=0x{:X} "
               "invIndxArrOff=0x{:X} invMatrArrOff=0x{:X}",
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

    const uint64_t boneSize = hasInverseArray ? 0x40 : 0x70;
    for (uint32_t i = 0; i < boneCount; ++i) {
        const uint64_t b = boneArray + i * boneSize;
        Bone bone;
        bone.name = cafe_.string(b);
        bone.index = r.u16(b + 0x04);
        const uint16_t parent = r.u16(b + 0x06);
        bone.parentIndex = parent == 0xFFFF ? -1 : parent;
        bone.smoothMatrixIndex = r.s16(b + 0x08);
        bone.rigidMatrixIndex = r.s16(b + 0x0A);
        bone.billboardIndex = r.u16(b + 0x0C);
        bone.flags = r.u32(b + 0x10);
        for (int k = 0; k < 3; ++k) {
            bone.scale[k] = r.f32(b + 0x14 + 4 * k);
        }
        for (int k = 0; k < 4; ++k) {
            bone.rotation[k] = r.f32(b + 0x20 + 4 * k);
        }
        for (int k = 0; k < 3; ++k) {
            bone.translation[k] = r.f32(b + 0x30 + 4 * k);
        }
        bone.userData = loadUserData(cafe_.offset(b + 0x3C));
        if (!hasInverseArray) {
            std::array<float, 12> m{};
            for (int k = 0; k < 12; ++k) {
                m[k] = r.f32(b + 0x40 + 4 * k);
            }
            bone.inverseMatrix = m;
        }
        log_.debug("  bone #{} {}: parIndx1={} smooth={} rigid={} bFlags=0x{:08X} scale=({}, {}, {}) "
                   "rot=({}, {}, {}, {}) pos=({}, {}, {})",
                   bone.index, bone.name, bone.parentIndex, bone.smoothMatrixIndex, bone.rigidMatrixIndex,
                   bone.flags, bone.scale[0], bone.scale[1], bone.scale[2], bone.rotation[0], bone.rotation[1],
                   bone.rotation[2], bone.rotation[3], bone.translation[0], bone.translation[1],
                   bone.translation[2]);
        s.bones.push_back(std::move(bone));
    }
}

void WiiULoader::loadVertexBuffer(VertexBuffer& vb, uint64_t o, uint16_t index) {
    const BinaryReader& r = reader_;
    if (r.fixedString(o, 4) != "FVTX") {
        throw FormatError("expected FVTX at 0x" + BinaryReader::toHex(o));
    }
    vb.fileOffset = o;
    const uint8_t attributeCount = r.u8(o + 0x04);
    const uint8_t bufferCount = r.u8(o + 0x05);
    vb.index = index;
    vb.vertexCount = r.u32(o + 0x08);
    vb.skinCount = r.u8(o + 0x0C);
    const uint64_t attributeArray = cafe_.offset(o + 0x10);
    const uint64_t bufferArray = cafe_.offset(o + 0x18);

    log_.debug("FVTX #{}: attCount={} buffCount={} vertCount={} skinCount={} attArrOff=0x{:X} buffArrOff=0x{:X}",
               index, attributeCount, bufferCount, vb.vertexCount, vb.skinCount, attributeArray, bufferArray);

    for (uint8_t i = 0; i < attributeCount; ++i) {
        const uint64_t a = attributeArray + uint64_t(i) * 12;
        VertexAttribute attr;
        attr.name = cafe_.string(a);
        attr.bufferIndex = r.u8(a + 4);
        attr.offset = r.u16(a + 6);
        attr.rawFormat = r.u32(a + 8);
        attr.format = fromGx2AttribFormat(attr.rawFormat);
        log_.debug("  attribute {}: vertType=0x{:04X} ({}) buffIndx={} buffOff={}", attr.name, attr.rawFormat,
                   attr.format.name(), attr.bufferIndex, attr.offset);
        vb.attributes.push_back(std::move(attr));
    }
    for (uint8_t i = 0; i < bufferCount; ++i) {
        const uint64_t b = bufferArray + uint64_t(i) * 24;
        VertexBufferData buf;
        buf.size = r.u32(b + 4);
        buf.stride = r.u16(b + 0x0C);
        buf.fileOffset = cafe_.offset(b + 0x14);
        buf.data = r.bytes(buf.fileOffset, buf.size);
        log_.debug("  buffer #{}: buffSize=0x{:X} strideSize={} dataOffset=0x{:X}", i, buf.size, buf.stride,
                   buf.fileOffset);
        vb.buffers.push_back(buf);
    }
}

void WiiULoader::loadShape(Shape& shape, uint64_t o) {
    const BinaryReader& r = reader_;
    if (r.fixedString(o, 4) != "FSHP") {
        throw FormatError("expected FSHP at 0x" + BinaryReader::toHex(o));
    }
    shape.fileOffset = o;
    shape.name = cafe_.string(o + 0x04);
    shape.flags = r.u32(o + 0x08);
    shape.index = r.u16(o + 0x0C);
    shape.materialIndex = r.u16(o + 0x0E);
    shape.boneIndex = r.u16(o + 0x10);
    shape.vertexBufferIndex = r.u16(o + 0x12);
    const uint16_t skinBoneCount = r.u16(o + 0x14);
    shape.skinCount = r.u8(o + 0x16);
    const uint8_t meshCount = r.u8(o + 0x17);
    const uint8_t keyShapeCount = r.u8(o + 0x18);
    shape.targetAttributeCount = r.u8(o + 0x19);
    if (version().atLeast(4, 5)) {
        const uint64_t radiusArray = cafe_.offset(o + 0x1C);
        for (uint8_t i = 0; i < meshCount && radiusArray; ++i) {
            shape.radius.push_back(r.f32(radiusArray + 4 * i));
        }
    } else {
        shape.radius.push_back(r.f32(o + 0x1C));
    }
    const uint64_t meshArray = cafe_.offset(o + 0x24);
    const uint64_t skinBoneArray = cafe_.offset(o + 0x28);
    const uint64_t keyShapeDic = cafe_.offset(o + 0x2C);

    log_.debug("FSHP #{} {}: fvtxIndx={} fmatIndx={} fsklIndx={} fsklIndxArrCount={} matrFlag={} lodMdlCount={} "
               "keyShapes={} lodMdlOff=0x{:X}",
               shape.index, shape.name, shape.vertexBufferIndex, shape.materialIndex, shape.boneIndex, skinBoneCount,
               shape.skinCount, meshCount, keyShapeCount, meshArray);

    for (uint16_t i = 0; i < skinBoneCount && skinBoneArray; ++i) {
        shape.skinBoneIndices.push_back(r.u16(skinBoneArray + 2 * i));
    }
    for (const auto& e : cafe_.dict(keyShapeDic)) {
        KeyShape key;
        key.name = e.key;
        for (int k = 0; k < 20 && e.data; ++k) {
            key.targetAttributeIndices[k] = r.u8(e.data + k);
        }
        shape.keyShapes.push_back(std::move(key));
    }

    for (uint8_t i = 0; i < meshCount; ++i) {
        const uint64_t m = meshArray + uint64_t(i) * 0x1C;
        Mesh mesh;
        mesh.fileOffset = m;
        mesh.rawPrimitive = r.u32(m);
        mesh.primitive = gx2Primitive(mesh.rawPrimitive);
        mesh.rawIndexFormat = r.u32(m + 0x04);
        mesh.indexCount = r.u32(m + 0x08);
        const uint16_t subMeshCount = r.u16(m + 0x0C);
        const uint64_t subMeshArray = cafe_.offset(m + 0x10);
        const uint64_t indexBuffer = cafe_.offset(m + 0x14);
        mesh.firstVertex = r.u32(m + 0x18);
        for (uint16_t k = 0; k < subMeshCount && subMeshArray; ++k) {
            mesh.subMeshes.push_back({r.u32(subMeshArray + 8 * k), r.u32(subMeshArray + 8 * k + 4)});
        }
        const uint32_t bufferSize = r.u32(indexBuffer + 4);
        mesh.indexBufferOffset = cafe_.offset(indexBuffer + 0x14);
        const bool wide = mesh.rawIndexFormat == 1 || mesh.rawIndexFormat == 9;
        const bool littleEndian = mesh.rawIndexFormat == 0 || mesh.rawIndexFormat == 1;
        const unsigned width = wide ? 4 : 2;
        const uint32_t count = std::min(bufferSize / width, mesh.indexCount ? mesh.indexCount : bufferSize / width);
        mesh.indices.resize(count);
        for (uint32_t k = 0; k < count; ++k) {
            const uint64_t p = mesh.indexBufferOffset + uint64_t(k) * width;
            if (wide) {
                mesh.indices[k] = littleEndian ? r.u32le(p) : r.u32be(p);
            } else {
                mesh.indices[k] = littleEndian ? r.u16le(p) : r.u16be(p);
            }
        }
        log_.debug("  LOD #{}: u1={} faceType={} dCount={} visGrpCount={} indxBuffOff=0x{:X} FaceCount={} "
                   "elmSkip={}",
                   i, mesh.rawPrimitive, mesh.rawIndexFormat, mesh.indexCount, subMeshCount, mesh.indexBufferOffset,
                   bufferSize, mesh.firstVertex);
        shape.meshes.push_back(std::move(mesh));
    }
}

void WiiULoader::loadMaterial(Material& material, uint64_t o) {
    const BinaryReader& r = reader_;
    if (r.fixedString(o, 4) != "FMAT") {
        throw FormatError("expected FMAT at 0x" + BinaryReader::toHex(o));
    }
    material.fileOffset = o;
    material.name = cafe_.string(o + 0x04);
    material.flags = r.u32(o + 0x08);
    material.index = r.u16(o + 0x0C);
    const uint16_t renderInfoCount = r.u16(o + 0x0E);
    const uint8_t samplerCount = r.u8(o + 0x10);
    const uint8_t textureCount = r.u8(o + 0x11);
    const uint16_t shaderParamCount = r.u16(o + 0x12);
    const uint64_t renderInfoDic = cafe_.offset(o + 0x1C);
    const uint64_t shaderAssign = cafe_.offset(o + 0x24);
    const uint64_t textureRefArray = cafe_.offset(o + 0x28);
    const uint64_t samplerArray = cafe_.offset(o + 0x2C);
    const uint64_t shaderParamArray = cafe_.offset(o + 0x34);
    const uint64_t sourceParam = cafe_.offset(o + 0x3C);
    const uint64_t userDataDic = cafe_.offset(o + 0x40);

    log_.debug("FMAT #{} {}: rendParamCount={} texSelCount={} texAttSelCount={} matParamCount={} texSelOff=0x{:X} "
               "texAttSelOff=0x{:X} matParamArrOff=0x{:X} matParamOff=0x{:X}",
               material.index, material.name, renderInfoCount, samplerCount, textureCount, shaderParamCount,
               textureRefArray, samplerArray, shaderParamArray, sourceParam);

    for (uint8_t i = 0; i < textureCount && textureRefArray; ++i) {
        material.textureNames.push_back(cafe_.string(textureRefArray + uint64_t(i) * 8));
    }
    for (uint8_t i = 0; i < samplerCount && samplerArray; ++i) {
        const uint64_t s = samplerArray + uint64_t(i) * 24;
        Sampler sampler;
        for (int k = 0; k < 3; ++k) {
            sampler.gx2Registers[k] = r.u32(s + 4 * k);
        }
        sampler.name = cafe_.string(s + 16);
        sampler.index = r.u8(s + 20);
        sampler.legacyFields = {r.u32(s), r.u32(s + 4), r.u32(s + 8), r.u32(s + 12)};
        material.samplers.push_back(std::move(sampler));
    }

    for (const auto& e : cafe_.dict(renderInfoDic)) {
        RenderInfo info;
        const uint16_t n = r.u16(e.data);
        info.type = RenderInfo::Type(r.u8(e.data + 2));
        info.name = cafe_.string(e.data + 4);
        for (uint16_t k = 0; k < n; ++k) {
            const uint64_t v = e.data + 8 + 4 * uint64_t(k);
            switch (info.type) {
            case RenderInfo::Type::Int32: info.ints.push_back(r.s32(v)); break;
            case RenderInfo::Type::Float: info.floats.push_back(r.f32(v)); break;
            case RenderInfo::Type::String: info.strings.push_back(cafe_.string(v)); break;
            }
        }
        material.renderInfos.push_back(std::move(info));
    }

    const uint64_t paramSize = version().atLeast(3, 4) ? 20 : (version().atLeast(3, 3) ? 24 : 12);
    for (uint16_t i = 0; i < shaderParamCount && shaderParamArray; ++i) {
        const uint64_t p = shaderParamArray + i * paramSize;
        ShaderParam param;
        param.type = ShaderParamType(std::min<uint8_t>(r.u8(p), uint8_t(ShaderParamType::Count)));
        param.sourceSize = r.u8(p + 1);
        param.sourceOffset = r.u16(p + 2);
        param.uniformOffset = r.s32(p + 4);
        if (paramSize >= 20) {
            param.dependedIndex = r.u16(p + 12);
            param.dependIndex = r.u16(p + 14);
        }
        param.name = cafe_.string(p + paramSize - 4);
        if (sourceParam && param.sourceSize) {
            const auto bytes = r.bytes(sourceParam + param.sourceOffset, param.sourceSize);
            param.value.assign(bytes.begin(), bytes.end());
        }
        material.shaderParams.push_back(std::move(param));
    }

    if (shaderAssign) {
        ShaderAssign sa;
        sa.archiveName = cafe_.string(shaderAssign);
        sa.modelName = cafe_.string(shaderAssign + 4);
        sa.revision = r.u32(shaderAssign + 8);
        const auto collect = [&](uint64_t field) {
            std::vector<std::pair<std::string, std::string>> out;
            for (const auto& e : cafe_.dictAt(field)) {
                out.emplace_back(e.key, e.data ? r.cString(e.data) : std::string());
            }
            return out;
        };
        sa.attribAssigns = collect(shaderAssign + 0x10);
        sa.samplerAssigns = collect(shaderAssign + 0x14);
        sa.options = collect(shaderAssign + 0x18);
        material.shaderAssign = std::move(sa);
    }
    material.userData = loadUserData(userDataDic);
}

} // namespace

void loadWiiU(ResFile& file, const Log& log, bool loadImages, bool loadMips) {
    WiiULoader(file, log, loadImages, loadMips).load();
}

} // namespace bfrass::bfres::detail
