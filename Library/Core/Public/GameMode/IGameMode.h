#pragma once

namespace NorvesLib::Core::GameMode
{
    class IStateMachine; // 前方宣言

    /**
     * @brief ゲームモードインターフェース
     *
     * ステートマシンで管理される各ゲームモード（タイトル、ゲームプレイ等）の
     * 基底インターフェース。
     */
    class IGameMode
    {
    public:
        // 仮想デストラクタ
        virtual ~IGameMode() = default;

        /**
         * @brief ステートに入ったときに呼ばれる
         * @param proc ステートマシンへのポインタ
         */
        virtual void Enter(IStateMachine *proc) = 0;

        /**
         * @brief ステート実行中に毎フレーム呼ばれる
         * @param proc ステートマシンへのポインタ
         * @param deltaTime フレーム間隔（秒）
         */
        virtual void Do(IStateMachine *proc, float deltaTime) = 0;

        /**
         * @brief ステートから出るときに呼ばれる
         * @param proc ステートマシンへのポインタ
         */
        virtual void Leave(IStateMachine *proc) = 0;
    };

} // namespace NorvesLib::Core::GameMode
