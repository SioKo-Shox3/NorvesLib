#pragma once

#include "Core/Public/GameMode/GameModeId.h"

// ---------------------------------------------------------------------------
// ゲーム固有のゲームモードID定数
//
// Coreレベルにゲーム名を持たせないため、各ゲームがここで Identity ベースの
// GameModeId 定数を定義する。GameModeRegistry への Register キーおよび
// Start/Change/Push の引数として使用する。
//
// Identity はランタイム構築のため constexpr にはできない。
// inline const（動的初期化）を使用する。IdentityPool は Meyers singleton で
// あるため静的初期化順序は安全。
// ---------------------------------------------------------------------------

namespace Game::GameModes
{

    /// 3Dレンダリングテストモードの GameModeId
    inline const NorvesLib::Core::GameMode::GameModeId Rendering3DTest =
        NorvesLib::Core::Identity("Rendering3DTest");

    /// メモリ寿命テストモードの GameModeId
    inline const NorvesLib::Core::GameMode::GameModeId MemoryAgingTest =
        NorvesLib::Core::Identity("MemoryAgingTest");

} // namespace Game::GameModes
