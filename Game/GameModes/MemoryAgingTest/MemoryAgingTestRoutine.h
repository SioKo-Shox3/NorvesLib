#pragma once

#include "Core/Public/GameMode/IGameMode.h"
#include "MemoryAgingTestData.h"

namespace Game::GameModes
{

    /**
     * @brief メモリエージングテストのロジッククラス
     *
     * メモリの確保と解放を繰り返し行い、メモリリークや断片化の
     * テストを行うためのゲームモードロジック。
     */
    class MemoryAgingTestRoutine
    {
    public:
        /**
         * @brief ステート開始時の処理
         * @param ctx  実行コンテキスト
         * @param data ゲームモードデータ
         * @return 入場結果
         */
        NorvesLib::Core::GameMode::GameModeEnterResult
        Enter(NorvesLib::Core::GameMode::GameModeContext& ctx, MemoryAgingTestData& data);

        /**
         * @brief ステート実行中の処理
         * @param ctx  実行コンテキスト
         * @param data ゲームモードデータ
         * @param deltaTime フレーム間隔（秒）
         */
        void Tick(NorvesLib::Core::GameMode::GameModeContext& ctx, MemoryAgingTestData& data, float deltaTime);

        /**
         * @brief ステート終了時の処理
         * @param ctx    実行コンテキスト
         * @param data   ゲームモードデータ
         * @param reason 退場理由
         */
        void Leave(NorvesLib::Core::GameMode::GameModeContext& ctx, MemoryAgingTestData& data,
                   NorvesLib::Core::GameMode::GameModeExitReason reason);

        static constexpr const char* DebugName = "MemoryAgingTest";
    };

} // namespace Game::GameModes
