#pragma once
#include "SkeletalAssetLoader.h"
namespace NorvesLib::Core::ResourceIO::Detail
{
    enum class SkeletalCreatePoint : uint8_t
    {
        Mesh,
        Skeleton,
        Clip,
        Aggregate
    };
    // 候補の生成直後・Load前の観測/故障注入。製品呼出はnullptrを使う。
    using SkeletalCreateProbe = bool (*)(SkeletalCreatePoint, uint32_t, const Container::TSharedPtr<Resource>&, void*);
    [[nodiscard]] bool AssembleCookedSkeletalAssetWithProbe(const CookedSkeletalCpuAsset& cpu,
                                                            const SkeletalAssetCreateContext& context,
                                                            Container::TSharedPtr<SkeletalAssetResource>& out,
                                                            SkeletalAssetLoadReport& report, SkeletalCreateProbe probe,
                                                            void* probeContext);
} // namespace NorvesLib::Core::ResourceIO::Detail
