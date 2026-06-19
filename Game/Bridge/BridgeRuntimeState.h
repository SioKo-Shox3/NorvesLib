#pragma once

#include <cstdint>

namespace Game::Bridge
{

    /**
     * @brief Bridge runtime（play/pause/stop）の状態
     *
     * SDK 非依存の純粋 enum。NORVES_BRIDGE_ENABLED の有無にかかわらず両ビルドで
     * 定義され、GameApplicationHandler が保持する。adapter（SDK ガード下）が
     * SetBridgeRuntimeState で更新し、ApplicationProcessor の Tick ゲートが
     * ShouldAdvanceSimulation 経由で参照する。
     */
    enum class BridgeRuntimeState : uint8_t
    {
        Edit,
        Playing,
        Paused,
        Stopped
    };

} // namespace Game::Bridge
