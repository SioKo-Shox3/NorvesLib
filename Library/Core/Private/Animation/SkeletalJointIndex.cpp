#include "Animation/SkeletalJointIndex.h"
#include <algorithm>
#include <cstring>
#include <utility>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        using Bytes = Container::Span<const uint8_t>;
        using Status = SkeletalJointIndexStatus;
        int Compare(Bytes a, Bytes b) noexcept
        {
            const size_t common = std::min(a.size(), b.size());
            if (common)
            {
                const int difference = std::memcmp(a.data(), b.data(), common);
                if (difference)
                {
                    return difference < 0 ? -1 : 1;
                }
            }
            return a.size() < b.size() ? -1 : a.size() > b.size() ? 1 : 0;
        }
        SkeletalJointIndexResult EncodingFailure(Asset::SkeletalNameStatus encoding, size_t index) noexcept
        {
            const auto status = encoding == Asset::SkeletalNameStatus::InvalidInput  ? Status::InvalidInput
                                : encoding == Asset::SkeletalNameStatus::NameTooLong ? Status::LimitExceeded
                                                                                     : Status::InvalidName;
            return {status, index, SIZE_MAX, encoding};
        }
    } // namespace
    void SkeletalJointIndex::Swap(SkeletalJointIndex& other) noexcept
    {
        static_assert(noexcept(m_Bytes.swap(other.m_Bytes)) && noexcept(m_Entries.swap(other.m_Entries)));
        m_Bytes.swap(other.m_Bytes);
        m_Entries.swap(other.m_Entries);
        std::swap(m_MaxNameBytes, other.m_MaxNameBytes);
    }
    SkeletalJointIndex::SkeletalJointIndex(SkeletalJointIndex&& other) noexcept
    {
        Swap(other);
    }
    SkeletalJointIndex& SkeletalJointIndex::operator=(const SkeletalJointIndex& other)
    {
        if (this != &other)
        {
            SkeletalJointIndex copy(other);
            Swap(copy);
        }
        return *this;
    }
    SkeletalJointIndex& SkeletalJointIndex::operator=(SkeletalJointIndex&& other) noexcept
    {
        if (this != &other)
        {
            SkeletalJointIndex moved(std::move(other));
            Swap(moved);
        }
        return *this;
    }
    SkeletalJointIndexResult BuildSkeletalJointIndex(Container::Span<const SkeletalJointNameView> names,
                                                     const SkeletalJointIndexLimits& limits, SkeletalJointIndex& out)
    {
        if (names.empty() ||
            !Asset::SkeletalNameDetail::ValidStorage(names.data(), names.size(), sizeof(SkeletalJointNameView),
                                                     alignof(SkeletalJointNameView)))
        {
            return {Status::InvalidInput};
        }
        SkeletalJointIndex result;
        if (names.size() > limits.MaxJoints || names.size() > UINT32_MAX || names.size() > result.m_Entries.max_size())
        {
            return {Status::LimitExceeded};
        }
        size_t total = 0;
        for (size_t i = 0; i < names.size(); ++i)
        {
            const auto name = names[i].Utf8;
            if (name.empty())
            {
                return {Status::EmptyName, i};
            }
            if (name.size() > limits.MaxNameBytes || name.size() > UINT32_MAX)
            {
                return {Status::LimitExceeded, i};
            }
            const auto measured = Asset::MeasureSkeletalNameEncoding<uint8_t>(2, name);
            if (!measured.Succeeded())
            {
                return EncodingFailure(measured.Status, i);
            }
            if (name.size() > SIZE_MAX - total || name.size() > UINT32_MAX - total || total > limits.MaxTotalBytes ||
                name.size() > limits.MaxTotalBytes - total)
            {
                return {Status::LimitExceeded, i};
            }
            total += name.size();
        }
        if (total > result.m_Bytes.max_size())
        {
            return {Status::LimitExceeded};
        }
        result.m_MaxNameBytes = limits.MaxNameBytes;
        result.m_Bytes.reserve(total);
        result.m_Entries.reserve(names.size());
        for (size_t i = 0; i < names.size(); ++i)
        {
            const auto name = names[i].Utf8;
            result.m_Entries.push_back({static_cast<uint32_t>(result.m_Bytes.size()),
                                        static_cast<uint32_t>(name.size()), static_cast<uint32_t>(i)});
            result.m_Bytes.insert(result.m_Bytes.end(), name.begin(), name.end());
        }
        const auto view = [&](const auto& entry) -> Bytes
        {
            return {result.m_Bytes.data() + entry.Offset, entry.Length};
        };
        std::sort(result.m_Entries.begin(), result.m_Entries.end(),
                  [&](const auto& a, const auto& b)
                  {
                      const int order = Compare(view(a), view(b));
                      return order < 0 || (order == 0 && a.OriginalIndex < b.OriginalIndex);
                  });
        for (size_t i = 1; i < result.m_Entries.size(); ++i)
        {
            if (Compare(view(result.m_Entries[i - 1]), view(result.m_Entries[i])) == 0)
            {
                return {Status::DuplicateName, result.m_Entries[i].OriginalIndex,
                        result.m_Entries[i - 1].OriginalIndex};
            }
        }
        out = std::move(result);
        return {Status::Success};
    }
    SkeletalJointIndexResult FindSkeletalJointIndex(const SkeletalJointIndex& index, Bytes name,
                                                    uint32_t& outIndex) noexcept
    {
        if (name.empty())
        {
            return {Status::EmptyName};
        }
        if (name.size() > index.m_MaxNameBytes || name.size() > UINT32_MAX)
        {
            return {Status::LimitExceeded};
        }
        const auto measured = Asset::MeasureSkeletalNameEncoding<uint8_t>(2, name);
        if (!measured.Succeeded())
        {
            return EncodingFailure(measured.Status, SIZE_MAX);
        }
        size_t first = 0, last = index.m_Entries.size();
        while (first < last)
        {
            const size_t middle = first + (last - first) / 2;
            const auto& entry = index.m_Entries[middle];
            const int order = Compare({index.m_Bytes.data() + entry.Offset, entry.Length}, name);
            if (order < 0)
            {
                first = middle + 1;
            }
            else if (order > 0)
            {
                last = middle;
            }
            else
            {
                outIndex = entry.OriginalIndex;
                return {Status::Success};
            }
        }
        return {Status::NotFound};
    }
} // namespace NorvesLib::Core::Animation
