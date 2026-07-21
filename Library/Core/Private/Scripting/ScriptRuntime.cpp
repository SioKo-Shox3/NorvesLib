#include "Scripting/ScriptRuntime.h"

#include "Scripting/AngelScriptEngineOwner.h"

#include <cstdlib>
#include <new>

namespace NorvesLib::Core
{
    class ScriptRuntime::Impl
    {
    public:
        static void* operator new(size_t size)
        {
            void* memory = std::malloc(size);
            if (memory == nullptr)
            {
                throw std::bad_alloc();
            }
            return memory;
        }

        static void operator delete(void* memory) noexcept
        {
            std::free(memory);
        }

        static void operator delete(void* memory, size_t) noexcept
        {
            std::free(memory);
        }

        Scripting::AngelScriptEngineOwner m_EngineOwner;
        World* m_World = nullptr;
    };

    bool ScriptBindingHandle::IsValid() const
    {
        return SlotIndex != InvalidSlotIndex;
    }

    void ScriptBindingHandle::Reset()
    {
        SlotIndex = InvalidSlotIndex;
        Generation = 0;
    }

    ScriptRuntime::ScriptRuntime()
        : m_Impl(MakeUnique<Impl>())
    {
    }

    ScriptRuntime::~ScriptRuntime() = default;

    EScriptRuntimeResult ScriptRuntime::Initialize(World& world)
    {
        if (m_Impl->m_EngineOwner.IsInitialized())
        {
            return EScriptRuntimeResult::AlreadyInitialized;
        }

        if (!m_Impl->m_EngineOwner.Initialize())
        {
            m_Impl->m_EngineOwner.SetLastResult(EScriptRuntimeResult::LoadFailed);
            return EScriptRuntimeResult::LoadFailed;
        }

        m_Impl->m_World = &world;
        m_Impl->m_EngineOwner.SetLastResult(EScriptRuntimeResult::Success);
        return EScriptRuntimeResult::Success;
    }

    EScriptRuntimeResult ScriptRuntime::Shutdown()
    {
        if (!m_Impl->m_EngineOwner.IsInitialized())
        {
            return EScriptRuntimeResult::NotInitialized;
        }

        if (!m_Impl->m_EngineOwner.Shutdown())
        {
            return EScriptRuntimeResult::ExecutionFailed;
        }

        m_Impl->m_World = nullptr;
        return EScriptRuntimeResult::Success;
    }

    EScriptRuntimeResult ScriptRuntime::BeginFrameMaintenance(float deltaSeconds)
    {
        (void)deltaSeconds;

        if (!m_Impl->m_EngineOwner.IsInitialized())
        {
            return EScriptRuntimeResult::NotInitialized;
        }

        return EScriptRuntimeResult::Success;
    }

    EScriptRuntimeResult ScriptRuntime::EndFrameMaintenance()
    {
        if (!m_Impl->m_EngineOwner.IsInitialized())
        {
            return EScriptRuntimeResult::NotInitialized;
        }

        return EScriptRuntimeResult::Success;
    }

    EScriptRuntimeResult ScriptRuntime::BindComponent(
        Component::ScriptComponent& component,
        ScriptBindingHandle& outHandle)
    {
        (void)component;
        (void)outHandle;

        if (!m_Impl->m_EngineOwner.IsInitialized())
        {
            return EScriptRuntimeResult::NotInitialized;
        }

        return EScriptRuntimeResult::BindFailed;
    }

    EScriptRuntimeResult ScriptRuntime::UnbindComponent(ScriptBindingHandle& handle)
    {
        (void)handle;

        if (!m_Impl->m_EngineOwner.IsInitialized())
        {
            return EScriptRuntimeResult::NotInitialized;
        }

        return EScriptRuntimeResult::InvalidHandle;
    }

    EScriptRuntimeResult ScriptRuntime::TickComponent(const ScriptBindingHandle& handle, float deltaSeconds)
    {
        (void)deltaSeconds;

        if (!m_Impl->m_EngineOwner.IsInitialized())
        {
            return EScriptRuntimeResult::NotInitialized;
        }

        return handle.IsValid() ? EScriptRuntimeResult::BindFailed : EScriptRuntimeResult::InvalidHandle;
    }

    bool ScriptRuntime::IsInitialized() const
    {
        return m_Impl->m_EngineOwner.IsInitialized();
    }

    const ScriptRuntimeDiagnostics& ScriptRuntime::GetDiagnostics() const
    {
        return m_Impl->m_EngineOwner.GetDiagnostics();
    }
} // namespace NorvesLib::Core
