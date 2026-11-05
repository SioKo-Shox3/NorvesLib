// ストリーミングするセルの優先度とヒステリシス付きの輪を純関数で判定する。
#pragma once

#include "Streaming/StreamingTypes.h"

namespace NorvesLib::Core::Streaming
{
    // 既定値は仮の値。見どころの間隔は未定のため、配置に合わせて調整する。
    struct StreamingRingSettings
    {
        float ActiveRadius = 384.0f;
        float DormantRadius = 768.0f;
        float UnloadRadius = 1024.0f;
        float DirectionWeight = 0.5f;
        float LatencySeconds = 0.5f;
    };

    // 全項目が有限で、0 <= Ra < Rd < Ru、重みと遅れが非負かを返す。
    bool ValidateRingSettings(const StreamingRingSettings& settings);

    struct StreamingFocus
    {
        Math::Vector3 Position;
        Math::Vector3 Velocity;
    };

    // 境界までの水平距離を返す。内側はゼロ、非有限の位置は無限大とする。
    float ComputeHorizontalDistance(const StreamingBounds& bounds, const Math::Vector3& position);

    // セル幅と水平速度に遅れを掛けた距離の和。非有限の焦点は無限大とする。
    float ComputeLookaheadDistance(float cellSize, const StreamingFocus& focus, const StreamingRingSettings& settings);

    // Ra と先読みの和を Rd で制限する。非有限の焦点は無限大を返して判定側へ伝える。
    float ComputeLoadRadius(float cellSize, const StreamingFocus& focus, const StreamingRingSettings& settings);

    // 小さい値を優先する。水平速度が 1e-4 未満なら方向補正せず、非有限の焦点は最大値とする。
    float ComputePriority(const StreamingBounds& bounds, const StreamingFocus& focus, const StreamingRingSettings& settings);

    enum class EStreamingRingDecision : uint8_t
    {
        None,
        Load,
        MakeActive,
        MakeDormant,
        Unload
    };

    // Ra・Rd・Ru のヒステリシスで一つだけ決める。非有限の距離・読み込み半径は距離無限大として扱う。
    EStreamingRingDecision DecideRing(EStreamingCellState state, float distance, float loadRadius, const StreamingRingSettings& settings);

    // コミット直後の距離が Ra を超える場合に Dormant へ入る。
    bool ShouldEnterDormant(float distance, const StreamingRingSettings& settings);
} // namespace NorvesLib::Core::Streaming
