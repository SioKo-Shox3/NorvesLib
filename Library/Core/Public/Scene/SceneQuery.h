#pragma once

#include "Math/GeometryTypes.h"
#include "Scene/PhysicsQueryTypes.h"
#include "Container/Containers.h"
#include "Container/Span.h"
#include "Object/EntityHandle.h"
#include "Thread/Thread.h"
#include <cstdint>
#include <cstddef>

namespace NorvesLib::Core
{
    class Entity;
    class World;
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
     * HitEntity は非所有の借用ポインタです。次の SceneQuery::Rebuild または World 変更まで有効です。
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
     * Entity* は非所有の借用ポインタとして保持します。毎 Rebuild で全 Entry を入れ替え、
     * 保持ポインタは次の Rebuild または World 変更まで有効です。
     */
    class SceneQuery
    {
    public:
        SceneQuery();
        SceneQuery(const SceneQuery&) = delete;
        SceneQuery(SceneQuery&&) = delete;
        SceneQuery& operator=(const SceneQuery&) = delete;
        SceneQuery& operator=(SceneQuery&&) = delete;

        void Rebuild(const World& world);
        void Rebuild(Container::Span<Entity* const> entities);

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
        struct Entry
        {
            Entity* EntityPtr = nullptr;
            Math::AABB Bounds;
            uint32_t OriginalIndex = 0;
        };

        struct BVHNode
        {
            Math::AABB Bounds;
            uint32_t Start = 0;
            uint32_t Count = 0;
            uint32_t Left = 0;
            uint32_t Right = 0;
            bool bLeaf = false;
        };

        static void CollectRecursive(Entity* entity, Container::VariableArray<Entry>& entries);
        static float GetCenterAxis(const Math::AABB& bounds, uint32_t axis);

        void BuildBVH();
        uint32_t BuildNode(uint32_t start, uint32_t count);
        bool RaycastNode(
            uint32_t nodeIndex,
            const Math::Ray& ray,
            RaycastHit& outHit,
            float& bestT,
            uint32_t& bestOriginalIndex) const;
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

        Container::VariableArray<BVHNode> m_Nodes;
        Container::VariableArray<Entry> m_Entries;
        IPhysicsSceneQueryProvider* m_PhysicsProvider = nullptr;
        Thread::Thread::ThreadId m_OwnerThreadId;
    };

} // namespace NorvesLib::Core::Scene
