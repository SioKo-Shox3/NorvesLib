#pragma once

#include "Core/Public/GameMode/GameModeId.h"

// ---------------------------------------------------------------------------
// ゲーム固有のゲームモードID定数
//
// Coreレベルにゲーム名を持たせないため、各ゲームがここで Identity ベースの
// GameModeId 定数を定義する。GameModeRegistry への Register キーおよび
// Start/Change/Push の引数として使用する。
//
// `"Name"_id` ユーザー定義リテラルにより Identity をコンパイル時に構築できる
// ため inline constexpr を使用する。コンパイル時パスはハッシュをコンパイル時に
// 計算し IdentityPool には登録しないが、ランタイム構築と同一ハッシュ値になるため
// レジストリのキーとして相互運用できる。
// ---------------------------------------------------------------------------

namespace Game::GameModes
{

    using namespace NorvesLib::Core::literals; // for ""_id

    /// 3Dレンダリングテストモードの GameModeId
    inline constexpr NorvesLib::Core::GameMode::GameModeId Rendering3DTest = "Rendering3DTest"_id;

    /// メモリ寿命テストモードの GameModeId
    inline constexpr NorvesLib::Core::GameMode::GameModeId MemoryAgingTest = "MemoryAgingTest"_id;

} // namespace Game::GameModes
