#pragma once
#include "CookOwnerBinding.h"
#include "CookOwnedState.h"
#include <filesystem>
namespace NorvesLib::Tools::AssetCook
{
    struct CookOwnerResolveRequest
    {
        std::filesystem::path SpecPath, FinalRuntimeRoot;
        Core::Container::AnsiString ManifestName;
    };
    struct CookResolvedOwnerBinding
    {
        // I/Oに使うcaller側の検査済みlocatorと、比較用identityを分離する。
        std::filesystem::path SpecLocator, FinalRuntimeRootLocator;
        CookOwnerIdentity Identity;
        CookStateBinding ExpectedBinding;
        bool bFinalRuntimeRootPresent = false;
    };
    // specは既存regular file。rootはASCII caller locatorの既存directoryか、既存直親下の不在leaf。
    // 不在rootは仮観測。作成後に再解決してidentityの完全一致を確認するまでは公開へ使えない。
    // read-only、成功時だけoutを変更。errorと入力/output文字列のaliasは診断も変更せず拒否する。
    // saved stateを読まず、既存rootの採用・更新・同時実行の許可は返さない。
    [[nodiscard]] bool ResolveCookOwnerBinding(const CookOwnerResolveRequest& request, CookResolvedOwnerBinding& out,
                                               Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
