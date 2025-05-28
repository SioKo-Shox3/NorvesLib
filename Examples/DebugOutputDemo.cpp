#include "Core/Public/Logging/LoggingModule.h"
#include "Core/Public/Debug/DebugOutput.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace NorvesLib::Core::Logging;
using namespace NorvesLib::Core::Container;

/**
 * @brief デバッグ出力機能のデモンストレーション
 */
void DemonstrateBasicLogging()
{
    std::cout << "=== 基本的なログ出力のデモ ===\n"
              << std::endl; // 各ログレベルでのメッセージ出力
    LOG_DEBUG("これはデバッグメッセージです");
    LOG_INFO("アプリケーションが正常に開始されました");
    LOG_WARNING("設定ファイルが見つかりません。デフォルト値を使用します");
    LOG_ERROR("リソースの読み込みに失敗しました");
    LOG_FATAL("致命的なエラーが発生しました");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

/**
 * @brief ファイル出力のデモンストレーション
 */
void DemonstrateFileLogging()
{
    std::cout << "\n=== ファイル出力のデモ ===\n"
              << std::endl; // ログシステムを初期化
    InitializeLogging(CreateLogConfig(LogLevel::Debug, LogOutput::Both, "debug_log.txt"));

    LOG_INFO("ファイル出力が有効になりました");
    LOG_DEBUG("この情報はファイルにも記録されます"); // ゲームシミュレーションのログ
    for (int i = 0; i < 5; ++i)
    {
        LOG_INFO_F("フレーム %d を処理中...", i + 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    Logger::GetInstance().Flush();
    std::cout << "ログがファイル 'debug_log.txt' に保存されました\n"
              << std::endl;
}

/**
 * @brief 非同期ログ出力のデモンストレーション
 */
void DemonstrateAsyncLogging()
{
    std::cout << "\n=== 非同期ログ出力のデモ ===\n"
              << std::endl; // 新しいLoggingシステムは常に非同期で動作するため、手動制御は不要
    LOG_INFO("高性能な非同期ログ出力を開始します");

    // 高頻度でログを出力（パフォーマンステスト）
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 100; ++i)
    {
        LOG_DEBUG_F("高頻度ログ #%d", i);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    LOG_INFO_F("100件のログ出力にかかった時間: %lld マイクロ秒", duration.count());
}

/**
 * @brief カスタムログ出力のデモンストレーション
 */
void DemonstrateCustomOutput()
{
    std::cout << "\n=== カスタムデバッグ出力のデモ ===\n"
              << std::endl; // デバッグ出力マクロの使用（DebugOutputクラスを使用）
    DEBUG_PRINT_VAR("カスタムデバッグ出力のテスト");

    // 条件付きデバッグ出力
    bool errorCondition = true;
    LOG_ERROR_IF(errorCondition, "エラー条件が検出されました");

    // 変数値の出力
    int playerScore = 12500;
    float playerHealth = 85.5f;
    DEBUG_PRINT_VAR(playerScore);
    DEBUG_PRINT_VAR(playerHealth);

    // 実行時間の測定
    {
        NORVES_LOG_PERFORMANCE_START(heavy_process);

        // 処理をシミュレート
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        NORVES_LOG_PERFORMANCE_END(heavy_process, "Performance");
    }
}

/**
 * @brief ログレベルフィルタリングのデモンストレーション
 */
void DemonstrateLogLevelFiltering()
{
    std::cout << "\n=== ログレベルフィルタリングのデモ ===\n"
              << std::endl; // 警告レベル以上のみ出力
    SetLogLevel(LogLevel::Warning);
    std::cout << "ログレベルを Warning に設定しました\n"
              << std::endl;

    LOG_DEBUG("このデバッグメッセージは表示されません");
    LOG_INFO("この情報メッセージも表示されません");
    LOG_WARNING("この警告メッセージは表示されます");
    LOG_ERROR("このエラーメッセージも表示されます");

    // レベルをリセット
    SetLogLevel(LogLevel::Debug);
    std::cout << "\nログレベルを Debug にリセットしました" << std::endl;
}

/**
 * @brief マルチスレッドログ出力のデモンストレーション
 */
void DemonstrateMultithreadedLogging()
{
    std::cout << "\n=== マルチスレッドログ出力のデモ ===\n"
              << std::endl; // 新しいLoggingシステムは標準で非同期・スレッドセーフ

    // 複数のスレッドからログ出力
    VariableArray<std::thread> threads;

    for (int i = 0; i < 3; ++i)
    {
        threads.emplace_back([i]()
                             {
            for (int j = 0; j < 5; ++j)
            {
                LOG_INFO_F("スレッド %d からのメッセージ %d", i, j);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } });
    } // 全スレッドの完了を待機
    for (auto &thread : threads)
    {
        thread.join();
    }

    std::cout << "マルチスレッドログ出力が完了しました\n"
              << std::endl;
}

int main()
{
    std::cout << "NorvesLib デバッグ出力システム - デモンストレーション\n"
              << std::endl;

    try
    {
        // 各デモを実行
        DemonstrateBasicLogging();
        DemonstrateFileLogging();
        DemonstrateAsyncLogging();
        DemonstrateCustomOutput();
        DemonstrateLogLevelFiltering();
        DemonstrateMultithreadedLogging();

        std::cout << "=== デモンストレーション完了 ===\n"
                  << std::endl;
        std::cout << "生成されたファイル: debug_log.txt" << std::endl;

        // ログシステム終了
        ShutdownLogging();
    }
    catch (const std::exception &e)
    {
        LOG_FATAL(String("例外が発生しました: ") + e.what());
        return 1;
    }

    return 0;
}