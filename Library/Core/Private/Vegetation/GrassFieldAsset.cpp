// 種別名をクック済みの順に並べ直し、各チャンクを実行時の場へ写す。
#include "Vegetation/GrassFieldAsset.h"

#include "Asset/CookedGrassFieldFormat.h"
#include "Vegetation/GrassField.h"
#include "Vegetation/GrassSpecies.h"

#include <cmath>
#include <limits>

namespace NorvesLib::Core::Vegetation
{
    Container::TUniquePtr<GrassField> BuildGrassFieldFromCooked(
        const Asset::CookedGrassFieldData& data, const GrassSpeciesTable& loadedTable,
        Container::String& outWarning)
    {
        outWarning.clear();
        if (!data.Source.IsValid() || !std::isfinite(data.ChunkSizeMeters) || data.ChunkSizeMeters <= 0.0f ||
            data.LayerAResolution == 0 || data.LayerAResolution > Asset::CookedGrassFieldFormatV0::MaxLayerResolution ||
            data.LayerBResolution == 0 || data.LayerBResolution > Asset::CookedGrassFieldFormatV0::MaxLayerResolution ||
            data.SpeciesNames.empty() || data.SpeciesNames.size() > GrassSpeciesMaxCount ||
            data.Chunks.size() > std::numeric_limits<uint32_t>::max() ||
            data.ChunkSizeMeters / static_cast<float>(data.LayerAResolution) <= 0.0f ||
            data.ChunkSizeMeters / static_cast<float>(data.LayerBResolution) <= 0.0f)
        {
            return nullptr;
        }

        GrassSpeciesTable orderedTable;
        for (const auto& cookedName : data.SpeciesNames)
        {
            const Container::String name(cookedName.c_str());
            const GrassSpecies* pMatchedSpecies = nullptr;
            for (const GrassSpecies& species : loadedTable.GetAll())
            {
                if (species.Name == name)
                {
                    pMatchedSpecies = &species;
                    break;
                }
            }
            GrassSpecies entry;
            if (pMatchedSpecies != nullptr)
            {
                entry = *pMatchedSpecies;
            }
            else
            {
                entry.Name = name;
                if (!outWarning.empty())
                {
                    outWarning += ", ";
                }
                outWarning += name;
            }
            if (!orderedTable.Add(entry))
            {
                return nullptr;
            }
        }

        GrassFieldDesc desc;
        desc.ChunkSizeMeters = data.ChunkSizeMeters;
        desc.LayerAResolution = data.LayerAResolution;
        desc.LayerBResolution = data.LayerBResolution;
        auto field = Container::MakeUnique<GrassField>(desc);
        field->SetSpeciesTable(orderedTable);
        for (uint32_t index = 0; index < data.Chunks.size(); ++index)
        {
            const auto& chunk = data.Chunks[index];
            if (!field->SetChunk({chunk.X, chunk.Z}, data.GetLayerA(index), data.GetLayerB(index)))
            {
                return nullptr;
            }
        }
        return field;
    }
} // namespace NorvesLib::Core::Vegetation
