// 環境の風が変わったときに設定を解決し、描画と同じ時計で風速を求める。
#include "AI/Perception/EngineWindSource.h"

#include "Environment/EnvironmentSystem.h"

#include <bit>

namespace NorvesLib::Modules::AI::Perception
{
    EngineWindSource::EngineWindSource(const Core::Environment::EnvironmentSystem* environment)
        : m_pEnvironment(environment)
    {
    }

    void EngineWindSource::SetEnvironment(const Core::Environment::EnvironmentSystem* environment)
    {
        m_pEnvironment = environment;
        m_bHasCachedWind = false;
    }

    Math::Vector3 EngineWindSource::SampleWind(const Math::Vector3& position, double gameTimeSeconds) const
    {
        if (!m_pEnvironment || !m_pEnvironment->IsEnabled())
        {
            return {};
        }

        // ResolveEnvironment は場面の風に、上書き後の風向・平均風速・突風の強さを重ねる。
        const auto& state = m_pEnvironment->GetState();
        const auto& sceneWind = m_pEnvironment->GetSceneBase().Wind;
        const WindInputs inputs{
            {std::bit_cast<uint32_t>(state.WindDirectionYawDegrees), std::bit_cast<uint32_t>(state.WindBaseSpeed),
             std::bit_cast<uint32_t>(state.WindGustStrength)},
            {std::bit_cast<uint32_t>(sceneWind.GustFrequencyHz), std::bit_cast<uint32_t>(sceneWind.GustScaleMeters),
             std::bit_cast<uint32_t>(sceneWind.Turbulence)},
            sceneWind.bEnabled
        };
        if (!m_bHasCachedWind || inputs != m_CachedInputs)
        {
            m_CachedWind = Core::Rendering::SanitizeWindParameters(m_pEnvironment->Resolve().Wind);
            m_CachedInputs = inputs;
            m_bHasCachedWind = true;
        }

        return Core::Rendering::EvaluateWind(m_CachedWind, position, Core::Rendering::WindClockSeconds(gameTimeSeconds));
    }
}
