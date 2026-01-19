#pragma once

#include "TGameMode.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::Core::GameMode
{

    /**
     * @brief ゲームモードファクトリ
     *
     * TGameModeのインスタンスを生成するためのファクトリクラス。
     */
    class GameModeFactory
    {
    public:
        /**
         * @brief テンプレート引数で指定されたゲームモードを作成
         * @tparam Routine ゲームモードのロジッククラス
         * @tparam Data ゲームモードのデータクラス
         * @return 作成されたゲームモードへのユニークポインタ
         */
        template <typename Routine, typename Data>
        static Container::TUniquePtr<IGameMode> CreateGameMode()
        {
            return Container::MakeUnique<TGameMode<Routine, Data>>();
        }

        /**
         * @brief 初期データ付きでゲームモードを作成（コピー版）
         * @tparam Routine ゲームモードのロジッククラス
         * @tparam Data ゲームモードのデータクラス
         * @param initialData 初期データ
         * @return 作成されたゲームモードへのユニークポインタ
         */
        template <typename Routine, typename Data>
        static Container::TUniquePtr<IGameMode> CreateGameMode(const Data &initialData)
        {
            auto gameMode = Container::MakeUnique<TGameMode<Routine, Data>>();
            gameMode->GetData() = initialData;
            return gameMode;
        }

        /**
         * @brief 初期データ付きでゲームモードを作成（ムーブ版）
         * @tparam Routine ゲームモードのロジッククラス
         * @tparam Data ゲームモードのデータクラス
         * @param initialData 初期データ（ムーブされる）
         * @return 作成されたゲームモードへのユニークポインタ
         */
        template <typename Routine, typename Data>
        static Container::TUniquePtr<IGameMode> CreateGameMode(Data &&initialData)
        {
            auto gameMode = Container::MakeUnique<TGameMode<Routine, Data>>();
            gameMode->GetData() = std::move(initialData);
            return gameMode;
        }
    };

} // namespace NorvesLib::Core::GameMode
