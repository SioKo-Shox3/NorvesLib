#include "Object/ObjectUtility.h"
#include "Object/IClass.h"
#include <memory>

namespace NorvesLib::Core
{
    IUnknown *CreateObjectImpl(const IClass *cls, IUnknown *outer = nullptr)
    {
        if (!cls)
            return nullptr;

        try
        {
            IUnknown *newObject = cls->NewInstance();
            if (!newObject)
            {
                return nullptr;
            }

            if (outer)
            {
                outer->AddInner(newObject);
            }
            else
            {
                newObject->AddRef();
            }

            return newObject;
        }
        catch (const std::exception &e)
        {
            // 例外が発生した場合はnullptrを返す
            return nullptr;
        }
    }

    IUnknown *ObjectUtility::CreateObject(const Identity &className, IUnknown *outer)
    {
        const IClass *cls = ClassRegistry::Get().FindClass(className);
        if (cls)
        {
            return CreateObjectImpl(cls, outer);
        }
        return nullptr;
    }

    IUnknown *ObjectUtility::CreateObject(uint64_t classId, IUnknown *outer)
    {
        const IClass *cls = ClassRegistry::Get().FindClass(classId);
        if (cls)
        {
            return CreateObjectImpl(cls, outer);
        }
        return nullptr;
    }

    IUnknown *ObjectUtility::CreateObject(const IClass *cls, IUnknown *outer)
    {
        if (cls)
        {
            return CreateObjectImpl(cls, outer);
        }
        return nullptr;
    }

    IUnknown *ObjectUtility::CreateObject(const Identity &className, const FieldInitializer *initializer, IUnknown *outer)
    {
        IUnknown *object = CreateObject(className, outer);
        if (object && initializer)
        {
            ApplyInitialValues(object, initializer);
        }
        return object;
    }

    IUnknown *ObjectUtility::CreateObject(uint64_t classId, const FieldInitializer *initializer, IUnknown *outer)
    {
        IUnknown *object = CreateObject(classId, outer);
        if (object && initializer)
        {
            ApplyInitialValues(object, initializer);
        }
        return object;
    }

    IUnknown *ObjectUtility::CreateObject(const IClass *cls, const FieldInitializer *initializer, IUnknown *outer)
    {
        IUnknown *object = CreateObject(cls, outer);
        if (object && initializer)
        {
            ApplyInitialValues(object, initializer);
        }
        return object;
    }

    bool ObjectUtility::DestroyObject(IUnknown *object)
    {
        if (!object)
        {
            return false;
        }

        try
        {
            if (object->GetRefCount() > 0)
            {
                object->Release();
            }
            else
            {
                object->Finalize();
                delete object;
            }
            return true;
        }
        catch (...)
        {
            // 例外が発生した場合は失敗
            return false;
        }
    }

    int ObjectUtility::ApplyInitialValues(IUnknown *object, const FieldInitializer *initializer)
    {
        if (!object || !initializer)
        {
            return 0;
        }

        // オブジェクトのクラス情報を取得
        const IClass *cls = object->GetClass();
        if (!cls)
        {
            return 0;
        }

        // すべてのプロパティを取得
        Container::VariableArray<const ClassProperty *> properties = cls->GetAllProperties();

        // 適用された初期値の数をカウント
        int appliedCount = 0;

        // 各プロパティに初期値を適用
        for (const ClassProperty *prop : properties)
        {
            if (prop && prop->ApplyInitialValue(object, initializer))
            {
                appliedCount++;
            }
        }

        return appliedCount;
    }

    int ObjectUtility::ApplyDefaultValues(IUnknown *object)
    {
        if (!object)
        {
            return 0;
        }

        // オブジェクトのクラス情報を取得
        const IClass *cls = object->GetClass();
        if (!cls)
        {
            return 0;
        }

        // すべてのプロパティを取得
        Container::VariableArray<const ClassProperty *> properties = cls->GetAllProperties();

        // 適用されたデフォルト値の数をカウント
        int appliedCount = 0;

        // 各プロパティにデフォルト値を適用
        for (const ClassProperty *prop : properties)
        {
            if (prop)
            {
                prop->ApplyDefaultValue(object);
                appliedCount++;
            }
        }

        return appliedCount;
    }

    void SetOuter(IUnknown *object, IUnknown *outer)
    {
        if (!object)
        {
            return;
        }

        try
        {
            // objectを直接UnknownImplにキャストして使用
            UnknownImpl *unknownImpl = dynamic_cast<UnknownImpl *>(object);
            if (unknownImpl)
            {
                if (outer)
                {
                    outer->AddInner(object);
                }
                else
                {
                    unknownImpl->SetOuter(nullptr);
                }
            }
        }
        catch (const std::exception &e)
        {
            // 例外が発生した場合は何もしない
        }
    }

} // namespace NorvesLib::Core
