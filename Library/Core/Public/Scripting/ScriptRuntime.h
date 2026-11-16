#pragma once

#include "CoreTypes.h"

#include <cstdint>

namespace NorvesLib::Core
{
    class World;

    namespace Environment
    {
        class EnvironmentSystem;
    }

    namespace Component
    {
        class ScriptComponent;
    }

    enum class EScriptRuntimeResult : uint8_t
    {
        Success,
        AlreadyInitialized,
        NotInitialized,
        WrongThread,
        InvalidArgument,
        InvalidHandle,
        LoadFailed,
        CompileFailed,
        BindFailed,
        ExecutionFailed
    };

    struct ScriptBindingHandle
    {
        static constexpr uint32_t InvalidSlotIndex = ~uint32_t{0};
        uint32_t SlotIndex = InvalidSlotIndex;
        uint32_t Generation = 0;

        bool IsValid() const;
        void Reset();
    };

    struct ScriptRuntimeDiagnostics
    {
        uint64_t AllocationCount = 0;
        uint64_t FreeCount = 0;
        uint64_t LiveAllocationCount = 0;
        uint64_t GcStepCount = 0;
        uint64_t ReloadGeneration = 0;
        uint32_t ActiveBindingCount = 0;
        EScriptRuntimeResult LastResult = EScriptRuntimeResult::NotInitialized;
    };

    class ScriptRuntime final
    {
    public:
        ScriptRuntime();
        ~ScriptRuntime();
        ScriptRuntime(const ScriptRuntime&) = delete;
        ScriptRuntime& operator=(const ScriptRuntime&) = delete;
        ScriptRuntime(ScriptRuntime&&) = delete;
        ScriptRuntime& operator=(ScriptRuntime&&) = delete;

        EScriptRuntimeResult Initialize(World& world);
        /// スクリプトの天気の関数が操作する気象の正本を渡す。nullptr なら天気の Set は false、Get は 0 を返す。
        /// 気象は GameThread 専用で、Runtime と同じスレッドで使う。
        EScriptRuntimeResult Initialize(World& world, Environment::EnvironmentSystem* environment);
        EScriptRuntimeResult Shutdown();
        EScriptRuntimeResult BeginFrameMaintenance(float deltaSeconds);
        EScriptRuntimeResult EndFrameMaintenance();
        EScriptRuntimeResult BindComponent(Component::ScriptComponent& component, ScriptBindingHandle& outHandle);
        EScriptRuntimeResult UnbindComponent(ScriptBindingHandle& handle);
        EScriptRuntimeResult TickComponent(const ScriptBindingHandle& handle, float deltaSeconds);
        bool IsInitialized() const;
        const ScriptRuntimeDiagnostics& GetDiagnostics() const;

    private:
        class Impl;
        TUniquePtr<Impl> m_Impl;
    };
} // namespace NorvesLib::Core
