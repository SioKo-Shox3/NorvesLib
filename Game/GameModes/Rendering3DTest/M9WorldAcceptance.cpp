#include "GameModes/Rendering3DTest/M9WorldAcceptance.h"

#include "Core/Public/Debug/DebugConfig.h"

#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Game::GameModes
{
    void EmitM9WorldSmokeMarker(const char* format, ...)
    {
#if !NORVES_ENABLE_LOGGING
        char message[512]{};
        va_list arguments;
        va_start(arguments, format);
        const int messageLength = vsnprintf_s(message, sizeof(message), _TRUNCATE, format, arguments);
        va_end(arguments);
        if (messageLength <= 0)
        {
            return;
        }

        HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        if (output == nullptr || output == INVALID_HANDLE_VALUE)
        {
            return;
        }
        DWORD written = 0;
        (void)WriteFile(output, message, static_cast<DWORD>(std::strlen(message)), &written, nullptr);
        (void)WriteFile(output, "\r\n", 2, &written, nullptr);
#else
        (void)format;
#endif
    }
} // namespace Game::GameModes
