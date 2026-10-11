#pragma once

#include "Math/GeometryTypes.h"
#include "Scene/PhysicsQueryTypes.h"
#include "Container/Containers.h"
#include "Container/Span.h"
#include "Ecs/EcsTypes.h"
#include "Object/EntityHandle.h"
#include "Thread/Thread.h"
#include <cstdint>
#include <cstddef>

namespace NorvesLib::Core
{
    class Entity;
    class World;
}

namespace NorvesLib::Debug
{
    class EngineChecks;
}

namespace NorvesLib::Core::Scene
{
    class IPhysicsSceneQueryProvider
    {
    public:
        virtual ~IPhysicsSceneQueryProvider() = default;

        // 未対応の既存providerもsource互換を維持する。非Successは出力を空にする。
        virtual EPhysicsSceneQueryResult ExecuteQuery(const PhysicsQueryDesc&,
            Container::VariableArray<PhysicsQueryHit>& outHits) const
        {
            outHits.clear();
            return EPhysicsSceneQueryResult::Unavailable;
        }

        // Successはbatch成立。個別成否はoutResultsで返す。未対応providerは空/Unavailable。
        virtual EPhysicsSceneQueryResult ExecuteBatch(Container::Span<const PhysicsQueryDesc>,
            Container::VariableArray<PhysicsQueryHit>& outHits,
            Container::VariableArray<PhysicsQueryBatchResult>& outResults) const
        {
            outHits.clear();
            outResults.clear();
            return EPhysicsSceneQueryResult::Unavailable;
        }

        // 明示query更新。未対応providerはUnavailable。固定更新を進めない。
        virtual EPhysicsSceneQueryResult RefreshDynamicSnapshot()
        {
            return EPhysicsSceneQueryResult::Unavailable;
        }

        virtual EPhysicsSceneQueryResult Raycast(
            const Math::Ray& ray,
            float maxDistance,
            PhysicsRaycastHit& outHit) const = 0;
        virtual EPhysicsSceneQueryResult OverlapSphere(
            const Math::Sphere& sphere,
            Container::VariableArray<PhysicsOverlapHit>& outHits) const = 0;
        virtual EPhysicsSceneQueryResult OverlapBox(
            const Math::OBB& box,
            Container::VariableArray<PhysicsOverlapHit>& outHits) const = 0;
        virtual EPhysicsSceneQueryResult OverlapCapsule(
            const Math::Capsule& capsule,
            Container::VariableArray<PhysicsOverlapHit>& outHits) const = 0;
        virtual EPhysicsSceneQueryResult IsAlive(ColliderHandle collider, bool& outAlive) const = 0;
        virtual EPhysicsSceneQueryResult IsAlive(BodyHandle body, bool& outAlive) const = 0;
        virtual EPhysicsSceneQueryResult GetPublishedSnapshotSequence(uint64_t& outSequence) const = 0;
    };

    /**
     * @brief Raycast の結果。
     *
     * HitEntity は非所有の借用ポインタです。次の SceneQuery::Rebuild / Update または World 変更まで有効です。
     */
    struct RaycastHit
    {
        Entity* HitEntity = nullptr;
        float Distance = 0.0f;
        bool bHit = false;
    };

    /**
     * @brief GameThread 専有のシーン空間検索キャッシュ。
     *
     * Entity* は非所有の借用ポインタとして保持します。保持ポインタは次の Rebuild / Update または World 変更まで有効です。
     *
     * 更新は 2 通り:
     * - Rebuild: 全 Entry を入れ替えて BVH を作り直す（テスト・World を持たない呼び出し・旧の経路）。
     * - Update: World の変更（ECS の変更ログ・行の削除のフック・描画同期の一覧）から、変わった Entity の境界だけを
     *   葉から親へ refit し、追加は保留の一覧、削除は墓石にする。保留・墓石・累積した更新が増えたときだけ BVH を作り直す。
     *   描画同期が旧の全走査のとき・SceneView が無いときは、毎回 Rebuild と同じ結果にする。
     */
    class SceneQuery
    {
    public:
        /// 直近の Update が行った仕事。静止では Updated・Added・Removed・Rebuilt がすべて 0
        struct UpdateStats
        {
            /// 境界が変わって refit した Entry の数
            uint32_t Updated = 0;
            /// 追加した Entry の数
            uint32_t Added = 0;
            /// 外した Entry の数
            uint32_t Removed = 0;
            /// BVH を作り直したか（0 か 1）
            uint32_t Rebuilt = 0;
            /// 更新の後の Entry の数
            uint32_t Entries = 0;
        };

        /// 行の削除のフックへ渡す相手（内部用）。World が替わったあとの古い呼び出しを見分ける印を持つ
        struct HookUser
        {
            Container::VariableArray<uint64_t>* Destroyed = nullptr;
            /// 追っている ECS の World（SceneQuery が持つ m_TrackedEcs）。Tag と違えば古い World の呼び出し
            const void* const* Current = nullptr;
            const void* Tag = nullptr;
        };

        SceneQuery();
        ~SceneQuery() = default;
        SceneQuery(const SceneQuery&) = delete;
        SceneQuery(SceneQuery&&) = delete;
        SceneQuery& operator=(const SceneQuery&) = delete;
        SceneQuery& operator=(SceneQuery&&) = delete;

        void Rebuild(const World& world);
        void Rebuild(Container::Span<Entity* const> entities);

        /// World の変更だけを反映する（毎フレーム、描画の同期の後に呼ぶ）。最初の呼び出しと切り戻しでは全量で作る。
        void Update(World& world);
        [[nodiscard]] const UpdateStats& GetLastUpdateStats() const
        {
            return m_LastStats;
        }

        /// 増分で保った結果が、全量で作った結果と一致するかを確かめる（検査 ECS_SCENEQUERY。有効なときだけ重い照合を走らせる）
        void RunCheck(const World& world, Debug::EngineChecks& checks);

        /// 増分で保った Entry と照会の結果を、全量で作った SceneQuery と照合する。食い違いがあれば false で、最初の説明を outDetail へ書く。
        bool VerifyAgainstRebuild(const World& world, uint32_t& outExamined, char* outDetail, size_t detailSize) const;

        // 保持中の全Entryを破棄する(shutdown時のdangling Entity*回避)。
        void Clear();
        bool Raycast(const Math::Ray& ray, RaycastHit& outHit) const;
        void OverlapSphere(const Math::Sphere& sphere, Container::VariableArray<Entity*>& outEntities) const;
        void OverlapBox(const Math::AABB& box, Container::VariableArray<Entity*>& outEntities) const;
        void QueryFrustum(const Math::Frustum& frustum, Container::VariableArray<Entity*>& outEntities) const;
        size_t GetEntryCount() const;

        // GameThread上で同じ公開snapshotを読む。非Successとprovider例外でoutHitsを空にする。
        // 例外は出力をclearして再送出。入力/出力の格納領域は重ならないこと。
        EPhysicsSceneQueryResult ExecuteQuery(const PhysicsQueryDesc& query,
            Container::VariableArray<PhysicsQueryHit>& outHits) const;

        // 要求順のResult/FirstHit/HitCount。個別失敗は0hitで継続、全体失敗/例外は両出力をclear。
        // 準備済み空batchはSuccess。入力と出力の格納領域は重ならず、同期呼出し中有効なこと。
        EPhysicsSceneQueryResult ExecuteBatch(Container::Span<const PhysicsQueryDesc> queries,
            Container::VariableArray<PhysicsQueryHit>& outHits,
            Container::VariableArray<PhysicsQueryBatchResult>& outResults) const;

        // GameThread専用。固定step sequenceを進めずqueryだけ更新。GR09までは全proxy再構築。
        // 同sequence内でも内容が変わるため、呼出側は自身のquery cacheを無効化すること。
        EPhysicsSceneQueryResult RefreshDynamicSnapshot();

        EPhysicsSceneQueryResult BindPhysicsProvider(IPhysicsSceneQueryProvider& provider);
        EPhysicsSceneQueryResult UnbindPhysicsProvider(IPhysicsSceneQueryProvider& provider);
        EPhysicsSceneQueryResult Raycast(
            const Math::Ray& ray,
            float maxDistance,
            PhysicsRaycastHit& outHit) const;
        EPhysicsSceneQueryResult OverlapSphere(
            const Math::Sphere& sphere,
            Container::VariableArray<PhysicsOverlapHit>& outHits) const;
        EPhysicsSceneQueryResult OverlapBox(
            const Math::OBB& box,
            Container::VariableArray<PhysicsOverlapHit>& outHits) const;
        EPhysicsSceneQueryResult OverlapCapsule(
            const Math::Capsule& capsule,
            Container::VariableArray<PhysicsOverlapHit>& outHits) const;
        EPhysicsSceneQueryResult IsAlive(ColliderHandle collider, bool& outAlive) const;
        EPhysicsSceneQueryResult IsAlive(BodyHandle body, bool& outAlive) const;
        /**
         * @brief 最後の固定更新で公開された physics query snapshot のsequenceを取得する（明示refreshでは増加しない）。
         * @param outSequence Success 時は 1 以上。失敗時は 0 に初期化される。
         */
        EPhysicsSceneQueryResult GetPublishedSnapshotSequence(uint64_t& outSequence) const;

    private:
        static constexpr uint32_t kNoNode = 0xFFFFFFFFu;

        struct Entry
        {
            /// null は外した Entry（墓石）。BVH の葉の範囲に残り、照会は飛ばす
            Entity* EntityPtr = nullptr;
            Math::AABB Bounds;
            uint32_t OriginalIndex = 0;
            /// このEntryを含む葉の節（BVH に入っている Entry だけ）
            uint32_t Leaf = kNoNode;
            /// 増分で追う Entity の行（EcsEntity を詰めた値）。Rebuild で作った Entry は 0
            uint64_t RowKey = 0;
        };

        struct BVHNode
        {
            Math::AABB Bounds;
            uint32_t Start = 0;
            uint32_t Count = 0;
            uint32_t Left = 0;
            uint32_t Right = 0;
            uint32_t Parent = kNoNode;
            bool bLeaf = false;
        };

        /// 増分で追う Entity（MeshComponent を持つもの）。Entry を持つのは、境界があり有効な Entity だけ
        struct TrackedEntity
        {
            Entity* EntityPtr = nullptr;
            Ecs::EcsEntity Row;
            /// m_Entries（bPending が false）か m_Pending（true）の添字。Entry が無いとき -1
            int32_t Index = -1;
            bool bPending = false;
            /// この Entity の MeshComponent の ComponentId
            Container::VariableArray<uint64_t> MeshIds;
        };

        static void CollectRecursive(Entity* entity, Container::VariableArray<Entry>& entries);
        static float GetCenterAxis(const Math::AABB& bounds, uint32_t axis);

        void BuildBVH();
        uint32_t BuildNode(uint32_t start, uint32_t count, uint32_t parent);
        bool RaycastNode(
            uint32_t nodeIndex,
            const Math::Ray& ray,
            RaycastHit& outHit,
            float& bestT,
            uint32_t& bestOriginalIndex) const;
        void RaycastEntry(
            const Entry& entry,
            const Math::Ray& ray,
            RaycastHit& outHit,
            float& bestT,
            uint32_t& bestOriginalIndex,
            bool& bFoundHit) const;
        void CollectSphereNode(
            uint32_t nodeIndex,
            const Math::Sphere& sphere,
            Container::VariableArray<Entity*>& outEntities) const;
        void CollectBoxNode(
            uint32_t nodeIndex,
            const Math::AABB& box,
            Container::VariableArray<Entity*>& outEntities) const;
        void CollectFrustumNode(
            uint32_t nodeIndex,
            const Math::Frustum& frustum,
            Container::VariableArray<Entity*>& outEntities) const;
        bool IsOwnerThread() const;

        // ---- 増分の更新 ----
        bool AttachTracking(World& world);
        void DetachTracking();
        void ForgetTrackedData();
        void BuildTrackedFromWorld(World& world);
        void RefreshEntity(const World& world, Entity& entity);
        void RefreshSubtree(const World& world, Entity& entity);
        void ApplyBounds(TrackedEntity& tracked, uint64_t rowKey, bool bMember, const Math::AABB& bounds);
        void RemoveTrackedEntity(uint64_t rowKey);
        void RemoveEntry(TrackedEntity& tracked);
        void RefitFrom(uint32_t leafNode);
        void RebuildTree();
        bool NeedsRebuild() const;
        void ReportUpdate();

        Container::VariableArray<BVHNode> m_Nodes;
        Container::VariableArray<Entry> m_Entries;
        /// BVH に入れていない追加分。照会は BVH の後にこれを線形に見る
        Container::VariableArray<Entry> m_Pending;
        IPhysicsSceneQueryProvider* m_PhysicsProvider = nullptr;
        Thread::Thread::ThreadId m_OwnerThreadId;

        // 増分の状態
        World* m_TrackedWorld = nullptr;
        const void* m_TrackedEcs = nullptr;
        uint32_t m_Consumers[3] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        HookUser m_HookUser;
        Container::UnorderedMap<uint64_t, TrackedEntity> m_Tracked;
        /// MeshComponent の ComponentId → それを持つ Entity の行
        Container::UnorderedMap<uint64_t, uint64_t> m_MeshOwner;
        Container::VariableArray<uint64_t> m_DestroyedRows;
        Container::UnorderedSet<uint64_t> m_RefreshedRows;
        Container::UnorderedSet<uint64_t> m_DescendedRows;
        uint32_t m_TombstoneCount = 0;
        uint32_t m_NextOriginalIndex = 0;
        /// 最後に BVH を作ってから refit した回数
        uint64_t m_UpdatesSinceBuild = 0;

        UpdateStats m_LastStats;
        uint64_t m_UpdateCalls = 0;
        uint64_t m_TotalUpdated = 0;
        uint64_t m_TotalRebuilt = 0;
        uint64_t m_CheckCalls = 0;
        uint64_t m_LastCheckReport = 0;
    };

} // namespace NorvesLib::Core::Scene
