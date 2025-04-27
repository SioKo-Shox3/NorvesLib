#pragma once

#include "TGameMode.h"
#include <memory> // std::make_uniqueを使用

namespace NorvesLib::GameMode
{
    // ゲームモードファクトリ
    class GameModeFactory
    {
    public:
        // テンプレート引数で指定されたゲームモードを作成
        template<typename Routine, typename Data>
        static std::unique_ptr<IGameMode> CreateGameMode()
        {
            // TGameModeのインスタンスを作成して一意なポインタで返す
            return std::make_unique<TGameMode<Routine, Data>>();
        }

        // 必要に応じて、初期データ付きで作成するオーバーロードなどを追加
        template<typename Routine, typename Data>
        static std::unique_ptr<IGameMode> CreateGameMode(const Data& initialData)
        {
            auto gameMode = std::make_unique<TGameMode<Routine, Data>>();
            gameMode->GetData() = initialData; // データをコピー
            return gameMode;
        }
         template<typename Routine, typename Data>
        static std::unique_ptr<IGameMode> CreateGameMode(Data&& initialData)
        {
            auto gameMode = std::make_unique<TGameMode<Routine, Data>>();
            gameMode->GetData() = std::move(initialData); // データをムーブ
            return gameMode;
        }
    };

} // namespace NorvesLib::GameMode
