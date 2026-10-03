#pragma once

#include "Container/Span.h"
#include <cstdint>
#include <limits>

namespace NorvesLib::Core { class JsonValue; }
namespace NorvesLib::Core::Skeletal
{
    enum class InfluenceAttributeKind { Other, Joints, Weights, Invalid };
    struct InfluenceAttributeName
    {
        InfluenceAttributeKind Kind = InfluenceAttributeKind::Other;
        uint32_t SetIndex = 0;
    };
    // JOINTS_n/WEIGHTS_nの正準10進名を分類する。未知attributeは変更しない。
    template<typename Char>
    [[nodiscard]] InfluenceAttributeName ClassifyInfluenceAttributeName(Container::Span<const Char> name) noexcept
    {
        if (!name.data() && !name.empty())
        {
            return {InfluenceAttributeKind::Invalid,0};
        }
        const auto matches=[&](const char* prefix,size_t count)
        {
            if (name.size()<count)
            {
                return false;
            }
            for (size_t i=0;i<count;++i)
            {
                if (name[i]!=static_cast<Char>(prefix[i]))
                {
                    return false;
                }
            }
            return true;
        };
        const bool bJoints=matches("JOINTS_",7);
        const bool bWeights=matches("WEIGHTS_",8);
        if (!bJoints && !bWeights)
        {
            return {};
        }
        const size_t start=bJoints ? 7 : 8;
        if (name.size()==start || (name.size()>start+1 && name[start]==static_cast<Char>('0')))
        {
            return {InfluenceAttributeKind::Invalid,0};
        }
        uint32_t index=0;
        for (size_t i=start;i<name.size();++i)
        {
            if (name[i]<static_cast<Char>('0') || name[i]>static_cast<Char>('9'))
            {
                return {InfluenceAttributeKind::Invalid,0};
            }
            const uint32_t digit=static_cast<uint32_t>(name[i]-static_cast<Char>('0'));
            if (index>(std::numeric_limits<uint32_t>::max()-digit)/10)
            {
                return {InfluenceAttributeKind::Invalid,0};
            }
            index=index*10+digit;
        }
        return {bJoints ? InfluenceAttributeKind::Joints : InfluenceAttributeKind::Weights,index};
    }
    enum class StrictInfluenceStatus { Success, AdditionalSet, InvalidAttribute };
    // decodeとcook前cache照合は同じStrict gateを使う。追加セットの値は黙認しない。
    [[nodiscard]] StrictInfluenceStatus ValidateStrictInfluenceAttributes(const JsonValue& attributes);
} // namespace NorvesLib::Core::Skeletal
