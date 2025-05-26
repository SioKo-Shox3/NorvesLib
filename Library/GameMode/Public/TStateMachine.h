#pragma once

#include "IStateMachine.h"
#include "Core/Public/Container/PointerTypes.h"
#include <utility> // std::move

namespace NorvesLib::GameMode
{
    // テンプレートステートマシンクラス
    template<typename StateType, typename FactoryType>
    class TStateMachine : public IStateMachine
    {
    public:
        using StatePtr = Core::Container::TUniquePtr<StateType>;

        TStateMachine() = default;
        virtual ~TStateMachine() override = default;

        // 次のステートを予約
        template<typename T>
        void ReserveState(Core::Container::TUniquePtr<T> nextState)
        {
            m_NextState = std::move(nextState);
        }

        // ステートマシンを更新
        void Update(float deltaTime)
        {
            m_DeltaTime = deltaTime;  // デルタタイムを更新
            
            // 予約されたステートがあれば遷移を実行
            if (m_NextState)
            {
                if (m_CurrentState)
                {
                    // StateMachine自身を引数として渡す
                    m_CurrentState->Leave(this);
                }
                m_CurrentState = std::move(m_NextState);
                if (m_CurrentState)
                {
                    m_CurrentState->Enter(this);
                }
            }

            // 現在のステートを実行
            if (m_CurrentState)
            {
                m_CurrentState->Do(this, deltaTime);
            }
        }

        // 現在のステートを取得
        StateType* GetCurrentState() const
        {
            return m_CurrentState.get();
        }

        // ファクトリへのアクセス
        FactoryType& GetFactory() { return m_Factory; }
        const FactoryType& GetFactory() const { return m_Factory; }
        
        // IStateMachineのインターフェース実装
        virtual void* GetFactoryImpl() const override
        {
            return const_cast<FactoryType*>(&m_Factory);
        }
        
        virtual float GetDeltaTime() const override
        {
            return m_DeltaTime;
        }

    private:
        StatePtr m_CurrentState = nullptr; // 現在のステート
        StatePtr m_NextState = nullptr;    // 次に遷移する予定のステート
        FactoryType m_Factory;             // ステートを生成するファクトリ
        float m_DeltaTime = 0.0f;          // 最新のデルタタイム
    };

    // IStateMachineのテンプレート関数の実装
    template<typename T>
    void IStateMachine::ReserveState(Core::Container::TUniquePtr<T> nextState)
    {
        // この実装は派生クラスで上書きされる
        // 何もしないデフォルト実装
    }

} // namespace NorvesLib::GameMode
