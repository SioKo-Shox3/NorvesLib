#pragma once

#include "Core/Public/Container/String.h"
#include "Core/Public/Container/VariableArray.h"
#include "Core/Public/Math/Vector3.h"

#include <cstdint>

namespace Game::Scripting
{
    class M6ScriptSmokeController
    {
    public:
        void Configure(const NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::String>& arguments);
        void Initialize();
        void Update();
        void Shutdown();

        [[nodiscard]] bool IsEnabled() const { return m_State != EState::Disabled; }

    private:
        enum class EState : uint8_t
        {
            Disabled,
            AwaitInitialMovement,
            AwaitGoodReload,
            AwaitV2StableMovement,
            AwaitBadCompile,
            Completed,
            Failed
        };

        void Fail(const char* reason);
        void Complete(const NorvesLib::Math::Vector3& position);
        bool ReadScriptHash(uint64_t& outHash) const;
        bool ResolveLoadedOwner(NorvesLib::Math::Vector3& outPosition) const;

        EState m_State = EState::Disabled;
        NorvesLib::Core::Container::String m_ResolvedScenePath;
        NorvesLib::Core::Container::String m_ResolvedScriptPath;
        uint64_t m_OwnerObjectId = 0;
        uint64_t m_BaseRuntimeGeneration = 0;
        uint64_t m_GoodRuntimeGeneration = 0;
        uint64_t m_ReadyBadScriptHash = 0;
        uint32_t m_BaseBindingCount = 0;
        uint32_t m_GoodBindingCount = 0;
        float m_InitialX = 0.0f;
        float m_InitialY = 0.0f;
        float m_PreReloadX = 0.0f;
        float m_GoodX = 0.0f;
        float m_ReadyBadX = 0.0f;
        float m_ReadyBadY = 0.0f;
        float m_BadObservationSeconds = 0.0f;
        bool m_bBadSourceObserved = false;
    };
} // namespace Game::Scripting
