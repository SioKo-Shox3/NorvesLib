#include "CoreModule.h"
#include "Logging/LoggingModule.h"

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core
{

    bool Initialize()
    { // ログシステムの初期化
        Logging::LogConfig config = Logging::CreateLogConfig(
            Logging::LogLevel::Trace,
            Logging::LogOutput::Both,
            String("NorvesLib.log"));

        if (!Logging::InitializeLogging(config))
        {
            return false;
        }

        // 初期化完了のログ出力
        LOG_INFO("Core module initialized successfully");

        return true;
    }

    void Shutdown()
    {
        LOG_INFO("Core module shutting down");

        // ログシステムの終了処理
        Logging::ShutdownLogging();
    }

} // namespace NorvesLib::Core
