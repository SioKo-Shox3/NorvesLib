#include "Resource/MaterialImportPolicy.h"
#include "Asset/CookedMaterialFormat.h"
#include <cmath>
#include <limits>

namespace NorvesLib::Core::AssetImport
{
    namespace
    {
        bool ValidChannel(ArmChannel channel)
        {
            return channel==ArmChannel::Occlusion || channel==ArmChannel::Roughness || channel==ArmChannel::Metallic;
        }
        bool Unit(double value)
        {
            return std::isfinite(value) && value>=0 && value<=1;
        }
        double Apply(double value, ArmChannel channel, double factor)
        {
            return channel==ArmChannel::Occlusion ? 1+factor*(value-1) : factor*value;
        }
        double Percentile(const uint64_t (&bins)[256], uint64_t count, uint64_t percent)
        {
            // Type 7の位置(count-1)*percent/100を整数部と余りに分け、巨大countでもoverflowさせない。
            const uint64_t n=count-1;
            const uint64_t lower=(n/100)*percent + ((n%100)*percent)/100;
            const uint64_t remainder=((n%100)*percent)%100;
            const auto at = [&](uint64_t rank)
            {
                uint64_t prefix=0;
                for (uint32_t value=0;value<256;++value)
                {
                    prefix+=bins[value];
                    if (prefix>rank)
                    {
                        return double(value);
                    }
                }
                return 255.0;
            };
            const double first=at(lower);
            return remainder==0 ? first : first+(at(lower+1)-first)*(double(remainder)/100);
        }
    }
    ArmImportPolicy DefaultArmImportPolicy(bool aiGenerated) noexcept
    {
        ArmImportPolicy result;
        result.Channels[0].Mode=ArmMode::Ignore;
        result.Channels[2].Mode=aiGenerated ? ArmMode::Ignore : ArmMode::Auto;
        return result;
    }
    MaterialPolicyStatus AnalyzeArmHistogram(const uint64_t (&bins)[256], ArmChannel channel,
        const ArmChannelPolicy& policy, double factor, ArmChannelDecision& out) noexcept
    {
        if (!ValidChannel(channel) || !Unit(factor) || !Unit(policy.Constant) || !Unit(policy.AutoWidth) ||
            (policy.Mode!=ArmMode::Texture && policy.Mode!=ArmMode::Ignore && policy.Mode!=ArmMode::Constant && policy.Mode!=ArmMode::Auto))
        {
            return MaterialPolicyStatus::InvalidInput;
        }
        ArmChannelDecision result;
        long double sum=0;
        bool any=false;
        for (uint32_t value=0;value<256;++value)
        {
            if (bins[value]>UINT64_MAX-result.SampleCount)
            {
                return MaterialPolicyStatus::HistogramOverflow;
            }
            result.SampleCount+=bins[value];
            sum+=static_cast<long double>(bins[value])*value;
            if (bins[value]!=0)
            {
                if (!any)
                {
                    result.Minimum=static_cast<uint8_t>(value);
                }
                result.Maximum=static_cast<uint8_t>(value);
                any=true;
            }
        }
        if (!any)
        {
            return MaterialPolicyStatus::EmptyHistogram;
        }
        result.Percentile1=Percentile(bins,result.SampleCount,1);
        result.Percentile99=Percentile(bins,result.SampleCount,99);
        result.Mean=static_cast<double>(sum/result.SampleCount/255);
        if (result.Mean>1)
        {
            result.Mean=1;
        }
        result.EffectivePercentileWidth=(double(result.Percentile99)-result.Percentile1)/255*factor;
        result.UseTexture=policy.Mode==ArmMode::Texture || (policy.Mode==ArmMode::Auto && result.EffectivePercentileWidth>policy.AutoWidth);
        result.Scalar=Apply(result.Mean,channel,factor);
        if (policy.Mode==ArmMode::Ignore)
        {
            result.Scalar=channel==ArmChannel::Metallic ? 0 : 1;
        }
        else if (policy.Mode==ArmMode::Constant)
        {
            result.Scalar=policy.Constant;
        }
        out=result;
        return MaterialPolicyStatus::Success;
    }
    MaterialPolicyStatus BakeArmByte(uint8_t sample, ArmChannel channel, double factor, uint8_t& out) noexcept
    {
        if (!ValidChannel(channel) || !Unit(factor))
        {
            return MaterialPolicyStatus::InvalidInput;
        }
        const double value=Apply(double(sample)/255,channel,factor);
        out=static_cast<uint8_t>(std::floor(value*255+0.5));
        return MaterialPolicyStatus::Success;
    }
    EmissiveScale SelectEmissiveScale(EmissiveScale material, EmissiveScale asset, EmissiveScale assetSet) noexcept
    {
        return material.Present ? material : asset.Present ? asset : assetSet;
    }
    MaterialPolicyStatus ImportEmission(const double (&factor)[3], double strength, EmissiveScale scale, ImportedEmission& out) noexcept
    {
        if (!Unit(factor[0]) || !Unit(factor[1]) || !Unit(factor[2]) || !std::isfinite(strength) || strength<0)
        {
            return MaterialPolicyStatus::InvalidInput;
        }
        if (scale.Present && (!std::isfinite(scale.NitsPerUnit) || scale.NitsPerUnit<=0))
        {
            return MaterialPolicyStatus::InvalidNitsPerUnit;
        }
        ImportedEmission result;
        result.Emitting=strength>0 && (factor[0]>0 || factor[1]>0 || factor[2]>0);
        if (!result.Emitting)
        {
            out=result;
            return MaterialPolicyStatus::Success;
        }
        if (!scale.Present)
        {
            return MaterialPolicyStatus::MissingNitsPerUnit;
        }
        double maximum=factor[0];
        for (uint32_t index=1;index<3;++index)
        {
            if (factor[index]>maximum)
            {
                maximum=factor[index];
            }
        }
        // 色比は最大成分で先に正規化し、subnormal factorでもYを0へ落とさない。
        const double relative[]{factor[0]/maximum,factor[1]/maximum,factor[2]/maximum};
        const double y=0.2126*relative[0]+0.7152*relative[1]+0.0722*relative[2];
        int colorExponent=0, strengthExponent=0, unitExponent=0;
        const double mantissa=std::frexp(maximum,&colorExponent)*std::frexp(strength,&strengthExponent)*
            std::frexp(scale.NitsPerUnit,&unitExponent)*y;
        // 元の3因子の途中積ではなく、最後の物理値へ一度だけ指数を戻す。
        const double nits=std::scalbn(mantissa,colorExponent+strengthExponent+unitExponent);
        if (!(y>0) || !std::isfinite(nits) || nits<=0 || nits>std::numeric_limits<float>::max())
        {
            return MaterialPolicyStatus::EmissiveOutOfRange;
        }
        Asset::CookedMaterialRecord record;
        record.EmissiveNits=static_cast<float>(nits);
        for (uint32_t index=0;index<3;++index)
        {
            record.EmissiveColor[index]=static_cast<float>(relative[index]/y);
        }
        if (Asset::ValidateCookedMaterialRecord(record,0)!=Asset::CookedMaterialStatus::Success)
        {
            return MaterialPolicyStatus::EmissiveOutOfRange;
        }
        result.Nits=record.EmissiveNits;
        for (uint32_t index=0;index<3;++index)
        {
            result.Color[index]=record.EmissiveColor[index];
        }
        out=result;
        return MaterialPolicyStatus::Success;
    }
}
