#pragma once

#include "GameModeId.h"
#include "GameModeParams.h"
#include "IGameMode.h"
#include "Container/PointerTypes.h"
#include "Container/Containers.h"

#include <functional>

namespace NorvesLib::Core::GameMode
{

    /**
     * @brief ゲームモードレジストリ
     *
     * GameModeId をキーに、IGameMode を生成するファクトリ関数（Creator）を
     * 保持する。GameBoot 等の初期化フェーズで Register を呼び、
     * ApplicationProcessor が Create を使ってインスタンスを生成する。
     *
     * 所有権モデル:
     * - GameModeRegistry 自体はサブシステム（GEngine 等）が保持する。
     * - Create が返す TUniquePtr の所有権は呼び出し側に移る。
     */
    class GameModeRegistry
    {
    public:
        /**
         * @brief ゲームモード生成関数の型
         *
         * GameModeParams を受け取り IGameMode の TUniquePtr を返す。
         * std::function は既存コード全体で使用されているため許可。
         */
        using Creator = std::function<Container::TUniquePtr<IGameMode>(const GameModeParams&)>;

        // ========== 登録 ==========

        /**
         * @brief GameModeId に Creator を登録する
         *
         * 後勝ち（同一IDの再登録は上書き）。
         *
         * @param id      登録するGameModeId
         * @param creator ゲームモードを生成するファクトリ関数
         */
        void Register(GameModeId id, Creator creator)
        {
            // 後勝ち（同一IDの再登録は上書き）
            m_Creators[id] = std::move(creator);
        }

        // ========== 生成 ==========

        /**
         * @brief 登録済みのCreatorを使ってゲームモードを生成する
         *
         * @param id     生成するGameModeId
         * @param params 生成時に渡すパラメータ
         * @return 生成されたゲームモード。未登録IDの場合は nullptr。
         */
        Container::TUniquePtr<IGameMode> Create(GameModeId id, const GameModeParams& params) const
        {
            auto it = m_Creators.find(id);
            if (it != m_Creators.end())
            {
                return it->second(params);
            }
            return Container::TUniquePtr<IGameMode>();
        }

        // ========== 問い合わせ ==========

        /**
         * @brief 指定IDが登録済みかどうかを返す
         *
         * @param id 確認するGameModeId
         * @return 登録済みの場合 true
         */
        bool Contains(GameModeId id) const
        {
            return m_Creators.find(id) != m_Creators.end();
        }

    private:
        /// GameModeId → Creator のマッピング。std::hash はenumを標準特殊化するため追加Hasherは不要。
        Container::UnorderedMap<GameModeId, Creator> m_Creators;
    };

} // namespace NorvesLib::Core::GameMode
