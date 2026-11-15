#include "Vegetation/Vegetation.h"

#include "Vegetation/GrassField.h"
#include "Rendering/RenderWorld.h"

#include <cmath>

namespace NorvesLib::Core::Vegetation
{
    bool Vegetation::RegisterGrassField(GrassField* pField)
    {
        if (pField == nullptr || (m_pGrassField != nullptr && m_pGrassField != pField))
        {
            return false;
        }
        if (m_pGrassField != pField)
        {
            m_pGrassField = pField;
            m_bPublishedOnce = false;
        }
        return true;
    }

    bool Vegetation::UnregisterGrassField(GrassField* pField)
    {
        if (pField == nullptr || m_pGrassField != pField)
        {
            return false;
        }
        m_pGrassField = nullptr;
        m_bPublishedOnce = false;
        return true;
    }

    GrassField* Vegetation::GetGrassField()
    {
        return m_pGrassField;
    }

    const GrassField* Vegetation::GetGrassField() const
    {
        return m_pGrassField;
    }

    DrynessMap& Vegetation::GetDrynessMap()
    {
        return m_DrynessMap;
    }

    const DrynessMap& Vegetation::GetDrynessMap() const
    {
        return m_DrynessMap;
    }

    void Vegetation::Tick(float worldDeltaSeconds)
    {
        m_DrynessMap.Tick(worldDeltaSeconds);
    }

    void Vegetation::PublishDrynessSources(Rendering::RenderWorld& renderWorld, float focusX, float focusZ)
    {
        m_DrynessMap.CollectGpuSources(focusX, focusZ, m_GpuDrynessSources);
        m_RenderDrynessSources.clear();
        for (size_t i = 0; i < m_GpuDrynessSources.size() && i < Rendering::WorldFieldDrynessMaxSources; ++i)
        {
            const auto& source = m_GpuDrynessSources[i];
            Rendering::WorldFieldDrynessSource renderSource{};
            renderSource.CenterXZ[0] = source.CenterX;
            renderSource.CenterXZ[1] = source.CenterZ;
            renderSource.RadiusA = source.RadiusA;
            renderSource.RadiusB = source.RadiusB;
            renderSource.CosYaw = std::cos(source.Yaw);
            renderSource.SinYaw = std::sin(source.Yaw);
            renderSource.Falloff = source.Falloff;
            renderSource.Level = source.CurrentLevel;
            m_RenderDrynessSources.push_back(renderSource);
        }
        renderWorld.SetWorldFieldDryness({m_RenderDrynessSources.data(), m_RenderDrynessSources.size()});
    }

    void Vegetation::PublishRenderData(Rendering::RenderWorld& renderWorld)
    {
        const uint64_t revision = m_pGrassField != nullptr ? m_pGrassField->GetRevision() : 0;
        if (m_bPublishedOnce && m_pPublishedField == m_pGrassField && m_PublishedRevision == revision)
        {
            return;
        }

        if (m_pGrassField != nullptr)
        {
            renderWorld.SetGrassField(m_pGrassField->BuildRenderData());
        }
        else
        {
            renderWorld.SetGrassField(nullptr);
        }
        m_PublishedRevision = revision;
        m_pPublishedField = m_pGrassField;
        m_bPublishedOnce = true;
    }
} // namespace NorvesLib::Core::Vegetation
