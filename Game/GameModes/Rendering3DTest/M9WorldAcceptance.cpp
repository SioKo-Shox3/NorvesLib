#include "GameModes/Rendering3DTest/M9WorldAcceptance.h"

#include "Core/Public/Debug/DebugConfig.h"
#include "Core/Public/Logging/LogMacros.h"

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

    void ObserveM9WorldCompareCapture(M9WorldAcceptanceConfig& config)
    {
#if NORVES_BUILD_DEVELOPMENT
        if (!config.bCompare || config.bCompareCaptured || config.ComparePngPath.empty())
        {
            return;
        }
        // ApplicationProcessor は PNG、全 PFM、最後に AOV の JSON を同期で書いてから OnPostRender を呼ぶ。
        // 新規の撮影先だけを受け付け、書き終わった印を見て通知する。撮影と終了は同じ処理が所有する。
        const auto dot = config.ComparePngPath.FindLast(TEXT('.'));
        if (dot == NorvesLib::Core::Container::String::npos)
        {
            return;
        }
        const auto base = config.ComparePngPath.substr(0, dot);
        const auto metadata = base + NorvesLib::Core::Container::String(TEXT(".aov.json"));
        if (GetFileAttributes(config.ComparePngPath.c_str()) != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributes(metadata.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            config.bCompareCaptured = true;
            LOG_INFO("M9_WORLD_COMPARE stage=captured pose_time=0 faces=9");
        }
#else
        (void)config;
#endif
    }
} // namespace Game::GameModes
