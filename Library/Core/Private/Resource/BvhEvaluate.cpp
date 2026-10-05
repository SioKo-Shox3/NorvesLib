#include "Resource/BvhEvaluate.h"
#include <cmath>
#include <numbers>
#include <type_traits>
#include <utility>

namespace NorvesLib::Core::Bvh
{
    namespace
    {
        using Status = BvhEvaluateStatus;
        bool Finite(const Vector3d& value) noexcept
        {
            return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
        }
        bool Finite(const RigidTransformd& value) noexcept
        {
            if (!Finite(value.Translation))
            {
                return false;
            }
            for (size_t i = 0; i < 9; ++i)
            {
                if (!std::isfinite(value.Rotation.Values[i]))
                {
                    return false;
                }
            }
            return true;
        }
        Vector3d Add(const Vector3d& a, const Vector3d& b) noexcept
        {
            return {a.X + b.X, a.Y + b.Y, a.Z + b.Z};
        }
        Vector3d Rotate(const Matrix3d& matrix, const Vector3d& point) noexcept
        {
            const auto& m = matrix.Values;
            return {m[0] * point.X + m[1] * point.Y + m[2] * point.Z, m[3] * point.X + m[4] * point.Y + m[5] * point.Z,
                    m[6] * point.X + m[7] * point.Y + m[8] * point.Z};
        }
        Matrix3d Multiply(const Matrix3d& a, const Matrix3d& b)
        {
            Matrix3d result;
            for (size_t row = 0; row < 3; ++row)
            {
                for (size_t column = 0; column < 3; ++column)
                {
                    double value = 0;
                    for (size_t k = 0; k < 3; ++k)
                    {
                        value += a.Values[row * 3 + k] * b.Values[k * 3 + column];
                    }
                    result.Values[row * 3 + column] = value;
                }
            }
            return result;
        }
        Matrix3d AxisRotation(unsigned axis, double degrees)
        {
            // 有限の大きなdegreeでもrad変換でoverflowさせず、周期を先に取り除く。
            const double radians = std::remainder(degrees, 360.0) * (std::numbers::pi_v<double> / 180.0);
            const double cosine = std::cos(radians), sine = std::sin(radians);
            Matrix3d result;
            auto& m = result.Values;
            if (axis == 0)
            {
                m[4] = cosine;
                m[5] = -sine;
                m[7] = sine;
                m[8] = cosine;
            }
            else if (axis == 1)
            {
                m[0] = cosine;
                m[2] = sine;
                m[6] = -sine;
                m[8] = cosine;
            }
            else
            {
                m[0] = cosine;
                m[1] = -sine;
                m[3] = sine;
                m[4] = cosine;
            }
            return result;
        }
        BvhEvaluateResult Validate(const BvhDocument& document, uint32_t frameIndex)
        {
            const size_t width = document.Channels.size();
            if (document.Joints.empty() || document.Joints.size() > UINT32_MAX || width == 0 || width > UINT32_MAX ||
                document.FrameCount == 0 || !std::isfinite(document.FrameTimeSeconds) ||
                !(document.FrameTimeSeconds > 0) ||
                !std::isfinite((document.FrameCount - 1) * document.FrameTimeSeconds) ||
                document.FrameCount > SIZE_MAX / width ||
                document.Values.size() != static_cast<size_t>(document.FrameCount) * width)
            {
                return {Status::InvalidDocument};
            }
            if (frameIndex >= document.FrameCount)
            {
                return {Status::FrameOutOfRange};
            }
            size_t cursor = 0;
            for (size_t i = 0; i < document.Joints.size(); ++i)
            {
                const auto& joint = document.Joints[i];
                if ((i == 0 ? joint.Parent != UINT32_MAX : joint.Parent >= i) || !Finite(joint.Offset) ||
                    (joint.bHasEndSite && !Finite(joint.EndSiteOffset)) || joint.ChannelOffset != cursor ||
                    joint.ChannelCount > 6 || cursor > width || joint.ChannelCount > width - cursor)
                {
                    return {Status::InvalidDocument, i};
                }
                unsigned mask = 0;
                bool bRotationStarted = false;
                for (size_t channel = cursor; channel < cursor + joint.ChannelCount; ++channel)
                {
                    const unsigned kind = static_cast<unsigned>(document.Channels[channel]);
                    if (kind > static_cast<unsigned>(Channel::Zrotation) || (mask & (1u << kind)))
                    {
                        return {Status::InvalidDocument, i};
                    }
                    mask |= 1u << kind;
                    if (kind >= static_cast<unsigned>(Channel::Xrotation))
                    {
                        bRotationStarted = true;
                    }
                    else if (bRotationStarted)
                    {
                        return {Status::UnsupportedChannels, i};
                    }
                }
                const auto positionMask = mask & 7u, rotationMask = mask & 56u;
                if ((positionMask != 0 && positionMask != 7) || (rotationMask != 0 && rotationMask != 56))
                {
                    return {Status::UnsupportedChannels, i};
                }
                cursor += joint.ChannelCount;
            }
            if (cursor != width)
            {
                return {Status::InvalidDocument};
            }
            return {Status::Success};
        }
    } // namespace
    BvhEvaluateResult EvaluateBvhFrame(const BvhDocument& document, uint32_t frameIndex,
                                       TranslationConvention convention, BvhPose& out)
    {
        if (convention != TranslationConvention::OffsetPlusChannels &&
            convention != TranslationConvention::AbsoluteLocalChannels)
        {
            return {Status::InvalidConvention};
        }
        const auto validated = Validate(document, frameIndex);
        if (!validated.Succeeded())
        {
            return validated;
        }
        BvhPose result;
        result.FrameIndex = frameIndex;
        result.Convention = convention;
        result.Joints.reserve(document.Joints.size());
        const size_t frameOffset = static_cast<size_t>(frameIndex) * document.Channels.size();
        for (size_t i = 0; i < document.Joints.size(); ++i)
        {
            const auto& joint = document.Joints[i];
            JointPose pose;
            Vector3d position;
            bool bPositionPresent = false;
            for (size_t channel = joint.ChannelOffset;
                 channel < static_cast<size_t>(joint.ChannelOffset) + joint.ChannelCount; ++channel)
            {
                const auto kind = document.Channels[channel];
                const double value = document.Values[frameOffset + channel];
                if (!std::isfinite(value))
                {
                    return {Status::InvalidDocument, i};
                }
                switch (kind)
                {
                case Channel::Xposition:
                    position.X = value;
                    bPositionPresent = true;
                    break;
                case Channel::Yposition:
                    position.Y = value;
                    bPositionPresent = true;
                    break;
                case Channel::Zposition:
                    position.Z = value;
                    bPositionPresent = true;
                    break;
                default:
                    pose.Local.Rotation = Multiply(
                        pose.Local.Rotation,
                        AxisRotation(static_cast<unsigned>(kind) - static_cast<unsigned>(Channel::Xrotation), value));
                    break;
                }
            }
            pose.Local.Translation = !bPositionPresent ? joint.Offset
                                     : convention == TranslationConvention::OffsetPlusChannels
                                         ? Add(joint.Offset, position)
                                         : position;
            if (!Finite(pose.Local))
            {
                return {Status::NonFiniteResult, i};
            }
            if (i == 0)
            {
                pose.World = pose.Local;
            }
            else
            {
                const auto& parent = result.Joints[joint.Parent].World;
                pose.World.Rotation = Multiply(parent.Rotation, pose.Local.Rotation);
                pose.World.Translation = Add(parent.Translation, Rotate(parent.Rotation, pose.Local.Translation));
            }
            if (!Finite(pose.World))
            {
                return {Status::NonFiniteResult, i};
            }
            pose.bHasEndSite = joint.bHasEndSite;
            if (joint.bHasEndSite)
            {
                pose.EndSiteWorld = Add(pose.World.Translation, Rotate(pose.World.Rotation, joint.EndSiteOffset));
                if (!Finite(pose.EndSiteWorld))
                {
                    return {Status::NonFiniteResult, i};
                }
            }
            result.Joints.push_back(std::move(pose));
        }
        static_assert(std::is_nothrow_move_assignable_v<BvhPose>);
        out = std::move(result);
        return {Status::Success};
    }
} // namespace NorvesLib::Core::Bvh
