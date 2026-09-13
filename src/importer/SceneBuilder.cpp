#include "importer/SceneBuilder.hpp"

#include "bfres/Dump.hpp"
#include "bfres/Skeleton.hpp"

#include <assimp/commonMetaData.h>
#include <assimp/mesh.h>
#include <assimp/metadata.h>
#include <assimp/scene.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <set>

namespace bfrass::importer {

namespace {

using bfres::Mesh;
using bfres::PrimitiveType;
using bfres::Shape;
using bfres::VertexBuffer;

aiMatrix4x4 toAi(const Mat34& m) {
    return aiMatrix4x4(m.m[0][0], m.m[0][1], m.m[0][2], m.m[0][3], m.m[1][0], m.m[1][1], m.m[1][2], m.m[1][3],
                       m.m[2][0], m.m[2][1], m.m[2][2], m.m[2][3], 0, 0, 0, 1);
}

using Stream = std::vector<std::array<float, 4>>;
using Influences = std::vector<std::pair<uint32_t, float>>;

struct ShapeData {
    uint32_t vertexCount = 0;
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<std::array<float, 4>> tangents;
    std::vector<std::array<float, 4>> bitangents;
    std::vector<Stream> colors;
    std::array<std::vector<std::array<float, 2>>, 5> uvs;
    std::vector<std::vector<Vec3>> morphPositions;
    std::vector<std::vector<Vec3>> morphNormals;
    std::vector<std::string> morphNames;
    std::vector<Influences> influences;
};

struct TmpNode {
    std::string name;
    aiMatrix4x4 transform;
    std::vector<unsigned> meshes;
    std::vector<std::unique_ptr<TmpNode>> children;
    std::vector<std::pair<std::string, std::string>> metadata;

    TmpNode* addChild(std::string childName) {
        children.push_back(std::make_unique<TmpNode>());
        children.back()->name = std::move(childName);
        return children.back().get();
    }
};

aiNode* finalizeNode(const TmpNode& tmp, aiNode* parent) {
    auto* node = new aiNode(tmp.name);
    node->mParent = parent;
    node->mTransformation = tmp.transform;
    if (!tmp.meshes.empty()) {
        node->mNumMeshes = static_cast<unsigned>(tmp.meshes.size());
        node->mMeshes = new unsigned[tmp.meshes.size()];
        std::copy(tmp.meshes.begin(), tmp.meshes.end(), node->mMeshes);
    }
    if (!tmp.children.empty()) {
        node->mNumChildren = static_cast<unsigned>(tmp.children.size());
        node->mChildren = new aiNode*[tmp.children.size()];
        for (size_t i = 0; i < tmp.children.size(); ++i) {
            node->mChildren[i] = finalizeNode(*tmp.children[i], node);
        }
    }
    if (!tmp.metadata.empty()) {
        node->mMetaData = aiMetadata::Alloc(static_cast<unsigned>(tmp.metadata.size()));
        for (size_t i = 0; i < tmp.metadata.size(); ++i) {
            node->mMetaData->Set(static_cast<unsigned>(i), tmp.metadata[i].first, aiString(tmp.metadata[i].second));
        }
    }
    return node;
}

Stream readStream(const BinaryReader& reader, const VertexBuffer& vb, const bfres::VertexAttribute& attr) {
    Stream out;
    if (attr.bufferIndex >= vb.buffers.size() || !attr.format.valid()) {
        return out;
    }
    const auto& buffer = vb.buffers[attr.bufferIndex];
    const uint32_t stride = buffer.stride ? buffer.stride : attr.format.byteSize();
    out.resize(vb.vertexCount);
    for (uint32_t v = 0; v < vb.vertexCount; ++v) {
        out[v] = bfres::readAttribute(reader, buffer.fileOffset + attr.offset + uint64_t(v) * stride, attr.format);
    }
    return out;
}

bool parseIndex(const std::string& s, size_t& value) {
    const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
    return result.ec == std::errc() && result.ptr == s.data() + s.size();
}

struct Face {
    std::array<uint32_t, 3> v{};
    uint8_t count = 3;
};

std::vector<Face> buildFaces(const Mesh& mesh, uint32_t vertexCount, const Log& log, const std::string& name) {
    std::vector<uint32_t> idx(mesh.indices.size());
    for (size_t i = 0; i < idx.size(); ++i) {
        idx[i] = mesh.indices[i] + mesh.firstVertex;
    }
    std::vector<Face> faces;
    const auto tri = [&](uint32_t a, uint32_t b, uint32_t c) {
        if (a != b && b != c && a != c) {
            faces.push_back({{a, b, c}, 3});
        }
    };
    switch (mesh.primitive) {
    case PrimitiveType::Points:
        for (uint32_t i : idx) {
            faces.push_back({{i, 0, 0}, 1});
        }
        break;
    case PrimitiveType::Lines:
        for (size_t i = 0; i + 1 < idx.size(); i += 2) {
            faces.push_back({{idx[i], idx[i + 1], 0}, 2});
        }
        break;
    case PrimitiveType::LineStrip:
    case PrimitiveType::LineLoop:
        for (size_t i = 0; i + 1 < idx.size(); ++i) {
            faces.push_back({{idx[i], idx[i + 1], 0}, 2});
        }
        if (mesh.primitive == PrimitiveType::LineLoop && idx.size() > 2) {
            faces.push_back({{idx.back(), idx.front(), 0}, 2});
        }
        break;
    case PrimitiveType::TriangleStrip:
        for (size_t i = 2; i < idx.size(); ++i) {
            if (i % 2 == 0) {
                tri(idx[i - 2], idx[i - 1], idx[i]);
            } else {
                tri(idx[i - 1], idx[i - 2], idx[i]);
            }
        }
        break;
    case PrimitiveType::TriangleFan:
        for (size_t i = 2; i < idx.size(); ++i) {
            tri(idx[0], idx[i - 1], idx[i]);
        }
        break;
    case PrimitiveType::Quads:
        for (size_t i = 0; i + 3 < idx.size(); i += 4) {
            tri(idx[i], idx[i + 1], idx[i + 2]);
            tri(idx[i], idx[i + 2], idx[i + 3]);
        }
        break;
    case PrimitiveType::QuadStrip:
        for (size_t i = 0; i + 3 < idx.size(); i += 2) {
            tri(idx[i], idx[i + 1], idx[i + 3]);
            tri(idx[i], idx[i + 3], idx[i + 2]);
        }
        break;
    default:
        if (mesh.primitive != PrimitiveType::Triangles) {
            log.warn("{}: unknown primitive type {}, reading indices as a triangle list", name, mesh.rawPrimitive);
        }
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            faces.push_back({{idx[i], idx[i + 1], idx[i + 2]}, 3});
        }
        break;
    }
    const size_t before = faces.size();
    std::erase_if(faces, [&](const Face& f) {
        for (uint8_t k = 0; k < f.count; ++k) {
            if (f.v[k] >= vertexCount) {
                return true;
            }
        }
        return false;
    });
    if (faces.size() != before) {
        log.warn("{}: dropped {} face(s) referencing vertices beyond the buffer", name, before - faces.size());
    }
    return faces;
}

class ModelBuilder {
public:
    ModelBuilder(const bfres::ResFile& file, const bfres::Model& model, const ImportConfig& config, const Log& log,
                 std::string boneSuffix, unsigned materialBase, std::vector<aiMesh*>& meshes)
        : model_(model), config_(config), log_(log), reader_(std::span<const uint8_t>(*file.buffer), file.endian),
          suffix_(std::move(boneSuffix)), materialBase_(materialBase), meshes_(meshes) {}

    void build(TmpNode& parent, std::set<std::string>& usedNames);

private:
    ShapeData decodeShape(const Shape& shape, const VertexBuffer& vb) const;
    void buildShape(const Shape& shape, TmpNode& modelNode, std::set<std::string>& usedNames);
    std::string uniqueName(std::string name, std::set<std::string>& usedNames) const;
    uint32_t resolveBone(uint32_t raw) const;

    const bfres::Model& model_;
    const ImportConfig& config_;
    const Log& log_;
    BinaryReader reader_;
    std::string suffix_;
    unsigned materialBase_;
    std::vector<aiMesh*>& meshes_;
    bfres::BoneTransforms transforms_;
    std::vector<TmpNode*> boneNodes_;
    std::vector<std::string> boneNames_;
};

uint32_t ModelBuilder::resolveBone(uint32_t raw) const {
    const auto& table = model_.skeleton.matrixToBone;
    if (raw < table.size()) {
        return table[raw];
    }
    return raw;
}

std::string ModelBuilder::uniqueName(std::string name, std::set<std::string>& usedNames) const {
    if (usedNames.insert(name).second) {
        return name;
    }
    for (unsigned i = 2;; ++i) {
        std::string candidate = name + " (" + std::to_string(i) + ")";
        if (usedNames.insert(candidate).second) {
            return candidate;
        }
    }
}

void ModelBuilder::build(TmpNode& modelNode, std::set<std::string>& usedNames) {
    const auto& skeleton = model_.skeleton;
    transforms_ = bfres::computeBoneTransforms(skeleton);
    boneNodes_.assign(skeleton.bones.size(), nullptr);
    boneNames_.resize(skeleton.bones.size());
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        const auto& bone = skeleton.bones[i];
        const bool hasParent = bone.parentIndex >= 0 && size_t(bone.parentIndex) < i && boneNodes_[bone.parentIndex];
        TmpNode* parent = hasParent ? boneNodes_[bone.parentIndex] : &modelNode;
        boneNames_[i] = uniqueName(bone.name + suffix_, usedNames);
        TmpNode* node = parent->addChild(boneNames_[i]);
        node->transform = toAi(transforms_.local[i]);
        node->metadata.emplace_back("bfres.bone.index", std::to_string(bone.index));
        node->metadata.emplace_back("bfres.bone.flags", std::format("0x{:08X}", bone.flags));
        node->metadata.emplace_back("bfres.bone.smoothMatrixIndex", std::to_string(bone.smoothMatrixIndex));
        node->metadata.emplace_back("bfres.bone.rigidMatrixIndex", std::to_string(bone.rigidMatrixIndex));
        node->metadata.emplace_back("bfres.bone.visible", (bone.flags & bfres::BoneFlag::Visible) ? "true" : "false");
        for (const auto& ud : bone.userData) {
            node->metadata.emplace_back("bfres.userdata." + ud.name, bfres::formatUserData(ud).substr(ud.name.size() + 2));
        }
        boneNodes_[i] = node;
    }
    for (const auto& shape : model_.shapes) {
        buildShape(shape, modelNode, usedNames);
    }
}

ShapeData ModelBuilder::decodeShape(const Shape& shape, const VertexBuffer& vb) const {
    ShapeData d;
    d.vertexCount = vb.vertexCount;
    Stream index[2], weight[2];
    unsigned indexComponents[2] = {0, 0};
    unsigned weightComponents[2] = {0, 0};
    std::map<unsigned, Stream> morphPos, morphNrm;
    std::array<bool, 5> uvWritten{};

    const auto setUv = [&](unsigned channel, const Stream& s, unsigned xi, unsigned yi, float scale, bool flip) {
        if (uvWritten[channel]) {
            return;
        }
        auto& target = d.uvs[channel];
        target.resize(s.size());
        for (size_t v = 0; v < s.size(); ++v) {
            const float u = s[v][xi] * scale;
            const float w = s[v][yi] * scale;
            target[v] = {u, flip ? 1.0f - w : w};
        }
        uvWritten[channel] = true;
    };

    if (log_.enabled(LogChannel::Debug) && !vb.buffers.empty()) {
        log_.debug("Vertex buffer starts at 0x{:X}", vb.buffers[0].fileOffset);
    }

    for (const auto& attr : vb.attributes) {
        if (!attr.format.valid()) {
            log_.warn("{}: attribute {} has unsupported format 0x{:X}, skipped", shape.name, attr.name, attr.rawFormat);
            continue;
        }
        const std::string& n = attr.name;
        if (n == "_p0") {
            const Stream s = readStream(reader_, vb, attr);
            d.positions.resize(s.size());
            for (size_t v = 0; v < s.size(); ++v) {
                d.positions[v] = {s[v][0], s[v][1], s[v][2]};
            }
        } else if (n.size() > 2 && n.starts_with("_p") && std::isdigit(static_cast<unsigned char>(n[2]))) {
            size_t k = 0;
            if (parseIndex(n.substr(2), k) && k >= 1) {
                morphPos[unsigned(k)] = readStream(reader_, vb, attr);
            }
        } else if (n == "_n0") {
            const Stream s = readStream(reader_, vb, attr);
            d.normals.resize(s.size());
            for (size_t v = 0; v < s.size(); ++v) {
                d.normals[v] = normalize({s[v][0], s[v][1], s[v][2]});
            }
        } else if (n.size() > 2 && n.starts_with("_n") && std::isdigit(static_cast<unsigned char>(n[2]))) {
            size_t k = 0;
            if (parseIndex(n.substr(2), k) && k >= 1) {
                morphNrm[unsigned(k)] = readStream(reader_, vb, attr);
            }
        } else if (n == "_t0") {
            d.tangents = readStream(reader_, vb, attr);
        } else if (n == "_b0") {
            d.bitangents = readStream(reader_, vb, attr);
        } else if (n.size() == 3 && n.starts_with("_c") && std::isdigit(static_cast<unsigned char>(n[2]))) {
            if (config_.vertexColors) {
                const unsigned slot = unsigned(n[2] - '0');
                if (d.colors.size() <= slot) {
                    d.colors.resize(slot + 1);
                }
                d.colors[slot] = readStream(reader_, vb, attr);
            }
        } else if (n == "detail") {
            setUv(2, readStream(reader_, vb, attr), 0, 1, 2.0f, false);
        } else if (n == "_u0" || n == "color") {
            setUv(0, readStream(reader_, vb, attr), 0, 1, 1.0f, true);
        } else if (n == "_u1") {
            setUv(1, readStream(reader_, vb, attr), 0, 1, 1.0f, true);
        } else if (n == "_u2") {
            setUv(2, readStream(reader_, vb, attr), 0, 1, 1.0f, true);
        } else if (n == "_u3") {
            setUv(3, readStream(reader_, vb, attr), 0, 1, 1.0f, true);
        } else if (n == "_u4" || n == "All") {
            setUv(4, readStream(reader_, vb, attr), 0, 1, 1.0f, true);
        } else if (n == "_g3d_02_u0_u1") {
            const Stream s = readStream(reader_, vb, attr);
            setUv(0, s, 0, 1, 1.0f, true);
            setUv(1, s, 2, 3, 1.0f, true);
        } else if (n == "_g3d_02_u2_u3") {
            const Stream s = readStream(reader_, vb, attr);
            setUv(2, s, 0, 1, 1.0f, true);
            setUv(3, s, 2, 3, 1.0f, true);
        } else if (n == "_g3d_02_u2_u0" || n == "_g3d_02__u0") {
            const Stream s = readStream(reader_, vb, attr);
            setUv(2, s, 0, 1, 1.0f, true);
            setUv(0, s, 2, 3, 1.0f, true);
        } else if (n == "_i0" || n == "_i1") {
            const unsigned slot = n[2] - '0';
            index[slot] = readStream(reader_, vb, attr);
            indexComponents[slot] = attr.format.components;
        } else if (n == "_w0" || n == "_w1") {
            const unsigned slot = n[2] - '0';
            weight[slot] = readStream(reader_, vb, attr);
            weightComponents[slot] = attr.format.components;
        }
    }
    while (!d.colors.empty() && d.colors.back().empty()) {
        d.colors.pop_back();
    }

    // Morph targets: _p1.._pN, named after key shapes when the counts line up.
    for (auto& [k, stream] : morphPos) {
        std::vector<Vec3> pos(stream.size());
        for (size_t v = 0; v < stream.size(); ++v) {
            pos[v] = {stream[v][0], stream[v][1], stream[v][2]};
        }
        std::vector<Vec3> nrm;
        const auto nit = morphNrm.find(k);
        if (nit != morphNrm.end()) {
            nrm.resize(nit->second.size());
            for (size_t v = 0; v < nrm.size(); ++v) {
                nrm[v] = normalize({nit->second[v][0], nit->second[v][1], nit->second[v][2]});
            }
        }
        std::string morphName = k < shape.keyShapes.size() && !shape.keyShapes[k].name.empty()
                                    ? shape.keyShapes[k].name
                                    : "Morph " + std::to_string(k);
        d.morphPositions.push_back(std::move(pos));
        d.morphNormals.push_back(std::move(nrm));
        d.morphNames.push_back(std::move(morphName));
    }

    d.influences.resize(d.vertexCount);
    const bool hasBones = !model_.skeleton.bones.empty();
    for (uint32_t v = 0; v < d.vertexCount && hasBones; ++v) {
        Influences& inf = d.influences[v];
        if (shape.skinCount == 0) {
            inf.emplace_back(shape.boneIndex, 1.0f);
            continue;
        }
        unsigned used = 0;
        for (unsigned s = 0; s < 2 && used < shape.skinCount; ++s) {
            if (index[s].empty()) {
                continue;
            }
            for (unsigned c = 0; c < indexComponents[s] && used < shape.skinCount; ++c, ++used) {
                float w;
                if (!weight[s].empty() && c < weightComponents[s]) {
                    w = weight[s][v][c];
                } else {
                    w = (s == 0 && c == 0) ? 1.0f : 0.0f;
                }
                if (w <= 0.0f) {
                    continue;
                }
                const uint32_t bone = resolveBone(uint32_t(std::lround(index[s][v][c])));
                if (bone < model_.skeleton.bones.size()) {
                    inf.emplace_back(bone, w);
                }
            }
        }
        float total = 0.0f;
        for (const auto& [b, w] : inf) {
            total += w;
        }
        if (total > 0.0f) {
            for (auto& [b, w] : inf) {
                w /= total;
            }
        }
    }
    return d;
}

void ModelBuilder::buildShape(const Shape& shape, TmpNode& modelNode, std::set<std::string>& usedNames) {
    if (shape.vertexBufferIndex >= model_.vertexBuffers.size()) {
        log_.warn("{}: vertex buffer {} does not exist", shape.name, shape.vertexBufferIndex);
        return;
    }
    const VertexBuffer& vb = model_.vertexBuffers[shape.vertexBufferIndex];
    log_.debug("Building polygon {}...", shape.name);
    ShapeData data = decodeShape(shape, vb);
    if (data.positions.empty()) {
        log_.warn("{}: no _p0 position attribute, shape skipped", shape.name);
        return;
    }

    const auto& skeleton = model_.skeleton;
    const bool hasBones = !skeleton.bones.empty();
    const bool rigidToShapeBone = shape.skinCount == 0;
    const bool perVertexRigid = shape.skinCount == 1;
    const RiggingMode mode = config_.rigging;

    // Vertices of rigid shapes are authored in bone space. Baking moves them to
    // model space, matching "reset XForms" (and the only sensible result without rigging).
    const bool bakeShapeBone = hasBones && rigidToShapeBone && mode == RiggingMode::SkinAndReset;
    const bool bakePerVertex = hasBones && perVertexRigid && mode != RiggingMode::SkinOnly;
    if (bakeShapeBone || bakePerVertex) {
        for (uint32_t v = 0; v < data.vertexCount; ++v) {
            if (data.influences[v].empty()) {
                continue;
            }
            const uint32_t bone = data.influences[v].front().first;
            if (bone >= transforms_.world.size()) {
                continue;
            }
            const Mat34& world = transforms_.world[bone];
            const Mat34 normalMatrix = world.normalMatrix();
            data.positions[v] = world.transformPoint(data.positions[v]);
            if (v < data.normals.size()) {
                data.normals[v] = normalize(normalMatrix.transformVector(data.normals[v]));
            }
            for (auto* stream : {&data.tangents, &data.bitangents}) {
                if (v < stream->size()) {
                    auto& t = (*stream)[v];
                    const Vec3 r = normalize(world.transformVector({t[0], t[1], t[2]}));
                    t = {r.x, r.y, r.z, t[3]};
                }
            }
            for (size_t m = 0; m < data.morphPositions.size(); ++m) {
                if (v < data.morphPositions[m].size()) {
                    data.morphPositions[m][v] = world.transformPoint(data.morphPositions[m][v]);
                }
                if (v < data.morphNormals[m].size()) {
                    data.morphNormals[m][v] = normalize(normalMatrix.transformVector(data.morphNormals[m][v]));
                }
            }
        }
    }

    const bool attachToBone = hasBones && rigidToShapeBone && mode != RiggingMode::SkinAndReset &&
                              shape.boneIndex < boneNodes_.size();
    TmpNode* parentNode = attachToBone ? boneNodes_[shape.boneIndex] : &modelNode;
    const bool skinned = hasBones && mode != RiggingMode::None;
    // Offsets are identity when the mesh already lives in bone space (rigid shape
    // parented to its bone) or when binding with un-reset transforms.
    const bool identityOffsets = (attachToBone) || (mode == RiggingMode::SkinOnly && perVertexRigid);

    const size_t lodCount = config_.importLods ? shape.meshes.size() : std::min<size_t>(1, shape.meshes.size());
    const unsigned materialIndex = materialBase_ + (shape.materialIndex < model_.materials.size() ? shape.materialIndex : 0);

    for (size_t l = 0; l < lodCount; ++l) {
        const Mesh& lod = shape.meshes[l];
        const std::string lodSuffix = lodCount != 1 ? " (LOD: " + std::to_string(l + 1) + ")" : "";
        const std::vector<Face> faces = buildFaces(lod, data.vertexCount, log_, shape.name);
        if (faces.empty()) {
            continue;
        }

        std::vector<int64_t> remap(data.vertexCount, -1);
        std::vector<uint32_t> order;
        for (const Face& f : faces) {
            for (uint8_t k = 0; k < f.count; ++k) {
                if (remap[f.v[k]] < 0) {
                    remap[f.v[k]] = int64_t(order.size());
                    order.push_back(f.v[k]);
                }
            }
        }

        struct Variant {
            std::string name;
            int uvLayer = -1;  // -1 keeps the configured channels, otherwise a single source layer
            int morph = -1;    // -1 base positions, otherwise a morph target as the base mesh
        };
        std::vector<Variant> variants;
        variants.push_back({shape.name + lodSuffix, -1, -1});
        if (config_.uvLayers == UvLayerMode::Split) {
            for (int layer = 1; layer < 5; ++layer) {
                if (data.uvs[layer].size() > 1) {
                    variants.push_back({shape.name + " Layer " + std::to_string(layer + 1) + lodSuffix, layer, -1});
                }
            }
        }
        if (config_.morphTargetsAsMeshes) {
            for (size_t m = 0; m < data.morphPositions.size(); ++m) {
                variants.push_back({shape.name + " [Morph " + std::to_string(m + 1) + "]" + lodSuffix, -1, int(m)});
            }
        }

        for (const Variant& variant : variants) {
            auto* mesh = new aiMesh();
            mesh->mName = aiString(variant.name);
            mesh->mMaterialIndex = materialIndex;
            mesh->mNumVertices = static_cast<unsigned>(order.size());

            const std::vector<Vec3>& positions = variant.morph >= 0 ? data.morphPositions[variant.morph] : data.positions;
            const std::vector<Vec3>& normals =
                (variant.morph >= 0 && !data.morphNormals[variant.morph].empty()) ? data.morphNormals[variant.morph]
                                                                                  : data.normals;
            mesh->mVertices = new aiVector3D[order.size()];
            for (size_t i = 0; i < order.size(); ++i) {
                const Vec3& p = positions[order[i]];
                mesh->mVertices[i] = aiVector3D(p.x, p.y, p.z);
            }
            if (normals.size() == data.vertexCount) {
                mesh->mNormals = new aiVector3D[order.size()];
                for (size_t i = 0; i < order.size(); ++i) {
                    const Vec3& n = normals[order[i]];
                    mesh->mNormals[i] = aiVector3D(n.x, n.y, n.z);
                }
            }
            if (data.tangents.size() == data.vertexCount && mesh->mNormals) {
                mesh->mTangents = new aiVector3D[order.size()];
                mesh->mBitangents = new aiVector3D[order.size()];
                for (size_t i = 0; i < order.size(); ++i) {
                    const auto& t = data.tangents[order[i]];
                    const aiVector3D tangent(t[0], t[1], t[2]);
                    mesh->mTangents[i] = tangent;
                    if (data.bitangents.size() == data.vertexCount) {
                        const auto& b = data.bitangents[order[i]];
                        mesh->mBitangents[i] = aiVector3D(b[0], b[1], b[2]);
                    } else {
                        // The w component carries handedness in NintendoWare tangent streams.
                        const float sign = t[3] < 0.0f ? -1.0f : 1.0f;
                        mesh->mBitangents[i] = (mesh->mNormals[i] ^ tangent) * sign;
                    }
                }
            }
            unsigned colorSet = 0;
            for (const Stream& colors : data.colors) {
                if (colors.size() != data.vertexCount || colorSet >= AI_MAX_NUMBER_OF_COLOR_SETS) {
                    continue;
                }
                mesh->mColors[colorSet] = new aiColor4D[order.size()];
                for (size_t i = 0; i < order.size(); ++i) {
                    const auto& c = colors[order[i]];
                    mesh->mColors[colorSet][i] = aiColor4D(std::clamp(c[0], 0.0f, 1.0f), std::clamp(c[1], 0.0f, 1.0f),
                                                           std::clamp(c[2], 0.0f, 1.0f), std::clamp(c[3], 0.0f, 1.0f));
                }
                ++colorSet;
            }

            std::vector<int> uvSources;
            if (variant.uvLayer >= 0) {
                uvSources.push_back(variant.uvLayer);
            } else if (config_.uvLayers == UvLayerMode::Merge) {
                for (int layer = 0; layer < 5; ++layer) {
                    if (data.uvs[layer].size() == data.vertexCount) {
                        uvSources.push_back(layer);
                    }
                }
            } else if (data.uvs[0].size() == data.vertexCount) {
                uvSources.push_back(0);
            }
            unsigned channel = 0;
            for (int layer : uvSources) {
                if (data.uvs[layer].size() != data.vertexCount || channel >= AI_MAX_NUMBER_OF_TEXTURECOORDS) {
                    continue;
                }
                mesh->mTextureCoords[channel] = new aiVector3D[order.size()];
                mesh->mNumUVComponents[channel] = 2;
                for (size_t i = 0; i < order.size(); ++i) {
                    const auto& uv = data.uvs[layer][order[i]];
                    mesh->mTextureCoords[channel][i] = aiVector3D(uv[0], uv[1], 0.0f);
                }
                ++channel;
            }

            mesh->mNumFaces = static_cast<unsigned>(faces.size());
            mesh->mFaces = new aiFace[faces.size()];
            for (size_t f = 0; f < faces.size(); ++f) {
                aiFace& face = mesh->mFaces[f];
                face.mNumIndices = faces[f].count;
                face.mIndices = new unsigned[faces[f].count];
                for (uint8_t k = 0; k < faces[f].count; ++k) {
                    face.mIndices[k] = static_cast<unsigned>(remap[faces[f].v[k]]);
                }
                mesh->mPrimitiveTypes |= faces[f].count == 1   ? aiPrimitiveType_POINT
                                         : faces[f].count == 2 ? aiPrimitiveType_LINE
                                                               : aiPrimitiveType_TRIANGLE;
            }

            if (skinned) {
                std::map<uint32_t, std::vector<aiVertexWeight>> weights;
                for (size_t i = 0; i < order.size(); ++i) {
                    for (const auto& [bone, w] : data.influences[order[i]]) {
                        weights[bone].emplace_back(static_cast<unsigned>(i), w);
                    }
                }
                if (!weights.empty()) {
                    mesh->mNumBones = static_cast<unsigned>(weights.size());
                    mesh->mBones = new aiBone*[weights.size()];
                    unsigned b = 0;
                    for (auto& [bone, list] : weights) {
                        auto* aiB = new aiBone();
                        aiB->mName = aiString(boneNames_[bone]);
                        aiB->mOffsetMatrix = identityOffsets ? aiMatrix4x4() : toAi(transforms_.world[bone].inverse());
                        aiB->mNumWeights = static_cast<unsigned>(list.size());
                        aiB->mWeights = new aiVertexWeight[list.size()];
                        std::copy(list.begin(), list.end(), aiB->mWeights);
                        mesh->mBones[b++] = aiB;
                    }
                }
            }

            if (variant.morph < 0 && !data.morphPositions.empty() && !config_.morphTargetsAsMeshes) {
                mesh->mNumAnimMeshes = static_cast<unsigned>(data.morphPositions.size());
                mesh->mAnimMeshes = new aiAnimMesh*[data.morphPositions.size()];
                mesh->mMethod = aiMorphingMethod_MORPH_NORMALIZED;
                for (size_t m = 0; m < data.morphPositions.size(); ++m) {
                    auto* anim = new aiAnimMesh();
                    anim->mName = aiString(data.morphNames[m]);
                    anim->mNumVertices = mesh->mNumVertices;
                    anim->mWeight = 0.0f;
                    anim->mVertices = new aiVector3D[order.size()];
                    for (size_t i = 0; i < order.size(); ++i) {
                        const Vec3& p = data.morphPositions[m][order[i]];
                        anim->mVertices[i] = aiVector3D(p.x, p.y, p.z);
                    }
                    if (data.morphNormals[m].size() == data.vertexCount) {
                        anim->mNormals = new aiVector3D[order.size()];
                        for (size_t i = 0; i < order.size(); ++i) {
                            const Vec3& n = data.morphNormals[m][order[i]];
                            anim->mNormals[i] = aiVector3D(n.x, n.y, n.z);
                        }
                    }
                    mesh->mAnimMeshes[m] = anim;
                }
            }

            const unsigned meshIndex = static_cast<unsigned>(meshes_.size());
            meshes_.push_back(mesh);
            TmpNode* meshNode = parentNode->addChild(uniqueName(variant.name, usedNames));
            meshNode->meshes.push_back(meshIndex);
            meshNode->metadata.emplace_back("bfres.shape.skinCount", std::to_string(shape.skinCount));
            meshNode->metadata.emplace_back("bfres.shape.lod", std::to_string(l));
        }
    }
}

} // namespace

SceneBuilder::SceneBuilder(const bfres::ResFile& file, const ImportConfig& config, const Log& log)
    : file_(file), config_(config), log_(log) {}

std::vector<size_t> SceneBuilder::selectModels() const {
    std::vector<size_t> selection;
    if (config_.models.empty()) {
        for (size_t i = 0; i < file_.models.size(); ++i) {
            selection.push_back(i);
        }
        return selection;
    }
    for (const auto& requested : config_.models) {
        bool found = false;
        for (size_t i = 0; i < file_.models.size(); ++i) {
            if (file_.models[i].name == requested) {
                selection.push_back(i);
                found = true;
            }
        }
        size_t index = 0;
        if (!found && parseIndex(requested, index) && index < file_.models.size()) {
            selection.push_back(index);
            found = true;
        }
        if (!found) {
            log_.warn("Model '{}' not found in {}", requested, file_.name);
        }
    }
    std::vector<size_t> unique;
    for (size_t i : selection) {
        if (std::find(unique.begin(), unique.end(), i) == unique.end()) {
            unique.push_back(i);
        }
    }
    return unique;
}

void SceneBuilder::build(aiScene* scene) {
    const std::vector<size_t> selection = selectModels();

    std::vector<std::string> referenced;
    for (size_t m : selection) {
        for (const auto& mat : file_.models[m].materials) {
            const size_t count = std::min(mat.samplers.size(), mat.textureNames.size());
            referenced.insert(referenced.end(), mat.textureNames.begin(), mat.textureNames.begin() + count);
        }
    }
    ImportConfig textureConfig = config_;
    if (file_.models.empty()) {
        textureConfig.exportUnreferencedTextures = true;
    }
    TextureTable textureTable(file_, textureConfig, log_);
    textureTable.prepare(referenced);

    std::vector<aiMaterial*> materials;
    std::vector<aiMesh*> meshes;
    TmpNode root;
    root.name = "FRES_" + file_.name;
    std::set<std::string> usedNames{root.name};

    for (size_t x = 0; x < selection.size(); ++x) {
        const auto& model = file_.models[selection[x]];
        const unsigned materialBase = static_cast<unsigned>(materials.size());
        for (const auto& mat : model.materials) {
            materials.push_back(buildMaterial(mat, file_, textureTable, config_, log_));
        }
        if (model.materials.empty()) {
            auto* fallback = new aiMaterial();
            const aiString name(model.name + "_default");
            fallback->AddProperty(&name, AI_MATKEY_NAME);
            materials.push_back(fallback);
        }
        std::string modelNodeName = "FMDL_" + model.name;
        usedNames.insert(modelNodeName);
        TmpNode* modelNode = root.addChild(modelNodeName);
        for (const auto& ud : model.userData) {
            modelNode->metadata.emplace_back("bfres.userdata." + ud.name, bfres::formatUserData(ud).substr(ud.name.size() + 2));
        }
        const std::string suffix = selection.size() > 1 ? "_" + std::to_string(x + 1) : "";
        ModelBuilder(file_, model, config_, log_, suffix, materialBase, meshes).build(*modelNode, usedNames);
    }

    if (materials.empty()) {
        auto* fallback = new aiMaterial();
        const aiString name("default");
        fallback->AddProperty(&name, AI_MATKEY_NAME);
        materials.push_back(fallback);
    }

    scene->mRootNode = finalizeNode(root, nullptr);
    scene->mNumMaterials = static_cast<unsigned>(materials.size());
    scene->mMaterials = new aiMaterial*[materials.size()];
    std::copy(materials.begin(), materials.end(), scene->mMaterials);
    if (!meshes.empty()) {
        scene->mNumMeshes = static_cast<unsigned>(meshes.size());
        scene->mMeshes = new aiMesh*[meshes.size()];
        std::copy(meshes.begin(), meshes.end(), scene->mMeshes);
    } else {
        scene->mFlags |= AI_SCENE_FLAGS_INCOMPLETE;
    }
    std::vector<aiTexture*> embedded = textureTable.releaseEmbedded();
    if (!embedded.empty()) {
        scene->mNumTextures = static_cast<unsigned>(embedded.size());
        scene->mTextures = new aiTexture*[embedded.size()];
        std::copy(embedded.begin(), embedded.end(), scene->mTextures);
    }

    scene->mMetaData = aiMetadata::Alloc(3);
    scene->mMetaData->Set(0, AI_METADATA_SOURCE_FORMAT, aiString("NintendoWare BFRES"));
    scene->mMetaData->Set(1, AI_METADATA_SOURCE_FORMAT_VERSION, aiString(file_.version.toString()));
    scene->mMetaData->Set(2, "bfres.platform", aiString(file_.platform == bfres::Platform::WiiU ? "WiiU" : "Switch"));
}

} // namespace bfrass::importer
