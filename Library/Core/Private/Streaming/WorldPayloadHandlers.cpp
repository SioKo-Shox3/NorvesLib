// ワールドの各種ペイロードを GameThread で段階的に配置・休眠・解放する。
#include "Streaming/WorldPayloadHandlers.h"

#include "Logging/LogMacros.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Rendering/Terrain/TerrainSystem.h"
#include "Scatter/ScatterSystem.h"

#include <utility>

namespace NorvesLib::Core::Streaming
{
    namespace
    {
        uint64_t PayloadKey(StreamingCellId cell, uint32_t payloadIndex)
        {
            return (static_cast<uint64_t>(cell) << 32) | payloadIndex;
        }

        uint64_t PayloadKey(const StreamingPayloadContext& context)
        {
            return PayloadKey(context.Cell, context.PayloadIndex);
        }

        bool SameName(Container::AnsiStringView left, Container::AnsiStringView right)
        {
            if (left.size() != right.size())
            {
                return false;
            }
            for (size_t index = 0; index < left.size(); ++index)
            {
                if (left[index] != right[index])
                {
                    return false;
                }
            }
            return true;
        }

        Container::AnsiString CopyName(Container::AnsiStringView name)
        {
            Container::AnsiString result(name.size(), '\0');
            for (size_t index = 0; index < name.size(); ++index)
            {
                result[index] = name[index];
            }
            return result;
        }

        EStreamingPrepareResult PrepareWithoutData(const StreamingCancelToken& cancel)
        {
            return cancel.IsCancelled() ? EStreamingPrepareResult::Cancelled : EStreamingPrepareResult::Success;
        }

        EStreamingStepResult FailPayload(const char* kind, const StreamingPayloadContext& context, const char* reason)
        {
            LOG_WARNING("STREAMING_PAYLOAD_FAILED kind=%s cell=%u path=%s %s",
                        kind, context.Cell, context.Desc != nullptr ? context.Desc->LogicalPath.c_str() : "", reason);
            return EStreamingStepResult::Failed;
        }
    } // namespace

    TerrainTilePayloadHandler::TerrainTilePayloadHandler(Rendering::Terrain::TerrainSystem& terrainSystem)
        : m_TerrainSystem(terrainSystem)
    {
    }

    const TerrainTilePayloadHandler::SiteBinding* TerrainTilePayloadHandler::FindSite(Container::AnsiStringView name) const
    {
        for (const auto& binding : m_Sites)
        {
            if (SameName(Container::AnsiStringView(binding.Name), name))
            {
                return &binding;
            }
        }
        return nullptr;
    }

    bool TerrainTilePayloadHandler::BindSite(Container::AnsiStringView name, ITerrainTileSite* pSite)
    {
        if (name.empty() || pSite == nullptr || FindSite(name) != nullptr)
        {
            return false;
        }
        m_Sites.push_back({CopyName(name), pSite, m_NextSiteId++});
        return true;
    }

    void TerrainTilePayloadHandler::UnbindSite(Container::AnsiStringView name)
    {
        for (auto it = m_Sites.begin(); it != m_Sites.end(); ++it)
        {
            if (SameName(Container::AnsiStringView(it->Name), name))
            {
                m_Sites.erase(it);
                return;
            }
        }
    }

    const char* TerrainTilePayloadHandler::GetKind() const
    {
        return kTerrainTilePayloadKind;
    }

    EStreamingPrepareResult TerrainTilePayloadHandler::Prepare(const StreamingPayloadContext&, const StreamingCancelToken& cancel)
    {
        return PrepareWithoutData(cancel);
    }

    EStreamingStepResult TerrainTilePayloadHandler::CommitStep(
        const StreamingPayloadContext& context, StreamingStepCursor& cursor, uint64_t& outCommittedBytes)
    {
        outCommittedBytes = 0;
        if (context.Desc == nullptr || context.CellDesc == nullptr)
        {
            return FailPayload(GetKind(), context, "セルまたはペイロードの記述がありません");
        }

        const auto* binding = FindSite(Container::AnsiStringView(context.Desc->LogicalPath));
        const auto key = PayloadKey(context);
        if (cursor.Step == 0)
        {
            if (binding == nullptr)
            {
                return FailPayload(GetKind(), context, "地形の配置先がありません");
            }
            const auto siteState = binding->Site->GetSiteState();
            if (siteState == ETerrainTileSiteState::Failed)
            {
                return FailPayload(GetKind(), context, "地形の配置先の準備に失敗しました");
            }
            if (siteState == ETerrainTileSiteState::Loading)
            {
                return EStreamingStepResult::Waiting;
            }

            auto [it, bInserted] = m_States.try_emplace(key);
            if (!bInserted)
            {
                return FailPayload(GetKind(), context, "地形の状態が既にあります");
            }
            auto& state = it->second;
            state.SiteId = binding->Id;
            binding->Site->CollectTilesInBounds(context.CellDesc->Bounds, state.Tiles);
            state.PlacedTileIds.reserve(state.Tiles.size());
            ++cursor.Step;
            return state.Tiles.empty() ? EStreamingStepResult::Done : EStreamingStepResult::InProgress;
        }

        auto it = m_States.find(key);
        if (it == m_States.end())
        {
            return FailPayload(GetKind(), context, "地形の状態がありません");
        }
        auto& state = it->second;
        if (binding == nullptr || binding->Id != state.SiteId)
        {
            return FailPayload(GetKind(), context, "地形の配置先が解除されました");
        }
        if (state.PlacedTileIds.size() >= state.Tiles.size())
        {
            return EStreamingStepResult::Done;
        }
        const auto& tile = state.Tiles[state.PlacedTileIds.size()];
        if (!binding->Site->PlaceTile(tile))
        {
            return FailPayload(GetKind(), context, "地形の区画を配置できませんでした");
        }
        const uint32_t tileId = binding->Site->GetTileId(tile);
        state.PlacedTileIds.push_back(tileId);
        m_TerrainSystem.SetTileResident(tileId, true);
        ++cursor.Step;
        return state.PlacedTileIds.size() == state.Tiles.size() ? EStreamingStepResult::Done : EStreamingStepResult::InProgress;
    }

    void TerrainTilePayloadHandler::SetDormant(const StreamingPayloadContext& context, bool bDormant)
    {
        const auto it = m_States.find(PayloadKey(context));
        if (it == m_States.end())
        {
            return;
        }
        for (const uint32_t tileId : it->second.PlacedTileIds)
        {
            m_TerrainSystem.SetTileResident(tileId, !bDormant);
        }
    }

    EStreamingStepResult TerrainTilePayloadHandler::UnloadStep(const StreamingPayloadContext& context, StreamingStepCursor& cursor)
    {
        auto it = m_States.find(PayloadKey(context));
        if (it == m_States.end())
        {
            return EStreamingStepResult::Done;
        }
        auto& state = it->second;
        const auto* binding = context.Desc != nullptr ? FindSite(Container::AnsiStringView(context.Desc->LogicalPath)) : nullptr;
        if (binding == nullptr || binding->Id != state.SiteId)
        {
            for (const uint32_t tileId : state.PlacedTileIds)
            {
                m_TerrainSystem.SetTileResident(tileId, false);
            }
            m_States.erase(it);
            return EStreamingStepResult::Done;
        }
        if (!state.PlacedTileIds.empty())
        {
            const size_t index = state.PlacedTileIds.size() - 1;
            m_TerrainSystem.SetTileResident(state.PlacedTileIds[index], false);
            binding->Site->RemoveTile(state.Tiles[index]);
            state.PlacedTileIds.pop_back();
            ++cursor.Step;
        }
        if (state.PlacedTileIds.empty())
        {
            m_States.erase(it);
            return EStreamingStepResult::Done;
        }
        return EStreamingStepResult::InProgress;
    }

    void TerrainTilePayloadHandler::DiscardPrepared(const StreamingPayloadContext& context)
    {
        const auto it = m_States.find(PayloadKey(context));
        if (it != m_States.end() && it->second.PlacedTileIds.empty())
        {
            m_States.erase(it);
        }
    }

    StreamingPayloadBytes TerrainTilePayloadHandler::GetResidentBytes(const StreamingPayloadContext&) const
    {
        return {};
    }

    ScatterChunkPayloadHandler::ScatterChunkPayloadHandler(Scatter::ScatterSystem& scatterSystem)
        : m_ScatterSystem(scatterSystem)
    {
    }

    const char* ScatterChunkPayloadHandler::GetKind() const
    {
        return kScatterChunkPayloadKind;
    }

    EStreamingPrepareResult ScatterChunkPayloadHandler::Prepare(const StreamingPayloadContext&, const StreamingCancelToken& cancel)
    {
        return PrepareWithoutData(cancel);
    }

    EStreamingStepResult ScatterChunkPayloadHandler::CommitStep(
        const StreamingPayloadContext& context, StreamingStepCursor& cursor, uint64_t& outCommittedBytes)
    {
        outCommittedBytes = 0;
        if (context.Desc == nullptr || context.CellDesc == nullptr)
        {
            return FailPayload(GetKind(), context, "セルまたはペイロードの記述がありません");
        }
        const auto handle = m_ScatterSystem.FindScatter(Container::AnsiStringView(context.Desc->LogicalPath));
        const auto key = PayloadKey(context);
        if (cursor.Step == 0)
        {
            if (handle == Scatter::kInvalidScatterHandle)
            {
                return FailPayload(GetKind(), context, "散布がありません");
            }
            auto [it, bInserted] = m_States.try_emplace(key);
            if (!bInserted)
            {
                return FailPayload(GetKind(), context, "散布の状態が既にあります");
            }
            auto& state = it->second;
            state.Handle = handle;
            m_ScatterSystem.CollectChunksInBounds(handle, context.CellDesc->Bounds.Min, context.CellDesc->Bounds.Max, state.Chunks);
            ++cursor.Step;
            return state.Chunks.empty() ? EStreamingStepResult::Done : EStreamingStepResult::InProgress;
        }

        auto it = m_States.find(key);
        if (it == m_States.end())
        {
            return FailPayload(GetKind(), context, "散布の状態がありません");
        }
        auto& state = it->second;
        if (handle == Scatter::kInvalidScatterHandle || handle != state.Handle)
        {
            return FailPayload(GetKind(), context, "散布が解除または変更されました");
        }
        if (state.PinnedCount >= state.Chunks.size())
        {
            return EStreamingStepResult::Done;
        }
        const uint32_t chunk = state.Chunks[state.PinnedCount];
        if (!m_ScatterSystem.PinChunk(state.Handle, chunk))
        {
            return FailPayload(GetKind(), context, "散布のチャンクを固定できませんでした");
        }
        ++state.PinnedCount;
        outCommittedBytes = m_ScatterSystem.GetChunkBytes(state.Handle, chunk);
        ++cursor.Step;
        return state.PinnedCount == state.Chunks.size() ? EStreamingStepResult::Done : EStreamingStepResult::InProgress;
    }

    void ScatterChunkPayloadHandler::SetDormant(const StreamingPayloadContext& context, bool bDormant)
    {
        const auto it = m_States.find(PayloadKey(context));
        if (it == m_States.end())
        {
            return;
        }
        const auto& state = it->second;
        for (size_t index = 0; index < state.PinnedCount; ++index)
        {
            m_ScatterSystem.SetChunkDormant(state.Handle, state.Chunks[index], bDormant);
        }
    }

    EStreamingStepResult ScatterChunkPayloadHandler::UnloadStep(const StreamingPayloadContext& context, StreamingStepCursor& cursor)
    {
        auto it = m_States.find(PayloadKey(context));
        if (it == m_States.end())
        {
            return EStreamingStepResult::Done;
        }
        auto& state = it->second;
        const auto handle = context.Desc != nullptr
            ? m_ScatterSystem.FindScatter(Container::AnsiStringView(context.Desc->LogicalPath))
            : Scatter::kInvalidScatterHandle;
        if (handle == Scatter::kInvalidScatterHandle || handle != state.Handle)
        {
            m_States.erase(it);
            return EStreamingStepResult::Done;
        }
        if (state.PinnedCount != 0)
        {
            --state.PinnedCount;
            m_ScatterSystem.UnpinChunk(state.Handle, state.Chunks[state.PinnedCount]);
            ++cursor.Step;
        }
        if (state.PinnedCount == 0)
        {
            m_States.erase(it);
            return EStreamingStepResult::Done;
        }
        return EStreamingStepResult::InProgress;
    }

    void ScatterChunkPayloadHandler::DiscardPrepared(const StreamingPayloadContext& context)
    {
        const auto it = m_States.find(PayloadKey(context));
        if (it != m_States.end() && it->second.PinnedCount == 0)
        {
            m_States.erase(it);
        }
    }

    StreamingPayloadBytes ScatterChunkPayloadHandler::GetResidentBytes(const StreamingPayloadContext& context) const
    {
        StreamingPayloadBytes bytes;
        const auto it = m_States.find(PayloadKey(context));
        if (it != m_States.end())
        {
            const auto& state = it->second;
            for (size_t index = 0; index < state.PinnedCount; ++index)
            {
                bytes.Cpu += m_ScatterSystem.GetChunkBytes(state.Handle, state.Chunks[index]);
            }
        }
        return bytes;
    }

    PlacementPayloadHandler::PlacementPayloadHandler(World& world)
        : m_World(world)
    {
    }

    const PlacementPayloadHandler::BuilderBinding* PlacementPayloadHandler::FindBuilder(Container::AnsiStringView name) const
    {
        for (const auto& binding : m_Builders)
        {
            if (SameName(Container::AnsiStringView(binding.Name), name))
            {
                return &binding;
            }
        }
        return nullptr;
    }

    bool PlacementPayloadHandler::RegisterBuilder(Container::AnsiStringView name, PlacementBuilder builder)
    {
        if (name.empty() || !builder.CountItems.IsBound() || !builder.SpawnItem.IsBound() || FindBuilder(name) != nullptr)
        {
            return false;
        }
        m_Builders.push_back({CopyName(name), std::move(builder), m_NextBuilderId++});
        return true;
    }

    void PlacementPayloadHandler::UnregisterBuilder(Container::AnsiStringView name)
    {
        for (auto it = m_Builders.begin(); it != m_Builders.end(); ++it)
        {
            if (SameName(Container::AnsiStringView(it->Name), name))
            {
                m_Builders.erase(it);
                return;
            }
        }
    }

    void PlacementPayloadHandler::SetItemsPerStep(uint32_t count)
    {
        m_ItemsPerStep = count == 0 ? 1 : count;
    }

    uint32_t PlacementPayloadHandler::GetItemsPerStep() const
    {
        return m_ItemsPerStep;
    }

    Entity* PlacementPayloadHandler::FindCellRoot(StreamingCellId cell, uint32_t payloadIndex) const
    {
        const auto it = m_States.find(PayloadKey(cell, payloadIndex));
        return it == m_States.end() || it->second.RootId == 0 ? nullptr : m_World.FindEntityByObjectId(it->second.RootId);
    }

    const char* PlacementPayloadHandler::GetKind() const
    {
        return kPlacementPayloadKind;
    }

    EStreamingPrepareResult PlacementPayloadHandler::Prepare(const StreamingPayloadContext&, const StreamingCancelToken& cancel)
    {
        return PrepareWithoutData(cancel);
    }

    EStreamingStepResult PlacementPayloadHandler::CommitStep(
        const StreamingPayloadContext& context, StreamingStepCursor& cursor, uint64_t& outCommittedBytes)
    {
        outCommittedBytes = 0;
        if (context.Desc == nullptr || context.CellDesc == nullptr)
        {
            return FailPayload(GetKind(), context, "セルまたはペイロードの記述がありません");
        }
        const auto key = PayloadKey(context);
        if (cursor.Step == 0)
        {
            const auto* binding = FindBuilder(Container::AnsiStringView(context.Desc->LogicalPath));
            if (binding == nullptr)
            {
                return FailPayload(GetKind(), context, "配置の作り手がありません");
            }
            const uint64_t builderId = binding->Id;
            const auto countItems = binding->Builder.CountItems;
            auto [it, bInserted] = m_States.try_emplace(key);
            if (!bInserted)
            {
                return FailPayload(GetKind(), context, "配置の状態が既にあります");
            }
            auto& state = it->second;
            state.BuilderId = builderId;
            Entity* root = m_World.SpawnEntity<Entity>();
            if (root == nullptr)
            {
                return FailPayload(GetKind(), context, "配置のルートを作れませんでした");
            }
            state.RootId = root->GetObjectId();
            const PlacementSpawnContext spawnContext{m_World, *root, context.Cell, context.CellDesc->Bounds};
            state.ItemCount = countItems(spawnContext);
            ++cursor.Step;
            return state.ItemCount == 0 ? EStreamingStepResult::Done : EStreamingStepResult::InProgress;
        }

        auto it = m_States.find(key);
        if (it == m_States.end())
        {
            return FailPayload(GetKind(), context, "配置の状態がありません");
        }
        auto& state = it->second;
        uint32_t spawnedThisStep = 0;
        while (spawnedThisStep < m_ItemsPerStep && state.SpawnedCount < state.ItemCount)
        {
            const auto* binding = FindBuilder(Container::AnsiStringView(context.Desc->LogicalPath));
            if (binding == nullptr || binding->Id != state.BuilderId)
            {
                return FailPayload(GetKind(), context, "配置の作り手が解除されました");
            }
            Entity* root = m_World.FindEntityByObjectId(state.RootId);
            if (root == nullptr)
            {
                return FailPayload(GetKind(), context, "配置のルートがありません");
            }
            const auto spawnItem = binding->Builder.SpawnItem;
            const PlacementSpawnContext spawnContext{m_World, *root, context.Cell, context.CellDesc->Bounds};
            if (!spawnItem(spawnContext, state.SpawnedCount))
            {
                return FailPayload(GetKind(), context, "配置の項目を作れませんでした");
            }
            ++state.SpawnedCount;
            ++spawnedThisStep;
        }
        ++cursor.Step;
        return state.SpawnedCount == state.ItemCount ? EStreamingStepResult::Done : EStreamingStepResult::InProgress;
    }

    void PlacementPayloadHandler::SetDormant(const StreamingPayloadContext& context, bool bDormant)
    {
        if (Entity* root = FindCellRoot(context.Cell, context.PayloadIndex))
        {
            root->SetActive(!bDormant);
        }
    }

    EStreamingStepResult PlacementPayloadHandler::UnloadStep(const StreamingPayloadContext& context, StreamingStepCursor& cursor)
    {
        auto it = m_States.find(PayloadKey(context));
        if (it != m_States.end())
        {
            if (Entity* root = m_World.FindEntityByObjectId(it->second.RootId))
            {
                m_World.RemoveEntity(root);
            }
            m_States.erase(it);
        }
        ++cursor.Step;
        return EStreamingStepResult::Done;
    }

    void PlacementPayloadHandler::DiscardPrepared(const StreamingPayloadContext& context)
    {
        const auto it = m_States.find(PayloadKey(context));
        if (it != m_States.end() && it->second.RootId == 0)
        {
            m_States.erase(it);
        }
    }

    StreamingPayloadBytes PlacementPayloadHandler::GetResidentBytes(const StreamingPayloadContext&) const
    {
        return {};
    }
} // namespace NorvesLib::Core::Streaming
