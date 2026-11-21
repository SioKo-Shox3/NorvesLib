#pragma once

#include "AI/IAIModule.h"
#include "AI/Motor/LocomotionProfileCache.h"
#include "AI/Navigation/NavigationService.h"
#include "AI/Steering/SteeringSystem.h"
#include "Container/Span.h"
#include "Debug/DebugConfig.h"
#include "Thread/Thread.h"

namespace NorvesLib::Modules::AI
{
    class SteeringAgentComponent;

    class AIModule final : public IAIModule
    {
    public:
        Core::Identity GetModuleId() const override;
        const char* GetName() const override;

        bool Install(Core::Engine::Engine& engine) override;
        void RegisterReflectedTypes() override;
        bool Initialize() override;
        // World の固定更新がモーターを動かす前に、この歩の操舵速度を確定する。
        void PreFixedTick(float fixedDeltaTime) override;
        void LateTick(float deltaTime) override;
        void Shutdown() override;
        void Uninstall(Core::Engine::Engine& engine) override;

        INavigationService& GetNavigation() override { return m_Navigation; }
        const INavigationService& GetNavigation() const override { return m_Navigation; }
        void SetNavDebugDrawEnabled(bool bEnabled) override;
        bool IsNavDebugDrawEnabled() const override;

        void RegisterAgent(SteeringAgentComponent& agent);
        void UnregisterAgent(SteeringAgentComponent& agent);
        uint32_t GetAgentCount() const;
        Core::Locomotion::QuadrupedLocomotionParams GetLocomotionProfile(Core::Container::AnsiStringView path);
        // 登録順に参照する。登録・解除・終了処理でビューは無効になる。
        Core::Container::Span<SteeringAgentComponent* const> GetAgents() const;
        const Spatial::UniformGridHash& GetNeighborHash() const { return m_Steering.GetNeighborHash(); }

    private:
        bool IsOwnerThread() const;

        const Thread::Thread::ThreadId m_OwnerThreadId = Thread::Thread::GetCurrentThreadId();
        Core::Engine::Engine* m_Engine = nullptr;
        NavigationService m_Navigation;
        LocomotionProfileCache m_LocomotionProfiles;
        SteeringSystem m_Steering;
        bool m_bInitialized = false;
#if NORVES_ENABLE_DEBUG_OUTPUT
        bool m_bNavDebugDrawEnabled = false;
#endif
    };
} // namespace NorvesLib::Modules::AI
