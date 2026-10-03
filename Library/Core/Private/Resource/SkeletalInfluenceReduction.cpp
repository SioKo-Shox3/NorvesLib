#include "Resource/SkeletalInfluenceReduction.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Skeletal
{
    namespace
    {
        struct PositiveSum
        {
            double Sum=0,Correction=0;
            void Add(double value) noexcept
            {
                const double next=Sum+value;
                Correction += Sum>=value ? (Sum-next)+value : (value-next)+Sum;
                Sum=next;
            }
            double Value() const noexcept { return Sum+Correction; }
        };
        bool Overlap(const void* a,size_t aSize,const void* b,size_t bSize) noexcept
        {
            const auto first=reinterpret_cast<uintptr_t>(a),second=reinterpret_cast<uintptr_t>(b);
            return aSize!=0 && bSize!=0 && (first<=second ? second-first<aSize : first-second<bSize);
        }
        bool ValidOptions(const InfluenceReductionOptions& o) noexcept
        {
            return std::isfinite(o.MinimumWeight) && o.MinimumWeight>=0 && o.MinimumWeight<1 &&
                std::isfinite(o.InputWeightSumTolerance) && o.InputWeightSumTolerance>=0 && o.InputWeightSumTolerance<1 &&
                std::isfinite(o.WarnDroppedWeight) && std::isfinite(o.FailDroppedWeight) &&
                o.WarnDroppedWeight>=0 && o.WarnDroppedWeight<=o.FailDroppedWeight && o.FailDroppedWeight<=1;
        }
    }
    InfluenceReductionOutcome ReduceSkinInfluences(Container::Span<const SkinInfluence> input,
        uint32_t jointCount,const InfluenceReductionOptions& options,Container::Span<SkinInfluence> workspace,
        ReducedSkinInfluences& out) noexcept
    {
        using Status=InfluenceReductionStatus;
        if (!ValidOptions(options))
        {
            return {Status::InvalidOptions};
        }
        if (!input.data() || input.empty() || jointCount==0 || input.size()>std::numeric_limits<uint32_t>::max() ||
            input.size()>std::numeric_limits<size_t>::max()/sizeof(SkinInfluence))
        {
            return {Status::InvalidInput};
        }
        if (!workspace.data() || workspace.size()<input.size() ||
            workspace.size()>std::numeric_limits<size_t>::max()/sizeof(SkinInfluence))
        {
            return {Status::InsufficientWorkspace};
        }
        const size_t inputBytes=input.size()*sizeof(SkinInfluence),workBytes=workspace.size()*sizeof(SkinInfluence);
        if (Overlap(input.data(),inputBytes,workspace.data(),workBytes) ||
            Overlap(input.data(),inputBytes,&out,sizeof(out)) || Overlap(workspace.data(),workBytes,&out,sizeof(out)) ||
            Overlap(workspace.data(),workBytes,&options,sizeof(options)) || Overlap(&out,sizeof(out),&options,sizeof(options)))
        {
            return {Status::OverlappingStorage};
        }
        InfluenceReductionOutcome result;
        for (const auto& influence : input)
        {
            if (influence.JointIndex>=jointCount)
            {
                return {Status::InvalidJoint};
            }
            if (!std::isfinite(influence.Weight) || influence.Weight<0)
            {
                return {Status::InvalidWeight};
            }
            if (influence.Weight>1+options.InputWeightSumTolerance)
            {
                return {Status::InvalidWeightSum};
            }
            result.OriginalNonzeroCount+=influence.Weight>0 ? 1u : 0u;
        }
        std::copy(input.begin(),input.end(),workspace.begin());
        // 足し合わせる順も正準化し、属性/slotの並び替えで選択結果を変えない。
        std::sort(workspace.begin(),workspace.begin()+input.size(),[](const auto& a,const auto& b)
        {
            return a.JointIndex!=b.JointIndex ? a.JointIndex<b.JointIndex : a.Weight<b.Weight;
        });
        PositiveSum total;
        size_t cursor=0,unique=0;
        while (cursor<input.size())
        {
            const uint32_t joint=workspace[cursor].JointIndex;
            PositiveSum group;
            do
            {
                group.Add(workspace[cursor].Weight);
                total.Add(workspace[cursor].Weight);
                ++cursor;
            }
            while (cursor<input.size() && workspace[cursor].JointIndex==joint);
            const double weight=group.Value();
            if (weight>0)
            {
                workspace[unique++]={joint,weight};
            }
        }
        const double originalTotal=total.Value();
        if (!std::isfinite(originalTotal) || originalTotal<=0 || std::abs(originalTotal-1)>options.InputWeightSumTolerance)
        {
            result.Status=Status::InvalidWeightSum;
            return result;
        }
        result.UniqueNonzeroCount=static_cast<uint32_t>(unique);
        PositiveSum dropped;
        size_t eligible=0;
        for (size_t i=0;i<unique;++i)
        {
            if (workspace[i].Weight<=options.MinimumWeight)
            {
                dropped.Add(workspace[i].Weight);
            }
            else
            {
                workspace[eligible++]=workspace[i];
            }
        }
        std::sort(workspace.begin(),workspace.begin()+eligible,[](const auto& a,const auto& b)
        {
            return a.Weight!=b.Weight ? a.Weight>b.Weight : a.JointIndex<b.JointIndex;
        });
        result.KeptCount=static_cast<uint32_t>(std::min(size_t{4},eligible));
        for (size_t i=result.KeptCount;i<eligible;++i)
        {
            dropped.Add(workspace[i].Weight);
        }
        result.DroppedWeight=dropped.Value()/originalTotal;
        result.bWarning=result.DroppedWeight>options.WarnDroppedWeight;
        result.bReduced=result.OriginalNonzeroCount>result.KeptCount;
        if (result.KeptCount==0)
        {
            result.Status=Status::AllWeightsRemoved;
            return result;
        }
        if (result.DroppedWeight>options.FailDroppedWeight)
        {
            result.Status=Status::DroppedWeightExceeded;
            return result;
        }
        PositiveSum retained;
        for (size_t i=0;i<result.KeptCount;++i)
        {
            retained.Add(workspace[i].Weight);
        }
        const double retainedTotal=retained.Value();
        result.bRenormalized=retainedTotal!=1;
        ReducedSkinInfluences candidate;
        for (size_t i=0;i<result.KeptCount;++i)
        {
            const double normalized=workspace[i].Weight/retainedTotal;
            const float weight=static_cast<float>(normalized);
            if (!std::isfinite(weight) || (normalized>0 && weight==0))
            {
                result.Status=Status::Unrepresentable;
                return result;
            }
            candidate.Joints[i]=workspace[i].JointIndex;
            candidate.Weights[i]=weight;
        }
        out=candidate;
        result.Status=Status::Success;
        return result;
    }
} // namespace NorvesLib::Core::Skeletal
