#pragma once

#include <cmath>

namespace NorvesLib::Modules::AI
{
    struct NavObstacleSettings
    {
        // 0.25 s・0.5 セルは仮の値で未計測。
        float MinRestampIntervalSeconds = 0.25f;
        float MinRestampDistanceCells = 0.5f;
        float MaxStampSpeed = 3.0f; // 押印できる速さの上限（m/s）。
        float HeightBand = 2.0f;    // 地面から判定する高さ（m）。
        float AgentPadding = 0.5f;  // 足跡の水平方向の余白（m）。

        bool IsValid() const
        {
            return std::isfinite(MinRestampIntervalSeconds) && MinRestampIntervalSeconds > 0.0f &&
                   std::isfinite(MinRestampDistanceCells) && MinRestampDistanceCells >= 0.0f &&
                   std::isfinite(MaxStampSpeed) && MaxStampSpeed > 0.0f &&
                   std::isfinite(HeightBand) && HeightBand > 0.0f &&
                   std::isfinite(AgentPadding) && AgentPadding >= 0.0f;
        }

        bool operator==(const NavObstacleSettings&) const = default;
    };
}
