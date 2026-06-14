#pragma once

#include "Text/IdentityPool.h"

namespace NorvesLib::Core::GameMode
{

    /**
     * @brief ゲームモード識別子（不透明な文字列ハッシュハンドル）
     *
     * GameModeRegistryへの登録・検索キーとして使用する。
     * Identity（文字列ハッシュ）のエイリアスであり、Coreレベルで
     * ゲーム固有のモード名を定義しない。各ゲームが独自のIDを定義する
     *（例: Game/GameModes/GameModeIds.h）。
     *
     * デフォルト構築した GameModeId は無効（hash == 0）。
     * 有効性チェックは IsValid() を使う。
     */
    using GameModeId = NorvesLib::Core::Identity;

} // namespace NorvesLib::Core::GameMode
