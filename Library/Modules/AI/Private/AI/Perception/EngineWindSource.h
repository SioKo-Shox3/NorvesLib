// 環境から解決した風を知覚に供給する。
#pragma once

#include "AI/Ports/IWindSource.h"
#include "Rendering/Wind.h"

#include <cstdint>

namespace NorvesLib::Core::Environment
{
    class EnvironmentSystem;
}

namespace NorvesLib::Modules::AI::Perception
{
    /// GameThread 専用。環境を借用し、借用中の寿命は呼び出し側が保証する。
    class EngineWindSource final : public IWindSource
    {
    public:
        explicit EngineWindSource(const Core::Environment::EnvironmentSystem* environment = nullptr);
        void SetEnvironment(const Core::Environment::EnvironmentSystem* environment);
        Math::Vector3 SampleWind(const Math::Vector3& position, double gameTimeSeconds) const override;

    private:
        // 正規化前の入力をビット列で比較し、同じ非数値でも毎回解決し直すのを防ぐ。
        struct WindInputs
        {
            uint32_t StateBits[3];
            uint32_t SceneBits[3];
            bool bEnabled;
            bool operator==(const WindInputs&) const = default;
        };

        const Core::Environment::EnvironmentSystem* m_pEnvironment;
        mutable WindInputs m_CachedInputs{};
        mutable Core::Rendering::WindParameters m_CachedWind;
        mutable bool m_bHasCachedWind = false;
    };
}
