#pragma once
#include "SingleAssetCook.h"
#include "Container/VariableArray.h"
#include <cstdint>
namespace NorvesLib::Tools::AssetCook
{
    enum class CookDependencyRole : uint8_t { Source, Sidecar, ExternalBuffer, ExternalImage };
    struct CookDependencyFile
    {
        std::filesystem::path Path;
        CookDependencyRole Role=CookDependencyRole::Source;
        bool bPresent=false;
        uint64_t Size=0, ContentHash=0;
    };
    struct CookDependencySnapshot
    {
        uint32_t SchemaVersion=1;
        uint64_t CookerRevision=0, Fingerprint=0;
        Core::Container::VariableArray<CookDependencyFile> Files;
    };
    // source/選択sidecar/全外部buffer・imageを読み、値所有の増分用印を作る。
    // 画像の完全decodeや出力cache検証は行わない。成功時だけoutを置換する。
    // cookerRevisionは呼出側が管理する非0の処理版。bSkipIfUnchangedと一時出力pathは印に含めない。
    // 複数fileのatomic snapshotではない。公開前にも再採取して一致確認すること。
    [[nodiscard]] bool CaptureCookDependencySnapshot(const SingleAssetCookRequest& request, uint64_t cookerRevision,
        CookDependencySnapshot& out, Core::Container::AnsiString& error);
}
