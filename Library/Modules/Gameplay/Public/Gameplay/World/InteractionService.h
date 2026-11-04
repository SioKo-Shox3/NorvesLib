#pragma once

#include "Container/PointerTypes.h"
#include "Delegate/MulticastDelegate.h"
#include "Gameplay/World/InteractionTypes.h"
#include "Thread/Thread.h"

namespace NorvesLib::Core
{
    class World;
}

namespace NorvesLib::Modules::Gameplay::World
{
    /// 作成した GameThread 専用。センサーは登録が最も早い生存中の一つだけを使う。
    class InteractionService
    {
    public:
        InteractionService();
        ~InteractionService();
        InteractionService(const InteractionService&) = delete;
        InteractionService& operator=(const InteractionService&) = delete;

        void RequestRegisterInteractable(Core::EntityHandle entity);
        void RequestUnregisterInteractable(Core::EntityHandle entity);
        void RequestRegisterSensor(Core::EntityHandle entity);
        void RequestUnregisterSensor(Core::EntityHandle entity);
        void NotifyInteractableUnavailable(Core::EntityHandle entity);

        void SetInteractPressedSource(Core::Delegate<bool> source);
        void SetEngagedQuery(Core::Delegate<bool, Core::EntityHandle> query);
        void SetMouthFreeQuery(Core::Delegate<bool, Core::EntityHandle> query);

        /// 呼び出し一回が一フレーム。評価と通知への再入は拒否する。
        void Evaluate(Core::World& world);
        const InteractionPrompt& GetCurrentPrompt() const;
        /// 購読と入力・照会を保ち、通知せず実行状態を消して次回拾い直す。
        void Reset();

        Core::MulticastDelegate<const InteractionPrompt&, const InteractionPrompt&> OnPromptChanged;
        Core::MulticastDelegate<const Core::Container::String&, uint64_t> OnInteractionTriggered;

    private:
        friend class ZoneSensorComponent;
        struct State;
        void RequestRegistration(Core::EntityHandle entity, bool bInteractable, bool bRegister);
        void RequestRegisterSensorComponent(Core::EntityHandle entity, uint64_t componentId);
        void RequestUnregisterSensorComponent(Core::EntityHandle entity, uint64_t componentId);
        bool IsOwnerThread() const;

        Thread::Thread::ThreadId m_OwnerThreadId;
        Core::Container::TUniquePtr<State> m_State;
        const InteractionPrompt m_EmptyPrompt;
        Core::Delegate<bool> m_InteractPressedSource;
        Core::Delegate<bool, Core::EntityHandle> m_EngagedQuery;
        Core::Delegate<bool, Core::EntityHandle> m_MouthFreeQuery;
        uint64_t m_ResetGeneration = 0;
        bool m_bEvaluating = false;
        bool m_bNotifying = false;
    };
} // namespace NorvesLib::Modules::Gameplay::World
