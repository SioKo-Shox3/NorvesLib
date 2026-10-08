#pragma once
// retarget等の厳密consumer向け所有索引。SkeletonResourceのIdentity索引とは別契約。
#include "Asset/CookedSkeletalNameCodec.h"
#include "Container/VariableArray.h"

namespace NorvesLib::Core::Skeletal
{
    struct SkeletalJoint;
}
namespace NorvesLib::Core::Animation
{
    struct SkeletalJointNameView
    {
        Container::Span<const uint8_t> Utf8;
    };
    struct SkeletalJointIndexLimits
    {
        uint32_t MaxJoints = 1024;
        size_t MaxNameBytes = 4096;
        size_t MaxTotalBytes = 1024u * 1024u;
    };
    enum class SkeletalJointIndexStatus : uint8_t
    {
        Success,
        NotFound,
        InvalidInput,
        EmptyName,
        InvalidName,
        DuplicateName,
        LimitExceeded
    };
    struct SkeletalJointIndexResult
    {
        SkeletalJointIndexStatus Status = SkeletalJointIndexStatus::InvalidInput;
        size_t NameIndex = SIZE_MAX, OtherNameIndex = SIZE_MAX;
        Asset::SkeletalNameStatus EncodingStatus = Asset::SkeletalNameStatus::Success;
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SkeletalJointIndexStatus::Success;
        }
    };
    class SkeletalJointIndex
    {
      public:
        SkeletalJointIndex() = default;
        SkeletalJointIndex(const SkeletalJointIndex&) = default;
        SkeletalJointIndex(SkeletalJointIndex&& other) noexcept;
        SkeletalJointIndex& operator=(const SkeletalJointIndex& other);
        SkeletalJointIndex& operator=(SkeletalJointIndex&& other) noexcept;
        [[nodiscard]] size_t GetCount() const noexcept
        {
            return m_Entries.size();
        }

      private:
        struct Entry
        {
            uint32_t Offset = 0, Length = 0, OriginalIndex = 0;
        };
        Container::VariableArray<uint8_t> m_Bytes;
        Container::VariableArray<Entry> m_Entries;
        size_t m_MaxNameBytes = SkeletalJointIndexLimits{}.MaxNameBytes;
        void Swap(SkeletalJointIndex& other) noexcept;
        friend SkeletalJointIndexResult BuildSkeletalJointIndex(Container::Span<const SkeletalJointNameView>,
                                                                const SkeletalJointIndexLimits&, SkeletalJointIndex&);
        friend SkeletalJointIndexResult FindSkeletalJointIndex(const SkeletalJointIndex&,
                                                               Container::Span<const uint8_t>, uint32_t&) noexcept;
    };
    // minor2 name codecと同じ厳密UTF8/NUL規則。さらに非空/完全一致名の一意性を要求する。
    // 成功時だけoutを置換し、入力の寿命から独立する。失敗/確保例外では既存outを保持する。
    [[nodiscard]] SkeletalJointIndexResult BuildSkeletalJointIndex(Container::Span<const SkeletalJointNameView> names,
                                                                   const SkeletalJointIndexLimits& limits,
                                                                   SkeletalJointIndex& out);
    // native TCHAR名をminor2 codecでUTF8へ変換する。親/IBMやResource/Identityは解釈しない。
    [[nodiscard]] SkeletalJointIndexResult BuildSkeletalJointIndexFromJoints(
        Container::Span<const Skeletal::SkeletalJoint> joints, const SkeletalJointIndexLimits& limits,
        SkeletalJointIndex& out);
    // 名前順に検索しても返すのは構築時の元配列番号。失敗時outIndexを変更しない。
    [[nodiscard]] SkeletalJointIndexResult FindSkeletalJointIndex(const SkeletalJointIndex& index,
                                                                  Container::Span<const uint8_t> utf8Name,
                                                                  uint32_t& outIndex) noexcept;
} // namespace NorvesLib::Core::Animation
