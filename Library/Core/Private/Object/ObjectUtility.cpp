#include "Object/ObjectUtility.h"
#include "Object/IClass.h"
#include "Object/ObjectHeap.h"
#include <memory>

namespace NorvesLib::Core
{
    namespace
    {
        ObjectHeap &GetCompatibilityObjectHeap()
        {
            static ObjectHeap s_ObjectHeap;
            return s_ObjectHeap;
        }
    }

    IUnknown *CreateObjectImpl(const IClass *cls, IUnknown *outer = nullptr)
    {
        if (!cls)
            return nullptr;

        try
        {
            ObjectHeap &heap = GetCompatibilityObjectHeap();
            ObjectHandle handle = heap.Create(cls);
            IUnknown *newObject = heap.Resolve(handle);
            if (!newObject)
            {
                return nullptr;
            }

            if (outer)
            {
                if (!outer->AddInner(newObject))
                {
                    heap.DestroyNow(handle);
                    return nullptr;
                }
            }
            else
            {
                newObject->AddRef();
            }

            return newObject;
        }
        catch (const std::exception &)
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
            if (IUnknown *outer = object->GetOuter())
            {
                const bool bHeapOwned = object->HasFlag(OF_HeapOwned);
                ObjectHandle handle = bHeapOwned ? GetCompatibilityObjectHeap().GetHandle(ObjectUtility::CastTo<Object>(object)) : ObjectHandle{};
                const bool bRemoved = outer->RemoveInner(object);
                if (bRemoved && bHeapOwned && handle)
                {
                    GetCompatibilityObjectHeap().DestroyNow(handle);
                }
                return bRemoved;
            }

            if (object->HasFlag(OF_HeapOwned))
            {
                ObjectHeap &heap = GetCompatibilityObjectHeap();
                ObjectHandle handle = heap.GetHandle(ObjectUtility::CastTo<Object>(object));
                if (!handle)
                {
                    return false;
                }

                if (object->GetRefCount() > 0)
                {
                    object->Release();
                }
                return heap.DestroyNow(handle);
            }

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

    int ObjectUtility::CopyEditableProperties(Object &dst, const Object &src)
    {
        const IClass *srcClass = src.GetClass();
        const IClass *dstClass = dst.GetClass();
        if (!srcClass || !dstClass || !dstClass->IsChildOf(srcClass))
        {
            return 0;
        }

        Container::VariableArray<const ClassProperty *> properties = srcClass->GetAllProperties();
        int copiedCount = 0;
        for (const ClassProperty *srcProperty : properties)
        {
            if (!srcProperty)
            {
                continue;
            }

            const ClassProperty *dstProperty = dstClass->GetProperty(srcProperty->GetName());
            if (!dstProperty)
            {
                continue;
            }

            if (dstProperty->CopyValueFrom(&dst, &src))
            {
                ++copiedCount;
            }
        }

        return copiedCount;
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
                    if (IUnknown *currentOuter = object->GetOuter())
                    {
                        currentOuter->RemoveInner(object);
                    }
                    else
                    {
                        unknownImpl->SetOuter(nullptr);
                    }
                }
            }
        }
        catch (const std::exception &)
        {
            // 例外が発生した場合は何もしない
        }
    }

} // namespace NorvesLib::Core
