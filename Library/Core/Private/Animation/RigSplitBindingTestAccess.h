#pragma once
#include "Animation/RigSplitBinding.h"
#include "Animation/ClipBankBindingTestAccess.h"
namespace NorvesLib::Core::Skeletal::Detail
{
    // 未登録候補の各作成境界だけに失敗を注入する。通常入口はnullで同じ実装を通る。
    [[nodiscard]] bool AssembleRigSplitWithProbe(const CookedRigSplitCpuAsset&,
                                                 const ResourceIO::SkeletalAssetCreateContext&,
                                                 Container::TSharedPtr<SkeletalAssetResource>&, RigSplitReport&,
                                                 RigCreateProbe, void*, const Container::String* bundleUri = nullptr);
} // namespace NorvesLib::Core::Skeletal::Detail
