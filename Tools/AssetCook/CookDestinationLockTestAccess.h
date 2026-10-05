#pragma once
#include "CookDestinationLock.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    enum class CookLockFault
    {
        None,
        Create,
        Wait,
        Reobserve,
        VolumeChanged,
        Release,
        Close
    };
    // Release/Close注入でも実OSのcleanupを試みた上で失敗報告経路を検査する。実OS故障の再現ではない。
    [[nodiscard]] CookDestinationLockResult WithCookDestinationLockForTest(const CookDestinationLockRequest& request,
                                                                           CookDestinationLockedWork work,
                                                                           void* userData,
                                                                           Core::Container::AnsiString& error,
                                                                           CookLockFault fault);
    // 実process間のkeepalive/衝突fixture用。観測だけでmutexは作成しない。
    [[nodiscard]] bool InspectCookDestinationMutexNameForTest(const CookDestinationLockRequest& request,
                                                              Core::Container::AnsiString& out,
                                                              Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook::Detail
