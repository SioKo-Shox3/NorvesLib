#pragma once

#include "IGameMode.h"

namespace NorvesLib::Core::GameMode
{

    /**
     * @brief ゲームモードテンプレートクラス
     *
     * ロジック（Routine）とデータ（Data）を分離したゲームモードの実装。
     *
     * @tparam Routine ゲームモードのロジッククラス（Enter, Tick, Leave メソッドと
     *                 静的メンバ DebugName を持つ）
     * @tparam Data    ゲームモードのデータクラス
     */
    template <typename Routine, typename Data>
    class TGameMode : public IGameMode
    {
    public:
        // コンストラクタ
        TGameMode() = default;
        virtual ~TGameMode() override = default;

        // IGameModeインターフェースの実装
        GameModeEnterResult Enter(GameModeContext& ctx) override
        {
            return m_Routine.Enter(ctx, m_Data);
        }

        void Tick(GameModeContext& ctx, float deltaTime) override
        {
            m_Routine.Tick(ctx, m_Data, deltaTime);
        }

        void Leave(GameModeContext& ctx, GameModeExitReason reason) override
        {
            m_Routine.Leave(ctx, m_Data, reason);
        }

        const char* GetDebugName() const override
        {
            return Routine::DebugName;
        }

        /**
         * @brief データへのアクセス
         */
        Data &GetData()
        {
            return m_Data;
        }

        /**
         * @brief データへの読み取り専用アクセス
         */
        const Data &GetData() const
        {
            return m_Data;
        }

    protected:
        Routine m_Routine; // ゲームモードのロジック
        Data m_Data;       // ゲームモードのデータ
    };

} // namespace NorvesLib::Core::GameMode
