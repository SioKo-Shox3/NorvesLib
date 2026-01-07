#include "Object/WorldObject.h"
#include "Object/Reflection.h"
#include "Object/ObjectUtility.h"

namespace NorvesLib::Core
{
    // IMPLEMENT_CLASSマクロを使用してリフレクション実装を生成
    IMPLEMENT_CLASS(WorldObject, Object)

    WorldObject::WorldObject()
        : Object()
    {
    }

    WorldObject::WorldObject(const FieldInitializer *initializer)
        : Object(initializer)
    {
    }

    WorldObject::WorldObject(const IUnknown *sourceObject)
        : Object(sourceObject)
    {
        // Objectのコンストラクタですべての必要な処理は行われる
    }

    WorldObject::~WorldObject()
    {
        Finalize();
    }

    void WorldObject::Initialize()
    {
        // 基本初期化を呼び出す
        Object::Initialize();
    }

    void WorldObject::Finalize()
    {
        // Worldからの削除通知
        if (m_World != nullptr)
        {
            OnRemovedFromWorld();
            m_World = nullptr;
        }

        Object::Finalize();
    }

    void WorldObject::SetActive(bool bActive)
    {
        if (m_bActive != bActive)
        {
            m_bActive = bActive;
            // TODO: アクティブ状態変更時のコールバック
        }
    }

} // namespace NorvesLib::Core
