#pragma once

#include "AI/IAIModule.h"
#include "AI/Motor/LocomotionProfileCache.h"
#include "AI/Navigation/NavObstacleSystem.h"
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
        bool SetNavObstacleSettings(const NavObstacleSettings& settings) override;
        NavObstacleSettings GetNavObstacleSettings() const override;
        void SetNavDebugDrawEnabled(bool bEnabled) override;
        bool IsNavDebugDrawEnabled() const override;
        void SetSteeringDebugDrawEnabled(bool bEnabled) override;
        bool IsSteeringDebugDrawEnabled() const override;

        const Core::World* GetEngineWorld() const;
        void RegisterObstacle(NavObstacleComponent& obstacle);
        void UnregisterObstacle(NavObstacleComponent& obstacle);
        void RegisterAgent(SteeringAgentComponent& agent);
        void UnregisterAgent(SteeringAgentComponent& agent);
        uint32_t GetAgentCount() const;
        Core::Locomotion::QuadrupedLocomotionParams GetLocomotionProfile(Core::Container::AnsiStringView path);
        const Spatial::UniformGridHash& GetNeighborHash() const { return m_Steering.GetNeighborHash(); }
        /// GetNeighborHash() の添字 i の個体は GetNeighborEntities()[i] で参照する。
        /// 直近の固定歩の収集にだけ対応し、ハンドルが取れていない個体は無効のハンドルになる。
        Core::Container::Span<const Core::EntityHandle> GetNeighborEntities() const;

    private:
        bool IsOwnerThread() const;

        const Thread::Thread::ThreadId m_OwnerThreadId = Thread::Thread::GetCurrentThreadId();
        Core::Engine::Engine* m_Engine = nullptr;
        NavigationService m_Navigation;
        NavObstacleSystem m_Obstacles;
        NavObstacleSettings m_ObstacleSettings;
        LocomotionProfileCache m_LocomotionProfiles;
        SteeringSystem m_Steering;
        bool m_bInitialized = false;
#if NORVES_ENABLE_DEBUG_OUTPUT
        bool m_bNavDebugDrawEnabled = false;
        bool m_bSteeringDebugDrawEnabled = false;
#endif
    };
} // namespace NorvesLib::Modules::AI
