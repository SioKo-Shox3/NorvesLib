#pragma once
#include "SkeletalAssetPublication.h"
namespace NorvesLib::Core::ResourceIO::Detail
{
    enum class SkeletalPublicationPoint : uint8_t
    {
        AfterAssembly,
        AfterClone,
        AfterRegister,
        AfterPlaceholder,
        BeforeCommit
    };
    // AfterAssemblyだけRegistry操作が可能。他pointはRegistry mutex内なので再入しない。
    // commitのswap列には呼び出さない。false/標準例外は公開前の故障注入。
    using SkeletalPublicationProbe = bool (*)(SkeletalPublicationPoint, uint32_t,
                                              const Container::TSharedPtr<SkeletalAssetResource>&, void*);
    [[nodiscard]] bool PrepareSkeletalPublicationWithProbe(const CookedSkeletalCpuAsset& cpu,
                                                           const SkeletalAssetCreateContext& context,
                                                           SkeletalPreparedPublication& out,
                                                           SkeletalPublicationReport& report,
                                                           SkeletalPublicationProbe probe, void* probeContext);
    [[nodiscard]] bool PrepareRigSplitPublicationWithProbe(Container::TSharedPtr<const RigSplitPublicationReceipt>,
                                                           const SkeletalAssetCreateContext&,
                                                           SkeletalPreparedPublication&, SkeletalPublicationReport&,
                                                           SkeletalPublicationProbe, void*);
    [[nodiscard]] bool CommitSkeletalPublicationWithProbe(const SkeletalPreparedPublication& prepared,
                                                          const Container::String& key,
                                                          const SkeletalPublicationLimits& limits,
                                                          SkeletalPublishedAsset& out,
                                                          SkeletalPublicationReport& report,
                                                          SkeletalPublicationProbe probe, void* probeContext);
    [[nodiscard]] Container::TSharedPtr<SkeletalAssetResource> GetPreparedSkeletalAsset(
        const SkeletalPreparedPublication& prepared);
    [[nodiscard]] size_t SkeletalRegistryPoolCountForTest(ResourceRegistry& registry);
    void SetSkeletalCacheDomainCounterForTest(ResourceRegistry& registry, uint64_t next);
    void StressSkeletalOuterRehashForTest(ResourceRegistry& registry);
} // namespace NorvesLib::Core::ResourceIO::Detail
