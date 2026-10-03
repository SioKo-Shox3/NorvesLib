#include "Resource/SkeletalInfluenceAttributes.h"
#include "Text/JsonDocument.h"

namespace NorvesLib::Core::Skeletal
{
    StrictInfluenceStatus ValidateStrictInfluenceAttributes(const JsonValue& attributes)
    {
        if (!attributes.IsObject())
        {
            return StrictInfluenceStatus::InvalidAttribute;
        }
        bool bAdditional=false;
        for (size_t index=0;index<attributes.GetObjectSize();++index)
        {
            const auto& name=attributes.GetMemberName(index);
            const auto parsed=ClassifyInfluenceAttributeName<Container::String::value_type>({name.data(),name.size()});
            if (parsed.Kind==InfluenceAttributeKind::Invalid)
            {
                return StrictInfluenceStatus::InvalidAttribute;
            }
            bAdditional=bAdditional || ((parsed.Kind==InfluenceAttributeKind::Joints ||
                parsed.Kind==InfluenceAttributeKind::Weights) && parsed.SetIndex>0);
        }
        return bAdditional ? StrictInfluenceStatus::AdditionalSet : StrictInfluenceStatus::Success;
    }
} // namespace NorvesLib::Core::Skeletal
