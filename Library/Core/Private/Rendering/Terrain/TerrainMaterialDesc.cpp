#include "Rendering/Terrain/TerrainMaterialDesc.h"

#include "Rendering/Terrain/TerrainHeightField.h"

namespace NorvesLib::Core::Rendering::Terrain
{
    namespace
    {
        struct ProvisionalSurfaceEntry
        {
            const char* LayerName;
            const char* SurfaceName;
        };

        // 名前の一覧は企画で未定の仮の値。
        constexpr ProvisionalSurfaceEntry kProvisionalSurfaces[] = {
            {"soil", "Soil"},
            {"meadow", "Soil"},
            {"gravel", "Gravel"},
            {"moss", "Moss"},
            {"mud", "Mud"},
            {"wet_mud", "Mud"},
            {"rock", "Rock"},
            {"rock_face", "Rock"},
            {"sand", "Sand"},
            {"water", "Water"}};
    } // namespace

    Container::AnsiStringView TerrainMaterialDesc::GetSurfaceName(uint8_t surfaceId) const
    {
        if (surfaceId >= Layers.size())
        {
            return {};
        }
        const auto& name = Layers[surfaceId].SurfaceName;
        return Container::AnsiStringView(name.data(), name.size());
    }

    Container::AnsiStringView GetProvisionalSurfaceName(Container::AnsiStringView layerName)
    {
        for (const auto& entry : kProvisionalSurfaces)
        {
            if (layerName == Container::AnsiStringView(entry.LayerName))
            {
                return Container::AnsiStringView(entry.SurfaceName);
            }
        }
        return Container::AnsiStringView("Soil");
    }

    TerrainMaterialDesc MakeProvisionalTerrainMaterialDesc(const TerrainHeightField& field)
    {
        TerrainMaterialDesc desc;
        desc.Layers.resize(field.GetSplatLayerCount());
        for (uint32_t layer = 0; layer < field.GetSplatLayerCount(); ++layer)
        {
            const auto layerName = field.GetSplatLayerName(layer);
            const auto surfaceName = GetProvisionalSurfaceName(layerName);
            desc.Layers[layer].LayerName.append(layerName.data(), layerName.size());
            desc.Layers[layer].SurfaceName.append(surfaceName.data(), surfaceName.size());
        }
        return desc;
    }
} // namespace NorvesLib::Core::Rendering::Terrain
