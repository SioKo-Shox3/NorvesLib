#pragma once
// 統合cooked骨格資産の内部CPU核。async/cache登録・GPU uploadは行わない。
#include "Asset/AssetResolveResult.h"
#include "Asset/CookedSkeletalFormat.h"
#include "Animation/SkeletalAssetResource.h"
#include "Container/PointerTypes.h"
#include "Thread/Thread.h"
namespace NorvesLib::Core
{
    class ResourceRegistry;
}
namespace NorvesLib::Core::Asset
{
    class AssetSystem;
}
namespace NorvesLib::Core::ResourceIO
{
    struct CookedSkeletalLoadPlan
    {
        Container::TSharedPtr<const Asset::AssetSystem> Assets;
        Container::AnsiString LogicalPath;
    };
    enum class SkeletalAssetLoadStatus : uint8_t
    {
        Success,
        InvalidRequest,
        ResolveRejected,
        FormatRejected,
        ParseRejected,
        MetadataMismatch,
        InvalidCpuResult,
        WrongOwnerThread,
        RegistryNotReady,
        ResourceCreateFailed,
        ResourceLoadFailed,
        InjectedFailure,
        Exception
    };
    struct SkeletalAssetLoadReport
    {
        SkeletalAssetLoadStatus Status = SkeletalAssetLoadStatus::InvalidRequest;
        Asset::AssetResolveSource Source = Asset::AssetResolveSource::None;
        Asset::AssetResolveStatus ResolveStatus = Asset::AssetResolveStatus::InvalidRequest;
        Asset::CookedSkeletalParseStatus ParseStatus = Asset::CookedSkeletalParseStatus::InvalidBlob;
        bool bResolveAttempted = false, bParseAttempted = false, bCreateAttempted = false;
        uint32_t CreatedResources = 0;
    };
    class CookedSkeletalCpuAsset;
    [[nodiscard]] bool LoadCookedSkeletalForWorker(const CookedSkeletalLoadPlan& plan, CookedSkeletalCpuAsset& out,
                                                   SkeletalAssetLoadReport& report);
    // private所有stateは成功したloaderだけが作る。default/moved-fromは空で、解析表の可変参照を渡さない。
    class CookedSkeletalCpuAsset
    {
      public:
        CookedSkeletalCpuAsset();
        ~CookedSkeletalCpuAsset();
        CookedSkeletalCpuAsset(CookedSkeletalCpuAsset&&) noexcept;
        CookedSkeletalCpuAsset& operator=(CookedSkeletalCpuAsset&&) noexcept;
        CookedSkeletalCpuAsset(const CookedSkeletalCpuAsset&) = delete;
        CookedSkeletalCpuAsset& operator=(const CookedSkeletalCpuAsset&) = delete;
        [[nodiscard]] const Asset::CookedSkeletalData* GetData() const noexcept;
        [[nodiscard]] Container::AnsiStringView GetLogicalPath() const noexcept;
        [[nodiscard]] const Container::String* GetResourcePath() const noexcept;

      private:
        struct State;
        Container::TUniquePtr<State> m_State;
        friend bool LoadCookedSkeletalForWorker(const CookedSkeletalLoadPlan&, CookedSkeletalCpuAsset&,
                                                SkeletalAssetLoadReport&);
    };
    struct SkeletalAssetCreateContext
    {
        ResourceRegistry* Registry = nullptr;
        // 所有者が組立より前に設定する。ここでOS main/GameThreadを自動判定しない。
        NorvesLib::Thread::Thread::ThreadId OwnerThread;
    };
    // const CPU結果を保持したまま候補を組み立て、最後にoutを置換する。
    // Registry登録・path cache公開はしない。Registryの寿命/再初期化はcallerが排他管理する。
    [[nodiscard]] bool AssembleCookedSkeletalAsset(const CookedSkeletalCpuAsset& cpu,
                                                   const SkeletalAssetCreateContext& context,
                                                   Container::TSharedPtr<SkeletalAssetResource>& out,
                                                   SkeletalAssetLoadReport& report);
} // namespace NorvesLib::Core::ResourceIO
