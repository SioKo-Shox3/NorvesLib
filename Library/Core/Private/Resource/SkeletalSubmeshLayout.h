#pragma once

#include "Container/Span.h"
#include "Resource/SkeletalSubMesh.h"
#include <cstdint>

namespace NorvesLib::Core::Skeletal
{
    enum class SkeletalSubmeshLayoutStatus
    {
        Success,
        InvalidInput,
        InvalidTotalIndexCount,
        IncompleteTables,
        SubmeshLimitExceeded,
        MaterialSlotLimitExceeded,
        InvalidIndexRange,
        InvalidMaterialSlot
    };
    struct SkeletalSubmeshLayoutResult
    {
        SkeletalSubmeshLayoutStatus Status = SkeletalSubmeshLayoutStatus::InvalidInput;
        uint32_t SubmeshCount = 0;
        uint32_t MaterialSlotCount = 0;
        bool bUsesImplicitSingleSubmesh = false;
        SkeletalSubMesh ImplicitSubmesh;
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SkeletalSubmeshLayoutStatus::Success;
        }
    };
    // 非空三角形範囲が[0,totalIndexCount)を昇順・隙間/重複なしで覆い、slotが範囲内か検査。
    // 両表空のみ旧互換の全index/slot0を返す。失敗は数量0、入力/所有配列を変更しない。
    // indexの実値/頂点範囲、slot名の妥当性/重複、wireのreservedは別の検証責務。
    [[nodiscard]] SkeletalSubmeshLayoutResult ResolveSkeletalSubmeshLayout(
        Container::Span<const SkeletalSubMesh> submeshes, uint64_t totalIndexCount, uint64_t materialSlotCount) noexcept;
} // namespace NorvesLib::Core::Skeletal
