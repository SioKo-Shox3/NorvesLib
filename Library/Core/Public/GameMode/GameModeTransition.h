#pragma once

#include "GameModeId.h"
#include "GameModeParams.h"

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
    };

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
        GameModeTransitionType Type   = GameModeTransitionType::None;
        GameModeId             Target = GameModeId::Rendering3DTest;
        GameModeParams         Params;
        int                    ExitCode = 0;
    };

} // namespace NorvesLib::Core::GameMode
