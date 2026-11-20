#include "AI/AIModule.h"

#include "Asset/AssetFileReader.h"
#include "Logging/LogMacros.h"

#if NORVES_ENABLE_DEBUG_OUTPUT
#include "AI/Navigation/NavDebugDraw.h"
#include "Engine/Engine.h"
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
        // 現在の AI モジュールには登録するリフレクション型がない。
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

    void AIModule::PreFixedTick(float)
    {
        if (IsOwnerThread() && m_bInitialized)
        {
            m_Navigation.RunBudget();
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

    void AIModule::LateTick(float)
    {
#if NORVES_ENABLE_DEBUG_OUTPUT
        if (!IsNavDebugDrawEnabled() || !m_bInitialized)
        {
            return;
        }
        const auto& camera = m_Engine->GetRenderWorld().GetRenderingCoordinator().GetMainCamera();
        const Math::Vector3 center(camera.PositionX, camera.PositionY, camera.PositionZ);
        auto& queue = Core::GEngine.GetDebugDraw();
        SubmitNavGridDebug(m_Navigation.GetGrid(), center, {}, queue);

        const Math::Vector4 green(0.0f, 1.0f, 0.0f, 1.0f);
        const Math::Vector4 yellow(1.0f, 1.0f, 0.0f, 1.0f);
        for (uint32_t i = 0; i < m_Navigation.GetPathSlotCount(); ++i)
        {
            const auto status = m_Navigation.GetSlotStatus(i);
            if (status == ENavPathStatus::Succeeded || status == ENavPathStatus::Partial)
            {
                SubmitNavPathDebug(m_Navigation.GetSlotPath(i), status == ENavPathStatus::Partial ? yellow : green, queue);
            }
        }
#endif
    }

    void AIModule::Shutdown()
    {
        if (IsOwnerThread())
        {
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
        if (m_bInitialized)
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
