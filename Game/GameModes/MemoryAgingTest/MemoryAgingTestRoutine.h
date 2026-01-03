#pragma once

#include "Core/Public/GameMode/IStateMachine.h"
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
         * @param proc ステートマシン
         * @param data ゲームモードデータ
         */
        void Enter(NorvesLib::Core::GameMode::IStateMachine* proc, MemoryAgingTestData& data);

        /**
         * @brief ステート実行中の処理
         * @param proc ステートマシン
         * @param data ゲームモードデータ
         * @param deltaTime フレーム間隔（秒）
         */
        void Do(NorvesLib::Core::GameMode::IStateMachine* proc, MemoryAgingTestData& data, float deltaTime);

        /**
         * @brief ステート終了時の処理
         * @param proc ステートマシン
         * @param data ゲームモードデータ
         */
        void Leave(NorvesLib::Core::GameMode::IStateMachine* proc, MemoryAgingTestData& data);
    };

} // namespace Game::GameModes
