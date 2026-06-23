#pragma once

#include "GameModeId.h"
#include "GameModeParams.h"
#include "Container/PointerTypes.h"

#include <cstdint>

namespace NorvesLib::Core::GameMode
{

    /**
     * @brief ゲームモード遷移種別
     *
     * IGameModeControllerを通じて要求できる遷移の種類を表す。
     */
    enum class GameModeTransitionType : uint32_t
    {
        None,
        Change,
        Push,
        Pop,
        ResetStack,
        Quit,
        PushSubRoutine, // 現在のトップ段にサブルーチンを積む（Enter を呼ぶ）
        PopSubRoutine,  // 現在のトップ段の最後のサブルーチンを取り除く（Leave を呼ぶ）
    };

    // 前方宣言: GameModeTransitionRequest が TUniquePtr<ISubRoutine> を運ぶ。
    // 完全な定義は ISubRoutine.h（GameModeContext.h 経由で利用側が include）。
    class ISubRoutine;

    /**
     * @brief ゲームモード退場理由
     *
     * Phase 3 の IGameMode::Leave に渡す理由コード。
     * 必ずここで一度だけ定義し、Phase 3 の IGameMode はこのヘッダを
     * インクルードすること。他の場所で再宣言しない。
     */
    enum class GameModeExitReason : uint32_t
    {
        Change,
        Push,
        Pop,
        Reset,
        Shutdown,
    };

    /**
     * @brief ゲームモード遷移リクエスト
     *
     * IGameModeControllerが受け取った遷移要求を1フレーム分まとめて保持する
     * データ構造。ApplicationProcessorが次フレーム先頭で処理する。
     */
    struct GameModeTransitionRequest
    {
        GameModeTransitionType Type     = GameModeTransitionType::None;
        GameModeId             Target;   // デフォルト構築 = 無効ID。Quit 遷移は Target を参照しない。
        GameModeParams         Params;
        int                    ExitCode = 0;

        // PushSubRoutine のときのみ有効: 積むサブルーチンの所有権を運ぶ。
        // 適用時にトップ段の sub-stack へ move される。TUniquePtr を含むため
        // このリクエストはムーブ専用（コピー不可）になる。ドレインは front()
        // から move-out すること（コピーアウトしない）。
        Container::TUniquePtr<ISubRoutine> SubRoutine;
    };

} // namespace NorvesLib::Core::GameMode
