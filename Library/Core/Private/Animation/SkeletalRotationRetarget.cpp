#include "Animation/SkeletalRotationRetarget.h"
#include "Animation/SkeletalRotationRetargetMath.h"
#include "Animation/SkeletalFloatEnvironment.h"
#include <limits>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        using Status = SkeletalRetargetStatus;
        using Side = SkeletalJointMappingSide;
        SkeletalRetargetResult Failure(Status status, Side side = Side::None, uint32_t joint = UINT32_MAX,
                                       size_t entry = SIZE_MAX) noexcept
        {
            return {status, side, joint, entry, 0};
        }
        template <class T> bool Valid(Container::Span<T> values) noexcept
        {
            return Detail::RetargetValidSpan(values);
        }
        template <class T, class U> bool Overlap(Container::Span<T> a, Container::Span<U> b) noexcept
        {
            const auto x = reinterpret_cast<uintptr_t>(a.data()), y = reinterpret_cast<uintptr_t>(b.data());
            return a.size() != 0 && b.size() != 0 && x < y + b.size() * sizeof(U) && y < x + a.size() * sizeof(T);
        }
        template <class T> uint32_t InvalidHierarchy(Container::Span<T> joints) noexcept
        {
            for (size_t i = 0; i < joints.size(); ++i)
            {
                if (joints[i].ParentIndex < -1 ||
                    (joints[i].ParentIndex >= 0 && static_cast<size_t>(joints[i].ParentIndex) >= joints.size()))
                {
                    return static_cast<uint32_t>(i);
                }
            }
            // 最大1024関節の有界走査。callerの深い木でnative stackを使い切らない。
            for (size_t i = 0; i < joints.size(); ++i)
            {
                int32_t parent = static_cast<int32_t>(i);
                size_t depth = 0;
                while (parent >= 0)
                {
                    if (++depth > joints.size())
                    {
                        return static_cast<uint32_t>(i);
                    }
                    parent = joints[static_cast<size_t>(parent)].ParentIndex;
                }
            }
            return UINT32_MAX;
        }
    } // namespace
    SkeletalRetargetResult EvaluateSkeletalRotationFrame(const SkeletalRetargetRotationRequest& r,
                                                         Container::Span<SkeletalRetargetRotationWork> work,
                                                         Container::Span<SkeletalRetargetRotationValue> out) noexcept
    {
        if (!Detail::SupportedSkeletalFloatEnvironment() || !std::numeric_limits<float>::is_iec559 ||
            std::numeric_limits<float>::digits != 24)
        {
            return Failure(Status::UnsupportedFloatEnvironment);
        }
        if (r.Policy != SkeletalRetargetRotationPolicy::PreserveHeadingHoldTranslations ||
            (r.SourceReuse != SkeletalSourceReusePolicy::Allow && r.SourceReuse != SkeletalSourceReusePolicy::Reject))
        {
            return Failure(Status::InvalidPolicy);
        }
        if (r.Source.empty() || r.Target.empty() || r.Mappings.empty() || r.Corrections.size() != r.Mappings.size() ||
            work.size() != r.Target.size() || out.size() != r.Mappings.size())
        {
            return Failure(Status::InvalidInput);
        }
        if (r.Source.size() > SkeletalRetargetMaxJoints || r.Target.size() > SkeletalRetargetMaxJoints ||
            r.Mappings.size() > SkeletalRetargetMaxJoints)
        {
            return Failure(Status::LimitExceeded);
        }
        if (!Valid(r.Source) || !Valid(r.Target) || !Valid(r.Mappings) || !Valid(r.Corrections) || !Valid(work) ||
            !Valid(out))
        {
            return Failure(Status::InvalidInput);
        }
        if (Overlap(work, out) || Overlap(work, r.Source) || Overlap(work, r.Target) || Overlap(work, r.Mappings) ||
            Overlap(work, r.Corrections) || Overlap(out, r.Source) || Overlap(out, r.Target) ||
            Overlap(out, r.Mappings) || Overlap(out, r.Corrections))
        {
            return Failure(Status::InvalidInput);
        }
        const uint32_t invalidSource = InvalidHierarchy(r.Source), invalidTarget = InvalidHierarchy(r.Target);
        if (invalidSource != UINT32_MAX)
        {
            return Failure(Status::InvalidHierarchy, Side::Source, invalidSource);
        }
        if (invalidTarget != UINT32_MAX)
        {
            return Failure(Status::InvalidHierarchy, Side::Target, invalidTarget);
        }
        if (r.Root.SourceIndex >= r.Source.size() || r.Root.TargetIndex >= r.Target.size())
        {
            return Failure(Status::InvalidRoot);
        }
        if (r.Source[r.Root.SourceIndex].ParentIndex != -1)
        {
            return Failure(Status::InvalidRoot, Side::Source, r.Root.SourceIndex);
        }
        if (r.Target[r.Root.TargetIndex].ParentIndex != -1)
        {
            return Failure(Status::InvalidRoot, Side::Target, r.Root.TargetIndex);
        }
        Bvh::Matrix3d temporary;
        for (size_t i = 0; i < r.Source.size(); ++i)
        {
            if (!Detail::RetargetProjectRotation(r.Source[i].WorldRotation, temporary))
            {
                return Failure(Status::InvalidRotation, Side::Source, static_cast<uint32_t>(i));
            }
        }
        for (size_t i = 0; i < r.Target.size(); ++i)
        {
            work[i] = SkeletalRetargetRotationWork{};
            if (!Detail::RetargetProjectRotation(r.Target[i].BindLocalRotation, work[i].LocalRotation) ||
                !Detail::RetargetProjectRotation(r.Target[i].BindWorldRotation, temporary))
            {
                return Failure(Status::InvalidRotation, Side::Target, static_cast<uint32_t>(i));
            }
        }
        bool bFoundRoot = false;
        for (size_t i = 0; i < r.Mappings.size(); ++i)
        {
            const auto& pair = r.Mappings[i];
            if (pair.SourceIndex >= r.Source.size() || pair.TargetIndex >= r.Target.size())
            {
                return Failure(Status::InvalidMapping, Side::None, UINT32_MAX, i);
            }
            if (work[pair.TargetIndex].MappingIndex >= 0)
            {
                return Failure(Status::DuplicateTarget, Side::Target, pair.TargetIndex, i);
            }
            if (r.SourceReuse == SkeletalSourceReusePolicy::Reject)
            {
                for (size_t previous = 0; previous < i; ++previous)
                {
                    if (r.Mappings[previous].SourceIndex == pair.SourceIndex)
                    {
                        return Failure(Status::DuplicateSource, Side::Source, pair.SourceIndex, i);
                    }
                }
            }
            if (pair.TargetIndex == r.Root.TargetIndex)
            {
                if (pair.SourceIndex != r.Root.SourceIndex)
                {
                    return Failure(Status::InvalidRoot, Side::Target, pair.TargetIndex, i);
                }
                bFoundRoot = true;
            }
            if (!Detail::RetargetProjectRotation(r.Corrections[i], temporary))
            {
                return Failure(Status::InvalidRotation, Side::None, UINT32_MAX, i);
            }
            work[pair.TargetIndex].MappingIndex = static_cast<int32_t>(i);
        }
        if (!bFoundRoot)
        {
            return Failure(Status::InvalidRoot);
        }
        size_t remaining = r.Target.size();
        while (remaining != 0)
        {
            bool bProgress = false;
            for (size_t i = 0; i < r.Target.size(); ++i)
            {
                auto& item = work[i];
                const int32_t parent = r.Target[i].ParentIndex;
                if (item.bDone || (parent >= 0 && !work[static_cast<size_t>(parent)].bDone))
                {
                    continue;
                }
                if (item.MappingIndex >= 0)
                {
                    const size_t entry = static_cast<size_t>(item.MappingIndex);
                    Bvh::Matrix3d c, d, b;
                    if (!Detail::RetargetProjectRotation(r.Corrections[entry], c) ||
                        !Detail::RetargetProjectRotation(r.Source[r.Mappings[entry].SourceIndex].WorldRotation, d) ||
                        !Detail::RetargetProjectRotation(r.Target[i].BindWorldRotation, b))
                    {
                        return Failure(Status::InvalidRotation, Side::Target, static_cast<uint32_t>(i), entry);
                    }
                    item.WorldRotation = Detail::RetargetMultiply(
                        Detail::RetargetMultiply(Detail::RetargetMultiply(c, d), Detail::RetargetTranspose(c)), b);
                    item.LocalRotation =
                        parent < 0 ? item.WorldRotation
                                   : Detail::RetargetMultiply(
                                         Detail::RetargetTranspose(work[static_cast<size_t>(parent)].WorldRotation),
                                         item.WorldRotation);
                    Detail::RetargetQuaterniond q;
                    if (!Detail::RetargetExtractQuaternion(item.LocalRotation, q))
                    {
                        return Failure(Status::InvalidRotation, Side::Target, static_cast<uint32_t>(i), entry);
                    }
                    item.Value = {static_cast<uint32_t>(i), static_cast<float>(q.X), static_cast<float>(q.Y),
                                  static_cast<float>(q.Z), static_cast<float>(q.W)};
                    if (!std::isfinite(item.Value.X) || !std::isfinite(item.Value.Y) || !std::isfinite(item.Value.Z) ||
                        !std::isfinite(item.Value.W))
                    {
                        return Failure(Status::InvalidRotation, Side::Target, static_cast<uint32_t>(i), entry);
                    }
                }
                else
                {
                    item.WorldRotation = parent < 0
                                             ? item.LocalRotation
                                             : Detail::RetargetMultiply(work[static_cast<size_t>(parent)].WorldRotation,
                                                                        item.LocalRotation);
                }
                item.bDone = true;
                --remaining;
                bProgress = true;
            }
            if (!bProgress)
            {
                return Failure(Status::InvalidHierarchy, Side::Target);
            }
        }
        for (size_t i = 0; i < r.Mappings.size(); ++i)
        {
            out[i] = work[r.Mappings[i].TargetIndex].Value;
        }
        return {Status::Success};
    }
} // namespace NorvesLib::Core::Animation
