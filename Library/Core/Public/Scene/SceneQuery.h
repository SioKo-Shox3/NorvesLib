#pragma once

#include "Math/GeometryTypes.h"
#include "Container/Containers.h"
#include "Container/Span.h"
#include <cstddef>

namespace NorvesLib::Core
{
    class Entity;
    class World;
}

namespace NorvesLib::Core::Scene
{
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
        void Rebuild(const World& world);
        void Rebuild(Container::Span<Entity* const> entities);

        // 保持中の全Entryを破棄する(shutdown時のdangling Entity*回避)。
        void Clear();
        bool Raycast(const Math::Ray& ray, RaycastHit& outHit) const;
        size_t GetEntryCount() const;

    private:
        struct Entry
        {
            Entity* EntityPtr = nullptr;
            Math::AABB Bounds;
        };

        static void CollectRecursive(Entity* entity, Container::VariableArray<Entry>& entries);

        Container::VariableArray<Entry> m_Entries;
    };

} // namespace NorvesLib::Core::Scene
