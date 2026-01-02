#pragma once

#include "IGameMode.h"
#include "IStateMachine.h"

namespace NorvesLib::Core::GameMode
{

    /**
     * @brief ゲームモードテンプレートクラス
     *
     * ロジック（Routine）とデータ（Data）を分離したゲームモードの実装。
     *
     * @tparam Routine ゲームモードのロジッククラス（Enter, Do, Leaveメソッドを持つ）
     * @tparam Data ゲームモードのデータクラス
     */
    template <typename Routine, typename Data>
    class TGameMode : public IGameMode
    {
    public:
        // コンストラクタ
        TGameMode() = default;
        virtual ~TGameMode() override = default;

        // IGameModeインターフェースの実装
        virtual void Enter(IStateMachine *proc) override
        {
            m_Routine.Enter(proc, m_Data);
        }

        virtual void Do(IStateMachine *proc, float deltaTime) override
        {
            m_Routine.Do(proc, m_Data, deltaTime);
        }

        virtual void Leave(IStateMachine *proc) override
        {
            m_Routine.Leave(proc, m_Data);
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
