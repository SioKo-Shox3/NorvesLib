#pragma once
#include "Asset/AssetManifest.h"
#include "Container/Span.h"
namespace NorvesLib::Tools::AssetCook
{
    struct CookOutputPackageFingerprint
    {
        uint64_t Size=0, ContentHash=0;
    };
    // callerが検証した期待参照と、既に読み込んだpackageを照合する。
    // source freshness/path安全性/所有権の証明ではない。全package hashは非暗号学的な増分用。
    // 成功時だけoutを置換し、失敗理由はerrorへ返す。入力bytesを保持しない。
    [[nodiscard]] bool ValidateCookOutputPackage(const Core::Asset::AssetCookedReference& expected,
        Core::Container::Span<const uint8_t> bytes, CookOutputPackageFingerprint& out,
        Core::Container::AnsiString& error);
}
