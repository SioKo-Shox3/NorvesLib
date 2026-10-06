#pragma once
// 名前/作者restを照合済みの所有結果だけをResource組立へ渡す。
#include "Asset/CookedClipBankV1.h"
#include "Resource/SkeletalAssetLoader.h"
namespace NorvesLib::Core::Skeletal
{
    struct BoundClipBankData
    {
        RigAuthoringCpu Target;
        Container::VariableArray<SkeletalAnimationClip> Clips;
        RigV1Report BindingReport;
    };
    class BoundClipBank
    {
      public:
        [[nodiscard]] const BoundClipBankData* GetData() const noexcept
        {
            return m_Data.get();
        }

      private:
        Container::TSharedPtr<const BoundClipBankData> m_Data;
        friend bool BindClipBankV1(const ClipBankV1&, const RigAuthoringCpu&, const RigBindingPolicy&, BoundClipBank&,
                                   RigV1Report&);
    };
    [[nodiscard]] bool BindClipBankV1(const ClipBankV1& bank, const RigAuthoringCpu& target,
                                      const RigBindingPolicy& policy, BoundClipBank& out, RigV1Report& report);
    // 読込workerでは呼ばない。未登録の候補を全成功時だけoutへ渡す。
    [[nodiscard]] bool AssembleBoundClipBank(const BoundClipBank&, const ResourceIO::SkeletalAssetCreateContext&,
                                             Container::TSharedPtr<SkeletalAssetResource>& out, RigV1Report& report);
} // namespace NorvesLib::Core::Skeletal
