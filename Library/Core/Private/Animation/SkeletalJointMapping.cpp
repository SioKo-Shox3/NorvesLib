#include "Animation/SkeletalJointMapping.h"
#include <utility>
#include <type_traits>
#include <initializer_list>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        using Status = SkeletalJointMappingStatus;
        using Side = SkeletalJointMappingSide;
        SkeletalJointMappingResult Failure(Status status, Side side = Side::None, size_t entry = SIZE_MAX,
                                           size_t other = SIZE_MAX, uint32_t joint = UINT32_MAX)
        {
            return {status, side, entry, other, joint};
        }
        SkeletalJointMappingResult NameFailure(Side side, size_t entry, SkeletalJointIndexResult name)
        {
            auto result = Failure(name.Status == SkeletalJointIndexStatus::NotFound        ? Status::UnknownName
                                  : name.Status == SkeletalJointIndexStatus::LimitExceeded ? Status::LimitExceeded
                                                                                           : Status::InvalidName,
                                  side, entry);
            result.NameResult = name;
            return result;
        }
    } // namespace
    void SkeletalJointMappingSet::Swap(SkeletalJointMappingSet& other) noexcept
    {
        static_assert(noexcept(Pairs.swap(other.Pairs)) &&
                      noexcept(UnmappedSourceIndices.swap(other.UnmappedSourceIndices)) &&
                      noexcept(UnmappedTargetIndices.swap(other.UnmappedTargetIndices)));
        Pairs.swap(other.Pairs);
        UnmappedSourceIndices.swap(other.UnmappedSourceIndices);
        UnmappedTargetIndices.swap(other.UnmappedTargetIndices);
        std::swap(SourceJointCount, other.SourceJointCount);
        std::swap(TargetJointCount, other.TargetJointCount);
        std::swap(RootMappingIndex, other.RootMappingIndex);
    }
    SkeletalJointMappingSet::SkeletalJointMappingSet(SkeletalJointMappingSet&& other) noexcept
    {
        Swap(other);
    }
    SkeletalJointMappingSet& SkeletalJointMappingSet::operator=(const SkeletalJointMappingSet& other)
    {
        if (this != &other)
        {
            SkeletalJointMappingSet candidate(other);
            Swap(candidate);
        }
        return *this;
    }
    SkeletalJointMappingSet& SkeletalJointMappingSet::operator=(SkeletalJointMappingSet&& other) noexcept
    {
        if (this != &other)
        {
            SkeletalJointMappingSet candidate(std::move(other));
            Swap(candidate);
        }
        return *this;
    }
    SkeletalJointMappingResult ResolveSkeletalJointMappings(
        const SkeletalJointIndex& source, const SkeletalJointIndex& target,
        Container::Span<const SkeletalJointMappingNameView> mappings, SkeletalJointMappingRoot requiredRoot,
        SkeletalSourceReusePolicy sourceReuse, const SkeletalJointMappingLimits& limits, SkeletalJointMappingSet& out)
    {
        if (mappings.empty() || !Asset::SkeletalNameDetail::ValidStorage(mappings.data(), mappings.size(),
                                                                         sizeof(SkeletalJointMappingNameView),
                                                                         alignof(SkeletalJointMappingNameView)))
        {
            return Failure(Status::InvalidInput);
        }
        if (sourceReuse != SkeletalSourceReusePolicy::Reject && sourceReuse != SkeletalSourceReusePolicy::Allow)
        {
            return Failure(Status::InvalidPolicy);
        }
        const size_t sourceCount = source.GetCount(), targetCount = target.GetCount();
        if (sourceCount == 0)
        {
            return Failure(Status::EmptyIndex, Side::Source);
        }
        if (targetCount == 0)
        {
            return Failure(Status::EmptyIndex, Side::Target);
        }
        SkeletalJointMappingSet candidate;
        Container::VariableArray<uint32_t> sourceFirst, targetFirst;
        if (sourceCount > limits.MaxCatalogJoints || sourceCount > UINT32_MAX || sourceCount > sourceFirst.max_size() ||
            sourceCount > candidate.UnmappedSourceIndices.max_size())
        {
            return Failure(Status::LimitExceeded, Side::Source);
        }
        if (targetCount > limits.MaxCatalogJoints || targetCount > UINT32_MAX || targetCount > targetFirst.max_size() ||
            targetCount > candidate.UnmappedTargetIndices.max_size())
        {
            return Failure(Status::LimitExceeded, Side::Target);
        }
        if (mappings.size() > limits.MaxMappings || mappings.size() > UINT32_MAX ||
            mappings.size() > candidate.Pairs.max_size())
        {
            return Failure(Status::LimitExceeded);
        }
        if (requiredRoot.SourceIndex >= sourceCount)
        {
            return Failure(Status::InvalidRoot, Side::Source, SIZE_MAX, SIZE_MAX, requiredRoot.SourceIndex);
        }
        if (requiredRoot.TargetIndex >= targetCount)
        {
            return Failure(Status::InvalidRoot, Side::Target, SIZE_MAX, SIZE_MAX, requiredRoot.TargetIndex);
        }
        // 名前byteを読む前に総走査量を制限する。個々のcodec/lookup上限は既存索引が検査する。
        size_t total = 0;
        for (size_t i = 0; i < mappings.size(); ++i)
        {
            for (const Side side : {Side::Source, Side::Target})
            {
                const auto name = side == Side::Source ? mappings[i].SourceName : mappings[i].TargetName;
                if (total > limits.MaxTotalNameBytes || name.size() > limits.MaxTotalNameBytes - total)
                {
                    return Failure(Status::LimitExceeded, side, i);
                }
                total += name.size();
            }
        }
        candidate.SourceJointCount = static_cast<uint32_t>(sourceCount);
        candidate.TargetJointCount = static_cast<uint32_t>(targetCount);
        candidate.Pairs.reserve(mappings.size());
        candidate.UnmappedSourceIndices.reserve(sourceCount);
        candidate.UnmappedTargetIndices.reserve(targetCount);
        sourceFirst.assign(sourceCount, UINT32_MAX);
        targetFirst.assign(targetCount, UINT32_MAX);
        for (size_t i = 0; i < mappings.size(); ++i)
        {
            uint32_t sourceJoint = UINT32_MAX, targetJoint = UINT32_MAX;
            auto found = FindSkeletalJointIndex(source, mappings[i].SourceName, sourceJoint);
            if (!found.Succeeded())
            {
                return NameFailure(Side::Source, i, found);
            }
            found = FindSkeletalJointIndex(target, mappings[i].TargetName, targetJoint);
            if (!found.Succeeded())
            {
                return NameFailure(Side::Target, i, found);
            }
            if (targetFirst[targetJoint] != UINT32_MAX)
            {
                return Failure(Status::DuplicateTarget, Side::Target, i, targetFirst[targetJoint], targetJoint);
            }
            if (sourceReuse == SkeletalSourceReusePolicy::Reject && sourceFirst[sourceJoint] != UINT32_MAX)
            {
                return Failure(Status::DuplicateSource, Side::Source, i, sourceFirst[sourceJoint], sourceJoint);
            }
            if (targetJoint == requiredRoot.TargetIndex && sourceJoint != requiredRoot.SourceIndex)
            {
                return Failure(Status::RootConflict, Side::Target, i, SIZE_MAX, targetJoint);
            }
            if (sourceFirst[sourceJoint] == UINT32_MAX)
            {
                sourceFirst[sourceJoint] = static_cast<uint32_t>(i);
            }
            targetFirst[targetJoint] = static_cast<uint32_t>(i);
            candidate.Pairs.push_back({sourceJoint, targetJoint, static_cast<uint32_t>(i)});
            if (sourceJoint == requiredRoot.SourceIndex && targetJoint == requiredRoot.TargetIndex)
            {
                candidate.RootMappingIndex = static_cast<uint32_t>(i);
            }
        }
        if (candidate.RootMappingIndex == UINT32_MAX)
        {
            return Failure(Status::RootMissing);
        }
        for (uint32_t i = 0; i < sourceCount; ++i)
        {
            if (sourceFirst[i] == UINT32_MAX)
            {
                candidate.UnmappedSourceIndices.push_back(i);
            }
        }
        for (uint32_t i = 0; i < targetCount; ++i)
        {
            if (targetFirst[i] == UINT32_MAX)
            {
                candidate.UnmappedTargetIndices.push_back(i);
            }
        }
        static_assert(std::is_nothrow_move_assignable_v<SkeletalJointMappingSet>);
        out = std::move(candidate);
        return {Status::Success};
    }
} // namespace NorvesLib::Core::Animation
