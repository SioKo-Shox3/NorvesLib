#include "Object/Entity.h"
#include "Object/World.h"
#include "Object/Reflection.h"
#include "Object/ObjectCast.h"
#include "Component/Component.h"
#include "Component/MeshComponent.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core
{
    // IMPLEMENT_CLASSマクロを使用してリフレクション実装を生成
    IMPLEMENT_CLASS(Entity, Object)

    namespace
    {
        Math::Vector3 ComponentMultiply(const Math::Vector3& lhs, const Math::Vector3& rhs)
        {
            return Math::Vector3(lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z);
        }

        Math::Vector3 ComponentDivide(const Math::Vector3& lhs, const Math::Vector3& rhs)
        {
            return Math::Vector3(
                rhs.x != 0.0f ? lhs.x / rhs.x : 0.0f,
                rhs.y != 0.0f ? lhs.y / rhs.y : 0.0f,
                rhs.z != 0.0f ? lhs.z / rhs.z : 0.0f);
        }

        Math::Quaternion InverseRotation(const Math::Quaternion& rotation)
        {
            return Math::Quaternion(-rotation.x, -rotation.y, -rotation.z, rotation.w);
        }

        float AbsFloat(float value)
        {
            return value < 0.0f ? -value : value;
        }

        float MaxFloat(float lhs, float rhs)
        {
            return lhs > rhs ? lhs : rhs;
        }

        Math::Transform ComposeTransform(const Math::Transform& parent, const Math::Transform& local)
        {
            return Math::Transform(
                parent.position + parent.rotation * ComponentMultiply(parent.scale, local.position),
                parent.rotation * local.rotation,
                ComponentMultiply(parent.scale, local.scale));
        }

        Math::Transform MakeLocalTransformFromWorld(
            const Math::Transform& parentWorld,
            const Math::Transform& worldTransform)
        {
            const Math::Quaternion parentInverseRotation = InverseRotation(parentWorld.rotation);
            const Math::Vector3 localPosition = ComponentDivide(
                parentInverseRotation * (worldTransform.position - parentWorld.position),
                parentWorld.scale);
            const Math::Quaternion localRotation = parentInverseRotation * worldTransform.rotation;
            const Math::Vector3 localScale = ComponentDivide(worldTransform.scale, parentWorld.scale);
            return Math::Transform(localPosition, localRotation, localScale);
        }

        bool ComputeMeshWorldAABB(
            const Component::MeshComponent& meshComponent,
            const Math::Transform& worldTransform,
            Math::AABB& outAABB)
        {
            const Rendering::BoundingBox& localBounds = meshComponent.GetLocalBounds();
            if (!localBounds.IsValid())
            {
                return false;
            }

            const Math::Vector3 localCenter(
                (localBounds.MinX + localBounds.MaxX) * 0.5f,
                (localBounds.MinY + localBounds.MaxY) * 0.5f,
                (localBounds.MinZ + localBounds.MaxZ) * 0.5f);
            const Math::Vector3 localHalfExtents(
                (localBounds.MaxX - localBounds.MinX) * 0.5f,
                (localBounds.MaxY - localBounds.MinY) * 0.5f,
                (localBounds.MaxZ - localBounds.MinZ) * 0.5f);
            const float localRadius = localHalfExtents.Length();

            const float maxScale = MaxFloat(
                AbsFloat(worldTransform.scale.x),
                MaxFloat(AbsFloat(worldTransform.scale.y), AbsFloat(worldTransform.scale.z)));
            const float worldRadius = localRadius * maxScale;
            if (worldRadius <= 0.0f)
            {
                return false;
            }

            const Math::Vector3 worldCenter = worldTransform.TransformPoint(localCenter);
            outAABB = Math::AABB::FromCenterExtents(
                worldCenter,
                Math::Vector3(worldRadius, worldRadius, worldRadius));
            return true;
        }

        bool DestroyContextOwnedInner(IUnknown &owner, IUnknown *inner)
        {
            if (!inner)
            {
                return false;
            }

            IUnknown *outer = inner->GetOuter();
            if (outer == &owner)
            {
                if (!owner.RemoveInner(inner))
                {
                    return false;
                }
            }
            else if (outer)
            {
                return false;
            }
            else
            {
                owner.RemoveInner(inner);
            }

            if (inner->HasFlag(OF_HeapOwned))
            {
                inner->SetFlag(OF_PendingDestroy, true);
                return true;
            }

            inner->Finalize();
            delete inner;
            return true;
        }
    }

    Entity::Entity()
        : Object()
    {
        // PROPERTYマクロは値初期化（0/false）するため、非ゼロデフォルト値を設定
        getbTickEnabled().Set(true);
        getbActive().Set(true);
        // RotationはQuaternionのデフォルトが単位回転(0,0,0,1)なので設定不要
        getScale().Set(Math::Vector3(1.0f, 1.0f, 1.0f));
    }

    Entity::Entity(const FieldInitializer *initializer)
        : Object(initializer)
    {
        getbTickEnabled().Set(true);
        getbActive().Set(true);
        getScale().Set(Math::Vector3(1.0f, 1.0f, 1.0f));
    }

    Entity::Entity(const IUnknown *sourceObject)
        : Object(sourceObject)
    {
        getbTickEnabled().Set(true);
        getbActive().Set(true);
        getScale().Set(Math::Vector3(1.0f, 1.0f, 1.0f));
    }

    Entity::~Entity()
    {
        Finalize();
    }

    bool Entity::SetCustomTimeDilation(float value)
    {
        if (!std::isfinite(value) || value < 0)
            return false;
        // 行が付いている間は列が正（予備の m_CustomTimeDilation は行を外すときに列から戻す）
        const Ecs::EcsRowRef row = GetEcsRow();
        const Ecs::EcsTimeDilation column{value};
        if (Ecs::EcsRowHasColumn(row, Ecs::EcsColumnInfoOf<Ecs::EcsTimeDilation>().Type))
        {
            return Ecs::EcsWriteColumnPart(row, Ecs::EcsColumnInfoOf<Ecs::EcsTimeDilation>().Type, 0,
                                           static_cast<uint32_t>(sizeof(column)), &column);
        }
        m_CustomTimeDilation = value;
        return true;
    }

    float Entity::GetCustomTimeDilation() const
    {
        Ecs::EcsTimeDilation column{m_CustomTimeDilation};
        (void)Ecs::EcsReadColumnPart(GetEcsRow(), Ecs::EcsColumnInfoOf<Ecs::EcsTimeDilation>().Type, 0,
                                     static_cast<uint32_t>(sizeof(column)), &column);
        return column.CustomTimeDilation;
    }

    void Entity::Initialize()
    {
        Object::Initialize();
    }

    void Entity::Finalize()
    {
        if (!HasFlag(OF_Initialized))
        {
            return;
        }

        if (World* world = GetWorld())
        {
            if (world->IsDeferringObjectRemoval())
            {
                MarkForDestroy();
                return;
            }
            world->ForgetPendingDestroy(*this);
            // 木にいるまま解放される場合も、解放の前に自身と部分木の行を切る
            world->UnbindEntitySubtreeRows(*this);
        }
        while (!m_Inners.empty())
        {
            IUnknown *inner = m_Inners.back();
            if (World* world = GetWorld())
            {
                if (auto* child = CastTo<Entity>(inner))
                {
                    world->InvalidateTickEntitySubtree(*child);
                }
                else if (auto* component = CastTo<Component::Component>(inner))
                {
                    world->InvalidateTickComponent(*component);
                }
            }
            if (!DestroyContextOwnedInner(*this, inner))
            {
                RemoveInner(inner);
            }
        }

        Object::Finalize();
    }

    void Entity::SetActive(bool bNewActive)
    {
        if (IsActive() == bNewActive)
        {
            return;
        }

        getbActive().Set(bNewActive);
        MarkRenderStateDirtyRecursive();
    }

    // ========================================
    // World関連（Outer経由）
    // ========================================

    World *Entity::GetWorld() const
    {
        // Outer関係はオーナーシップの概念であり、constの制約を受けない
        IUnknown *outer = const_cast<IUnknown *>(GetOuter());
        while (outer)
        {
            if (auto *world = CastTo<World>(outer))
            {
                return world;
            }
            outer = outer->GetOuter();
        }
        return nullptr;
    }

    bool Entity::IsInWorld() const
    {
        return GetWorld() != nullptr;
    }

    Ecs::EcsRowRef Entity::GetEcsRow() const
    {
        World *world = GetWorld();
        if (!world || m_EcsEntity.IsNull())
        {
            return {};
        }
        return Ecs::EcsRowRef{&world->GetEcsWorld(), m_EcsEntity};
    }

    bool Entity::RemoveInner(IUnknown* inner)
    {
        if (!inner)
        {
            return false;
        }
        bool bFound = false;
        for (IUnknown* candidate : m_Inners)
        {
            if (candidate == inner)
            {
                bFound = true;
                break;
            }
        }
        if (!bFound)
        {
            return false;
        }
        if (World* world = GetWorld())
        {
            if (auto* component = CastTo<Component::Component>(inner))
            {
                world->InvalidateTickComponent(*component);
                // Component は解放や付け替えのために外れる。外す前に行を切る（値を予備へ戻す）
                world->ReleaseComponentRow(*component);
            }
            else if (auto* child = CastTo<Entity>(inner))
            {
                world->InvalidateTickEntitySubtree(*child);
            }
            // 木から外れたものは、破棄予約の一覧から外す（外した後に削除されても一覧が壊れない）
            if (auto* child = CastTo<Entity>(inner))
            {
                world->ForgetPendingDestroySubtree(*child);
                // 子 Entity は解放や付け替えのために外れる。外す前に部分木の行を切る
                world->UnbindEntitySubtreeRows(*child);
            }
            else
            {
                world->ForgetPendingDestroy(*inner);
            }
        }
        return UnknownImpl::RemoveInner(inner);
    }

    bool Entity::AddInner(IUnknown* inner)
    {
        if (CastTo<Entity>(inner) && IsInWorld())
        {
            return false;
        }

        const bool bAdded = UnknownImpl::AddInner(inner);
        // 破棄予約済みの Component を木に戻したら、一覧へ積む
        if (bAdded && inner->HasFlag(OF_PendingDestroy) && !CastTo<Entity>(inner))
        {
            if (World* world = GetWorld())
            {
                world->RegisterPendingDestroy(*inner);
            }
        }
        // World の木にいる Entity に付けた Component には、行を作る
        if (bAdded)
        {
            if (auto* component = CastTo<Component::Component>(inner))
            {
                if (World* world = GetWorld())
                {
                    world->BindComponentRow(*component);
                }
            }
        }
        return bAdded;
    }

    // ========================================
    // トランスフォーム
    // ========================================

    void Entity::SetLocalPosition(const Math::Vector3& pos)
    {
        Math::Transform local = LoadLocalTransform();
        if (local.position == pos)
        {
            return;
        }

        local.position = pos;
        if (StoreLocalTransform(local))
        {
            MarkWorldTransformDirty();
        }
    }

    void Entity::SetLocalPosition(float x, float y, float z)
    {
        SetLocalPosition(Math::Vector3(x, y, z));
    }

    void Entity::SetLocalRotation(const Math::Quaternion& rot)
    {
        Math::Transform local = LoadLocalTransform();
        if (local.rotation == rot)
        {
            return;
        }

        local.rotation = rot;
        if (StoreLocalTransform(local))
        {
            MarkWorldTransformDirty();
        }
    }

    void Entity::SetLocalRotation(float x, float y, float z, float w)
    {
        SetLocalRotation(Math::Quaternion(x, y, z, w));
    }

    void Entity::SetLocalScale(const Math::Vector3& scale)
    {
        Math::Transform local = LoadLocalTransform();
        if (local.scale == scale)
        {
            return;
        }

        local.scale = scale;
        if (StoreLocalTransform(local))
        {
            MarkWorldTransformDirty();
        }
    }

    void Entity::SetLocalScale(float x, float y, float z)
    {
        SetLocalScale(Math::Vector3(x, y, z));
    }

    void Entity::SetWorldTransform(const Math::Transform& worldTransform)
    {
        const Entity* parent = GetParentEntity();
        if (!parent)
        {
            SetLocalTransform(worldTransform);
            return;
        }

        const Math::Transform parentWorld = parent->EvaluateWorldTransformNonMutating();
        SetLocalTransform(MakeLocalTransformFromWorld(parentWorld, worldTransform));
    }

    Math::Transform Entity::GetLocalTransform() const
    {
        return LoadLocalTransform();
    }

    Math::Transform Entity::GetWorldTransform() const
    {
        if (m_bWorldTransformDirty && !GetParentEntity())
        {
            return LoadLocalTransform();
        }

        return LoadWorldTransformCache();
    }

    void Entity::SetRenderInterpolationEnabled(bool enabled)
    {
        m_bRenderInterpolationOverride = true;
        if (m_bRenderInterpolationEnabled == enabled)
            return;
        m_bRenderInterpolationEnabled = enabled;
        ResetRenderInterpolation();
        if (World *world = GetWorld())
        {
            world->NoteRenderInterpolationChange(*this);
        }
    }
    void Entity::ClearRenderInterpolationOverride()
    {
        m_bRenderInterpolationOverride = false;
        SetDefaultRenderInterpolationEnabled(m_bRenderInterpolationDefault);
    }
    void Entity::SetDefaultRenderInterpolationEnabled(bool enabled)
    {
        m_bRenderInterpolationDefault = enabled;
        if (m_bRenderInterpolationOverride || m_bRenderInterpolationEnabled == enabled)
            return;
        m_bRenderInterpolationEnabled = enabled;
        ResetRenderInterpolation();
        if (World *world = GetWorld())
        {
            world->NoteRenderInterpolationChange(*this);
        }
    }
    Math::Transform Entity::GetRenderWorldTransform() const
    {
        // 補間する行とその子孫だけが描画の変換の列を持つ。無い・値をまだ求めていない行はワールドの変換と同じ
        Ecs::EcsRenderTransform column{};
        if (Ecs::EcsReadColumnPart(GetEcsRow(), Ecs::EcsColumnInfoOf<Ecs::EcsRenderTransform>().Type, 0,
                                   static_cast<uint32_t>(sizeof(column)), &column) &&
            column.bValid != 0)
        {
            return Math::Transform(column.position, column.rotation, column.scale);
        }
        return GetWorldTransform();
    }
    void Entity::ResetRenderInterpolation()
    {
        if (World *world = GetWorld())
        {
            world->ResetEntityRenderInterpolation(*this);
        }
    }

    bool Entity::GetWorldAABB(Math::AABB& outAABB) const
    {
        const Math::Transform worldTransform = EvaluateWorldTransformNonMutating();
        bool bHasBounds = false;

        auto components = GetComponents();
        for (auto *component : components)
        {
            auto *meshComponent = CastTo<Component::MeshComponent>(component);
            if (!meshComponent)
            {
                continue;
            }

            Math::AABB componentAABB;
            if (!ComputeMeshWorldAABB(*meshComponent, worldTransform, componentAABB))
            {
                continue;
            }

            if (!bHasBounds)
            {
                outAABB = componentAABB;
                bHasBounds = true;
                continue;
            }

            outAABB.Merge(componentAABB);
        }

        return bHasBounds;
    }

    void Entity::SetPosition(const Math::Vector3& pos)
    {
        Math::Transform worldTransform = EvaluateWorldTransformNonMutating();
        worldTransform.position = pos;
        SetWorldTransform(worldTransform);
    }

    void Entity::SetPosition(float x, float y, float z)
    {
        SetPosition(Math::Vector3(x, y, z));
    }

    Math::Vector3 Entity::GetPosition() const
    {
        return GetWorldTransform().position;
    }

    void Entity::SetRotation(const Math::Quaternion& rot)
    {
        Math::Transform worldTransform = EvaluateWorldTransformNonMutating();
        worldTransform.rotation = rot;
        SetWorldTransform(worldTransform);
    }

    void Entity::SetRotation(float x, float y, float z, float w)
    {
        SetRotation(Math::Quaternion(x, y, z, w));
    }

    Math::Quaternion Entity::GetRotation() const
    {
        return GetWorldTransform().rotation;
    }

    void Entity::SetScale(const Math::Vector3& scale)
    {
        Math::Transform worldTransform = EvaluateWorldTransformNonMutating();
        worldTransform.scale = scale;
        SetWorldTransform(worldTransform);
    }

    void Entity::SetScale(float x, float y, float z)
    {
        SetScale(Math::Vector3(x, y, z));
    }

    Math::Vector3 Entity::GetScale() const
    {
        return GetWorldTransform().scale;
    }

    Entity* Entity::GetParentEntity() const
    {
        return CastTo<Entity>(const_cast<IUnknown *>(GetOuter()));
    }

    void Entity::MarkWorldTransformDirty()
    {
        if (m_bWorldTransformDirty)
        {
            return;
        }

        m_bWorldTransformDirty = true;
        for (auto *inner : m_Inners)
        {
            if (auto *child = CastTo<Entity>(inner))
            {
                child->MarkWorldTransformDirty();
            }
        }
    }

    void Entity::RecomputeWorldTransform(const Math::Transform& parentWorld)
    {
        const Math::Transform newWorldTransform = ComposeTransform(parentWorld, LoadLocalTransform());
        if (newWorldTransform != LoadWorldTransformCache())
        {
            StoreWorldTransformCache(newWorldTransform);
            ++m_TransformVersion;
        }

        m_bWorldTransformDirty = false;
    }

    void Entity::SetLocalTransform(const Math::Transform& transform)
    {
        StoreLocalTransform(transform);
        MarkWorldTransformDirty();
    }

    Math::Transform Entity::LoadLocalTransform() const
    {
        Ecs::EcsLocalTransform column{m_EcsFallback_Position, m_EcsFallback_Rotation, m_EcsFallback_Scale};
        // 列に無いとき（行なし・並列の中）は予備の値のまま返す
        (void)Ecs::EcsReadColumnPart(GetEcsRow(), Ecs::EcsColumnInfoOf<Ecs::EcsLocalTransform>().Type, 0,
                                     static_cast<uint32_t>(sizeof(column)), &column);
        return Math::Transform(column.position, column.rotation, column.scale);
    }

    bool Entity::StoreLocalTransform(const Math::Transform& transform)
    {
        // 並列の中の書き込みは、行がなくて予備へ書く場合も止める
        if (!Ecs::Detail::EcsCheckSerialAccess())
        {
            return false;
        }
        const Ecs::EcsLocalTransform column{transform.position, transform.rotation, transform.scale};
        const Ecs::EcsRowRef row = GetEcsRow();
        const TypeId type = Ecs::EcsColumnInfoOf<Ecs::EcsLocalTransform>().Type;
        if (Ecs::EcsRowHasColumn(row, type))
        {
            return Ecs::EcsWriteColumnPart(row, type, 0, static_cast<uint32_t>(sizeof(column)), &column);
        }
        m_EcsFallback_Position = transform.position;
        m_EcsFallback_Rotation = transform.rotation;
        m_EcsFallback_Scale = transform.scale;
        return true;
    }

    Math::Transform Entity::LoadWorldTransformCache() const
    {
        Ecs::EcsWorldTransform column{m_WorldTransformFallback.position, m_WorldTransformFallback.rotation,
                                      m_WorldTransformFallback.scale};
        (void)Ecs::EcsReadColumnPart(GetEcsRow(), Ecs::EcsColumnInfoOf<Ecs::EcsWorldTransform>().Type, 0,
                                     static_cast<uint32_t>(sizeof(column)), &column);
        return Math::Transform(column.position, column.rotation, column.scale);
    }

    bool Entity::StoreWorldTransformCache(const Math::Transform& transform)
    {
        if (!Ecs::Detail::EcsCheckSerialAccess())
        {
            return false;
        }
        const Ecs::EcsWorldTransform column{transform.position, transform.rotation, transform.scale};
        const Ecs::EcsRowRef row = GetEcsRow();
        const TypeId type = Ecs::EcsColumnInfoOf<Ecs::EcsWorldTransform>().Type;
        if (Ecs::EcsRowHasColumn(row, type))
        {
            return Ecs::EcsWriteColumnPart(row, type, 0, static_cast<uint32_t>(sizeof(column)), &column);
        }
        m_WorldTransformFallback = transform;
        return true;
    }

    Math::Transform Entity::EvaluateWorldTransformNonMutating() const
    {
        const Math::Transform localTransform = LoadLocalTransform();
        const Entity* parent = GetParentEntity();
        if (!parent)
        {
            return localTransform;
        }

        return ComposeTransform(parent->EvaluateWorldTransformNonMutating(), localTransform);
    }

    // ========================================
    // コンポーネント管理（Outer/Inner経由）
    // ========================================

    bool Entity::AddComponent(Component::Component *component)
    {
        if (!component)
        {
            return false;
        }

        // 重複チェック（既にInnerに存在するか）
        for (auto *inner : m_Inners)
        {
            if (inner == component)
            {
                return false;
            }
        }

        // Innerとして追加（Outerも自動設定される）
        if (!AddInner(component))
        {
            return false;
        }

        // ライフサイクル
        component->Initialize();
        component->BeginPlay();
        return true;
    }

    void Entity::RemoveComponent(Component::Component* component)
    {
        if (!component || component->GetOwner() != this)
        {
            return;
        }
        if (World* world = GetWorld())
        {
            if (world->IsDeferringObjectRemoval())
            {
                component->MarkForDestroy();
#ifndef NDEBUG
                NORVES_LOG_WARNING("World", "TICK_DEFERRED_REMOVE_COMPONENT id=%llu",
                    static_cast<uint64_t>(component->GetComponentId()));
#endif
                return;
            }
        }
        RemoveComponentImmediately(component);
    }

    void Entity::RemoveComponentImmediately(Component::Component *component)
    {
        if (!component)
        {
            return;
        }

        // Innersに存在するか確認
        for (auto *inner : m_Inners)
        {
            if (inner == component)
            {
                if (World* world = GetWorld())
                {
                    world->InvalidateTickComponent(*component);
                }
                // Innerから除去して破棄する
                if (!DestroyContextOwnedInner(*this, component))
                {
                    RemoveInner(component);
                }
                return;
            }
        }
    }

    Container::VariableArray<Component::Component *> Entity::GetComponents() const
    {
        Container::VariableArray<Component::Component *> result;
        for (auto *inner : m_Inners)
        {
            if (auto *comp = CastTo<Component::Component>(inner))
            {
                result.push_back(comp);
            }
        }
        return result;
    }

    Container::VariableArray<Entity*> Entity::GetChildEntities() const
    {
        Container::VariableArray<Entity*> result;
        for (auto *inner : m_Inners)
        {
            if (auto *child = CastTo<Entity>(inner))
            {
                result.push_back(child);
            }
        }
        return result;
    }

    void Entity::TickComponents(float deltaTime)
    {
        for (auto *inner : m_Inners)
        {
            auto *comp = CastTo<Component::Component>(inner);
            if (comp && comp->IsActive() && comp->IsTickEnabled())
            {
                comp->Tick(deltaTime);
            }
        }
    }

    void Entity::MarkRenderStateDirtyRecursive()
    {
        auto components = GetComponents();
        for (auto *component : components)
        {
            component->MarkRenderStateDirty();
        }

        auto children = GetChildEntities();
        for (auto *child : children)
        {
            child->MarkRenderStateDirtyRecursive();
        }
    }

} // namespace NorvesLib::Core
