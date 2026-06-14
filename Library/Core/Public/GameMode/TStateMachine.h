#pragma once

#include "IStateMachine.h"
#include "Container/PointerTypes.h"
#include <utility> // std::move

namespace NorvesLib::Core::GameMode
{

    /**
     * @brief テンプレートステートマシンクラス
     *
     * ステート型とファクトリ型をテンプレート引数として受け取り、
     * ステートの遷移と更新を管理します。
     *
     * @tparam StateType ステートの型（IGameMode派生）
     * @tparam FactoryType ファクトリの型
     */
    template <typename StateType, typename FactoryType>
    class TStateMachine : public IStateMachine
    {
    public:
        using StatePtr = Container::TUniquePtr<StateType>;

        TStateMachine() = default;
        virtual ~TStateMachine() override
        {
            Shutdown();
        }

        /**
         * @brief ステートマシンを明示的にシャットダウン（冪等）
         *
         * 現在のステートに Leave を呼んでから解放し、予約済みステートも破棄する。
         * 2回目以降の呼び出しおよびデストラクタからの呼び出しでは何もしない。
         */
        virtual void Shutdown() override
        {
            if (m_bShutdown)
            {
                return;
            }
            m_bShutdown = true;

            if (m_CurrentState)
            {
                m_CurrentState->Leave(this);
                m_CurrentState.reset();
            }
            m_NextState.reset();
        }

        /**
         * @brief 次のステートを予約
         * @tparam T ステートの具象型
         * @param nextState 次のステート
         */
        template <typename T>
        void ReserveState(Container::TUniquePtr<T> nextState)
        {
            m_NextState = std::move(nextState);
        }

        /**
         * @brief ステートマシンを更新
         * @param deltaTime フレーム間隔（秒）
         */
        virtual void Update(float deltaTime) override
        {
            m_DeltaTime = deltaTime; // デルタタイムを更新

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

        /**
         * @brief 現在のステートを取得
         * @return 現在のステートへのポインタ
         */
        StateType *GetCurrentState() const
        {
            return m_CurrentState.get();
        }

        /**
         * @brief ファクトリへのアクセス
         */
        FactoryType &GetFactory()
        {
            return m_Factory;
        }

        /**
         * @brief ファクトリへの読み取り専用アクセス
         */
        const FactoryType &GetFactory() const
        {
            return m_Factory;
        }

        // IStateMachineのインターフェース実装
        virtual void *GetFactoryImpl() const override
        {
            return const_cast<FactoryType *>(&m_Factory);
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
        bool m_bShutdown = false;          // Shutdown済みフラグ（二重Leave防止）
    };

} // namespace NorvesLib::Core::GameMode
