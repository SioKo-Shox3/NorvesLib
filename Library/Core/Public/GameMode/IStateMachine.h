#pragma once

#include "Container/PointerTypes.h"

namespace NorvesLib::Core::GameMode
{
    // 前方宣言
    class IFactory;

    /**
     * @brief ステートマシンインターフェース
     *
     * ゲームモードの状態遷移を管理するステートマシンの基底インターフェース。
     */
    class IStateMachine
    {
    public:
        // 仮想デストラクタ
        virtual ~IStateMachine() = default;

        /**
         * @brief ステートマシンを明示的にシャットダウン
         *
         * 現在のステートに対して Leave を呼び、保持しているステートを解放する。
         * 冪等であり複数回呼ばれても安全。エンジンの破棄順序を制御するため、
         * デストラクタより前に外部から明示的に呼び出されることを想定する。
         */
        virtual void Shutdown() = 0;

        /**
         * @brief ステートマシンを更新
         * @param deltaTime フレーム間隔（秒）
         */
        virtual void Update(float deltaTime) = 0;

        /**
         * @brief ファクトリを取得（内部実装用）
         * @return ファクトリへのvoid*ポインタ
         */
        virtual void *GetFactoryImpl() const = 0;

        /**
         * @brief 最新のデルタタイムを取得
         * @return デルタタイム（秒）
         */
        virtual float GetDeltaTime() const = 0;

        /**
         * @brief ファクトリを型安全に取得するためのテンプレートメソッド
         * @tparam FactoryType ファクトリの型
         * @return ファクトリへの参照
         */
        template <typename FactoryType>
        FactoryType &GetFactory() const
        {
            return *static_cast<FactoryType *>(GetFactoryImpl());
        }
    };

} // namespace NorvesLib::Core::GameMode
