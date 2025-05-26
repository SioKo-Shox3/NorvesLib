#include "Core/Public/Debug/Logger.h"
#include "Core/Public/Debug/DebugOutput.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace NorvesLib::Core::Debug;
using namespace NorvesLib::Core::Container;

/**
 * @brief デバッグ出力機能のデモンストレーション
 */
void DemonstrateBasicLogging()
{
    std::cout << "=== 基本的なログ出力のデモ ===\n" << std::endl;

    // 各ログレベルでのメッセージ出力
    NORVES_LOG_DEBUG("これはデバッグメッセージです");
    NORVES_LOG_INFO("アプリケーションが正常に開始されました");
    NORVES_LOG_WARNING("設定ファイルが見つかりません。デフォルト値を使用します");
    NORVES_LOG_ERROR("リソースの読み込みに失敗しました");
    NORVES_LOG_CRITICAL("致命的なエラーが発生しました");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

/**
 * @brief ファイル出力のデモンストレーション
 */
void DemonstrateFileLogging()
{
    std::cout << "\n=== ファイル出力のデモ ===\n" << std::endl;

    auto& logger = Logger::GetInstance();
    
    // ファイル出力を有効化
    logger.EnableFileOutput("debug_log.txt");
    
    NORVES_LOG_INFO("ファイル出力が有効になりました");
    NORVES_LOG_DEBUG("この情報はファイルにも記録されます");
    
    // ゲームシミュレーションのログ
    for (int i = 0; i < 5; ++i)
    {
        String message = "フレーム " + std::to_string(i + 1) + " を処理中...";
        NORVES_LOG_INFO(message);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    logger.Flush();
    std::cout << "ログがファイル 'debug_log.txt' に保存されました\n" << std::endl;
}

/**
 * @brief 非同期ログ出力のデモンストレーション
 */
void DemonstrateAsyncLogging()
{
    std::cout << "\n=== 非同期ログ出力のデモ ===\n" << std::endl;

    auto& logger = Logger::GetInstance();
    
    // 非同期出力を開始
    logger.StartAsyncOutput();
    NORVES_LOG_INFO("非同期ログ出力が開始されました");
    
    // 高頻度でログを出力（パフォーマンステスト）
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 100; ++i)
    {
        String message = "高頻度ログ #" + std::to_string(i);
        NORVES_LOG_DEBUG(message);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // 非同期出力を停止
    logger.StopAsyncOutput();
    
    String perfMessage = "100件のログ出力にかかった時間: " + std::to_string(duration.count()) + " マイクロ秒";
    NORVES_LOG_INFO(perfMessage);
}

/**
 * @brief カスタムログ出力のデモンストレーション
 */
void DemonstrateCustomOutput()
{
    std::cout << "\n=== カスタムデバッグ出力のデモ ===\n" << std::endl;

    // デバッグ出力マクロの使用
    NORVES_DEBUG_OUTPUT("カスタムデバッグ出力のテスト");
    
    // 条件付きデバッグ出力
    bool errorCondition = true;
    NORVES_DEBUG_OUTPUT_IF(errorCondition, "エラー条件が検出されました");
    
    // 変数値の出力
    int playerScore = 12500;
    float playerHealth = 85.5f;
    NORVES_DEBUG_VAR(playerScore);
    NORVES_DEBUG_VAR(playerHealth);
    
    // 実行時間の測定
    {
        NORVES_DEBUG_TIMER("重い処理");
        
        // 処理をシミュレート
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // スコープを抜ける際に自動的に実行時間が出力される
    }
}

/**
 * @brief ログレベルフィルタリングのデモンストレーション
 */
void DemonstrateLogLevelFiltering()
{
    std::cout << "\n=== ログレベルフィルタリングのデモ ===\n" << std::endl;

    auto& logger = Logger::GetInstance();
    
    // 警告レベル以上のみ出力
    logger.SetLogLevel(LogLevel::Warning);
    std::cout << "ログレベルを Warning に設定しました\n" << std::endl;
    
    NORVES_LOG_DEBUG("このデバッグメッセージは表示されません");
    NORVES_LOG_INFO("この情報メッセージも表示されません");
    NORVES_LOG_WARNING("この警告メッセージは表示されます");
    NORVES_LOG_ERROR("このエラーメッセージも表示されます");
    
    // レベルをリセット
    logger.SetLogLevel(LogLevel::Debug);
    std::cout << "\nログレベルを Debug にリセットしました" << std::endl;
}

/**
 * @brief マルチスレッドログ出力のデモンストレーション
 */
void DemonstrateMultithreadedLogging()
{
    std::cout << "\n=== マルチスレッドログ出力のデモ ===\n" << std::endl;

    auto& logger = Logger::GetInstance();
    logger.StartAsyncOutput();
    
    // 複数のスレッドからログ出力
    VariableArray<std::thread> threads;
    
    for (int i = 0; i < 3; ++i)
    {
        threads.emplace_back([i]()
        {
            for (int j = 0; j < 5; ++j)
            {
                String message = "スレッド " + std::to_string(i) + " からのメッセージ " + std::to_string(j);
                NORVES_LOG_INFO(message);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    
    // 全スレッドの完了を待機
    for (auto& thread : threads)
    {
        thread.join();
    }
    
    logger.StopAsyncOutput();
    std::cout << "マルチスレッドログ出力が完了しました\n" << std::endl;
}

int main()
{
    std::cout << "NorvesLib デバッグ出力システム - デモンストレーション\n" << std::endl;
    
    try
    {
        // 各デモを実行
        DemonstrateBasicLogging();
        DemonstrateFileLogging();
        DemonstrateAsyncLogging();
        DemonstrateCustomOutput();
        DemonstrateLogLevelFiltering();
        DemonstrateMultithreadedLogging();
        
        std::cout << "=== デモンストレーション完了 ===\n" << std::endl;
        std::cout << "生成されたファイル: debug_log.txt" << std::endl;
    }
    catch (const std::exception& e)
    {
        NORVES_LOG_CRITICAL(String("例外が発生しました: ") + e.what());
        return 1;
    }
    
    return 0;
}