#include "bfres/Skeleton.hpp"

namespace bfrass::bfres {

Mat34 boneRotationTranslation(const Skeleton& skeleton, const Bone& bone) {
    Mat34 rt = skeleton.usesQuaternions()
                   ? Mat34::fromQuaternion(bone.rotation[0], bone.rotation[1], bone.rotation[2], bone.rotation[3])
                   : Mat34::fromEulerXYZ(bone.rotation[0], bone.rotation[1], bone.rotation[2]);
    rt.setTranslation({bone.translation[0], bone.translation[1], bone.translation[2]});
    return rt;
}

BoneTransforms computeBoneTransforms(const Skeleton& skeleton) {
    const size_t count = skeleton.bones.size();
    BoneTransforms out;
    out.world.resize(count);
    out.local.resize(count);
    std::vector<Mat34> withoutScale(count);
    std::vector<Vec3> scale(count);
    std::vector<Vec3> accumulated(count);
    const uint32_t mode = skeleton.scaleMode();

    for (size_t i = 0; i < count; ++i) {
        const Bone& bone = skeleton.bones[i];
        Mat34 rt = boneRotationTranslation(skeleton, bone);
        scale[i] = (bone.flags & BoneFlag::ScaleOne) == BoneFlag::ScaleOne
                       ? Vec3{1, 1, 1}
                       : Vec3{bone.scale[0], bone.scale[1], bone.scale[2]};

        const int32_t parent = bone.parentIndex;
        const bool hasParent = parent >= 0 && size_t(parent) < i;
        if (!hasParent) {
            withoutScale[i] = rt;
            accumulated[i] = scale[i];
        } else {
            const size_t p = size_t(parent);
            switch (mode) {
            case Skeleton::ScaleNone:
                withoutScale[i] = withoutScale[p] * rt;
                break;
            case Skeleton::ScaleMaya:
                if (bone.flags & BoneFlag::SegmentScaleCompensate) {
                    const Vec3 t = rt.translationPart();
                    rt.setTranslation({t.x * scale[p].x, t.y * scale[p].y, t.z * scale[p].z});
                    withoutScale[i] = withoutScale[p] * rt;
                } else {
                    withoutScale[i] = withoutScale[p] * Mat34::scale(scale[p]) * rt;
                }
                break;
            case Skeleton::ScaleSoftimage: {
                const Vec3 t = rt.translationPart();
                const Vec3& ps = accumulated[p];
                rt.setTranslation({t.x * ps.x, t.y * ps.y, t.z * ps.z});
                withoutScale[i] = withoutScale[p] * rt;
                accumulated[i] = {ps.x * scale[i].x, ps.y * scale[i].y, ps.z * scale[i].z};
                break;
            }
            default:
                withoutScale[i] = withoutScale[p] * Mat34::scale(scale[p]) * rt;
                break;
            }
        }

        switch (mode) {
        case Skeleton::ScaleNone:
            out.world[i] = withoutScale[i];
            break;
        case Skeleton::ScaleSoftimage:
            out.world[i] = withoutScale[i] * Mat34::scale(accumulated[i]);
            break;
        default:
            out.world[i] = withoutScale[i] * Mat34::scale(scale[i]);
            break;
        }

        out.local[i] = hasParent ? out.world[size_t(parent)].inverse() * out.world[i] : out.world[i];
    }
    return out;
}

} // namespace bfrass::bfres
