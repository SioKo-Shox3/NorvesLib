#pragma once

#include "SkeletalSamplingMath.h"
#include "Math/MatrixUtils.h"
#include "Math/QuaternionUtils.h"
#include <cmath>

namespace NorvesLib::Core::Animation::Detail
{
    // 現行Samplerの行ベクトル算術を共有する。Resourceや作者時rest snapshotは扱わない。
    struct JointTransform
    {
        Math::Vector3 Translation = Math::Vector3::Zero;
        Math::Quaternion Rotation = Math::Quaternion::Identity;
        Math::Vector3 Scale = Math::Vector3::One;
    };

    inline bool IsFiniteMatrix(const Math::Matrix4x4& matrix)
    {
        for (const float value : matrix.values)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
        return true;
    }

    inline bool TryInverseMatrix(const Math::Matrix4x4& matrix, Math::Matrix4x4& outInverse)
    {
        if (!IsFiniteMatrix(matrix))
        {
            return false;
        }
        const float determinant = Math::MatrixUtils::Determinant(matrix);
        if (!std::isfinite(determinant) || std::abs(determinant) < Math::Constants::EPSILON)
        {
            return false;
        }
        const Math::Matrix4x4 candidate = Math::MatrixUtils::Inverse(matrix);
        if (!IsFiniteMatrix(candidate))
        {
            return false;
        }
        outInverse = candidate;
        return true;
    }

    // 旧分解を保存する。shear/反射の厳密復元やTRSの有限性を保証しない。
    // 後続clipの絶対上書きで回復する入力があるため、ここで新しい拒否を加えない。
    inline JointTransform DecomposeRowTransform(const Math::Matrix4x4& matrix)
    {
        JointTransform result;
        result.Translation = matrix.GetTranslationRow();
        result.Scale = Math::MatrixUtils::ExtractScale(matrix);
        const Math::Matrix4x4 rotation = Math::MatrixUtils::ExtractRotationRowVector(matrix, result.Scale);
        result.Rotation = NormalizeQuaternion(Math::QuaternionUtils::FromRotationMatrix(rotation));
        return result;
    }

    inline Math::Matrix4x4 ComposeSkeletalLocalRowTransform(const JointTransform& transform)
    {
        Math::Matrix4x4 result =
            Math::MatrixUtils::CreateWorldRowVector(transform.Translation, transform.Rotation, Math::Vector3::One);
        result.m00 *= transform.Scale.x;
        result.m01 *= transform.Scale.x;
        result.m02 *= transform.Scale.x;
        result.m10 *= transform.Scale.y;
        result.m11 *= transform.Scale.y;
        result.m12 *= transform.Scale.y;
        result.m20 *= transform.Scale.z;
        result.m21 *= transform.Scale.z;
        result.m22 *= transform.Scale.z;
        return result;
    }

    // inverse(IBM) * meshGlobal。失敗では出力を保持し、入力とのaliasも許可する。
    inline bool TryBuildBindGlobalRow(const Math::Matrix4x4& inverseBind, const Math::Matrix4x4& meshGlobal,
                                      Math::Matrix4x4& outGlobal)
    {
        Math::Matrix4x4 inverse;
        if (!TryInverseMatrix(inverseBind, inverse))
        {
            return false;
        }
        const Math::Matrix4x4 candidate = inverse * meshGlobal;
        if (!IsFiniteMatrix(candidate))
        {
            return false;
        }
        outGlobal = candidate;
        return true;
    }

    // rootはparent=nullptr。childGlobal * inverse(parentGlobal)をそのまま使う。
    inline bool TryBuildBindLocalRow(const Math::Matrix4x4& childGlobal, const Math::Matrix4x4* parentGlobal,
                                     Math::Matrix4x4& outLocal)
    {
        Math::Matrix4x4 candidate = childGlobal;
        if (parentGlobal != nullptr)
        {
            Math::Matrix4x4 inverseParent;
            if (!TryInverseMatrix(*parentGlobal, inverseParent))
            {
                return false;
            }
            candidate = childGlobal * inverseParent;
        }
        if (!IsFiniteMatrix(candidate))
        {
            return false;
        }
        outLocal = candidate;
        return true;
    }
} // namespace NorvesLib::Core::Animation::Detail
