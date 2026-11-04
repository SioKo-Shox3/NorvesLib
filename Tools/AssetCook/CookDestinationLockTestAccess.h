#pragma once
#include "CookDestinationLock.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
namespace NorvesLib::Tools::AssetCook::Detail
{
    // 試験だけの mutex 名前空間の最大バイト数。
    inline constexpr size_t MaximumCookDestinationLockNamespaceBytesForTest = 40;

    // プロセス内の名前空間を交換し、前の値を返す。nullptr または空なら解除する。
    // 英数字と '-' 以外、または上限を超える長さの場合は変更しない。
    [[nodiscard]] bool ExchangeCookDestinationLockNamespaceForTest(
        const char* next, char (&previous)[MaximumCookDestinationLockNamespaceBytesForTest + 1]);

    // 試験の範囲内だけ名前空間を設定し、終了時に前の値へ戻す。
    class ScopedCookDestinationLockNamespaceForTest
    {
    public:
        explicit ScopedCookDestinationLockNamespaceForTest(const char* testNamespace)
            : m_bValid(ExchangeCookDestinationLockNamespaceForTest(testNamespace, m_Previous))
        {
        }

        ScopedCookDestinationLockNamespaceForTest(const ScopedCookDestinationLockNamespaceForTest&) = delete;
        ScopedCookDestinationLockNamespaceForTest& operator=(const ScopedCookDestinationLockNamespaceForTest&) = delete;

        ~ScopedCookDestinationLockNamespaceForTest()
        {
            char discarded[MaximumCookDestinationLockNamespaceBytesForTest + 1]{};
            if (m_bValid && !ExchangeCookDestinationLockNamespaceForTest(m_Previous, discarded))
            {
                std::fputs("mutex の名前空間を元に戻せませんでした\n", stderr);
                std::fflush(stderr);
                std::_Exit(EXIT_FAILURE);
            }
        }

        [[nodiscard]] bool Valid() const
        {
            return m_bValid;
        }

    private:
        char m_Previous[MaximumCookDestinationLockNamespaceBytesForTest + 1]{};
        bool m_bValid = false;
    };

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
