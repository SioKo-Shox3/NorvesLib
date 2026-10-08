#pragma once
#include "Container/String.h"
namespace NorvesLib::Tools::AssetCook
{
    struct CookOwnerIdentity
    {
        // callerが保存stateとは独立に確定する比較用UTF8 identity。I/Oのlocatorへ戻さない。
        // この値層ではcanonical化・alias解決・存在確認は行わない。
        Core::Container::AnsiString CanonicalSpecLocator;
        Core::Container::AnsiString CanonicalFinalRuntimeRootIdentity;
        Core::Container::AnsiString ManifestName;
    };
    inline constexpr size_t MaximumCookOwnerIdentityBytes = 4096;
    // Windows 10 / Server 2016以降のCNGを使う。成功は認証・所有権取得・公開許可ではない。
    // case/Unicodeの正規化なし。SourceRoot等の入力選択は共通依存fingerprintで判定する。
    // false時outOwnerIdを保持する。hash入力と同じout objectを渡さない。
    [[nodiscard]] bool ComputeCookOwnerId(const CookOwnerIdentity& identity, Core::Container::AnsiString& outOwnerId,
                                          Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
