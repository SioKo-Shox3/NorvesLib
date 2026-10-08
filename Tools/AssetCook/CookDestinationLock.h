#pragma once
#include "Container/String.h"
#include <filesystem>
namespace NorvesLib::Tools::AssetCook
{
    struct CookDestinationLockRequest
    {
        std::filesystem::path FinalRuntimeRoot;
    };
    struct CookDestinationLockContext
    {
        std::filesystem::path FinalRuntimeRootLocator;
        Core::Container::AnsiString CanonicalFinalRuntimeRootIdentity, CanonicalVolumeGuid;
        bool bFinalRuntimeRootPresent = false;
        // falseでも永続journalの検査を省略できない。mutexはcrashの永続印ではない。
        bool bAbandoned = false;
    };
    enum class CookDestinationLockResult
    {
        Executed,
        Busy,
        Error
    };
    using CookDestinationLockedWork = bool (*)(const CookDestinationLockContext& context, void* userData,
                                               Core::Container::AnsiString& error);
    // 同volumeの協調writerを直列化する同期callback。待ち時間0、同threadの再入は禁止。
    // callbackの非同期作業はreturn前にjoinする。HANDLE/解除権限を外へ渡さない。
    // Executedはcallback成功と解除成功だけ。state採用/transaction commit/認証は保証しない。
    // Errorでもcallbackが実行済みの場合がある。副作用のrollbackを保証しない。
    [[nodiscard]] CookDestinationLockResult WithCookDestinationLock(const CookDestinationLockRequest& request,
                                                                    CookDestinationLockedWork work, void* userData,
                                                                    Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
