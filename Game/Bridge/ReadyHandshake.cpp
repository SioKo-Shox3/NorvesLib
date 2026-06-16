#include "ReadyHandshake.h"

// Bridge ソースは SDK の有無に関わらず GLOB で収集されるため、SDK 非設定ビルドでも
// コンパイルされる。このヘルパは SDK ヘッダに依存しないが、一貫性と「SDK 未設定時は
// Bridge を完全に不活性化する」方針のため、本体を NORVES_BRIDGE_ENABLED でガードする。
#if defined(NORVES_BRIDGE_ENABLED)

#include <cstdio>

#include <Windows.h>

namespace Game::Bridge
{

    bool WriteReadyLine(uint16_t port)
    {
        // "READY <port>\n" を ASCII で組み立てる。最大長は "READY 65535\n" + 終端で十分。
        char line[32] = {};
        const int written = std::snprintf(line, sizeof(line), "READY %u\n", static_cast<unsigned>(port));
        if (written <= 0 || written >= static_cast<int>(sizeof(line)))
        {
            return false;
        }

        // CRT が stdout を差し替えていても継承パイプへ届くよう、生ハンドルへ直接書く。
        const HANDLE stdoutHandle = ::GetStdHandle(STD_OUTPUT_HANDLE);
        if (stdoutHandle == INVALID_HANDLE_VALUE || stdoutHandle == nullptr)
        {
            return false;
        }

        DWORD totalWritten = 0;
        const DWORD totalToWrite = static_cast<DWORD>(written);
        while (totalWritten < totalToWrite)
        {
            DWORD chunkWritten = 0;
            if (!::WriteFile(stdoutHandle,
                             line + totalWritten,
                             totalToWrite - totalWritten,
                             &chunkWritten,
                             nullptr))
            {
                return false;
            }
            if (chunkWritten == 0)
            {
                return false;
            }
            totalWritten += chunkWritten;
        }

        // パイプの場合 FlushFileBuffers は失敗し得る（ERROR_INVALID_FUNCTION）が、
        // パイプ書き込みは即座に相手へ渡るため、戻り値は無視してよい。
        ::FlushFileBuffers(stdoutHandle);
        return true;
    }

} // namespace Game::Bridge

#endif // NORVES_BRIDGE_ENABLED
