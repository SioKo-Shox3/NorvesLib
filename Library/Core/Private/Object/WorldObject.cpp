#include "Object/WorldObject.h"
#include "Object/World.h"
#include "Object/Reflection.h"
#include "Object/ObjectUtility.h"
#include "Component/Component.h"

namespace NorvesLib::Core
{
    // IMPLEMENT_CLASSマクロを使用してリフレクション実装を生成
    IMPLEMENT_CLASS(WorldObject, Object)

    WorldObject::WorldObject()
        : Object()
    {
        // PROPERTYマクロは値初期化（0/false）するため、非ゼロデフォルト値を設定
        bTickEnabled = true;
        bActive = true;
        // RotationはQuaternionのデフォルトが単位回転(0,0,0,1)なので設定不要
        Scale = Math::Vector3(1.0f, 1.0f, 1.0f);
    }

    WorldObject::WorldObject(const FieldInitializer *initializer)
        : Object(initializer)
    {
        bTickEnabled = true;
        bActive = true;
        Scale = Math::Vector3(1.0f, 1.0f, 1.0f);
    }

    WorldObject::WorldObject(const IUnknown *sourceObject)
        : Object(sourceObject)
    {
        bTickEnabled = true;
        bActive = true;
        Scale = Math::Vector3(1.0f, 1.0f, 1.0f);
    }

    WorldObject::~WorldObject()
    {
        Finalize();
    }

    void WorldObject::Initialize()
    {
        Object::Initialize();
    }

    void WorldObject::Finalize()
    {
        if (!HasFlag(OF_Initialized))
        {
            return;
        }

        while (!m_Inners.empty())
        {
            IUnknown *inner = m_Inners.back();
            RemoveInner(inner);
        }

        Object::Finalize();
    }

    void WorldObject::SetActive(bool bNewActive)
    {
        if (bActive != bNewActive)
        {
            bActive = bNewActive;
        }
    }

    // ========================================
    // World関連（Outer経由）
    // ========================================

    World *WorldObject::GetWorld() const
    {
        // Outer関係はオーナーシップの概念であり、constの制約を受けない
        return ObjectUtility::CastTo<World>(const_cast<IUnknown *>(GetOuter()));
    }

    bool WorldObject::IsInWorld() const
    {
        return ObjectUtility::CastTo<World>(GetOuter()) != nullptr;
    }

    // ========================================
    // コンポーネント管理（Outer/Inner経由）
    // ========================================

    bool WorldObject::AddComponent(Component::Component *component)
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

    void WorldObject::RemoveComponent(Component::Component *component)
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
                // Innerから除去（Outerも自動クリアされる）
                RemoveInner(component);
                return;
            }
        }
    }

    Container::VariableArray<Component::Component *> WorldObject::GetComponents() const
    {
        Container::VariableArray<Component::Component *> result;
        for (auto *inner : m_Inners)
        {
            if (auto *comp = ObjectUtility::CastTo<Component::Component>(inner))
            {
                result.push_back(comp);
            }
        }
        return result;
    }

    void WorldObject::TickComponents(float deltaTime)
    {
        for (auto *inner : m_Inners)
        {
            auto *comp = ObjectUtility::CastTo<Component::Component>(inner);
            if (comp && comp->IsActive() && comp->IsTickEnabled())
            {
                comp->Tick(deltaTime);
            }
        }
    }

} // namespace NorvesLib::Core
