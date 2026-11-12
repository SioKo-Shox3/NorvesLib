// ワールドの地形区画・散布チャンク・コード配置をセル単位で出し入れする。
// ハンドラーの所有と StreamingSystem への登録は利用側が行う。
// 休眠時は地形の描画を残して物理の印を落とし、散布の範囲を無効にし、配置のルートを非アクティブにする。
#pragma once

#include "Container/StringView.h"
#include "Container/UnorderedMap.h"
#include "Delegate/Delegate.h"
#include "Streaming/IStreamingPayloadHandler.h"
#include "Streaming/ITerrainTileSite.h"

namespace NorvesLib::Core
{
    class Entity;
    class World;
}

namespace NorvesLib::Core::Rendering
{
    struct ProceduralMeshGPUData;
}

namespace NorvesLib::Core::Rendering::Terrain
{
    class TerrainSystem;
}

namespace NorvesLib::Core::Scatter
{
    class ScatterSystem;
}

namespace NorvesLib::Core::Streaming
{
    // メッシュの頂点・インデックスのバッファの GetSize の和を GPU のバイトとして返す。バッファが無ければ 0 として数える。
    StreamingPayloadBytes MeasureMeshGpuBytes(const Rendering::ProceduralMeshGPUData& mesh);

    // 3 種とも事前に読むファイルはなく、Prepare はワーカーで取り消しの印だけを見る。
    // それ以外の操作とペイロードの状態はすべて GameThread に限る。
    // TerrainSystem はハンドラーより長く生き、site は束ねている間は利用側が生かす。
    class TerrainTilePayloadHandler final : public IStreamingPayloadHandler
    {
    public:
        explicit TerrainTilePayloadHandler(Rendering::Terrain::TerrainSystem& terrainSystem);
        TerrainTilePayloadHandler(const TerrainTilePayloadHandler&) = delete;
        TerrainTilePayloadHandler& operator=(const TerrainTilePayloadHandler&) = delete;

        bool IsPayloadReady(const StreamingPayloadContext& context) const override;

        bool BindSite(Container::AnsiStringView name, ITerrainTileSite* pSite);
        // 束ねを外した後は区画を除去できないため、site 側で片付ける。
        void UnbindSite(Container::AnsiStringView name);

        const char* GetKind() const override;
        EStreamingPrepareResult Prepare(const StreamingPayloadContext& context, const StreamingCancelToken& cancel) override;
        EStreamingStepResult CommitStep(const StreamingPayloadContext& context, StreamingStepCursor& cursor, uint64_t& outCommittedBytes) override;
        void SetDormant(const StreamingPayloadContext& context, bool bDormant) override;
        EStreamingStepResult UnloadStep(const StreamingPayloadContext& context, StreamingStepCursor& cursor) override;
        void DiscardPrepared(const StreamingPayloadContext& context) override;
        StreamingPayloadBytes GetResidentBytes(const StreamingPayloadContext& context) const override;

    private:
        struct SiteBinding
        {
            Container::AnsiString Name;
            ITerrainTileSite* Site = nullptr;
            uint64_t Id = 0;
        };

        struct PayloadState
        {
            uint64_t SiteId = 0;
            Container::VariableArray<TerrainTileCoordinate> Tiles;
            // 置いた区画の接頭部分に対応し、site の解除後も常駐の印を落とせる。
            Container::VariableArray<uint32_t> PlacedTileIds;
        };

        const SiteBinding* FindSite(Container::AnsiStringView name) const;

        Rendering::Terrain::TerrainSystem& m_TerrainSystem;
        Container::VariableArray<SiteBinding> m_Sites;
        Container::UnorderedMap<uint64_t, PayloadState> m_States;
        uint64_t m_NextSiteId = 1;
    };

    // ScatterSystem はハンドラーより長く生き、対象の散布はセルを外すまで同じ名前を保つ。
    class ScatterChunkPayloadHandler final : public IStreamingPayloadHandler
    {
    public:
        explicit ScatterChunkPayloadHandler(Scatter::ScatterSystem& scatterSystem);
        ScatterChunkPayloadHandler(const ScatterChunkPayloadHandler&) = delete;
        ScatterChunkPayloadHandler& operator=(const ScatterChunkPayloadHandler&) = delete;

        const char* GetKind() const override;
        EStreamingPrepareResult Prepare(const StreamingPayloadContext& context, const StreamingCancelToken& cancel) override;
        EStreamingStepResult CommitStep(const StreamingPayloadContext& context, StreamingStepCursor& cursor, uint64_t& outCommittedBytes) override;
        void SetDormant(const StreamingPayloadContext& context, bool bDormant) override;
        EStreamingStepResult UnloadStep(const StreamingPayloadContext& context, StreamingStepCursor& cursor) override;
        void DiscardPrepared(const StreamingPayloadContext& context) override;
        StreamingPayloadBytes GetResidentBytes(const StreamingPayloadContext& context) const override;

    private:
        struct PayloadState
        {
            uint32_t Handle = 0;
            Container::VariableArray<uint32_t> Chunks;
            size_t PinnedCount = 0;
        };

        Scatter::ScatterSystem& m_ScatterSystem;
        Container::UnorderedMap<uint64_t, PayloadState> m_States;
    };

    struct PlacementSpawnContext
    {
        World& WorldRef;
        Entity& Root;
        StreamingCellId Cell;
        const StreamingBounds& Bounds;
    };

    struct PlacementBuilder
    {
        Delegate<uint32_t, const PlacementSpawnContext&> CountItems;
        Delegate<bool, const PlacementSpawnContext&, uint32_t> SpawnItem;
        // 任意。SpawnItem が成功した項目が作った資源のバイト（GPU は作った資源の大きさ。メッシュなら MeasureMeshGpuBytes）を返す。未束縛なら 0。
        Delegate<StreamingPayloadBytes, const PlacementSpawnContext&, uint32_t> MeasureItem;
    };

    // World はハンドラーより長く生きる。World を片付ける前に StreamingSystem の
    // StopAndJoin / Shutdown でセルを外す。作り手は生成物を必ず Root の下に置く。
    class PlacementPayloadHandler final : public IStreamingPayloadHandler
    {
    public:
        explicit PlacementPayloadHandler(World& world);
        PlacementPayloadHandler(const PlacementPayloadHandler&) = delete;
        PlacementPayloadHandler& operator=(const PlacementPayloadHandler&) = delete;

        // Delegate の参照先は登録中、利用側が生かす。
        bool RegisterBuilder(Container::AnsiStringView name, PlacementBuilder builder);
        void UnregisterBuilder(Container::AnsiStringView name);
        void SetItemsPerStep(uint32_t count);
        uint32_t GetItemsPerStep() const;
        Entity* FindCellRoot(StreamingCellId cell, uint32_t payloadIndex) const;

        const char* GetKind() const override;
        EStreamingPrepareResult Prepare(const StreamingPayloadContext& context, const StreamingCancelToken& cancel) override;
        EStreamingStepResult CommitStep(const StreamingPayloadContext& context, StreamingStepCursor& cursor, uint64_t& outCommittedBytes) override;
        void SetDormant(const StreamingPayloadContext& context, bool bDormant) override;
        EStreamingStepResult UnloadStep(const StreamingPayloadContext& context, StreamingStepCursor& cursor) override;
        void DiscardPrepared(const StreamingPayloadContext& context) override;
        StreamingPayloadBytes GetResidentBytes(const StreamingPayloadContext& context) const override;

    private:
        struct BuilderBinding
        {
            Container::AnsiString Name;
            PlacementBuilder Builder;
            uint64_t Id = 0;
        };

        struct PayloadState
        {
            uint64_t BuilderId = 0;
            uint64_t RootId = 0;
            uint32_t ItemCount = 0;
            uint32_t SpawnedCount = 0;
            StreamingPayloadBytes ResidentBytes;
        };

        const BuilderBinding* FindBuilder(Container::AnsiStringView name) const;

        World& m_World;
        Container::VariableArray<BuilderBinding> m_Builders;
        Container::UnorderedMap<uint64_t, PayloadState> m_States;
        uint64_t m_NextBuilderId = 1;
        uint32_t m_ItemsPerStep = 8;
    };
} // namespace NorvesLib::Core::Streaming
