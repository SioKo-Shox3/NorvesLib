#include "Component/ScriptComponent.h"

#include "Ecs/EcsTypes.h"
#include "Engine/NorvesEngine.h"
#include "Logging/LogMacros.h"
#include "Object/World.h"

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(ScriptComponent, Component)

    ScriptComponent::ScriptComponent() = default;

    ScriptComponent::~ScriptComponent()
    {
        ReleaseBinding();
    }

    void ScriptComponent::BeginPlay()
    {
        Component::BeginPlay();
        if (!bBegunPlay || m_BindingHandle.IsValid())
        {
            return;
        }

        ScriptRuntime& runtime = GEngine.GetScriptRuntime();
        if (!runtime.IsInitialized())
        {
            return;
        }

        const EScriptRuntimeResult result = runtime.BindComponent(*this, m_BindingHandle);
        if (result != EScriptRuntimeResult::Success)
        {
            NORVES_LOG_WARNING("Scripting", "ScriptComponent bind failed with result %u", static_cast<uint32_t>(result));
        }
    }

    void ScriptComponent::EndPlay()
    {
        ReleaseBinding();
        Component::EndPlay();
    }

    void ScriptComponent::RegisterTick(World& world)
    {
        // 主群（GetTickGroup）だけで呼ぶ。群・mask・優先度は Component の設定のまま。
        // スクリプトは姿勢を読み書きする。段の最中に起きた生成・破棄は、Object は即時、行は段の終わりの同期点で作られる
        ComponentTickRegistration script;
        script.Name = "Script.Tick";
        script.Run = [](Component& component, float deltaTime)
        { static_cast<ScriptComponent&>(component).TickScript(deltaTime); };
        script.Reads.push_back(Ecs::EcsColumnInfoOf<Ecs::EcsWorldTransform>().Type);
        script.Writes.push_back(Ecs::EcsColumnInfoOf<Ecs::EcsLocalTransform>().Type);
        world.RegisterComponentTick(StaticClass(), script);
    }

    void ScriptComponent::TickScript(float deltaTime)
    {
        if (!m_BindingHandle.IsValid())
        {
            return;
        }

        ScriptRuntime& runtime = GEngine.GetScriptRuntime();
        if (runtime.IsInitialized())
        {
            runtime.TickComponent(m_BindingHandle, deltaTime);
        }
    }

    void ScriptComponent::Finalize()
    {
        if (bBegunPlay)
        {
            EndPlay();
        }
        else
        {
            ReleaseBinding();
        }

        Component::Finalize();
    }

    void ScriptComponent::ReleaseBinding()
    {
        if (!m_BindingHandle.IsValid())
        {
            return;
        }

        ScriptRuntime& runtime = GEngine.GetScriptRuntime();
        if (runtime.IsInitialized())
        {
            runtime.UnbindComponent(m_BindingHandle);
        }

        m_BindingHandle.Reset();
    }
} // namespace NorvesLib::Core::Component
