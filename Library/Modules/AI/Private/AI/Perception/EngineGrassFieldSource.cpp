// 描画と共通の枯れ度マップを使い、視線上の草を照会する。
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

    GrassPathSample EngineGrassFieldSource::QueryGrassPath(const Math::Vector3& eye, const Math::Vector3& target,
                                                           float stepMeters) const
    {
        const auto* field = m_pVegetation ? m_pVegetation->GetGrassField() : nullptr;
        if (!field)
        {
            return {};
        }
        GrassPathSample sample;
        sample.OpticalDepth = field->QueryOpticalDepth(eye, target, stepMeters, &m_pVegetation->GetDrynessMap(),
                                                      &sample.MaxTopHeight);
        return sample;
    }
}
