#include "Resource/BvhEvaluate.h"
#include "Resource/BvhRotationSource.h"
#include "Animation/SkeletalFloatEnvironment.h"
#include "Animation/SkeletalJointIndex.h"
#include "Text/UnicodeText.h"
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

    void BvhRotationSourcePlan::Swap(BvhRotationSourcePlan& other) noexcept
    {
        static_assert(noexcept(m_Joints.swap(other.m_Joints)));
        m_Joints.swap(other.m_Joints);
        std::swap(m_Values, other.m_Values);
        std::swap(m_FrameCount, other.m_FrameCount);
        std::swap(m_FrameWidth, other.m_FrameWidth);
        std::swap(m_bValid, other.m_bValid);
    }
    BvhRotationSourcePlan::BvhRotationSourcePlan(BvhRotationSourcePlan&& other) noexcept
    {
        Swap(other);
    }
    BvhRotationSourcePlan& BvhRotationSourcePlan::operator=(BvhRotationSourcePlan&& other) noexcept
    {
        if (this != &other)
        {
            BvhRotationSourcePlan candidate(std::move(other));
            Swap(candidate);
        }
        return *this;
    }
    BvhRotationSourceResult PrepareBvhRotationSource(const BvhDocument& document, const BvhRotationSourceLimits& limits,
                                                     BvhRotationSourcePlan& out)
    {
        using RStatus = BvhRotationSourceStatus;
        if (!Animation::Detail::SupportedSkeletalFloatEnvironment())
        {
            return {RStatus::UnsupportedFloatEnvironment};
        }
        const size_t count = document.Joints.size(), width = document.Channels.size();
        if (count == 0 || count > UINT32_MAX || width == 0 || width > UINT32_MAX || document.FrameCount == 0 ||
            !std::isfinite(document.FrameTimeSeconds) || document.FrameTimeSeconds <= 0 ||
            !std::isfinite((document.FrameCount - 1) * document.FrameTimeSeconds) ||
            document.FrameCount > SIZE_MAX / width ||
            document.Values.size() != static_cast<size_t>(document.FrameCount) * width)
        {
            return {RStatus::InvalidDocument};
        }
        if (count > limits.MaxJoints || document.FrameCount > limits.MaxFrames ||
            document.Values.size() > limits.MaxValues || limits.MaxDepth == 0)
        {
            return {RStatus::LimitExceeded};
        }
        BvhRotationSourcePlan candidate;
        Container::VariableArray<Animation::SkeletalJointNameView> names;
        if (count > candidate.m_Joints.max_size() || count > names.max_size())
        {
            return {RStatus::LimitExceeded};
        }
        size_t totalNameBytes = 0;
        for (size_t i = 0; i < count; ++i)
        {
            const auto& name = document.Joints[i].Name;
            if (name.empty())
            {
                return {RStatus::InvalidName, i};
            }
            if (name.size() > limits.MaxNameBytes || totalNameBytes > limits.MaxTotalNameBytes ||
                name.size() > limits.MaxTotalNameBytes - totalNameBytes)
            {
                return {RStatus::LimitExceeded, i};
            }
            totalNameBytes += name.size();
            bool bToken = true;
            const bool bUtf8 = TextDetail::ForEachUnicodeScalar<uint8_t>(
                Container::Span<const uint8_t>(name),
                [&](uint32_t scalar)
                {
                    if (scalar <= 32 || (scalar >= 0x7f && scalar <= 0x9f) || scalar == 0xfeff || scalar == '{' ||
                        scalar == '}' || scalar == '"')
                    {
                        bToken = false;
                    }
                });
            if (!bUtf8 || !bToken)
            {
                return {RStatus::InvalidName, i};
            }
        }
        candidate.m_Joints.resize(count);
        names.reserve(count);
        size_t cursor = 0;
        for (size_t i = 0; i < count; ++i)
        {
            const auto& joint = document.Joints[i];
            auto& item = candidate.m_Joints[i];
            if ((i == 0 ? joint.Parent != UINT32_MAX : joint.Parent >= i) || !Finite(joint.Offset) ||
                (joint.bHasEndSite && !Finite(joint.EndSiteOffset)) || joint.ChannelOffset != cursor ||
                joint.ChannelCount > 6 || cursor > width || joint.ChannelCount > width - cursor)
            {
                return {RStatus::InvalidDocument, i};
            }
            item.Parent = joint.Parent;
            item.Depth = i == 0 ? 1 : candidate.m_Joints[joint.Parent].Depth + 1;
            if (item.Depth > limits.MaxDepth)
            {
                return {RStatus::LimitExceeded, i};
            }
            unsigned mask = 0;
            for (size_t channel = cursor; channel < cursor + joint.ChannelCount; ++channel)
            {
                const unsigned kind = static_cast<unsigned>(document.Channels[channel]);
                if (kind > static_cast<unsigned>(Channel::Zrotation) || (mask & (1u << kind)))
                {
                    return {RStatus::InvalidDocument, i};
                }
                mask |= 1u << kind;
                if (kind >= static_cast<unsigned>(Channel::Xrotation))
                {
                    item.ValueIndices[item.RotationCount] = static_cast<uint32_t>(channel);
                    item.Axes[item.RotationCount] =
                        static_cast<uint8_t>(kind - static_cast<unsigned>(Channel::Xrotation));
                    ++item.RotationCount;
                }
            }
            if ((mask & 56u) != 0 && (mask & 56u) != 56u)
            {
                return {RStatus::UnsupportedRotationChannels, i};
            }
            cursor += joint.ChannelCount;
            names.push_back({Container::Span<const uint8_t>(joint.Name)});
        }
        if (cursor != width)
        {
            return {RStatus::InvalidDocument};
        }
        // 位置として使わない値も、元documentの有限性契約から除外しない。
        for (size_t i = 0; i < document.Values.size(); ++i)
        {
            if (!std::isfinite(document.Values[i]))
            {
                return {RStatus::InvalidDocument, SIZE_MAX, static_cast<uint32_t>(i / width), i};
            }
        }
        Animation::SkeletalJointIndex namesIndex;
        const Animation::SkeletalJointIndexLimits nameLimits{limits.MaxJoints, limits.MaxNameBytes,
                                                             limits.MaxTotalNameBytes};
        const auto named = Animation::BuildSkeletalJointIndex(
            Container::Span<const Animation::SkeletalJointNameView>(names), nameLimits, namesIndex);
        if (!named.Succeeded())
        {
            return {named.Status == Animation::SkeletalJointIndexStatus::DuplicateName   ? RStatus::DuplicateName
                    : named.Status == Animation::SkeletalJointIndexStatus::LimitExceeded ? RStatus::LimitExceeded
                                                                                         : RStatus::InvalidName,
                    named.NameIndex};
        }
        candidate.m_Values = Container::Span<const double>(document.Values);
        candidate.m_FrameCount = document.FrameCount;
        candidate.m_FrameWidth = static_cast<uint32_t>(width);
        candidate.m_bValid = true;
        out.Swap(candidate);
        return {RStatus::Success};
    }
    BvhRotationSourceResult EvaluateBvhRotationFrame(const BvhRotationSourcePlan& plan, uint32_t frameIndex,
                                                     Container::Span<Matrix3d> outWorld) noexcept
    {
        using RStatus = BvhRotationSourceStatus;
        if (!Animation::Detail::SupportedSkeletalFloatEnvironment())
        {
            return {RStatus::UnsupportedFloatEnvironment};
        }
        if (!plan.m_bValid)
        {
            return {RStatus::InvalidDocument};
        }
        if (frameIndex >= plan.m_FrameCount)
        {
            return {RStatus::FrameOutOfRange, SIZE_MAX, frameIndex};
        }
        if (outWorld.size() != plan.m_Joints.size() ||
            !Asset::SkeletalNameDetail::ValidStorage(outWorld.data(), outWorld.size(), sizeof(Matrix3d),
                                                     alignof(Matrix3d)) ||
            Asset::SkeletalNameDetail::Overlaps(outWorld.data(), outWorld.size() * sizeof(Matrix3d),
                                                plan.m_Values.data(), plan.m_Values.size() * sizeof(double)))
        {
            return {RStatus::InvalidOutput, SIZE_MAX, frameIndex};
        }
        const size_t offset = static_cast<size_t>(frameIndex) * plan.m_FrameWidth;
        for (size_t i = 0; i < plan.m_Joints.size(); ++i)
        {
            const auto& joint = plan.m_Joints[i];
            Matrix3d local;
            for (size_t r = 0; r < joint.RotationCount; ++r)
            {
                const size_t index = offset + joint.ValueIndices[r];
                const double degrees = plan.m_Values[index];
                if (!std::isfinite(degrees))
                {
                    return {RStatus::InvalidDocument, i, frameIndex, index};
                }
                local = Multiply(local, AxisRotation(joint.Axes[r], degrees));
            }
            for (double value : local.Values)
            {
                if (!std::isfinite(value))
                {
                    return {RStatus::NonFiniteRotation, i, frameIndex};
                }
            }
            outWorld[i] = joint.Parent == UINT32_MAX ? local : Multiply(outWorld[joint.Parent], local);
            for (double value : outWorld[i].Values)
            {
                if (!std::isfinite(value))
                {
                    return {RStatus::NonFiniteRotation, i, frameIndex};
                }
            }
        }
        return {RStatus::Success};
    }
} // namespace NorvesLib::Core::Bvh
