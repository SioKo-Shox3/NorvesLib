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

    /// 固定/可変駆動と描画補間の比較用モード。明示フラグ時のみ起動する。
    inline constexpr NorvesLib::Core::GameMode::GameModeId DogMovementSmoke = "DogMovementSmoke"_id;

    /// メモリ寿命テストモードの GameModeId
    inline constexpr NorvesLib::Core::GameMode::GameModeId MemoryAgingTest = "MemoryAgingTest"_id;

    /// ランチャー（シーン選択）。引数なしの起動で入り、--scene-cycle-smoke がシーンと行き来する相手でもある
    inline constexpr NorvesLib::Core::GameMode::GameModeId Launcher = "Launcher"_id;

    /// 空と平らな地面だけの確認用シーン（開発ビルドだけランチャーに出る）。自由飛行カメラの確認に使う
    inline constexpr NorvesLib::Core::GameMode::GameModeId Sandbox = "Sandbox"_id;

    /// 仮想化ジオメトリ（MegaGeometry）で生成したバナナを床いっぱいに敷き詰めるシーン
    inline constexpr NorvesLib::Core::GameMode::GameModeId MegaBananas = "MegaBananas"_id;

    /// 荒野のシーン。高さ場から焼いた起伏のある地形（仮想化ジオメトリ）を、物理空と太陽で描く
    inline constexpr NorvesLib::Core::GameMode::GameModeId Wasteland = "Wasteland"_id;

    /// 夜の集落のシーン。荒野と同じ地形の一角に生成した建物と小物で道沿いの集落を組み、月明かりと夜空で描く（荒野のモードの型を使う）
    inline constexpr NorvesLib::Core::GameMode::GameModeId NightVillage = "NightVillage"_id;

    /// 材質ギャラリー。石の床の展示場に Poly Haven の材質を台座の板と球で並べ、金属度 × 粗さの球の格子を置く
    inline constexpr NorvesLib::Core::GameMode::GameModeId MaterialGallery = "MaterialGallery"_id;

    /// 仮想化ジオメトリの負荷シーン（開発ビルドだけランチャーに出る）。多種のメッシュの個別 Entity を置く
    inline constexpr NorvesLib::Core::GameMode::GameModeId MegaStress = "MegaStress"_id;

} // namespace Game::GameModes
