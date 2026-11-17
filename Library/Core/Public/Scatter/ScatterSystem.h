// 焼いた散布の要求・常駐予算・解放と、種ごとの描画経路を GameThread で管理する。
// セルからチャンクの常駐を固定・解除し、休眠を切り替える口を持つ。
// 常駐個体の空間照会と個体ごとの非表示、および非表示集合の保存・復元を提供する。
// ScatterSystem は Engine::Engine が所有し、Entity は Load に渡した World の木が所有する。
// Unload で Entity を World に返し、CookedScatterData は共有参照で保持する。
// World が先に Finalize された場合、Clear は Entity に触らず状態だけ捨てる。
// foliage はチャンクを FoliageCell とし、常駐の組が変わったときだけ種ごとの束を作り直す。
// mega に色味・個体差の種・RT の印を渡し、mega と foliage の影に NVSCT の印を反映する。
// foliage の影は結び付けと NVSCT の印に従い、無効なら束の ShadowDistance を 0 にする。
#pragma once

#include "Asset/CookedScatterFormat.h"
#include "Container/PointerTypes.h"
#include "Container/StringView.h"
#include "Math/Vector3.h"
#include "Object/EntityHandle.h"
#include "Rendering/FoliageTypes.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/SkyOcclusion.h"

namespace NorvesLib::Core
{
    class Entity;
    class World;
}

namespace NorvesLib::Core::Component
{
    class MegaGeometryScatterComponent;
}

namespace NorvesLib::Core::Rendering
{
    class RenderWorld;
}

namespace NorvesLib::Core::Scatter
{
    /// NVSCT の種の名前と描画先の対応。表にない種は描かない。
    struct ScatterSpeciesBinding
    {
        Container::AnsiString Name;
        Rendering::MegaGeometry::MegaMeshHandle MegaMesh;
        /// foliage 用の雛形。Instances と Cells は使わない。
        Rendering::FoliageBatch FoliageTemplate;
        bool bCastShadow = true;
        /// カメラからチャンクの箱までの常駐距離[m]。
        float ResidencyDistance = 128.0f;
        /// 小さいほど先に要求する。
        uint32_t Priority = 0;
    };

    struct ScatterBudget
    {
        uint32_t MaxResidentPerFrame = 8;
        /// 元のレコードのバイト上限。最初の候補は上限によらず常駐させる。
        uint64_t MaxBytesPerFrame = 256 * 1024;
        /// 常駐作業の時間上限[ms]。0 なら時間では切らない。
        float MaxMillisecondsPerFrame = 1.0f;
        float ReleaseHysteresisMeters = 16.0f;
    };

    struct ScatterResidencyStats
    {
        uint32_t ResidentUnits = 0;
        uint64_t ResidentBytes = 0;
        uint32_t PendingUnits = 0;
        uint32_t ResidentThisFrame = 0;
        uint64_t BytesThisFrame = 0;
        uint32_t ReleasedThisFrame = 0;
    };

    /// 常駐・解放を行った時点の Update の通し番号と、元データのチャンク・種の番号。
    struct ScatterResidencyEvent
    {
        uint32_t Frame = 0;
        uint32_t Chunk = 0;
        uint32_t Species = 0;
        bool bResident = true;
    };

    using ScatterHandle = uint32_t;
    inline constexpr ScatterHandle kInvalidScatterHandle = 0;

    /// 散布の 1 本の識別子。Index は種の番号順に各種の範囲を続けて数えたチャンク内の通し番号。
    struct ScatterInstanceId
    {
        ScatterHandle Scatter = kInvalidScatterHandle;
        uint32_t Chunk = 0;
        uint32_t Index = 0;
    };

    /// Scatter が無効なら全散布。SpeciesMask は種の番号のビット（64 種まで）。
    struct ScatterQueryFilter
    {
        ScatterHandle Scatter = kInvalidScatterHandle;
        uint64_t SpeciesMask = ~0ull;
    };

    struct ScatterQueryHit
    {
        ScatterInstanceId Id;
        uint32_t Species = 0;
        Math::Vector3 Position;
        float YawRadians = 0.0f;
        float Scale = 1.0f;
    };

    /// すべての操作は GameThread の並列の段の外で行う。
    class ScatterSystem
    {
    public:
        ScatterSystem() = default;
        ScatterSystem(const ScatterSystem&) = delete;
        ScatterSystem& operator=(const ScatterSystem&) = delete;

        void SetRenderWorld(Rendering::RenderWorld* renderWorld);
        void SetBudget(const ScatterBudget& budget);
        const ScatterBudget& GetBudget() const;
        /// mega の種ごとに原点・単位変換の Entity と Component を作る。失敗時は 0。
        ScatterHandle Load(World& world, Container::TSharedPtr<const Asset::CookedScatterData> data,
                           Container::Span<const ScatterSpeciesBinding> bindings);
        void Unload(ScatterHandle handle);
        /// 散布に重複しない名前を付ける。空なら名前を外す。
        bool SetScatterName(ScatterHandle handle, Container::AnsiStringView name);
        ScatterHandle FindScatter(Container::AnsiStringView name) const;
        Container::TSharedPtr<const Asset::CookedScatterData> GetData(ScatterHandle handle) const;
        /// 距離による常駐を切り替える。無効にすると固定されていない単位を解放する。
        bool SetDistanceResidency(ScatterHandle handle, bool bEnabled);
        /// チャンクを予算によらず常駐させる。mega の行が未準備なら後の Update で常駐させる。
        bool PinChunk(ScatterHandle handle, uint32_t chunk);
        /// 固定と休眠を解除する。距離による常駐が無効ならその場で解放する。
        bool UnpinChunk(ScatterHandle handle, uint32_t chunk);
        bool IsChunkPinned(ScatterHandle handle, uint32_t chunk) const;
        /// データのある全種の単位が常駐しているか。対象の単位がなければ true。
        bool IsChunkResident(ScatterHandle handle, uint32_t chunk) const;
        /// 常駐を保ったままチャンクの表示を休眠・再開する。
        bool SetChunkDormant(ScatterHandle handle, uint32_t chunk, bool bDormant);
        bool IsChunkDormant(ScatterHandle handle, uint32_t chunk) const;
        /// XZ の半開区間に格子の中心が入るチャンクを番号順に返す。
        void CollectChunksInBounds(ScatterHandle handle, const Math::Vector3& boundsMin, const Math::Vector3& boundsMax,
                                   Container::VariableArray<uint32_t>& outChunks) const;
        uint64_t GetChunkBytes(ScatterHandle handle, uint32_t chunk) const;
        void Update(const Math::Vector3& cameraPosition);
        /// この散布の距離内と固定の要求、および mega の行の作成がすべて済んだか。
        bool IsSettled(ScatterHandle handle) const;
        const ScatterResidencyStats& GetStats() const;
        /// 最後に作り直した foliage の一覧。
        const Rendering::FoliageScene& GetFoliageScene() const;
        /// foliage の一覧を作り直すたびに増える番号。
        uint64_t GetFoliageVersion() const;
        /// ビューは次の Update・固定や解放の操作・Unload・Clear まで有効。
        Container::Span<const ScatterResidencyEvent> GetResidencyLog(ScatterHandle handle) const;
        Component::MegaGeometryScatterComponent* GetMegaComponent(ScatterHandle handle, uint32_t speciesIndex) const;
        /// 範囲の単位は 64 行。該当しなければ first と count を 0 にする。
        void GetChunkRanges(ScatterHandle handle, uint32_t chunk, uint32_t species,
                            uint32_t& outFirst, uint32_t& outCount) const;
        bool IsResident(ScatterHandle handle, uint32_t chunk, uint32_t species) const;
        /// center から 3 次元の距離が radius 以下の常駐個体を渡す。隠した個体は渡さない。
        /// 球と交わるチャンクの対象の単位がすべて常駐なら true。負・非有限の radius は何も呼ばず true。
        template <typename TVisitor>
        bool ForEachInstanceInRadius(const Math::Vector3& center, float radius, const ScatterQueryFilter& filter,
                                     TVisitor&& visitor) const
        {
            auto callback = [&visitor](const ScatterQueryHit& hit)
            {
                visitor(hit);
            };
            return VisitInstancesInRadius(center, radius, filter, &callback,
                                          [](void* context, const ScatterQueryHit& hit)
            {
                (*static_cast<decltype(callback)*>(context))(hit);
            });
        }
        /// 範囲外の識別子は false。非表示の印はチャンクの解放後も保持する。
        bool SetInstanceHidden(const ScatterInstanceId& id, bool bHidden);
        bool IsInstanceHidden(const ScatterInstanceId& id) const;
        uint32_t GetHiddenInstanceCount(ScatterHandle handle) const;
        /// 非表示の集合をバイト列にする。失敗時は出力を変えない。
        bool SerializeHiddenInstances(ScatterHandle handle, Container::VariableArray<uint8_t>& outBytes) const;
        /// バイト列から非表示の集合を置き換える。失敗時は何も変えない。
        bool RestoreHiddenInstances(ScatterHandle handle, Container::Span<const uint8_t> bytes);
        void Clear();

    private:
        struct ChunkUnit
        {
            uint32_t FirstRange = 0;
            uint32_t RangeCount = 0;
            uint64_t Bytes = 0;
            bool bResident = false;
            bool bRequested = false;
            bool bDistanceRequested = false;
            bool bWarnedUnsortedRank = false;
            /// このチャンク・種だけが持つ常駐配列。解放時は容量ごと返す。
            Container::VariableArray<Rendering::FoliageInstance> FoliageInstances;
            Rendering::FoliageCell FoliageBounds;
        };

        struct SpeciesState
        {
            Asset::ScatterRenderPath RenderPath = Asset::ScatterRenderPath::Mega;
            EntityHandle OwnerHandle;
            uint64_t ObjectId = 0;
            uint64_t ComponentId = 0;
            float ResidencyDistance = 0.0f;
            uint32_t Priority = 0;
            Rendering::FoliageBatch FoliageTemplate;
            bool bCastShadow = true;
            bool bRayTracingExcluded = false;
            Rendering::SkyOccluderSpeciesDesc SkyOccluderDesc;
            Container::VariableArray<ChunkUnit> Chunks;
        };

        struct ChunkState
        {
            bool bPinned = false;
            bool bDormant = false;
        };

        struct SkyOccluderChunk
        {
            uint32_t ChunkIndex = 0;
            uint64_t Key = 0;
            uint64_t Revision = 0;
            Container::VariableArray<Rendering::SkyOccluderProxy> Occluders;
        };

        struct ScatterState
        {
            ScatterHandle Handle = kInvalidScatterHandle;
            World* WorldPtr = nullptr;
            Container::TSharedPtr<const Asset::CookedScatterData> Data;
            Container::AnsiString Name;
            bool bDistanceResidency = true;
            Container::VariableArray<ChunkState> Chunks;
            Container::VariableArray<SpeciesState> Species;
            /// 描画用の常駐配列とは独立した、チャンク内の番号の昇順集合。
            Container::VariableArray<Container::VariableArray<uint32_t>> HiddenInstances;
            Container::VariableArray<ScatterResidencyEvent> Log;
            bool bSkyOccluderKeysValid = true;
            Container::VariableArray<SkyOccluderChunk> SkyOccluderChunks;
        };

        ScatterState* Find(ScatterHandle handle);
        const ScatterState* Find(ScatterHandle handle) const;
        Entity* ResolveOwner(const ScatterState& scatter, const SpeciesState& species) const;
        Component::MegaGeometryScatterComponent* ResolveMega(const ScatterState& scatter, const SpeciesState& species) const;
        void ReleaseEntities(ScatterState& scatter);
        void RecountStats();
        void RecordEvent(ScatterState& scatter, uint32_t chunk, uint32_t species, bool bResident);
        bool ReleaseChunkUnits(ScatterState& scatter, uint32_t chunk);
        bool MakeFoliageResident(ScatterState& scatter, uint32_t chunk, uint32_t species);
        bool BuildFoliageUnit(const ScatterState& scatter, uint32_t chunk, uint32_t species,
                              const Container::VariableArray<uint32_t>& hidden, ChunkUnit& outUnit) const;
        bool ResolveInstanceIndex(const ScatterState& scatter, uint32_t chunk, uint32_t index,
                                  uint32_t& outSpecies, uint32_t& outLocalIndex) const;
        void ApplyMegaHiddenInstances(const ScatterState& scatter, uint32_t chunk, uint32_t species) const;
        bool VisitInstancesInRadius(const Math::Vector3& center, float radius, const ScatterQueryFilter& filter,
                                    void* context, void (*visitor)(void*, const ScatterQueryHit&)) const;
        void RebuildFoliageScene();
        void RebuildSkyOccluderChunk(ScatterState& scatter, uint32_t chunk);
        void PublishSkyOccluders();

        Container::VariableArray<ScatterState> m_Scatters;
        ScatterBudget m_Budget;
        ScatterResidencyStats m_Stats;
        Rendering::FoliageScene m_FoliageScene;
        uint64_t m_FoliageVersion = 0;
        uint64_t m_SkyOccluderRevision = 0;
        bool m_bSkyOccludersDirty = false;
        /// foliage の描画先の借用参照。初期化済みの間だけ一覧を渡す。
        Rendering::RenderWorld* m_RenderWorld = nullptr;
        ScatterHandle m_NextHandle = 1;
        uint32_t m_Frame = 0;
    };
} // namespace NorvesLib::Core::Scatter
