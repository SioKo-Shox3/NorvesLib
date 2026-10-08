#include "Animation/SkeletalAnimationSampler.h"
#include "Animation/SkeletalPoseBuilder.h"
#include "Animation/SkeletalBindRowMath.h"
#include "Math/MatrixUtils.h"
#include "Resource/SkeletalGltfData.h"
#include <cmath>

namespace NorvesLib::Core::Animation
{
    bool SkeletalAnimationSampler::Sample(const SkeletonResource& skeleton, const AnimationClipResource& clip,
        const SkinnedMeshResource& mesh, float timeSeconds, const Math::Matrix4x4& meshTransform,
        SkeletalPoseSnapshot& out)
    {
        out.Clear();
        SkeletalPoseContext context;
        PoseScratch scratch;
        return std::isfinite(timeSeconds) && SkeletalPoseBuilder::Prepare(skeleton, clip, mesh, meshTransform, context) &&
            SkeletalPoseBuilder::Sample(context, clip, timeSeconds, scratch, out);
    }

    SkinnedVertexSample SkeletalAnimationSampler::SkinVertex(
        const Skeletal::SkeletalVertex& vertex,
        const Container::VariableArray<Math::Matrix4x4>& bonePalette)
    {
        const Math::Vector3 sourcePosition(vertex.Position.X, vertex.Position.Y, vertex.Position.Z);
        const Math::Vector3 sourceNormal(vertex.Normal.X, vertex.Normal.Y, vertex.Normal.Z);
        Math::Vector3 position = Math::Vector3::Zero;
        Math::Vector3 normal = Math::Vector3::Zero;
        float totalWeight = 0.0f;
        for (uint32_t influenceIndex = 0; influenceIndex < 4; ++influenceIndex)
        {
            const float weight = vertex.JointWeights[influenceIndex];
            const uint32_t jointIndex = vertex.JointIndices[influenceIndex];
            if (!std::isfinite(weight) || weight <= 0.0f || jointIndex >= bonePalette.size() ||
                !Detail::IsFiniteMatrix(bonePalette[jointIndex]))
            {
                continue;
            }
            const Math::Matrix4x4& palette = bonePalette[jointIndex];
            position += Math::MatrixUtils::TransformPointRowVector(palette, sourcePosition) * weight;
            const Math::Matrix4x4 normalMatrix = Math::MatrixUtils::CreateNormalMatrix(palette);
            normal += Math::MatrixUtils::TransformVectorRowVector(normalMatrix, sourceNormal) * weight;
            totalWeight += weight;
        }
        if (totalWeight <= Math::Constants::EPSILON)
        {
            return {sourcePosition, sourceNormal};
        }
        position /= totalWeight;
        normal /= totalWeight;
        if (normal.LengthSquared() > Math::Constants::EPSILON)
        {
            normal.Normalize();
        }
        return {position, normal};
    }
} // namespace NorvesLib::Core::Animation
