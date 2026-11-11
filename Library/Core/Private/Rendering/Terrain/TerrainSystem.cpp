// ---------------------------------------------------------------------------
// 地形 Component が登録した高さ場を、Game とエンジンの部品へ共有する。
// 登録順に範囲を検索し、世代付きハンドルで解除済みの登録を区別する。
// 登録・解除・検索・資産の設定は GameThread で行う。
// ---------------------------------------------------------------------------
#include "Rendering/Terrain/TerrainSystem.h"

#include "Engine/Engine.h"

#include <cmath>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Rendering::Terrain
{
    namespace
    {
        // 格子の始点以上で最初に表現できる float を選ぶ。
        bool FindFirstCoordinate(double first, float& coordinate)
        {
            const double maxFloat = static_cast<double>(std::numeric_limits<float>::max());
            if (!std::isfinite(first) || first > maxFloat)
            {
                return false;
            }

            if (first < -maxFloat)
            {
                coordinate = -std::numeric_limits<float>::max();
                return true;
            }

            coordinate = static_cast<float>(first);
            if (static_cast<double>(coordinate) < first)
            {
                coordinate = std::nextafter(coordinate, std::numeric_limits<float>::infinity());
            }
            return true;
        }

        bool HasSampleRange(const TerrainHeightField& field)
        {
            if (!field.IsGrid())
            {
                return true;
            }

            const TerrainGridLayout layout = field.GetGridLayout();
            float x = 0.0f;
            float z = 0.0f;
            return layout.Columns >= 2u && layout.Rows >= 2u &&
                   std::isfinite(layout.Spacing) && layout.Spacing > 0.0 &&
                   FindFirstCoordinate(layout.FirstX, x) &&
                   FindFirstCoordinate(layout.FirstZ, z) &&
                   field.Contains(x, z);
        }
    } // namespace

    TerrainHandle TerrainSystem::RegisterTerrain(Container::TSharedPtr<const TerrainHeightField> field)
    {
        if (!field || !HasSampleRange(*field))
        {
            return {};
        }

        uint32_t index = m_FirstFree;
        if (index != TerrainHandle::InvalidIndex)
        {
            m_FirstFree = m_Slots[index].Next;
        }
        else
        {
            if (m_Slots.size() >= TerrainHandle::InvalidIndex)
            {
                return {};
            }
            index = static_cast<uint32_t>(m_Slots.size());
            m_Slots.emplace_back();
        }

        TerrainSlot& slot = m_Slots[index];
        slot.Field = std::move(field);
        slot.Previous = m_LastTerrain;
        slot.Next = TerrainHandle::InvalidIndex;
        if (m_LastTerrain == TerrainHandle::InvalidIndex)
        {
            m_FirstTerrain = index;
        }
        else
        {
            m_Slots[m_LastTerrain].Next = index;
        }
        m_LastTerrain = index;
        ++m_TerrainCount;
        return {index, slot.Generation};
    }

    void TerrainSystem::UnregisterTerrain(TerrainHandle handle)
    {
        if (IsLiveHandle(handle))
        {
            ReleaseSlot(handle.Index);
        }
    }

    Container::TSharedPtr<const TerrainHeightField> TerrainSystem::FindTerrain(float x, float z) const
    {
        for (uint32_t index = m_FirstTerrain; index != TerrainHandle::InvalidIndex; index = m_Slots[index].Next)
        {
            const TerrainSlot& slot = m_Slots[index];
            if (slot.Field->Contains(x, z))
            {
                return slot.Field;
            }
        }
        return {};
    }

    Container::TSharedPtr<const TerrainHeightField> TerrainSystem::GetTerrain(TerrainHandle handle) const
    {
        return IsLiveHandle(handle) ? m_Slots[handle.Index].Field : Container::TSharedPtr<const TerrainHeightField>{};
    }

    uint32_t TerrainSystem::GetTerrainCount() const
    {
        return m_TerrainCount;
    }

    bool TerrainSystem::SampleGround(float x, float z, GroundSample& outSample) const
    {
        return SampleGround(x, z, outSample, 0u);
    }

    bool TerrainSystem::SampleGround(float x, float z, GroundSample& outSample, uint32_t lod) const
    {
        outSample = GroundSample{};
        const TerrainHeightField* field = FindTerrainRaw(x, z);
        if (!field)
        {
            return false;
        }

        const float height = field->SampleHeight(x, z, lod);
        if (std::isnan(height))
        {
            return false;
        }

        const TerrainNormal normal = field->SampleNormal(x, z, TerrainNormalKind::Face, lod);
        outSample.Height = height;
        outSample.Normal = Math::Vector3(normal.X, normal.Y, normal.Z);
        outSample.SlopeRadians = std::acos(normal.Y);
        outSample.SurfaceId = field->SampleSurface(x, z);
        outSample.TileId = field->GetTileId(x, z);
        outSample.bValid = true;
        return true;
    }

    bool TerrainSystem::RaycastGround(const Math::Ray& ray, float maxDistance, GroundHit& outHit) const
    {
        return RaycastGround(ray, maxDistance, outHit, 0u);
    }

    bool TerrainSystem::RaycastGround(const Math::Ray& ray, float maxDistance, GroundHit& outHit, uint32_t lod) const
    {
        outHit = GroundHit{};
        for (uint32_t index = m_FirstTerrain; index != TerrainHandle::InvalidIndex; index = m_Slots[index].Next)
        {
            const TerrainSlot& slot = m_Slots[index];
            TerrainRayHit hit;
            if (!slot.Field->Raycast(ray, maxDistance, hit, lod) ||
                (outHit.bValid && hit.Distance >= outHit.Distance))
            {
                continue;
            }

            outHit.Distance = hit.Distance;
            outHit.Point = hit.Point;
            outHit.Normal = hit.Normal;
            outHit.SurfaceId = slot.Field->SampleSurface(hit.Point.x, hit.Point.z);
            outHit.TileId = slot.Field->GetTileId(hit.Point.x, hit.Point.z);
            outHit.Terrain = {index, slot.Generation};
            outHit.bValid = true;
        }
        return outHit.bValid;
    }

    void TerrainSystem::SampleGroundBatch(Container::Span<const Math::Vector2> positions,
                                          Container::Span<GroundSample> outSamples) const
    {
        const auto count = positions.size() < outSamples.size() ? positions.size() : outSamples.size();
        for (std::size_t index = 0; index < count; ++index)
        {
            SampleGround(positions[index].x, positions[index].y, outSamples[index]);
        }
        for (std::size_t index = count; index < outSamples.size(); ++index)
        {
            outSamples[index] = GroundSample{};
        }
    }

    void TerrainSystem::SetTileResident(uint32_t tileId, bool bResident)
    {
        if (tileId == kInvalidTerrainTileId || IsTileResident(tileId) == bResident)
        {
            return;
        }

        auto residency = m_TileResidency
            ? Container::MakeShared<TerrainTileResidency>(*m_TileResidency)
            : Container::MakeShared<TerrainTileResidency>();
        residency->SetResident(tileId, bResident);
        if (residency->IsAllResident())
        {
            m_TileResidency.reset();
        }
        else
        {
            m_TileResidency = std::move(residency);
        }
    }

    bool TerrainSystem::IsTileResident(uint32_t tileId) const
    {
        return !m_TileResidency || m_TileResidency->IsResident(tileId);
    }

    Container::TSharedPtr<const TerrainTileResidency> TerrainSystem::GetTileResidency() const
    {
        return m_TileResidency;
    }

    void TerrainSystem::SetAssetSystem(Container::TSharedPtr<const Asset::AssetSystem> assetSystem)
    {
        m_AssetSystem = std::move(assetSystem);
    }

    Container::TSharedPtr<const Asset::AssetSystem> TerrainSystem::GetAssetSystem() const
    {
        return m_AssetSystem;
    }

    void TerrainSystem::SetWetness(float wetness)
    {
        m_Wetness = std::isfinite(wetness)
            ? (wetness < 0.0f ? 0.0f : (wetness > 1.0f ? 1.0f : wetness)) : 0.0f;
        if (Engine::GEngine != nullptr && &Engine::GEngine->GetTerrainSystem() == this)
        {
            const TerrainWetnessRenderData data{m_Wetness};
            Engine::GEngine->GetRenderWorld().SetExtensionData(kTerrainWetnessRenderKey, data);
        }
    }

    float TerrainSystem::GetWetness() const
    {
        return m_Wetness;
    }

    void TerrainSystem::Clear()
    {
        while (m_FirstTerrain != TerrainHandle::InvalidIndex)
        {
            ReleaseSlot(m_FirstTerrain);
        }
        m_TileResidency.reset();
        SetWetness(0.0f);
    }

    const TerrainHeightField* TerrainSystem::FindTerrainRaw(float x, float z) const
    {
        for (uint32_t index = m_FirstTerrain; index != TerrainHandle::InvalidIndex; index = m_Slots[index].Next)
        {
            const TerrainSlot& slot = m_Slots[index];
            if (slot.Field->Contains(x, z))
            {
                return slot.Field.get();
            }
        }
        return nullptr;
    }

    bool TerrainSystem::IsLiveHandle(TerrainHandle handle) const
    {
        return handle.IsValid() && handle.Index < m_Slots.size() &&
               m_Slots[handle.Index].Field && m_Slots[handle.Index].Generation == handle.Generation;
    }

    void TerrainSystem::ReleaseSlot(uint32_t index)
    {
        TerrainSlot& slot = m_Slots[index];
        if (slot.Previous == TerrainHandle::InvalidIndex)
        {
            m_FirstTerrain = slot.Next;
        }
        else
        {
            m_Slots[slot.Previous].Next = slot.Next;
        }
        if (slot.Next == TerrainHandle::InvalidIndex)
        {
            m_LastTerrain = slot.Previous;
        }
        else
        {
            m_Slots[slot.Next].Previous = slot.Previous;
        }

        slot.Field.reset();
        slot.Previous = TerrainHandle::InvalidIndex;
        --m_TerrainCount;
        if (slot.Generation == std::numeric_limits<uint32_t>::max())
        {
            slot.Next = TerrainHandle::InvalidIndex;
            return;
        }

        ++slot.Generation;
        slot.Next = m_FirstFree;
        m_FirstFree = index;
    }
} // namespace NorvesLib::Core::Rendering::Terrain
