#pragma once
#include "Animation/PoseTypes.h"
#include <cstdint>

namespace NorvesLib::Core::Animation
{
    // Resourceが設定時に作る不変の評価情報。per-mesh IBMをsplit skeletonへ混ぜない。
    struct SkeletonPoseRuntime
    {
        Container::VariableArray<int32_t> Parents;
        Container::VariableArray<uint32_t> EvaluationOrder;
        Container::VariableArray<Math::Matrix4x4> LegacyInverseBind;
        Container::VariableArray<Math::Matrix4x4> LegacyBindModel;
        LocalPose AuthorRestLocal;
        Math::Matrix4x4 RootFrame = Math::Matrix4x4::Identity;
        bool bValid = false;
        bool bSplit = false;

        size_t AllocatedBytes() const noexcept
        {
            return Parents.capacity() * sizeof(int32_t) + EvaluationOrder.capacity() * sizeof(uint32_t) +
                   (LegacyInverseBind.capacity() + LegacyBindModel.capacity()) * sizeof(Math::Matrix4x4) +
                   AuthorRestLocal.capacity() * sizeof(JointTransform);
        }
    };

    struct JointChannelSelection
    {
        uint32_t JointIndex = 0;
        size_t Channels[3] = {SIZE_MAX, SIZE_MAX, SIZE_MAX};
    };
    struct ClipPoseRuntime
    {
        // 関節添字順の疎な表。入力の最大添字を配列長にしない。
        Container::VariableArray<JointChannelSelection> Joints;
        uint64_t RequiredJointCount = 0;
        bool bLegacyValid = true;
        bool bSplitValid = false;

        size_t AllocatedBytes() const noexcept
        {
            return Joints.capacity() * sizeof(JointChannelSelection);
        }
    };
} // namespace NorvesLib::Core::Animation
