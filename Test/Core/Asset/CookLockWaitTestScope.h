// cook のロック待ちと試験用名前空間を範囲内だけ設定し、元の状態へ戻す。
#pragma once

#include "Container/VariableArray.h"
#include "Tools/AssetCook/CookDestinationLockTestAccess.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_WIN32)
#include <Windows.h>

namespace CookLockWaitTest
{
    inline constexpr char VariableName[] = "NORVES_COOK_LOCK_WAIT_MS";
    inline constexpr char TestWaitMilliseconds[] = "120000";
    inline constexpr char NamespaceVariableName[] = "NORVES_COOK_LOCK_TEST_NAMESPACE";

    namespace Detail = NorvesLib::Tools::AssetCook::Detail;

    // 親プロセスで一意な名前を作り、子が継承する環境変数と同時に設定する。
    class ScopedCookLockTestNamespace
    {
        struct Environment
        {
            char Value[Detail::MaximumCookDestinationLockNamespaceBytesForTest + 1] = "!";
            NorvesLib::Core::Container::VariableArray<char> Previous;
            bool bPresent = false;
            bool bSet = false;

            Environment()
            {
                LARGE_INTEGER counter{};
                if (!QueryPerformanceCounter(&counter))
                {
                    return;
                }
                char value[sizeof(Value)]{};
                const int length = std::snprintf(value, sizeof(value), "p%08lx-%016llx-%08lx",
                                                 static_cast<unsigned long>(GetCurrentProcessId()),
                                                 static_cast<unsigned long long>(GetTickCount64()),
                                                 static_cast<unsigned long>(
                                                     static_cast<unsigned long long>(counter.QuadPart) & 0xffffffffull));
                if (length <= 0 || static_cast<size_t>(length) >= sizeof(value))
                {
                    return;
                }

                SetLastError(ERROR_SUCCESS);
                const DWORD size = GetEnvironmentVariableA(NamespaceVariableName, nullptr, 0);
                if (!size)
                {
                    const DWORD error = GetLastError();
                    if (error != ERROR_SUCCESS && error != ERROR_ENVVAR_NOT_FOUND)
                    {
                        return;
                    }
                    bPresent = error == ERROR_SUCCESS;
                    if (bPresent)
                    {
                        Previous.push_back(0);
                    }
                }
                else
                {
                    Previous.resize(size);
                    SetLastError(ERROR_SUCCESS);
                    const DWORD previousLength = GetEnvironmentVariableA(NamespaceVariableName, Previous.data(), size);
                    if (previousLength >= size || (previousLength == 0 && GetLastError() != ERROR_SUCCESS))
                    {
                        return;
                    }
                    bPresent = true;
                }
                if (!SetEnvironmentVariableA(NamespaceVariableName, value))
                {
                    return;
                }
                bSet = true;
                std::memcpy(Value, value, static_cast<size_t>(length) + 1);
            }

            ~Environment()
            {
                if (bSet && !SetEnvironmentVariableA(NamespaceVariableName, bPresent ? Previous.data() : nullptr))
                {
                    std::fputs("環境変数を元に戻せませんでした\n", stderr);
                    std::fflush(stderr);
                    std::_Exit(EXIT_FAILURE);
                }
            }
        };

        Environment m_Environment;
        Detail::ScopedCookDestinationLockNamespaceForTest m_Namespace;

    public:
        ScopedCookLockTestNamespace() : m_Namespace(m_Environment.Value)
        {
        }

        ScopedCookLockTestNamespace(const ScopedCookLockTestNamespace&) = delete;
        ScopedCookLockTestNamespace& operator=(const ScopedCookLockTestNamespace&) = delete;

        [[nodiscard]] bool Valid() const
        {
            return m_Environment.bSet && m_Namespace.Valid();
        }
    };

    // 子プロセスでは継承した名前が不正または欠落していれば設定しない。
    class ScopedChildCookLockTestNamespace
    {
        struct Environment
        {
            char Value[Detail::MaximumCookDestinationLockNamespaceBytesForTest + 1] = "!";

            Environment()
            {
                const DWORD length =
                    GetEnvironmentVariableA(NamespaceVariableName, Value, static_cast<DWORD>(sizeof(Value)));
                if (!length || length >= sizeof(Value))
                {
                    Value[0] = '!';
                    Value[1] = 0;
                }
            }
        };

        Environment m_Environment;
        Detail::ScopedCookDestinationLockNamespaceForTest m_Namespace;

    public:
        ScopedChildCookLockTestNamespace() : m_Namespace(m_Environment.Value)
        {
        }

        ScopedChildCookLockTestNamespace(const ScopedChildCookLockTestNamespace&) = delete;
        ScopedChildCookLockTestNamespace& operator=(const ScopedChildCookLockTestNamespace&) = delete;

        [[nodiscard]] bool Valid() const
        {
            return m_Namespace.Valid();
        }
    };

    class ScopedCookLockWait
    {
    public:
        explicit ScopedCookLockWait(const char* value)
        {
            SetLastError(ERROR_SUCCESS);
            const DWORD size = GetEnvironmentVariableA(VariableName, nullptr, 0);
            if (!size)
            {
                const DWORD error = GetLastError();
                if (error != ERROR_SUCCESS && error != ERROR_ENVVAR_NOT_FOUND)
                {
                    return;
                }
                m_bPresent = error == ERROR_SUCCESS;
                if (m_bPresent)
                {
                    m_Previous.push_back(0);
                }
            }
            else
            {
                m_Previous.resize(size);
                SetLastError(ERROR_SUCCESS);
                const DWORD length = GetEnvironmentVariableA(VariableName, m_Previous.data(), size);
                if (length >= size || (length == 0 && GetLastError() != ERROR_SUCCESS))
                {
                    return;
                }
                m_bPresent = true;
            }
            m_bValid = SetEnvironmentVariableA(VariableName, value) != FALSE;
        }

        ScopedCookLockWait(const ScopedCookLockWait&) = delete;
        ScopedCookLockWait& operator=(const ScopedCookLockWait&) = delete;

        ~ScopedCookLockWait()
        {
            if (m_bValid && !SetEnvironmentVariableA(VariableName, m_bPresent ? m_Previous.data() : nullptr))
            {
                std::fputs("環境変数を元に戻せませんでした\n", stderr);
                std::fflush(stderr);
                std::_Exit(EXIT_FAILURE);
            }
        }

        bool Valid() const
        {
            return m_bValid;
        }

    private:
        NorvesLib::Core::Container::VariableArray<char> m_Previous;
        bool m_bPresent = false;
        bool m_bValid = false;
    };

    inline ScopedCookLockWait WaitForCookLock()
    {
        return ScopedCookLockWait(TestWaitMilliseconds);
    }

    inline ScopedCookLockWait DoNotWaitForCookLock()
    {
        return ScopedCookLockWait(nullptr);
    }
} // namespace CookLockWaitTest: 試験用の待ち設定
#endif
