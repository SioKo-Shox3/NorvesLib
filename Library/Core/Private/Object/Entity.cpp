#include "Object/Entity.h"
#include "Object/World.h"
#include "Object/Reflection.h"
#include "Object/ObjectCast.h"
#include "Component/Component.h"

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
        bTickEnabled = true;
        bActive = true;
        // RotationはQuaternionのデフォルトが単位回転(0,0,0,1)なので設定不要
        Scale = Math::Vector3(1.0f, 1.0f, 1.0f);
        SyncLocalTransformFromProperties();
        m_CachedWorldTransform = m_LocalTransform;
    }

    Entity::Entity(const FieldInitializer *initializer)
        : Object(initializer)
    {
        bTickEnabled = true;
        bActive = true;
        Scale = Math::Vector3(1.0f, 1.0f, 1.0f);
        SyncLocalTransformFromProperties();
        m_CachedWorldTransform = m_LocalTransform;
    }

    Entity::Entity(const IUnknown *sourceObject)
        : Object(sourceObject)
    {
        bTickEnabled = true;
        bActive = true;
        Scale = Math::Vector3(1.0f, 1.0f, 1.0f);
        SyncLocalTransformFromProperties();
        m_CachedWorldTransform = m_LocalTransform;
    }

    Entity::~Entity()
    {
        Finalize();
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

        while (!m_Inners.empty())
        {
            IUnknown *inner = m_Inners.back();
            if (!DestroyContextOwnedInner(*this, inner))
            {
                RemoveInner(inner);
            }
        }

        Object::Finalize();
    }

    void Entity::SetActive(bool bNewActive)
    {
        if (bActive == bNewActive)
        {
            return;
        }

        bActive = bNewActive;

        auto components = GetComponents();
        for (auto *component : components)
        {
            component->MarkRenderStateDirty();
        }
    }

    // ========================================
    // World関連（Outer経由）
    // ========================================

    World *Entity::GetWorld() const
    {
        // Outer関係はオーナーシップの概念であり、constの制約を受けない
        return CastTo<World>(const_cast<IUnknown *>(GetOuter()));
    }

    bool Entity::IsInWorld() const
    {
        return CastTo<World>(GetOuter()) != nullptr;
    }

    // ========================================
    // トランスフォーム
    // ========================================

    void Entity::SetLocalPosition(const Math::Vector3& pos)
    {
        if (m_LocalTransform.position == pos && Position.Get() == pos)
        {
            return;
        }

        Position = pos;
        m_LocalTransform.position = pos;
        MarkWorldTransformDirty();
    }

    void Entity::SetLocalPosition(float x, float y, float z)
    {
        SetLocalPosition(Math::Vector3(x, y, z));
    }

    void Entity::SetLocalRotation(const Math::Quaternion& rot)
    {
        if (m_LocalTransform.rotation == rot && Rotation.Get() == rot)
        {
            return;
        }

        Rotation = rot;
        m_LocalTransform.rotation = rot;
        MarkWorldTransformDirty();
    }

    void Entity::SetLocalRotation(float x, float y, float z, float w)
    {
        SetLocalRotation(Math::Quaternion(x, y, z, w));
    }

    void Entity::SetLocalScale(const Math::Vector3& scale)
    {
        if (m_LocalTransform.scale == scale && Scale.Get() == scale)
        {
            return;
        }

        Scale = scale;
        m_LocalTransform.scale = scale;
        MarkWorldTransformDirty();
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

    const Math::Transform& Entity::GetWorldTransform() const
    {
        if (m_bWorldTransformDirty && !GetParentEntity())
        {
            return m_LocalTransform;
        }

        return m_CachedWorldTransform;
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

    const Math::Vector3& Entity::GetPosition() const
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

    const Math::Quaternion& Entity::GetRotation() const
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

    const Math::Vector3& Entity::GetScale() const
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
        SyncLocalTransformFromProperties();

        const Math::Transform newWorldTransform = ComposeTransform(parentWorld, m_LocalTransform);
        if (newWorldTransform != m_CachedWorldTransform)
        {
            m_CachedWorldTransform = newWorldTransform;
            ++m_TransformVersion;
        }

        m_bWorldTransformDirty = false;
    }

    void Entity::SetLocalTransform(const Math::Transform& transform)
    {
        Position = transform.position;
        Rotation = transform.rotation;
        Scale = transform.scale;
        m_LocalTransform = transform;
        MarkWorldTransformDirty();
    }

    void Entity::SyncLocalTransformFromProperties()
    {
        m_LocalTransform.position = Position;
        m_LocalTransform.rotation = Rotation;
        m_LocalTransform.scale = Scale;
    }

    Math::Transform Entity::EvaluateWorldTransformNonMutating() const
    {
        const Math::Transform localTransform(Position.Get(), Rotation.Get(), Scale.Get());
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

    void Entity::RemoveComponent(Component::Component *component)
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

} // namespace NorvesLib::Core
