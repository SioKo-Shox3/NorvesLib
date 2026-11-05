// 地形システムから座標に対応する高さ場を検索する。
#include "Vegetation/TerrainGroundHeightSource.h"

#include "Rendering/Terrain/TerrainSystem.h"

namespace NorvesLib::Core::Vegetation
{
    TerrainGroundHeightSource::TerrainGroundHeightSource(const Rendering::Terrain::TerrainSystem* pTerrainSystem,
                                                       float defaultHeight)
        : m_pTerrainSystem(pTerrainSystem)
        , m_DefaultHeight(defaultHeight)
    {
    }

    float TerrainGroundHeightSource::GetGroundHeight(float x, float z) const
    {
        if (m_pTerrainSystem == nullptr)
        {
            return m_DefaultHeight;
        }
        const auto heightField = m_pTerrainSystem->FindTerrain(x, z);
        return heightField ? heightField->SampleHeight(x, z) : m_DefaultHeight;
    }
} // namespace NorvesLib::Core::Vegetation
