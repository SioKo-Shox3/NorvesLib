// ストリーミングするセルとペイロードの共通型を定義する。
#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Math/Vector3.h"

#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Streaming
{
    // セルをまとめて停止するためのスコープ。
    using StreamingScopeId = uint64_t;
    inline constexpr StreamingScopeId kDefaultStreamingScopeId = 0;

    using StreamingCellId = uint32_t;
    inline constexpr StreamingCellId kInvalidStreamingCellId = 0xFFFFFFFFu;

    /// 地形の区画を表すペイロードの種類。
    inline constexpr const char* kTerrainTilePayloadKind = "terrain-tile";
    /// 散布のチャンクを表すペイロードの種類。
    inline constexpr const char* kScatterChunkPayloadKind = "scatter-chunk";
    /// 静的なコライダーを表すペイロードの種類。
    inline constexpr const char* kStaticCollidersPayloadKind = "static-colliders";
    /// コードで組む配置を表すペイロードの種類。
    inline constexpr const char* kPlacementPayloadKind = "placement";

    enum class EStreamingCellState : uint8_t
    {
        Unloaded,
        Preparing,
        ReadyToCommit,
        Committing,
        Active,
        Dormant,
        Unloading,
        Failed // 読み込みを諦めた状態。Ru の外へ出ると Unloaded に戻り、再試行できる。
    };

    inline const char* ToString(EStreamingCellState state)
    {
        switch (state)
        {
        case EStreamingCellState::Unloaded: return "Unloaded";
        case EStreamingCellState::Preparing: return "Preparing";
        case EStreamingCellState::ReadyToCommit: return "ReadyToCommit";
        case EStreamingCellState::Committing: return "Committing";
        case EStreamingCellState::Active: return "Active";
        case EStreamingCellState::Dormant: return "Dormant";
        case EStreamingCellState::Unloading: return "Unloading";
        case EStreamingCellState::Failed: return "Failed";
        }
        return "Unknown";
    }

    enum class EStreamingStepResult : uint8_t
    {
        InProgress,
        Done,
        Failed,
        /// 進みがなく、このフレームはこのセルのコミットの歩をやめる。CommitStep だけが返す。
        /// UnloadStep は返さない。返したら InProgress と同じに扱う。
        Waiting
    };

    enum class EStreamingPrepareResult : uint8_t
    {
        Success,
        Failed,
        Cancelled
    };

    struct StreamingBounds
    {
        Math::Vector3 Min;
        Math::Vector3 Max;
    };

    /// X と Z の半開区間 [Min, Max) に点が入るかを返す。非有限の水平成分は含めない。
    inline bool ContainsHorizontalPoint(const StreamingBounds& bounds, float x, float z)
    {
        return std::isfinite(x) && std::isfinite(z) &&
            std::isfinite(bounds.Min.x) && std::isfinite(bounds.Max.x) &&
            std::isfinite(bounds.Min.z) && std::isfinite(bounds.Max.z) &&
            bounds.Min.x <= x && x < bounds.Max.x && bounds.Min.z <= z && z < bounds.Max.z;
    }

    struct StreamingPayloadDesc
    {
        Container::String Kind;
        Container::String LogicalPath;
    };

    struct StreamingCellDesc
    {
        StreamingCellId Id;
        StreamingBounds Bounds;
        Container::VariableArray<StreamingPayloadDesc> Payloads;
    };

    // ペイロードごとに保持し、手続きが自由に使う進み具合。
    struct StreamingStepCursor
    {
        uint32_t Step = 0;
        uint64_t User = 0;
    };

    struct StreamingPayloadBytes
    {
        uint64_t Cpu = 0;
        uint64_t Gpu = 0;
    };

    struct StreamingPayloadContext
    {
        StreamingCellId Cell;
        uint32_t PayloadIndex;
        const StreamingPayloadDesc* Desc;
        /// payload が属するセルの記述。境界から含める区画やチャンクを決める。非所有で、マニフェストの差し替えまで有効。
        const StreamingCellDesc* CellDesc = nullptr;
    };
} // namespace NorvesLib::Core::Streaming
