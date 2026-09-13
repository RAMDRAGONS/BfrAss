#pragma once

#include "bfres/ResFile.hpp"
#include "core/Math.hpp"

#include <vector>

namespace bfrass::bfres {

struct BoneTransforms {
    // Model space bind pose of every bone, including its own (or accumulated) scale.
    std::vector<Mat34> world;
    // Transform relative to the parent's model space matrix.
    std::vector<Mat34> local;
};

// Evaluates the rest pose using the same hierarchy rules as nn::g3d::SkeletonObj
// (standard, Maya segment scale compensation, Softimage and scale-less modes).
BoneTransforms computeBoneTransforms(const Skeleton& skeleton);

// Scale/rotation/translation of a single bone as authored.
Mat34 boneRotationTranslation(const Skeleton& skeleton, const Bone& bone);

} // namespace bfrass::bfres
