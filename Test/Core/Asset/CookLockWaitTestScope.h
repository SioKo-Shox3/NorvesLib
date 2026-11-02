// cook のロック待ち環境変数を試験の範囲内だけ変更し、元の状態へ戻す。
#pragma once

#include "Container/VariableArray.h"
#include <cstdio>
#include <cstdlib>
#if defined(_WIN32)
#include <Windows.h>

namespace CookLockWaitTest
{
    inline constexpr char VariableName[] = "NORVES_COOK_LOCK_WAIT_MS";
    inline constexpr char TestWaitMilliseconds[] = "120000";

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
