#include "Object/ObjectUtility.h"
#include "Object/IClass.h"
#include <memory>

namespace NorvesLib::Core
{
    IUnknown* CreateObjectImpl(const IClass* cls)
    {
        if (!cls) return nullptr;

        // デフォルトオブジェクトを取得
        const IUnknown* defaultObject = cls->GetDefaultObject();
        if (!defaultObject) return nullptr;

        // RTTI情報を使用して適切な型のインスタンスを生成
        IUnknown* newObject = nullptr;
        
        try 
        {
            // デフォルトオブジェクトからコピーするためのUnknownImplコンストラクタを使用
            // dynamic_castを使わずに直接インスタンス生成
            newObject = new UnknownImpl(defaultObject);
            
            if (newObject)
            {
                // デフォルトオブジェクトのフラグは引き継がない
                newObject->SetFlag(OF_DefaultObject, false);
                
                // 変数コンテナを初期化
                VariableContainer* container = newObject->GetVariableContainer();
                if (container)
                {
                    cls->InitializeVariableContainer(container->GetData());
                }
                
                // デフォルト値を適用
                ObjectUtility::ApplyDefaultValues(newObject);
            }
        }
        catch (const std::exception& e)
        {
            // 例外が発生した場合はnullptrを返す
            return nullptr;
        }

        return newObject;
    }

    IUnknown* ObjectUtility::CreateObject(const Identity& className)
    {
        const IClass* cls = ClassRegistry::Get().FindClass(className);
        if (cls)
        {
            return CreateObjectImpl(cls);
        }
        return nullptr;
    }

    IUnknown* ObjectUtility::CreateObject(uint64_t classId)
    {
        const IClass* cls = ClassRegistry::Get().FindClass(classId);
        if (cls)
        {
            return CreateObjectImpl(cls);
        }
        return nullptr;
    }

    IUnknown* ObjectUtility::CreateObject(const IClass* cls)
    {
        if (cls)
        {
            return CreateObjectImpl(cls);
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