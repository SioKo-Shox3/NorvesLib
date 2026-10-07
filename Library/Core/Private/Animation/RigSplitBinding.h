#pragma once
// 全bankの束縛成功後だけ公開する所有CPU集合。Registry公開/cacheは別責務。
#include "Asset/CookedSkinMeshV1.h"
#include "Resource/SkeletalAssetLoader.h"
namespace NorvesLib::Core::Skeletal
{
    struct RigSplitCpuData
    {
        SkeletonV1 Skeleton;
        SkinMeshV1 Mesh;
        Container::VariableArray<ClipBankV1> Banks;
        Container::VariableArray<SkeletalAnimationClip> Clips;
        RigSplitReport BindingReport;
    };
    class CookedRigSplitCpuAsset
    {
      public:
        [[nodiscard]] const RigSplitCpuData* GetData() const noexcept
        {
            return m_Data.get();
        }

      private:
        Container::TSharedPtr<const RigSplitCpuData> m_Data;
        friend bool BindRigSplitV1(const SkeletonV1&, const SkinMeshV1&, Container::Span<const ClipBankV1>,
                                   const RigBindingPolicy&, CookedRigSplitCpuAsset&, RigSplitReport&,
                                   const RigV1Limits&);
    };
    [[nodiscard]] bool BindRigSplitV1(const SkeletonV1&, const SkinMeshV1&,
                                      Container::Span<const ClipBankV1> orderedBanks, const RigBindingPolicy&,
                                      CookedRigSplitCpuAsset& out, RigSplitReport&, const RigV1Limits& = {});
    [[nodiscard]] bool AssembleRigSplitV1(const CookedRigSplitCpuAsset&, const ResourceIO::SkeletalAssetCreateContext&,
                                          Container::TSharedPtr<SkeletalAssetResource>& out, RigSplitReport&);
} // namespace NorvesLib::Core::Skeletal
