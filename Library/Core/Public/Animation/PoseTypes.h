#pragma once
#include "Container/VariableArray.h"
#include "Math/Matrix4x4.h"
#include "Math/Quaternion.h"
#include "Math/Vector3.h"

namespace NorvesLib::Core::Animation
{
    // RotationはglTFと同じ列規約。入力を読む時に正規化し、行列化では再正規化しない。
    struct JointTransform
    {
        Math::Vector3 Translation = Math::Vector3::Zero;
        Math::Quaternion Rotation = Math::Quaternion::Identity;
        Math::Vector3 Scale = Math::Vector3::One;
    };
    using LocalPose = Container::VariableArray<JointTransform>;

    // 行・列の共役変換をこの境界に閉じ込める。既存の分解・合成の丸めと拒否規則を維持する。
    [[nodiscard]] Math::Matrix4x4 ToRowMatrix(const JointTransform& transform);
    [[nodiscard]] JointTransform FromRowMatrix(const Math::Matrix4x4& matrix);

    // 呼び出し側が保持する作業領域。準備後、同じ関節数の評価で確保し直さない。
    struct PoseScratch
    {
        LocalPose Local;
        Container::VariableArray<Math::Matrix4x4> LocalMatrices;
        Container::VariableArray<Math::Matrix4x4> GlobalMatrices;

        void Resize(size_t jointCount)
        {
            Local.resize(jointCount);
            LocalMatrices.resize(jointCount);
            GlobalMatrices.resize(jointCount);
        }
    };
} // namespace NorvesLib::Core::Animation
