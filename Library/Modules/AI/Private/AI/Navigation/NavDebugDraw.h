// ナビ格子と経路のデバッグ描画。
#pragma once

#include "Debug/DebugConfig.h"

#if NORVES_ENABLE_DEBUG_OUTPUT
#include "Container/Span.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class DebugDrawQueue;
}

namespace NorvesLib::Modules::AI
{
    class NavGrid;

    struct NavDebugDrawSettings
    {
        float Radius = 24.0f;       // 中心からの XZ 平面上の描画半径（m）
        float HeightOffset = 0.05f; // 地面から印を持ち上げる高さ（m）
        uint32_t MaxCells = 4096;   // 1 回に印を描くセル数の上限
    };

    /// 半径内の通行不能セルに地形別の色の × 印を、浅い水に線を描く。
    void SubmitNavGridDebug(const NavGrid& grid, const Math::Vector3& center, const NavDebugDrawSettings& settings,
                            Core::Rendering::DebugDrawQueue& queue);

    /// 隣り合う経路点の線分を、center を中心とする XZ 平面の半径 settings.Radius の円内に切り詰め、円内の点に縦の印を描く。
    void SubmitNavPathDebug(Core::Container::Span<const Math::Vector3> points, const Math::Vector3& center,
                            const NavDebugDrawSettings& settings, const Math::Vector4& color,
                            Core::Rendering::DebugDrawQueue& queue);
} // namespace NorvesLib::Modules::AI
#endif
