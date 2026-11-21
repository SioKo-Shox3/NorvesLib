// 草の透過率に描画と共通の枯れ度マップを適用する。
#include "AI/Perception/EngineGrassFieldSource.h"

#include "Vegetation/GrassField.h"
#include "Vegetation/Vegetation.h"

namespace NorvesLib::Modules::AI::Perception
{
    EngineGrassFieldSource::EngineGrassFieldSource(const Core::Vegetation::Vegetation* vegetation)
        : m_pVegetation(vegetation)
    {
    }

    void EngineGrassFieldSource::SetVegetation(const Core::Vegetation::Vegetation* vegetation)
    {
        m_pVegetation = vegetation;
    }

    float EngineGrassFieldSource::QueryTransmittance(const Math::Vector3& a, const Math::Vector3& b, float stepMeters) const
    {
        const auto* field = m_pVegetation ? m_pVegetation->GetGrassField() : nullptr;
        if (!field)
        {
            return 1.0f;
        }
        return field->QueryOcclusion(a, b, stepMeters, &m_pVegetation->GetDrynessMap());
    }
}
