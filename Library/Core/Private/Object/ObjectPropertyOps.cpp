#include "Object/ObjectPropertyOps.h"
#include "Object/Object.h"

namespace NorvesLib::Core
{
    namespace
    {
        StablePropertyId MakeInitializerStablePropertyId(const IClass &cls, const ClassProperty &property)
        {
            return MakeStableSchemaId(
                "NorvesLib",
                "Property",
                cls.GetClassName().GetView(),
                property.GetName().GetView());
        }
    }

    int ApplyInitialValues(IUnknown *object, const FieldInitializer *initializer)
    {
        if (!object || !initializer)
        {
            return 0;
        }

        const IClass *cls = object->GetClass();
        if (!cls)
        {
            return 0;
        }

        Container::VariableArray<const ClassProperty *> properties = cls->GetAllProperties();
        int appliedCount = 0;

        for (const ClassProperty *prop : properties)
        {
            if (!prop)
            {
                continue;
            }

            const StablePropertyId stablePropertyId = MakeInitializerStablePropertyId(*cls, *prop);
            if (const PropertyValue *stableValue = initializer->FindValue(stablePropertyId))
            {
                if (prop->ApplyValue(object, *stableValue))
                {
                    ++appliedCount;
                }
                continue;
            }

            if (prop->ApplyInitialValue(object, initializer))
            {
                ++appliedCount;
            }
        }

        return appliedCount;
    }

    int ApplyDefaultValues(IUnknown *object)
    {
        if (!object)
        {
            return 0;
        }

        const IClass *cls = object->GetClass();
        if (!cls)
        {
            return 0;
        }

        Container::VariableArray<const ClassProperty *> properties = cls->GetAllProperties();
        int appliedCount = 0;

        for (const ClassProperty *prop : properties)
        {
            if (prop)
            {
                prop->ApplyDefaultValue(object);
                ++appliedCount;
            }
        }

        return appliedCount;
    }

    int CopyEditableProperties(Object &dst, const Object &src)
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

} // namespace NorvesLib::Core
