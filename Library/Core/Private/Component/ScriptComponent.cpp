#include "Component/ScriptComponent.h"

#include "Engine/NorvesEngine.h"
#include "Logging/LogMacros.h"

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

    void ScriptComponent::Tick(float deltaTime)
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
