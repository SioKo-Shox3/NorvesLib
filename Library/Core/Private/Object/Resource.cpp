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
        m_Metadata.Type = Identity("Resource");
    }

    Resource::Resource(const FieldInitializer *initializer)
        : Object(initializer)
    {
        m_Metadata.Type = Identity("Resource");
    }

    Resource::Resource(const IUnknown *sourceObject)
        : Object(sourceObject)
    {
        // Objectのコンストラクタですべての必要な処理は行われる
        m_Metadata.Type = Identity("Resource");
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
        Object::Initialize();
        if (!initializer)
        {
            return false;
        }

        ObjectUtility::ApplyInitialValues(this, initializer);
        return true;
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

    Container::VariableArray<FunctionDesc> Resource::BuildResourceFunctionDescs()
    {
        Container::VariableArray<FunctionDesc> result;

        auto makeDesc = [](const char *name, FunctionDesc::InvokeFn invoke)
        {
            FunctionDesc desc;
            desc.StableId = MakeStableSchemaId("NorvesLib", "Function", Container::StringView("Resource"), Container::StringView(name));
            desc.Name = name;
            desc.ReturnType = TypeRegistry::Get().GetTypeId<bool>();
            desc.Flags = FunctionFlags::RuntimeCallable |
                         FunctionFlags::EditorCallable |
                         FunctionFlags::GameThreadOnly |
                         FunctionFlags::Mutating;
            desc.Thread = ThreadPolicy::GameThreadOnly;
            desc.Invoke = invoke;
            return desc;
        };

        result.push_back(makeDesc("Reload", [](IUnknown *instance, const Container::VariableArray<PropertyValue> &arguments, PropertyValue *outReturnValue)
        {
            (void)arguments;
            Resource *resource = ObjectUtility::CastTo<Resource>(instance);
            if (!resource || !outReturnValue)
            {
                return false;
            }
            resource->Unload();
            const bool bLoaded = resource->Load();
            outReturnValue->Set<bool>(bLoaded);
            return true;
        }));

        result.push_back(makeDesc("Reimport", [](IUnknown *instance, const Container::VariableArray<PropertyValue> &arguments, PropertyValue *outReturnValue)
        {
            (void)arguments;
            Resource *resource = ObjectUtility::CastTo<Resource>(instance);
            if (!resource || !outReturnValue)
            {
                return false;
            }
            resource->Unload();
            const bool bLoaded = resource->Load();
            outReturnValue->Set<bool>(bLoaded);
            return true;
        }));

        result.push_back(makeDesc("Unload", [](IUnknown *instance, const Container::VariableArray<PropertyValue> &arguments, PropertyValue *outReturnValue)
        {
            (void)arguments;
            Resource *resource = ObjectUtility::CastTo<Resource>(instance);
            if (!resource || !outReturnValue)
            {
                return false;
            }
            resource->Unload();
            outReturnValue->Set<bool>(!resource->IsLoaded());
            return true;
        }));

        result.push_back(makeDesc("Pin", [](IUnknown *instance, const Container::VariableArray<PropertyValue> &arguments, PropertyValue *outReturnValue)
        {
            (void)arguments;
            Resource *resource = ObjectUtility::CastTo<Resource>(instance);
            if (!resource || !outReturnValue)
            {
                return false;
            }
            outReturnValue->Set<bool>(true);
            return true;
        }));

        return result;
    }

} // namespace NorvesLib::Core
