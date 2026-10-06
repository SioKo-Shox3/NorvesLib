#pragma once
#include "Animation/ClipBankBinding.h"
namespace NorvesLib::Core::Skeletal::Detail
{
    using RigCreateProbe = bool (*)(uint32_t ordinal, const Container::TSharedPtr<Resource>& candidate, void* context);
    // productionと同じ組立関数への限定probe。通常false/例外を未登録候補の境界で注入する。
    [[nodiscard]] bool AssembleBoundClipBankWithProbe(const BoundClipBank&,
                                                      const ResourceIO::SkeletalAssetCreateContext&,
                                                      Container::TSharedPtr<SkeletalAssetResource>&, RigV1Report&,
                                                      RigCreateProbe, void*);
} // namespace NorvesLib::Core::Skeletal::Detail
