#include "Platform/PlatformConsole.h"

#include "Debug/DebugConfig.h"

#if NORVES_ENABLE_DEBUG_OUTPUT
#include <Windows.h>
#include <cstdio>
#include <ios>
#endif

namespace NorvesLib::Core::Platform
{

    bool OpenDebugConsole()
    {
#if NORVES_ENABLE_DEBUG_OUTPUT
        // 親プロセスのコンソールにアタッチを試み、失敗時は新規割り当て
        if (!AttachConsole(ATTACH_PARENT_PROCESS))
        {
            if (!AllocConsole())
            {
                return false;
            }
        }

        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$",  "r", stdin);
        std::ios::sync_with_stdio(true);
        SetConsoleTitleA("NorvesLib Debug Console");
        return true;
#else
        return false;
#endif
    }

    void CloseDebugConsole()
    {
#if NORVES_ENABLE_DEBUG_OUTPUT
        FreeConsole();
#endif
    }

} // namespace NorvesLib::Core::Platform
