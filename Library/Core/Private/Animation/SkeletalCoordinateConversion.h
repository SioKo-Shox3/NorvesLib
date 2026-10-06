#pragma once
// 明示したsource基底をcanonical +Y上/+Z前/right-handedへ移す。modelの正面は推定しない。
#include "Resource/ImportSettings.h"
#include "Resource/BvhEvaluate.h"

namespace NorvesLib::Core::Animation
{
    enum class SkeletalSourceHandedness : uint8_t
    {
        Unspecified,
        Right,
        Left
    };
    enum class SkeletalCoordinateStatus : uint8_t
    {
        Success,
        InvalidAxes,
        InvalidHandedness,
        InvalidScale,
        InvalidConversion,
        NonFiniteInput,
        UnrepresentableOutput,
        UnsupportedFloatEnvironment
    };
    class SkeletalCoordinateConversion final
    {
      public:
        // 暗黙のidentity設定にしない。必須の宣言でBuildした値だけを使用する。
        SkeletalCoordinateConversion() = default;
        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_bValid;
        }

      private:
        Container::FixedArray<uint8_t, 3> m_SourceAxes{0, 1, 2};
        Container::FixedArray<int8_t, 3> m_Signs{1, 1, 1};
        double m_PositionScale = 1;
        bool m_bValid = false;
        friend SkeletalCoordinateStatus BuildSkeletalCoordinateConversion(AssetImport::SignedAxis,
                                                                          AssetImport::SignedAxis,
                                                                          SkeletalSourceHandedness, double,
                                                                          SkeletalCoordinateConversion&) noexcept;
        friend SkeletalCoordinateStatus ConvertSkeletalTranslation(const SkeletalCoordinateConversion&,
                                                                   const Bvh::Vector3d&, Bvh::Vector3d&) noexcept;
        friend SkeletalCoordinateStatus ConvertSkeletalMatrixBasis(const SkeletalCoordinateConversion&,
                                                                   const Bvh::Matrix3d&, Bvh::Matrix3d&) noexcept;
    };
    // 全関数は成功時だけoutを変更し、入力/outのaliasを許可する。推測/Autoは行わない。
    // IEEE binary64、nearest丸め、gradual underflowを要求し、非対応FP環境は変更せず拒否する。
    [[nodiscard]] SkeletalCoordinateStatus BuildSkeletalCoordinateConversion(
        AssetImport::SignedAxis up, AssetImport::SignedAxis forward, SkeletalSourceHandedness handedness,
        double positionScale, SkeletalCoordinateConversion& out) noexcept;
    [[nodiscard]] SkeletalCoordinateStatus ConvertSkeletalTranslation(const SkeletalCoordinateConversion& conversion,
                                                                      const Bvh::Vector3d& input,
                                                                      Bvh::Vector3d& out) noexcept;
    // 全finite 3x3を代数的に共役変換する。SO3検査/正規化は後続consumerの責務。
    [[nodiscard]] SkeletalCoordinateStatus ConvertSkeletalMatrixBasis(const SkeletalCoordinateConversion& conversion,
                                                                      const Bvh::Matrix3d& input,
                                                                      Bvh::Matrix3d& out) noexcept;
    [[nodiscard]] SkeletalCoordinateStatus ConvertSkeletalTransform(const SkeletalCoordinateConversion& conversion,
                                                                    const Bvh::RigidTransformd& input,
                                                                    Bvh::RigidTransformd& out) noexcept;
} // namespace NorvesLib::Core::Animation
