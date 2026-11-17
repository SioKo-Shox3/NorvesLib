#pragma once
// 関節の重みが 0 より大きい部位の頂点を、メッシュ空間のバインド姿勢で包む AABB を保持する。
// 実行時は各箱を関節のスキン行列で動かし、部位の全関節の箱の和で部位を包む。
#include "Container/FixedArray.h"
#include "Container/VariableArray.h"
#include <cstdint>

namespace NorvesLib::Core::Skeletal
{
    inline constexpr uint32_t MaximumSkinLodCount = 8; // LOD 数の上限。
    inline constexpr uint32_t MaximumSkinPartCount = 256; // 部位数の上限。
    inline constexpr uint32_t MaximumSkinPartJointCount = 16; // 1 部位に影響する関節数の上限。
    inline constexpr uint32_t MaximumSkinPartLodRangeCount =
        MaximumSkinPartCount * MaximumSkinLodCount; // 部位と LOD の組数の上限。

    struct SkinLodV1
    {
        uint32_t TriangleCount = 0; // この LOD 全体の三角形数。
        float MaxError = 0.0f; // 頂点座標と同じオブジェクト空間の単位による最大誤差。
    };

    struct SkinPartLodRangeV1
    {
        uint32_t FirstIndex = 0; // 全 LOD を含む INDX の中の開始位置。
        uint32_t IndexCount = 0; // この部位と LOD が所有する索引数。
    };

    struct SkinPartJointBoundsV1
    {
        uint32_t JointIndex = 0; // AABB に対応する関節番号。
        Container::FixedArray<float, 3> Min{0.0f, 0.0f, 0.0f}; // メッシュ空間のバインド姿勢での最小座標。
        Container::FixedArray<float, 3> Max{0.0f, 0.0f, 0.0f}; // メッシュ空間のバインド姿勢での最大座標。
    };

    struct SkinPartV1
    {
        uint32_t SubMeshIndex = 0; // この部位を所有するサブメッシュ番号。
        Container::FixedArray<SkinPartLodRangeV1, MaximumSkinLodCount> LodRanges{}; // LOD 数以降は 0。
        Container::VariableArray<SkinPartJointBoundsV1> JointBounds; // 関節番号の昇順で 1〜16 件。
    };

    struct SkinLodTablesV1
    {
        Container::VariableArray<SkinLodV1> Lods; // LOD 番号順の表。
        Container::VariableArray<SkinPartV1> Parts; // サブメッシュ順、各サブメッシュ内では部位順の表。

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return Lods.empty() && Parts.empty();
        }
    };
} // namespace NorvesLib::Core::Skeletal
