#include "Object/ObjectUtility.h"
#include "Object/IClass.h"
#include <memory>

namespace NorvesLib::Core
{
    IUnknown* CreateObjectImpl(const IClass* cls, IUnknown* outer = nullptr)
    {
        if (!cls) return nullptr;

        try 
        {
            // クラス情報のNewInstanceメソッドを使用して新しいインスタンスを生成
            // 親子関係の設定はここで行うため、outer引数は渡さない
            IUnknown* newObject = cls->NewInstance();
            
            // 親子関係の設定（指定されていれば）
            if (newObject && outer)
            {
                outer->AddInner(newObject);
                
                // 親オブジェクト参照を設定
                // dynamic_castの代わりにTryCastを使用
                auto* unknownImpl = ObjectUtility::TryCast<UnknownImpl>(newObject);
                if (unknownImpl)
                {
                    unknownImpl->m_Outer = outer;
                }
            }
            
            return newObject;
        }
        catch (const std::exception& e)
        {
            // 例外が発生した場合はnullptrを返す
            return nullptr;
        }
    }

    IUnknown* ObjectUtility::CreateObject(const Identity& className, IUnknown* outer)
    {
        const IClass* cls = ClassRegistry::Get().FindClass(className);
        if (cls)
        {
            return CreateObjectImpl(cls, outer);
        }
        return nullptr;
    }

    IUnknown* ObjectUtility::CreateObject(uint64_t classId, IUnknown* outer)
    {
        const IClass* cls = ClassRegistry::Get().FindClass(classId);
        if (cls)
        {
            return CreateObjectImpl(cls, outer);
        }
        return nullptr;
    }

    IUnknown* ObjectUtility::CreateObject(const IClass* cls, IUnknown* outer)
    {
        if (cls)
        {
            return CreateObjectImpl(cls, outer);
        }
        return nullptr;
    }

    IUnknown* ObjectUtility::CreateObject(const Identity& className, const FieldInitializer* initializer, IUnknown* outer)
    {
        IUnknown* object = CreateObject(className, outer);
        if (object && initializer)
        {
            ApplyInitialValues(object, initializer);
        }
        return object;
    }

    IUnknown* ObjectUtility::CreateObject(uint64_t classId, const FieldInitializer* initializer, IUnknown* outer)
    {
        IUnknown* object = CreateObject(classId, outer);
        if (object && initializer)
        {
            ApplyInitialValues(object, initializer);
        }
        return object;
    }

    IUnknown* ObjectUtility::CreateObject(const IClass* cls, const FieldInitializer* initializer, IUnknown* outer)
    {
        IUnknown* object = CreateObject(cls, outer);
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