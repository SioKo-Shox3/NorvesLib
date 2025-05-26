#include "CoreModule.h"
#include "Logging/Logger.h"
#include "Debug/DebugOutput.h"

namespace NorvesLib::Core
{

bool Initialize()
{
    // ログシステムの初期化
    auto& logger = Logging::Logger::GetInstance();
    logger.SetMinimumLogLevel(Logging::LogLevel::Info);
    
    // コンソール出力とファイル出力を有効化
    logger.SetConsoleEnabled(true);
    logger.SetFileLoggingEnabled(true, "NorvesLib.log");
    
    // デバッグ出力システムの初期化
    Debug::DebugOutput::Initialize();
    
    // 初期化完了のログ出力
    NORVES_LOG_INFO("Core module initialized successfully");
    
    return true;
}

void Shutdown()
{
    NORVES_LOG_INFO("Core module shutting down");
    
    // デバッグ出力システムの終了処理
    Debug::DebugOutput::Shutdown();
    
    // ログシステムの終了処理（最後に行う）
    auto& logger = Logging::Logger::GetInstance();
    logger.Flush();
}

} // namespace NorvesLib::Core