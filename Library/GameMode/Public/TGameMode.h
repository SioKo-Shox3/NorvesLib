#pragma once

#include "IGameMode.h"
#include "IStateMachine.h"
#include "Core/Object/Reflection.h" // リフレクションマクロを使用する場合

namespace NorvesLib::GameMode
{
    // ゲームモードテンプレートクラス
    template<typename Routine, typename Data>
    class TGameMode : public IGameMode
    {
        // Core::Objectを継承する場合はREFLECTION_CLASSマクロを使用
        // REFLECTION_CLASS(TGameMode<Routine, Data>, IGameMode); // 必要に応じて親クラスを指定

    public:
        // コンストラクタ
        TGameMode() = default;
        virtual ~TGameMode() override = default;

        // IGameModeインターフェースの実装
        virtual void Enter(IStateMachine* proc) override
        {
            m_Routine.Enter(proc, m_Data);
        }

        virtual void Do(IStateMachine* proc, float deltaTime) override
        {
            m_Routine.Do(proc, m_Data, deltaTime);
        }

        virtual void Leave(IStateMachine* proc) override
        {
            m_Routine.Leave(proc, m_Data);
        }

        // データへのアクセス
        Data& GetData() { return m_Data; }
        const Data& GetData() const { return m_Data; }

    protected:
        Routine m_Routine; // ゲームモードのロジック
        Data m_Data;       // ゲームモードのデータ
    };

} // namespace NorvesLib::GameMode
