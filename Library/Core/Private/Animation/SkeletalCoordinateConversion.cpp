#include "Animation/SkeletalCoordinateConversion.h"
#include "Animation/SkeletalFloatEnvironment.h"

namespace NorvesLib::Core::Animation
{
    SkeletalCoordinateStatus BuildSkeletalCoordinateConversion(AssetImport::SignedAxis up,
                                                               AssetImport::SignedAxis forward,
                                                               SkeletalSourceHandedness handedness,
                                                               double positionScale,
                                                               SkeletalCoordinateConversion& out) noexcept
    {
        using Status = SkeletalCoordinateStatus;
        if (!Detail::SupportedSkeletalFloatEnvironment())
        {
            return Status::UnsupportedFloatEnvironment;
        }
        const auto u = static_cast<uint8_t>(up), f = static_cast<uint8_t>(forward);
        if (u > static_cast<uint8_t>(AssetImport::SignedAxis::NegativeZ) ||
            f > static_cast<uint8_t>(AssetImport::SignedAxis::NegativeZ) || u / 2 == f / 2)
        {
            return Status::InvalidAxes;
        }
        if (handedness != SkeletalSourceHandedness::Right && handedness != SkeletalSourceHandedness::Left)
        {
            return Status::InvalidHandedness;
        }
        if (!std::isfinite(positionScale) || positionScale <= 0)
        {
            return Status::InvalidScale;
        }
        SkeletalCoordinateConversion candidate;
        candidate.m_SourceAxes[1] = u / 2;
        candidate.m_SourceAxes[2] = f / 2;
        candidate.m_SourceAxes[0] = 3 - candidate.m_SourceAxes[1] - candidate.m_SourceAxes[2];
        candidate.m_Signs[1] = (u & 1) ? -1 : 1;
        candidate.m_Signs[2] = (f & 1) ? -1 : 1;
        // 右手系の第1行はup×forward。左手sourceでは第1行だけを反転する。
        const int crossSign = (candidate.m_SourceAxes[1] + 1) % 3 == candidate.m_SourceAxes[2] ? 1 : -1;
        candidate.m_Signs[0] = static_cast<int8_t>(crossSign * candidate.m_Signs[1] * candidate.m_Signs[2] *
                                                   (handedness == SkeletalSourceHandedness::Left ? -1 : 1));
        candidate.m_PositionScale = positionScale;
        candidate.m_bValid = true;
        out = candidate;
        return Status::Success;
    }
    SkeletalCoordinateStatus ConvertSkeletalTranslation(const SkeletalCoordinateConversion& conversion,
                                                        const Bvh::Vector3d& input, Bvh::Vector3d& out) noexcept
    {
        using Status = SkeletalCoordinateStatus;
        if (!conversion.m_bValid)
        {
            return Status::InvalidConversion;
        }
        if (!Detail::SupportedSkeletalFloatEnvironment())
        {
            return Status::UnsupportedFloatEnvironment;
        }
        const double values[3] = {input.X, input.Y, input.Z};
        for (double value : values)
        {
            if (!std::isfinite(value))
            {
                return Status::NonFiniteInput;
            }
        }
        double candidate[3]{};
        for (size_t i = 0; i < 3; ++i)
        {
            const double original = values[conversion.m_SourceAxes[i]];
            const double scaled = original * conversion.m_PositionScale;
            if (!std::isfinite(scaled) || (original != 0 && scaled == 0))
            {
                return Status::UnrepresentableOutput;
            }
            candidate[i] = conversion.m_Signs[i] < 0 ? -scaled : scaled;
        }
        out = {candidate[0], candidate[1], candidate[2]};
        return Status::Success;
    }
    SkeletalCoordinateStatus ConvertSkeletalMatrixBasis(const SkeletalCoordinateConversion& conversion,
                                                        const Bvh::Matrix3d& input, Bvh::Matrix3d& out) noexcept
    {
        using Status = SkeletalCoordinateStatus;
        if (!conversion.m_bValid)
        {
            return Status::InvalidConversion;
        }
        if (!Detail::SupportedSkeletalFloatEnvironment())
        {
            return Status::UnsupportedFloatEnvironment;
        }
        for (double value : input.Values)
        {
            if (!std::isfinite(value))
            {
                return Status::NonFiniteInput;
            }
        }
        Bvh::Matrix3d candidate;
        for (size_t row = 0; row < 3; ++row)
        {
            for (size_t column = 0; column < 3; ++column)
            {
                const double value = input.Values[conversion.m_SourceAxes[row] * 3 + conversion.m_SourceAxes[column]];
                candidate.Values[row * 3 + column] =
                    conversion.m_Signs[row] == conversion.m_Signs[column] ? value : -value;
            }
        }
        // 固定inline double値だけを代入し、確保/要素例外を経由しない。
        for (size_t i = 0; i < 9; ++i)
        {
            out.Values[i] = candidate.Values[i];
        }
        return Status::Success;
    }
    SkeletalCoordinateStatus ConvertSkeletalTransform(const SkeletalCoordinateConversion& conversion,
                                                      const Bvh::RigidTransformd& input,
                                                      Bvh::RigidTransformd& out) noexcept
    {
        Bvh::RigidTransformd candidate;
        auto status = ConvertSkeletalMatrixBasis(conversion, input.Rotation, candidate.Rotation);
        if (status != SkeletalCoordinateStatus::Success)
        {
            return status;
        }
        status = ConvertSkeletalTranslation(conversion, input.Translation, candidate.Translation);
        if (status != SkeletalCoordinateStatus::Success)
        {
            return status;
        }
        for (size_t i = 0; i < 9; ++i)
        {
            out.Rotation.Values[i] = candidate.Rotation.Values[i];
        }
        out.Translation = candidate.Translation;
        return SkeletalCoordinateStatus::Success;
    }
} // namespace NorvesLib::Core::Animation
