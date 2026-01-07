#include "Object/Resource.h"
#include "Object/Reflection.h"
#include "Object/ObjectUtility.h"

namespace NorvesLib::Core
{
    // IMPLEMENT_CLASSマクロを使用してリフレクション実装を生成
    IMPLEMENT_CLASS(Resource, Object)

    Resource::Resource()
        : Object()
    {
    }

    Resource::Resource(const FieldInitializer *initializer)
        : Object(initializer)
    {
    }

    Resource::Resource(const IUnknown *sourceObject)
        : Object(sourceObject)
    {
        // Objectのコンストラクタですべての必要な処理は行われる
    }

    Resource::~Resource()
    {
        Finalize();
    }

    void Resource::Initialize()
    {
        // 基本初期化を呼び出す
        Object::Initialize();
    }

    bool Resource::Initialize(const FieldInitializer *initializer)
    {
        // 基本初期化
        Object::Initialize();

        if (initializer)
        {
            const IClass *cls = GetClass();
            if (cls)
            {
                const Container::VariableArray<const ClassProperty *> properties = cls->GetAllProperties();

                // 各プロパティに初期値を適用
                for (const ClassProperty *prop : properties)
                {
                    if (prop)
                    {
                        prop->ApplyInitialValue(this, initializer);
                    }
                }
                return true;
            }
        }

        return false;
    }

    void Resource::Finalize()
    {
        // リソースをアンロード
        if (m_State == ResourceState::Loaded)
        {
            Unload();
        }

        Object::Finalize();
    }

    bool Resource::Load()
    {
        // 基底クラスのデフォルト実装
        // 派生クラスでオーバーライドして実際のロード処理を実装
        if (m_State == ResourceState::Loaded)
        {
            return true; // すでにロード済み
        }

        m_State = ResourceState::Loading;

        // 基底クラスでは何もロードしない
        // 派生クラスでこのメソッドをオーバーライドして実装
        m_State = ResourceState::Loaded;

        return true;
    }

    void Resource::Unload()
    {
        // 基底クラスのデフォルト実装
        // 派生クラスでオーバーライドして実際のアンロード処理を実装
        if (m_State != ResourceState::Loaded)
        {
            return;
        }

        m_State = ResourceState::Unloading;

        // 派生クラスでこのメソッドをオーバーライドして実装

        m_State = ResourceState::Unloaded;
    }

} // namespace NorvesLib::Core
