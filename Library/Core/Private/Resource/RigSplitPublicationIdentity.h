#pragma once
// 分離要求の全内容を所有して索引と成功receiptを区別する。永続cacheは持たない。
#include "Resource/RigSplitAssetLoader.h"
namespace NorvesLib::Core::ResourceIO
{
    inline constexpr size_t RigSplitMaximumKeyBytes = 256 * 1024;
    enum class RigSplitIdentityStatus : uint8_t
    {
        Success,
        InvalidRequest,
        LimitExceeded,
        Exception
    };
    struct RigSplitRequestIdentityData
    {
        RigSplitLoadPlan Plan;
        Container::String Key, BundleUri;
        Container::VariableArray<Asset::AssetCookedReference> References;
        uint64_t Session = 0, Domain = 0, Generation = 0;
        bool bCanCache = false;
    };
    class RigSplitRequestIdentity
    {
      public:
        const RigSplitRequestIdentityData* GetData() const noexcept
        {
            return m_Data.get();
        }

      private:
        Container::TSharedPtr<const RigSplitRequestIdentityData> m_Data;
        friend RigSplitIdentityStatus BuildRigSplitRequestIdentity(const Skeletal::RigSplitRequest&,
                                                                   Container::TSharedPtr<const Asset::AssetSystem>,
                                                                   uint64_t, uint64_t, uint64_t, size_t,
                                                                   RigSplitRequestIdentity&);
    };
    [[nodiscard]] RigSplitIdentityStatus BuildRigSplitRequestIdentity(const Skeletal::RigSplitRequest&,
                                                                      Container::TSharedPtr<const Asset::AssetSystem>,
                                                                      uint64_t session, uint64_t domain,
                                                                      uint64_t generation, size_t maxKeyBytes,
                                                                      RigSplitRequestIdentity& out);
    [[nodiscard]] bool SameRigSplitRequestIdentity(const RigSplitRequestIdentity&,
                                                   const RigSplitRequestIdentity&) noexcept;
    [[nodiscard]] bool SameRigSplitReference(const Asset::AssetCookedReference&,
                                             const Asset::AssetCookedReference&) noexcept;
    class RigSplitPublicationReceipt
    {
      public:
        RigSplitPublicationReceipt() = default;
        RigSplitPublicationReceipt(const RigSplitPublicationReceipt&) = default;
        const RigSplitRequestIdentity& GetIdentity() const noexcept
        {
            return m_Identity;
        }
        const Skeletal::CookedRigSplitCpuAsset& GetCpu() const noexcept
        {
            return m_Cpu;
        }
        const RigSplitLoadEvidence& GetEvidence() const noexcept
        {
            return m_Evidence;
        }
        const Container::TSharedPtr<const RigSplitAssetDiagnostics>& GetDiagnostics() const noexcept
        {
            return m_Diagnostics;
        }
        size_t GetOwnedBytes() const noexcept
        {
            return m_OwnedBytes;
        }
        bool IsValid() const noexcept
        {
            return m_Identity.GetData() && m_Cpu.GetData() && m_Diagnostics &&
                   m_Evidence.Entries.size() == m_Identity.GetData()->References.size() &&
                   m_Identity.GetData()->bCanCache;
        }

      private:
        friend class SkeletalBundlePublisherAccess;
        friend class RigSplitAssetAccess;
        friend bool LoadRigSplitForPublication(const RigSplitRequestIdentity&,
                                               Container::TSharedPtr<const RigSplitPublicationReceipt>&,
                                               RigSplitAssetDiagnostics&);
        friend Container::TSharedPtr<const RigSplitPublicationReceipt> CompleteRigSplitOwnerReceipt(
            const RigSplitPublicationReceipt&, const Skeletal::RigSplitReport&);
        RigSplitRequestIdentity m_Identity;
        Skeletal::CookedRigSplitCpuAsset m_Cpu;
        RigSplitLoadEvidence m_Evidence;
        Container::TSharedPtr<const RigSplitAssetDiagnostics> m_Diagnostics;
        size_t m_OwnedBytes = 0;
    };
    // 失敗の詳細も事前確保したdiagnosticsへ書く。成功receiptとoutは最後だけ置換。
    [[nodiscard]] bool LoadRigSplitForPublication(const RigSplitRequestIdentity&,
                                                  Container::TSharedPtr<const RigSplitPublicationReceipt>& out,
                                                  RigSplitAssetDiagnostics&);
    [[nodiscard]] Container::TSharedPtr<const RigSplitPublicationReceipt> CompleteRigSplitOwnerReceipt(
        const RigSplitPublicationReceipt&, const Skeletal::RigSplitReport&);
    class RigSplitAssetAccess
    {
      public:
        static void Attach(SkeletalAssetResource&, Container::TSharedPtr<const RigSplitPublicationReceipt>);
        static const Container::TSharedPtr<const RigSplitPublicationReceipt>& Get(const SkeletalAssetResource&);
        [[nodiscard]] static bool Matches(const SkeletalAssetResource&, const RigSplitRequestIdentity&) noexcept;
        [[nodiscard]] static size_t MemorySize(const SkeletalAssetResource&) noexcept;
    };
} // namespace NorvesLib::Core::ResourceIO
