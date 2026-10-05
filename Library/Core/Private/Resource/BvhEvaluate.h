#pragma once
// sourceの単位と軸を保ったdouble FK。エンジンの行ベクトル行列とは独立した値型。
#include "BvhDecode.h"
#include "Container/FixedArray.h"

namespace NorvesLib::Core::Bvh
{
    enum class TranslationConvention : uint8_t
    {
        Unspecified,
        OffsetPlusChannels,
        AbsoluteLocalChannels
    };
    struct Matrix3d
    {
        // row-major格納、数学的には列ベクトルへ左から作用する。
        Container::FixedArray<double, 9> Values{1, 0, 0, 0, 1, 0, 0, 0, 1};
    };
    struct RigidTransformd
    {
        Matrix3d Rotation;
        Vector3d Translation;
    };
    struct JointPose
    {
        RigidTransformd Local, World;
        bool bHasEndSite = false;
        Vector3d EndSiteWorld;
    };
    struct BvhPose
    {
        Container::VariableArray<JointPose> Joints;
        uint32_t FrameIndex = 0;
        TranslationConvention Convention = TranslationConvention::Unspecified;
    };
    enum class BvhEvaluateStatus : uint8_t
    {
        Success,
        InvalidConvention,
        InvalidDocument,
        FrameOutOfRange,
        UnsupportedChannels,
        NonFiniteResult
    };
    struct BvhEvaluateResult
    {
        BvhEvaluateStatus Status = BvhEvaluateStatus::InvalidDocument;
        size_t JointIndex = SIZE_MAX;
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == BvhEvaluateStatus::Success;
        }
    };
    // conventionは必須。完全3位置成分/完全3回転成分/静止だけを受理し、位置→回転の順を要求する。
    // 構造とOFFSET、使用frameの有限値を検証する。名前や他frameの値はこの評価では解釈しない。
    // 成功時だけoutを置換し、失敗/確保例外では既存outを保持する。
    [[nodiscard]] BvhEvaluateResult EvaluateBvhFrame(const BvhDocument& document, uint32_t frameIndex,
                                                     TranslationConvention convention, BvhPose& out);
} // namespace NorvesLib::Core::Bvh
