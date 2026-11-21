// 地形と草の表面を登録簿の番号に対応付け、同じ位置の濡れを共通の式で求める。
#include "Scene/TerrainSurfaceProvider.h"

#include "Environment/EnvironmentSystem.h"
#include "Rendering/Terrain/TerrainHeightField.h"
#include "Rendering/Terrain/TerrainMaterialDesc.h"
#include "Rendering/Terrain/TerrainSystem.h"
#include "Scene/SurfaceRegistry.h"
#include "Vegetation/GrassField.h"
#include "Vegetation/Vegetation.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace NorvesLib::Core::Scene
{
    namespace
    {
        bool IsFiniteQuery(const SurfaceQuery& query)
        {
            return std::isfinite(query.Position.x) && std::isfinite(query.Position.y) && std::isfinite(query.Position.z)
                && std::isfinite(query.Normal.x) && std::isfinite(query.Normal.y) && std::isfinite(query.Normal.z)
                && std::isfinite(query.Radius);
        }
    }

    float ComputeGroundWetness01(const Environment::EnvironmentSystem& environment,
        const Rendering::Terrain::TerrainSystem& terrain, const Vegetation::GrassField* grass, float x, float z)
    {
        const float globalWetness = environment.IsEnabled() ? environment.GetState().Wetness : terrain.GetWetness();
        float moistureBias = 0.5f;
        if (grass)
        {
            const auto surface = grass->QuerySurface(x, z);
            if (surface.bValid)
            {
                moistureBias = surface.MoistureBias;
            }
        }
        return Vegetation::ComputeEffectiveWetness(globalWetness, moistureBias);
    }

    TerrainSurfaceProvider::TerrainSurfaceProvider(const Rendering::Terrain::TerrainSystem& terrain,
        const SurfaceRegistry& registry, const Environment::EnvironmentSystem& environment,
        const Vegetation::Vegetation& vegetation)
        : m_Terrain(terrain), m_Registry(registry), m_Environment(environment), m_Vegetation(vegetation)
    {
    }

    bool TerrainSurfaceProvider::SetLayerSurfaceNames(
        const Container::TSharedPtr<const Rendering::Terrain::TerrainHeightField>& field,
        const Rendering::Terrain::TerrainMaterialDesc& desc)
    {
        if (!field || desc.Layers.size() > kMaxLayers)
        {
            return false;
        }

        const auto expired = std::remove_if(m_Bindings.begin(), m_Bindings.end(), [](const LayerSurfaceBinding& entry)
        {
            return entry.Field.expired();
        });
        m_Bindings.erase(expired, m_Bindings.end());

        LayerSurfaceBinding updated;
        updated.Field = field;
        updated.Raw = field.get();
        updated.LayerCount = desc.Layers.size();
        for (size_t layer = 0; layer < updated.LayerCount; ++layer)
        {
            updated.Names[layer] = desc.Layers[layer].SurfaceName;
        }
        const auto binding = std::find_if(m_Bindings.begin(), m_Bindings.end(), [&](const LayerSurfaceBinding& entry)
        {
            return entry.Raw == field.get() && !entry.Field.expired();
        });
        if (binding != m_Bindings.end())
        {
            *binding = std::move(updated);
        }
        else
        {
            m_Bindings.push_back(std::move(updated));
        }
        ++m_BindingRevision;
        return true;
    }

    const TerrainSurfaceProvider::LayerIds& TerrainSurfaceProvider::GetLayerIds(
        const Container::TSharedPtr<const Rendering::Terrain::TerrainHeightField>& field) const
    {
        const uint64_t revision = m_Registry.GetRevision();
        size_t firstExpired = m_Cache.size();
        size_t index = 0;
        for (; index < m_Cache.size(); ++index)
        {
            const auto& cached = m_Cache[index];
            if (cached.Field.expired())
            {
                firstExpired = std::min(firstExpired, index);
            }
            else if (cached.Raw == field.get())
            {
                if (cached.RegistryRevision == revision && cached.BindingRevision == m_BindingRevision)
                {
                    return cached.Ids;
                }
                break;
            }
        }
        const auto binding = std::find_if(m_Bindings.begin(), m_Bindings.end(), [&](const LayerSurfaceBinding& entry)
        {
            return entry.Raw == field.get() && !entry.Field.expired();
        });
        const auto resolve = [this](const Container::AnsiString& name)
        {
            return m_Registry.FindByName(Container::StringView(name.data(), name.size()));
        };
        LayerIds ids;
        if (binding != m_Bindings.end())
        {
            for (size_t layer = 0; layer < binding->LayerCount; ++layer)
            {
                ids[layer] = resolve(binding->Names[layer]);
            }
        }
        else
        {
            const auto material = Rendering::Terrain::MakeProvisionalTerrainMaterialDesc(*field);
            const size_t count = std::min(material.Layers.size(), ids.size());
            for (size_t layer = 0; layer < count; ++layer)
            {
                ids[layer] = resolve(material.Layers[layer].SurfaceName);
            }
        }
        if (index == m_Cache.size())
        {
            // 期限切れの枠を優先し、全枠が有効なら先頭の最古の枠を空ける。新規の高さ場は末尾に置く。
            index = firstExpired < m_Cache.size() ? firstExpired : 0;
            for (; index + 1 < m_Cache.size(); ++index)
            {
                m_Cache[index] = std::move(m_Cache[index + 1]);
            }
        }
        auto& entry = m_Cache[index];
        entry.Ids = ids;
        entry.Field = field;
        entry.Raw = field.get();
        entry.RegistryRevision = revision;
        entry.BindingRevision = m_BindingRevision;
        return entry.Ids;
    }

    EPhysicsSceneQueryResult TerrainSurfaceProvider::SampleSurface(const SurfaceQuery& query, SurfaceContribution& out) const
    {
        out = SurfaceContribution{};
        if (!IsFiniteQuery(query))
        {
            return EPhysicsSceneQueryResult::NoHit;
        }
        const float x = query.Position.x;
        const float z = query.Position.z;
        out.Wetness01 = ComputeGroundWetness01(m_Environment, m_Terrain, m_Vegetation.GetGrassField(), x, z);
        // GR59 の水深は CPU から取得できない。SamplePuddlePotential は凹み度を表すため、水深は 0 とする。
        out.PuddleDepthM = 0.0f;

        const auto field = m_Terrain.FindTerrain(x, z);
        if (!field || field->GetSplatLayerCount() == 0)
        {
            return EPhysicsSceneQueryResult::Success;
        }
        const auto layout = field->GetGridLayout();
        const uint32_t layers = field->GetSplatLayerCount();
        // 対応表に収まらない層数なら、表面を選ばず濡れだけを返す。
        if (layers > kMaxLayers)
        {
            return EPhysicsSceneQueryResult::Success;
        }
        // 双線形補間には両軸とも最低 2 頂点が必要。
        if (layout.Columns < 2 || layout.Rows < 2)
        {
            return EPhysicsSceneQueryResult::Success;
        }
        // 格子座標を求められるのは間隔が有限で正のときだけ。
        if (!(layout.Spacing > 0.0) || !std::isfinite(layout.Spacing))
        {
            return EPhysicsSceneQueryResult::Success;
        }
        const auto weights = field->GetSplatWeights();
        // 配列長から収まる行数を逆算し、必要バイト数の乗算あふれと範囲外の読みを防ぐ。
        if (weights.size() / layers / layout.Columns < layout.Rows)
        {
            return EPhysicsSceneQueryResult::Success;
        }
        // 外周も最後の升目に含める。最外頂点では補間係数が 1 になり、その頂点の重みが残る。
        const double column = std::clamp((x - layout.FirstX) / layout.Spacing, 0.0, double(layout.Columns - 1));
        const double row = std::clamp((z - layout.FirstZ) / layout.Spacing, 0.0, double(layout.Rows - 1));
        const uint32_t left = std::min(static_cast<uint32_t>(column), layout.Columns - 2);
        const uint32_t top = std::min(static_cast<uint32_t>(row), layout.Rows - 2);
        const size_t topLeft = (size_t(top) * layout.Columns + left) * layers;
        const size_t bottomLeft = topLeft + size_t(layout.Columns) * layers;

        const auto& ids = GetLayerIds(field);
        double primaryWeight = -1.0;
        double secondaryWeight = 0.0;
        for (uint32_t layer = 0; layer < layers; ++layer)
        {
            const size_t upper = topLeft + layer;
            const size_t lower = bottomLeft + layer;
            const double upperWeight = std::lerp(double(weights[upper]), double(weights[upper + layers]), column - left);
            const double lowerWeight = std::lerp(double(weights[lower]), double(weights[lower + layers]), column - left);
            const double weight = std::lerp(upperWeight, lowerWeight, row - top);

            // 番号順に走査し、同じ重みなら先に選んだ層を優先する。
            if (weight > primaryWeight)
            {
                if (primaryWeight > 0.0)
                {
                    secondaryWeight = primaryWeight;
                    out.Secondary = out.Primary;
                }
                primaryWeight = weight;
                out.Primary = ids[layer];
            }
            else if (weight > secondaryWeight)
            {
                secondaryWeight = weight;
                out.Secondary = ids[layer];
            }
        }
        out.bHasSurface = true;
        out.PrimaryWeight = static_cast<float>(primaryWeight);
        out.SecondaryWeight = static_cast<float>(secondaryWeight);
        return EPhysicsSceneQueryResult::Success;
    }

    VegetationSurfaceProvider::VegetationSurfaceProvider(const Vegetation::Vegetation& vegetation,
        const SurfaceRegistry& registry, const Rendering::Terrain::TerrainSystem& terrain,
        const Environment::EnvironmentSystem& environment)
        : m_Vegetation(vegetation), m_Registry(registry), m_Terrain(terrain), m_Environment(environment)
    {
    }

    EPhysicsSceneQueryResult VegetationSurfaceProvider::SampleSurface(const SurfaceQuery& query, SurfaceContribution& out) const
    {
        out = SurfaceContribution{};
        const auto* grass = m_Vegetation.GetGrassField();
        if (!grass || !IsFiniteQuery(query))
        {
            return EPhysicsSceneQueryResult::NoHit;
        }
        const float x = query.Position.x;
        const float z = query.Position.z;
        const auto sample = grass->Sample(x, z);
        if (!sample.bValid)
        {
            return EPhysicsSceneQueryResult::NoHit;
        }
        out.VegetationDensity01 = std::clamp(sample.Density, 0.0f, 1.0f);
        out.Wetness01 = ComputeGroundWetness01(m_Environment, m_Terrain, grass, x, z);
        const uint64_t revision = m_Registry.GetRevision();
        if (m_CachedRevision != revision)
        {
            m_bFoundGrass = m_Registry.TryFindByName("Grass_Tall", m_GrassId);
            m_CachedRevision = revision;
        }
        if (m_bFoundGrass && out.VegetationDensity01 > m_Registry.GetDesc(m_GrassId).VegetationDensityThreshold)
        {
            out.bHasSurface = true;
            out.Primary = m_GrassId;
        }
        return EPhysicsSceneQueryResult::Success;
    }
}
