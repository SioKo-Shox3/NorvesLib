#include "Object/ObjectUtility.h"
#include "Object/IClass.h"

namespace NorvesLib::Core
{
    IUnknown* ObjectUtility::CreateObject(const Identity& className)
    {
        const IClass* cls = ClassRegistry::Get().FindClass(className);
        if (cls)
        {
            return cls->CreateInstance();
        }
        return nullptr;
    }

    IUnknown* ObjectUtility::CreateObject(uint64_t classId)
    {
        const IClass* cls = ClassRegistry::Get().FindClass(classId);
        if (cls)
        {
            return cls->CreateInstance();
        }
        return nullptr;
    }

    IUnknown* ObjectUtility::CreateObject(const IClass* cls)
    {
        if (cls)
        {
            return cls->CreateInstance();
        }
        return nullptr;
    }

    IUnknown* ObjectUtility::CreateObject(const Identity& className, const FieldInitializer* initializer)
    {
        IUnknown* object = CreateObject(className);
        if (object && initializer)
        {
            ApplyInitialValues(object, initializer);
        }
        return object;
    }

    IUnknown* ObjectUtility::CreateObject(uint64_t classId, const FieldInitializer* initializer)
    {
        IUnknown* object = CreateObject(classId);
        if (object && initializer)
        {
            ApplyInitialValues(object, initializer);
        }
        return object;
    }

    IUnknown* ObjectUtility::CreateObject(const IClass* cls, const FieldInitializer* initializer)
    {
        IUnknown* object = CreateObject(cls);
        if (object && initializer)
        {
            ApplyInitialValues(object, initializer);
        }
        return object;
    }

    bool ObjectUtility::DestroyObject(IUnknown* object)
    {
        if (!object)
        {
            return false;
        }

        // オブジェクトの破棄処理
        // ここでは単純にdeleteを使用していますが、
        // メモリプールやガベージコレクションを使用する場合は
        // 適切な破棄メカニズムに置き換えてください
        try
        {
            delete object;
            return true;
        }
        catch (...)
        {
            // 例外が発生した場合は失敗
            return false;
        }
    }

    int ObjectUtility::ApplyInitialValues(IUnknown* object, const FieldInitializer* initializer)
    {
        if (!object || !initializer)
        {
            return 0;
        }

        // オブジェクトのクラス情報を取得
        const IClass* cls = object->GetClass();
        if (!cls)
        {
            return 0;
        }

        // すべてのプロパティを取得
        Container::VariableArray<const ClassProperty*> properties = cls->GetAllProperties();
        
        // 適用された初期値の数をカウント
        int appliedCount = 0;
        
        // 各プロパティに初期値を適用
        for (const ClassProperty* prop : properties)
        {
            if (prop && prop->ApplyInitialValue(object, initializer))
            {
                appliedCount++;
            }
        }

        return appliedCount;
    }

    int ObjectUtility::ApplyDefaultValues(IUnknown* object)
    {
        if (!object)
        {
            return 0;
        }

        // オブジェクトのクラス情報を取得
        const IClass* cls = object->GetClass();
        if (!cls)
        {
            return 0;
        }

        // すべてのプロパティを取得
        Container::VariableArray<const ClassProperty*> properties = cls->GetAllProperties();
        
        // 適用されたデフォルト値の数をカウント
        int appliedCount = 0;
        
        // 各プロパティにデフォルト値を適用
        for (const ClassProperty* prop : properties)
        {
            if (prop)
            {
                prop->ApplyDefaultValue(object);
                appliedCount++;
            }
        }

        return appliedCount;
    }

} // namespace NorvesLib::Core