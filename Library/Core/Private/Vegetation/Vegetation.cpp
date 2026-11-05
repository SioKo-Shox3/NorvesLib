#include "Vegetation/Vegetation.h"

#include "Vegetation/GrassField.h"
#include "Rendering/RenderWorld.h"

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
