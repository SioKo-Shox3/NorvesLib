// 地形システムに登録された高さ場を、草の場の地面として参照する。
// 地形がない位置は指定した既定の高さを返す。
#pragma once

#include "Vegetation/GroundHeightSource.h"

namespace NorvesLib::Core::Rendering::Terrain
{
    class TerrainSystem;
}

namespace NorvesLib::Core::Vegetation
{
    /** @brief GameThread 専用。地形システムを借用するため、この実装より長く生かすこと。 */
    class TerrainGroundHeightSource final : public IGroundHeightSource
    {
    public:
        explicit TerrainGroundHeightSource(const Rendering::Terrain::TerrainSystem* pTerrainSystem,
                                           float defaultHeight = 0.0f);
        float GetGroundHeight(float x, float z) const override;

    private:
        const Rendering::Terrain::TerrainSystem* m_pTerrainSystem;
        float m_DefaultHeight;
    };
} // namespace NorvesLib::Core::Vegetation
