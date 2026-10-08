#include "Resource/SkeletalInfluenceAttributes.h"
#include "Text/JsonDocument.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace NorvesLib::Core::Skeletal
{
    InfluenceSetCollectionStatus CollectSkeletalInfluenceSets(const JsonValue& attributes,
        Container::VariableArray<SkeletalInfluenceSet>& outSets)
    {
        using Status=InfluenceSetCollectionStatus;
        if (!attributes.IsObject())
        {
            return Status::InvalidAttributes;
        }
        Container::VariableArray<InfluenceAttributeAccessor> entries;
        for (size_t index=0;index<attributes.GetObjectSize();++index)
        {
            const auto& name=attributes.GetMemberName(index);
            const auto parsed=ClassifyInfluenceAttributeName<Container::String::value_type>({name.data(),name.size()});
            if (parsed.Kind==InfluenceAttributeKind::Invalid)
            {
                return Status::InvalidAttributes;
            }
            if (parsed.Kind==InfluenceAttributeKind::Other)
            {
                continue;
            }
            const auto value=attributes.GetMemberValue(index);
            if (!value.IsNumber() || !std::isfinite(value.AsNumber()) || value.AsNumber()<0 ||
                value.AsNumber()>std::numeric_limits<uint32_t>::max() || std::floor(value.AsNumber())!=value.AsNumber())
            {
                return Status::InvalidAccessorIndex;
            }
            entries.push_back({parsed.Kind,parsed.SetIndex,static_cast<uint32_t>(value.AsNumber())});
        }
        std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b)
        {
            return a.SetIndex!=b.SetIndex ? a.SetIndex<b.SetIndex : a.Kind<b.Kind;
        });
        const auto pairs=CheckSortedInfluenceSetPairs(entries);
        if (!pairs.bValid)
        {
            return Status::InvalidSetPairs;
        }
        Container::VariableArray<SkeletalInfluenceSet> candidate;
        candidate.reserve(pairs.SetCount);
        for (size_t index=0;index<pairs.SetCount;++index)
        {
            candidate.push_back({entries[index*2].AccessorIndex,entries[index*2+1].AccessorIndex});
        }
        outSets=std::move(candidate);
        return Status::Success;
    }

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
