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
    }

    Entity::Entity(const FieldInitializer *initializer)
        : Object(initializer)
    {
        bTickEnabled = true;
        bActive = true;
        Scale = Math::Vector3(1.0f, 1.0f, 1.0f);
    }

    Entity::Entity(const IUnknown *sourceObject)
        : Object(sourceObject)
    {
        bTickEnabled = true;
        bActive = true;
        Scale = Math::Vector3(1.0f, 1.0f, 1.0f);
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
