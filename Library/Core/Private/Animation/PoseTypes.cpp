#include "Animation/PoseTypes.h"
#include "Animation/SkeletalBindRowMath.h"

namespace NorvesLib::Core::Animation
{
    Math::Matrix4x4 ToRowMatrix(const JointTransform& transform)
    {
        Detail::JointTransform row;
        row.Translation = transform.Translation;
        row.Rotation = {-transform.Rotation.x, -transform.Rotation.y, -transform.Rotation.z, transform.Rotation.w};
        row.Scale = transform.Scale;
        return Detail::ComposeSkeletalLocalRowTransform(row);
    }

    JointTransform FromRowMatrix(const Math::Matrix4x4& matrix)
    {
        const auto row = Detail::DecomposeRowTransform(matrix);
        JointTransform result;
        result.Translation = row.Translation;
        result.Rotation = {-row.Rotation.x, -row.Rotation.y, -row.Rotation.z, row.Rotation.w};
        result.Scale = row.Scale;
        return result;
    }
} // namespace NorvesLib::Core::Animation
