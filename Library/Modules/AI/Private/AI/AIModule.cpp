#include "AI/AIModule.h"

#include "AI/Navigation/NavObstacleComponent.h"
#include "AI/Steering/SteeringAgentComponent.h"
#include "Asset/AssetFileReader.h"
#include "Engine/Engine.h"
#include "Logging/LogMacros.h"
#include "Object/IClass.h"

#if NORVES_ENABLE_DEBUG_OUTPUT
#include "AI/Navigation/NavDebugDraw.h"
#include "AI/Steering/SteeringDebugDraw.h"
#include "Engine/NorvesEngine.h"
#include "Rendering/RenderWorld.h"
#include "Rendering/RenderingCoordinator.h"
#endif

#include <utility>

namespace NorvesLib::Modules::AI
{
    namespace
    {
        constexpr const char* kAIModuleName = "NorvesAIModule";
    } // namespace

    Core::Identity AIModule::GetModuleId() const
    {
        return Core::Identity(kAIModuleName);
    }

    const char* AIModule::GetName() const
    {
        return kAIModuleName;
    }

    bool AIModule::IsOwnerThread() const
    {
        return m_OwnerThreadId == Thread::Thread::GetCurrentThreadId();
    }

    bool AIModule::Install(Core::Engine::Engine& engine)
    {
        if (!IsOwnerThread() || m_Engine != nullptr)
        {
            return false;
        }
        m_Engine = &engine;
        return true;
    }

    void AIModule::RegisterReflectedTypes()
    {
        Core::ClassRegistry::Get().RegisterClass(SteeringAgentComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(NavObstacleComponent::StaticClass());
    }

    const Core::World* AIModule::GetEngineWorld() const
    {
        return m_Engine ? &m_Engine->GetWorld() : nullptr;
    }

    void AIModule::RegisterObstacle(NavObstacleComponent& obstacle)
    {
        if (IsOwnerThread())
        {
            m_Obstacles.Register(obstacle);
        }
    }

    void AIModule::UnregisterObstacle(NavObstacleComponent& obstacle)
    {
        if (IsOwnerThread())
        {
            m_Obstacles.Unregister(obstacle, m_Navigation);
        }
    }

    bool AIModule::SetNavObstacleSettings(const NavObstacleSettings& settings)
    {
        if (!IsOwnerThread() || !settings.IsValid())
        {
            return false;
        }
        if (m_ObstacleSettings != settings)
        {
            m_ObstacleSettings = settings;
            m_Obstacles.MarkAllForRestamp();
        }
        return true;
    }

    NavObstacleSettings AIModule::GetNavObstacleSettings() const
    {
        return IsOwnerThread() ? m_ObstacleSettings : NavObstacleSettings{};
    }

    void AIModule::RegisterAgent(SteeringAgentComponent& agent)
    {
        if (IsOwnerThread())
        {
            m_Steering.RegisterAgent(agent);
        }
    }

    void AIModule::UnregisterAgent(SteeringAgentComponent& agent)
    {
        if (IsOwnerThread())
        {
            m_Steering.UnregisterAgent(agent, m_Navigation);
        }
    }

    uint32_t AIModule::GetAgentCount() const
    {
        return IsOwnerThread() ? m_Steering.GetAgentCount() : 0u;
    }

    Core::Container::Span<const Core::EntityHandle> AIModule::GetNeighborEntities() const
    {
        if (!IsOwnerThread())
        {
            return {};
        }
        return m_Steering.GetNeighborEntities();
    }

    Core::Locomotion::QuadrupedLocomotionParams AIModule::GetLocomotionProfile(Core::Container::AnsiStringView path)
    {
        return IsOwnerThread() ? m_LocomotionProfiles.Get(path) : Core::Locomotion::QuadrupedLocomotionParams{};
    }

    bool AIModule::Initialize()
    {
        if (!IsOwnerThread() || m_Engine == nullptr)
        {
            return false;
        }
        if (m_bInitialized)
        {
            return true;
        }

        NavAgentProfileSet profiles;
        const auto asset = Core::Asset::AssetFileReader().Read("AI/NavProfiles.json");
        if (asset.Succeeded())
        {
            Core::Container::String text;
            if (asset.Blob.GetSize() != 0)
            {
                text.append(reinterpret_cast<const char*>(asset.Blob.GetData()), asset.Blob.GetSize());
            }
            if (!profiles.LoadFromJson(text))
            {
                NORVES_LOG_WARNING("AI", "AI/NavProfiles.json の内容が不正なため、組み込みのナビプロファイルを使います。");
            }
        }
        else
        {
            NORVES_LOG_WARNING("AI", "AI/NavProfiles.json を読み込めないため、組み込みのナビプロファイルを使います。");
        }
        m_Navigation.SetProfiles(std::move(profiles));
        m_bInitialized = true;
        return true;
    }

    void AIModule::PreFixedTick(float dt)
    {
        if (IsOwnerThread() && m_bInitialized)
        {
            m_Obstacles.Step(dt, m_Navigation, m_Engine->GetWorld(), m_ObstacleSettings);
            const auto stepAgents = m_Steering.BeginStep(m_Navigation, m_Engine->GetWorld());
            m_Navigation.RunBudget();
            m_Steering.Compute(dt, m_Navigation, stepAgents);
        }
    }

    void AIModule::SetNavDebugDrawEnabled(bool bEnabled)
    {
#if NORVES_ENABLE_DEBUG_OUTPUT
        if (IsOwnerThread())
        {
            m_bNavDebugDrawEnabled = bEnabled;
        }
#else
        (void)bEnabled;
#endif
    }

    bool AIModule::IsNavDebugDrawEnabled() const
    {
#if NORVES_ENABLE_DEBUG_OUTPUT
        return IsOwnerThread() && m_bNavDebugDrawEnabled;
#else
        return false;
#endif
    }

    void AIModule::SetSteeringDebugDrawEnabled(bool bEnabled)
    {
#if NORVES_ENABLE_DEBUG_OUTPUT
        if (IsOwnerThread())
        {
            m_bSteeringDebugDrawEnabled = bEnabled;
        }
#else
        (void)bEnabled;
#endif
    }

    bool AIModule::IsSteeringDebugDrawEnabled() const
    {
#if NORVES_ENABLE_DEBUG_OUTPUT
        return IsOwnerThread() && m_bSteeringDebugDrawEnabled;
#else
        return false;
#endif
    }

    void AIModule::LateTick(float)
    {
        if (!IsOwnerThread() || !m_bInitialized)
        {
            return;
        }
        const auto& world = m_Engine->GetWorld();
        for (uint32_t i = 0; i < m_Steering.GetAgentCount(); ++i)
        {
            auto* agent = m_Steering.ResolveAgent(i, world);
            if (agent && SteeringSystem::CanSteer(*agent))
            {
                agent->PublishMotorVisual();
            }
        }

#if NORVES_ENABLE_DEBUG_OUTPUT
        if (!m_bNavDebugDrawEnabled && !m_bSteeringDebugDrawEnabled)
        {
            return;
        }
        const auto& camera = m_Engine->GetRenderWorld().GetRenderingCoordinator().GetMainCamera();
        const Math::Vector3 center(camera.PositionX, camera.PositionY, camera.PositionZ);
        auto& queue = Core::GEngine.GetDebugDraw();
        if (m_bNavDebugDrawEnabled)
        {
            const NavDebugDrawSettings settings{};
            SubmitNavGridDebug(m_Navigation.GetGrid(), center, settings, queue);
            const Math::Vector4 green(0.0f, 1.0f, 0.0f, 1.0f);
            const Math::Vector4 yellow(1.0f, 1.0f, 0.0f, 1.0f);
            for (uint32_t i = 0; i < m_Navigation.GetPathSlotCount(); ++i)
            {
                const auto status = m_Navigation.GetSlotStatus(i);
                if (status == ENavPathStatus::Succeeded || status == ENavPathStatus::Partial)
                {
                    SubmitNavPathDebug(m_Navigation.GetSlotPath(i), center, settings,
                                       status == ENavPathStatus::Partial ? yellow : green, queue);
                }
            }
        }
        if (m_bSteeringDebugDrawEnabled)
        {
            SubmitSteeringDebug(m_Steering, world, center, {}, queue);
        }
#endif
    }

    void AIModule::Shutdown()
    {
        if (IsOwnerThread())
        {
            m_Steering.Reset(m_Navigation, GetEngineWorld());
            m_Obstacles.Reset(m_Navigation);
            m_LocomotionProfiles.Clear();
            m_Navigation.Reset();
            m_bInitialized = false;
        }
    }

    void AIModule::Uninstall(Core::Engine::Engine&)
    {
        if (!IsOwnerThread())
        {
            return;
        }
        if (m_bInitialized || m_Steering.GetAgentCount() != 0 || m_Obstacles.HasRegisteredObstacles())
        {
            Shutdown();
        }
        m_Engine = nullptr;
    }

    IAIModule* RegisterAIModule(Core::Module::ModuleRegistry& registry)
    {
        if (auto* existing = FindAIModule(registry))
        {
            return existing;
        }
        return dynamic_cast<IAIModule*>(registry.Register(Core::Container::MakeUnique<AIModule>()));
    }

    IAIModule* FindAIModule(Core::Module::ModuleRegistry& registry)
    {
        return dynamic_cast<IAIModule*>(registry.FindModule(Core::Identity(kAIModuleName)));
    }
} // namespace NorvesLib::Modules::AI
