#pragma once

#include "Core/Object/IUnknown.h" // IUnknownをインクルード

namespace NorvesLib::GameMode
{
    class IStateMachine; // 前方宣言を追加

    // ゲームモードインターフェース
    class IGameMode : public Core::IUnknown
    {
    public:
        // 仮想デストラクタ
        virtual ~IGameMode() = default;

        // ステートに入ったときに呼ばれる
        virtual void Enter(IStateMachine* proc) = 0; // 引数を IStateMachine* に変更

        // ステート実行中に毎フレーム呼ばれる
        virtual void Do(IStateMachine* proc, float deltaTime) = 0; // 引数を IStateMachine* に変更

        // ステートから出るときに呼ばれる
        virtual void Leave(IStateMachine* proc) = 0; // 引数を IStateMachine* に変更
    };

} // namespace NorvesLib::GameMode
