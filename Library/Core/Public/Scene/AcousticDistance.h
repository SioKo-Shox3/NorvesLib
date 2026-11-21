#pragma once

#include "Container/Span.h"
#include <cstdint>

namespace NorvesLib::Core::Scene
{
    inline constexpr float kAcousticMinDistanceFloor = 0.01f; // 距離減衰の開始距離の下限 (m)。

    enum class AcousticRolloff : uint8_t
    {
        InverseClamped,
        Linear,
        LogDb,
        Table
    };

    struct AcousticDistanceTablePoint
    {
        float NormalizedDistance = 0.0f; // 距離を [0, 1] に正規化した座標。
        float Gain = 1.0f;
    };

    struct AcousticDistanceParams
    {
        float MinDistance = 1.0f;
        float MaxDistance = 50.0f;
        AcousticRolloff Rolloff = AcousticRolloff::InverseClamped;
        float LogDbRange = 60.0f; // LogDb で正規化する前の、最大距離での減衰量 (dB)。
        Container::Span<const AcousticDistanceTablePoint> Table;
    };

    /**
     * 距離 d における振幅利得を返す。Min = max(MinDistance, kAcousticMinDistanceFloor) とし、非有限の MinDistance は 1 とする。
     * 負の距離は 0 とし、距離が NaN なら利得は 0。Max が有限で d ≥ Max なら、Min にかかわらず利得は 0。
     * 残りの距離では d ≤ Min なら利得は 1。d > Min で Max が非有限なら利得は 0。
     * 区間内は t = (d − Min)/(Max − Min) として、次の利得を [0, 1] に収める。
     * InverseClamped は (Min/d − Min/Max)/(1 − Min/Max)、Linear は 1 − t。
     * LogDb は r = clamp(LogDbRange, 1, 120)、f = 10^(−r/20) として (10^(−r·t/20) − f)/(1 − f)。
     * Table は (0, 1)、距離が昇順の有効な点、(1, 0) を線形補間する。各利得の範囲は [0, 直前の利得]。
     */
    [[nodiscard]] float EvaluateAcousticDistanceGain(const AcousticDistanceParams& params, float distance) noexcept;
}
