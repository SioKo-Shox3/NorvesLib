#include "Scripting/AngelScriptEngineOwner.h"

#include "EngineGlobals/MemoryOverrides.h"

#include <cstdlib>

namespace NorvesLib::Core::Scripting
{
    namespace
    {
        struct AngelScriptAllocatorState
        {
            uint64_t AllocationCount = 0;
            uint64_t FreeCount = 0;
            asIScriptEngine* Engine = nullptr;
            bool bActive = false;
        };

        AngelScriptAllocatorState GAngelScriptAllocatorState;

        void* AngelScriptAllocate(size_t size)
        {
            void* memory = Memory::Malloc(size);
            if (memory != nullptr)
            {
                ++GAngelScriptAllocatorState.AllocationCount;
            }

            return memory;
        }

        void AngelScriptFree(void* memory)
        {
            if (memory != nullptr)
            {
                ++GAngelScriptAllocatorState.FreeCount;
            }

            Memory::Free(memory);
        }

        void UpdateAllocationDiagnostics(ScriptRuntimeDiagnostics& diagnostics)
        {
            diagnostics.AllocationCount = GAngelScriptAllocatorState.AllocationCount;
            diagnostics.FreeCount = GAngelScriptAllocatorState.FreeCount;
            diagnostics.LiveAllocationCount = diagnostics.AllocationCount >= diagnostics.FreeCount
                ? diagnostics.AllocationCount - diagnostics.FreeCount
                : 0;
        }
    }

    asIScriptEngine* CreateDefaultScriptEngine()
    {
        return asCreateScriptEngine();
    }

    asIScriptEngine* GetActiveAngelScriptEngine()
    {
        return GAngelScriptAllocatorState.Engine;
    }

    AngelScriptEngineOwner::AngelScriptEngineOwner() = default;

    AngelScriptEngineOwner::~AngelScriptEngineOwner()
    {
        if (m_bOwnsAllocator && !Shutdown())
        {
            std::abort();
        }
    }

    bool AngelScriptEngineOwner::Initialize(ScriptEngineFactory factory, ScriptEngineValidator validator)
    {
        if (m_Engine != nullptr || GAngelScriptAllocatorState.bActive || factory == nullptr)
        {
            return false;
        }

        GAngelScriptAllocatorState = {};
        GAngelScriptAllocatorState.bActive = true;
        m_Diagnostics = {};

        if (asSetGlobalMemoryFunctions(&AngelScriptAllocate, &AngelScriptFree) < 0)
        {
            GAngelScriptAllocatorState.bActive = false;
            asResetGlobalMemoryFunctions();
            return false;
        }
        m_bOwnsAllocator = true;

        m_Engine = factory();
        if (m_Engine == nullptr)
        {
            asResetGlobalMemoryFunctions();
            UpdateAllocationDiagnostics(m_Diagnostics);
            GAngelScriptAllocatorState.bActive = false;
            m_bOwnsAllocator = false;
            return false;
        }

        GAngelScriptAllocatorState.Engine = m_Engine;
        if (validator != nullptr && !validator(m_Engine))
        {
            const int remainingReferences = m_Engine->ShutDownAndRelease();
            if (remainingReferences != 0)
            {
                m_Engine->AddRef();
                m_bShutdownPending = true;
                UpdateAllocationDiagnostics(m_Diagnostics);
                m_Diagnostics.LastResult = EScriptRuntimeResult::ExecutionFailed;
                return false;
            }

            m_Engine = nullptr;
            GAngelScriptAllocatorState.Engine = nullptr;
            asResetGlobalMemoryFunctions();
            UpdateAllocationDiagnostics(m_Diagnostics);
            m_Diagnostics.LastResult = EScriptRuntimeResult::LoadFailed;
            GAngelScriptAllocatorState.bActive = false;
            m_bOwnsAllocator = false;
            return false;
        }

        m_Diagnostics.LastResult = EScriptRuntimeResult::Success;
        return true;
    }

    bool AngelScriptEngineOwner::Shutdown()
    {
        if (!m_bOwnsAllocator)
        {
            return false;
        }

        if (m_Engine != nullptr)
        {
            const int remainingReferences = m_bShutdownPending
                ? m_Engine->Release()
                : m_Engine->ShutDownAndRelease();
            if (remainingReferences != 0)
            {
                m_Engine->AddRef();
                m_bShutdownPending = true;
                UpdateAllocationDiagnostics(m_Diagnostics);
                m_Diagnostics.LastResult = EScriptRuntimeResult::ExecutionFailed;
                return false;
            }

            m_Engine = nullptr;
        }

        asResetGlobalMemoryFunctions();
        UpdateAllocationDiagnostics(m_Diagnostics);
        m_Diagnostics.ActiveBindingCount = 0;
        m_Diagnostics.LastResult = EScriptRuntimeResult::Success;
        GAngelScriptAllocatorState.Engine = nullptr;
        GAngelScriptAllocatorState.bActive = false;
        m_bOwnsAllocator = false;
        m_bShutdownPending = false;
        return true;
    }

    bool AngelScriptEngineOwner::IsInitialized() const
    {
        return m_Engine != nullptr;
    }

    bool AngelScriptEngineOwner::OwnsGlobalAllocator() const
    {
        return m_bOwnsAllocator && GAngelScriptAllocatorState.bActive;
    }

    asIScriptEngine* AngelScriptEngineOwner::GetEngine() const
    {
        return m_Engine;
    }

    const ScriptRuntimeDiagnostics& AngelScriptEngineOwner::GetDiagnostics() const
    {
        return m_Diagnostics;
    }

    void AngelScriptEngineOwner::SetLastResult(EScriptRuntimeResult result)
    {
        m_Diagnostics.LastResult = result;
    }
} // namespace NorvesLib::Core::Scripting
