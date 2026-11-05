// 焼いた散布の要求・常駐予算・解放と、種ごとの描画経路を GameThread で管理する。
// ScatterSystem は Engine::Engine が所有し、Entity は Load に渡した World の木が所有する。
// Unload で Entity を World に返し、CookedScatterData は共有参照で保持する。
// World が先に Finalize された場合、Clear は Entity に触らず状態だけ捨てる。
// foliage はチャンクを FoliageCell とし、常駐の組が変わったときだけ種ごとの束を作り直す。
// foliage の影は結び付けの bCastShadow に従い、無効なら束の ShadowDistance を 0 にする。
#pragma once

#include "Asset/CookedScatterFormat.h"
#include "Container/PointerTypes.h"
#include "Math/Vector3.h"
#include "Object/EntityHandle.h"
#include "Rendering/FoliageTypes.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"

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

    /// 常駐・解放を行った Update の通し番号と、元データのチャンク・種の番号。
    struct ScatterResidencyEvent
    {
        uint32_t Frame = 0;
        uint32_t Chunk = 0;
        uint32_t Species = 0;
        bool bResident = true;
    };

    using ScatterHandle = uint32_t;
    inline constexpr ScatterHandle kInvalidScatterHandle = 0;

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
        void Update(const Math::Vector3& cameraPosition);
        /// この散布の距離内の要求と、mega の行の作成がすべて済んだか。
        bool IsSettled(ScatterHandle handle) const;
        const ScatterResidencyStats& GetStats() const;
        /// 最後に作り直した foliage の一覧。
        const Rendering::FoliageScene& GetFoliageScene() const;
        /// foliage の一覧を作り直すたびに増える番号。
        uint64_t GetFoliageVersion() const;
        /// ビューは次の Update・Unload・Clear まで有効。
        Container::Span<const ScatterResidencyEvent> GetResidencyLog(ScatterHandle handle) const;
        Component::MegaGeometryScatterComponent* GetMegaComponent(ScatterHandle handle, uint32_t speciesIndex) const;
        /// 範囲の単位は 64 行。該当しなければ first と count を 0 にする。
        void GetChunkRanges(ScatterHandle handle, uint32_t chunk, uint32_t species,
                            uint32_t& outFirst, uint32_t& outCount) const;
        bool IsResident(ScatterHandle handle, uint32_t chunk, uint32_t species) const;
        void Clear();

    private:
        struct ChunkUnit
        {
            uint32_t FirstRange = 0;
            uint32_t RangeCount = 0;
            uint64_t Bytes = 0;
            bool bResident = false;
            bool bRequested = false;
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
            Container::VariableArray<ChunkUnit> Chunks;
        };

        struct ScatterState
        {
            ScatterHandle Handle = kInvalidScatterHandle;
            World* WorldPtr = nullptr;
            Container::TSharedPtr<const Asset::CookedScatterData> Data;
            Container::VariableArray<SpeciesState> Species;
            Container::VariableArray<ScatterResidencyEvent> Log;
        };

        const ScatterState* Find(ScatterHandle handle) const;
        Entity* ResolveOwner(const ScatterState& scatter, const SpeciesState& species) const;
        Component::MegaGeometryScatterComponent* ResolveMega(const ScatterState& scatter, const SpeciesState& species) const;
        void ReleaseEntities(ScatterState& scatter);
        void RecountStats();
        void RecordEvent(ScatterState& scatter, uint32_t chunk, uint32_t species, bool bResident);
        bool MakeFoliageResident(ScatterState& scatter, uint32_t chunk, uint32_t species);
        void RebuildFoliageScene();

        Container::VariableArray<ScatterState> m_Scatters;
        ScatterBudget m_Budget;
        ScatterResidencyStats m_Stats;
        Rendering::FoliageScene m_FoliageScene;
        uint64_t m_FoliageVersion = 0;
        /// foliage の描画先の借用参照。初期化済みの間だけ一覧を渡す。
        Rendering::RenderWorld* m_RenderWorld = nullptr;
        ScatterHandle m_NextHandle = 1;
        uint32_t m_Frame = 0;
    };
} // namespace NorvesLib::Core::Scatter
