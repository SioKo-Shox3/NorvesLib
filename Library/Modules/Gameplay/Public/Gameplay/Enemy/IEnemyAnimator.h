#pragma once

#include "Animation/AnimationEvents.h"
#include "Delegate/Delegate.h"
#include "Text/IdentityPool.h"

namespace NorvesLib::Modules::Gameplay::Enemy
{
    /// 敵の状態が使うアニメーションの窓口。すべての操作と通知は GameThread 専用。
    class IEnemyAnimator
    {
    public:
        virtual ~IEnemyAnimator() = default;

        /// 状態変更を受理した場合だけ true を返す。
        virtual bool RequestState(Core::Identity state, float blendSeconds) = 0;
        /// 最後に受理した状態の正規化時間。切り替え待ちや取得不能なら 0。
        virtual double NormalizedTime() const = 0;
        /// 最後に受理した非ループ状態が終端に達した場合だけ true。
        virtual bool IsFinished() const = 0;
        /// 通知先を一つだけ保持する。空の Delegate で解除する。
        virtual void SetEventListener(Core::Delegate<void, const Core::Animation::AnimEventInfo&> listener) = 0;
    };
} // namespace NorvesLib::Modules::Gameplay::Enemy
