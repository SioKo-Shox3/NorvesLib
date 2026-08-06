#pragma once

#include "Container/Containers.h"
#include "Math/GeometryTypes.h"
#include "Math/Matrix4x4.h"
#include "Math/Vector3.h"

namespace NorvesLib::Core
{
    class AnimationClipResource;
    class SkeletonResource;
    class SkinnedMeshResource;

    namespace Skeletal
    {
        struct SkeletalVertex;
    }
}

namespace NorvesLib::Core::Animation
{
    struct SkinnedVertexSample
    {
        Math::Vector3 Position;
        Math::Vector3 Normal;
    };

    struct SkeletalPoseSnapshot
    {
        Container::VariableArray<Math::Matrix4x4> BonePalette;
        Math::AABB AnimatedBounds;
        bool bHasAnimatedBounds = false;

        void Clear()
        {
            BonePalette.clear();
            AnimatedBounds = Math::AABB{};
            bHasAnimatedBounds = false;
        }
    };

    class SkeletalAnimationSampler final
    {
    public:
        [[nodiscard]] static bool Sample(const SkeletonResource& skeleton,
                                         const AnimationClipResource& clip,
                                         const SkinnedMeshResource& mesh,
                                         float timeSeconds,
                                         const Math::Matrix4x4& meshNodeGlobalRow,
                                         SkeletalPoseSnapshot& outPose);

        [[nodiscard]] static SkinnedVertexSample SkinVertex(
            const Skeletal::SkeletalVertex& vertex,
            const Container::VariableArray<Math::Matrix4x4>& bonePalette);
    };
} // namespace NorvesLib::Core::Animation
