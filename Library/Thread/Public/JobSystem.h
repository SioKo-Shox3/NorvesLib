#pragma once

#include "Task.h"
#include "WorkStealingQueue.h"
#include "ConditionVariable.h"
#include "Mutex.h"
#include "Atomic.h"
#include <atomic>
#include <memory>
#include <queue>
#include <random>
#include "Core/Public/Container/Containers.h"

namespace NorvesLib::Thread
{

class Thread;

/**
 * @brief ジョブシステム
 * 
 * スレッドプールと非同期タスク実行を提供するジョブシステム
 */
class JobSystem
{
public:
    /**
     * @brief ジョブシステムの実行モード
     */
    enum class ExecutionMode
    {
        SIMPLE,           ///< 単純なグローバルキュー方式
        WORK_STEALING     ///< ワークスチーリング方式
    };

    /**
     * @brief スレッドプールのサイジングモード
     */
    enum class SizingMode
    {
        FIXED,            ///< 固定サイズ
        DYNAMIC           ///< 動的サイズ調整
    };

    /**
     * @brief シングルトンインスタンスを取得
     * @return JobSystemのインスタンス
     */
    static JobSystem& Get();

    /**
     * @brief ジョブシステムを初期化する
     * @param threadCount スレッド数（0の場合はハードウェアスレッド数を使用）
     * @param mode 実行モード（デフォルトはワークスチーリング）
     */
    void Initialize(uint32_t threadCount = 0, ExecutionMode mode = ExecutionMode::WORK_STEALING);

    /**
     * @brief ジョブシステムをシャットダウンする
     */
    void Shutdown();

    /**
     * @brief タスクをキューに追加する
     * @param task 実行するタスク
     */
    void SubmitTask(TaskPtr task);

    /**
     * @brief 複数のタスクをバッチ実行する
     * @param tasks 実行するタスクの配列
     * @param priority バッチタスクの優先度（デフォルトは通常優先度）
     * @return バッチ実行を表すタスク
     */
    TaskPtr SubmitTasks(const Core::Container::VariableArray<TaskPtr>& tasks, 
                        TaskPriority priority = TaskPriority::NORMAL);

    /**
     * @brief すべてのタスクが完了するのを待機する
     */
    void WaitForAll();

    /**
     * @brief タスクキューに存在するタスク数を取得
     * @return 現在のタスク数
     */
    size_t GetQueuedTaskCount() const;

    /**
     * @brief アクティブなワーカースレッド数を取得
     * @return アクティブなスレッド数
     */
    size_t GetActiveThreadCount() const;

    /**
     * @brief 総ワーカースレッド数を取得
     * @return ワーカースレッド数
     */
    size_t GetWorkerThreadCount() const;

    /**
     * @brief ジョブシステムにタスクワーカーを追加する
     * @param thread ワーカースレッドポインタ
     * @note このメソッドはテスト用途やシステムの拡張時に使用します
     */
    void AddWorkerThread(std::unique_ptr<Thread> thread);

    /**
     * @brief 各CPUコアに対してワーカースレッドのアフィニティを設定する
     * @param enableAffinityMasks 有効にするかどうか
     */
    void SetWorkerThreadsAffinity(bool enableAffinityMasks);

    /**
     * @brief ワーカースレッドの統計情報を取得する
     * @return 処理されたタスク数の配列（各スレッドごと）
     */
    Core::Container::VariableArray<size_t> GetWorkerThreadsStats() const;

    /**
     * @brief 現在の実行モードを取得する
     * @return 現在の実行モード
     */
    ExecutionMode GetExecutionMode() const;

    /**
     * @brief 実行モードを設定する（再初期化が必要）
     * @param mode 新しい実行モード
     * @note この設定は次回のInitialize()呼び出し時に反映されます
     */
    void SetExecutionMode(ExecutionMode mode);

    /**
     * @brief スチールされたタスクの総数を取得
     * @return スチールされたタスク数
     */
    size_t GetTotalStolenTasks() const;
    
    /**
     * @brief 動的サイジングモードを有効/無効にする
     * @param enabled 有効にするかどうか
     * @param minThreads 最小スレッド数（0の場合はハードウェアスレッド数の1/4）
     * @param maxThreads 最大スレッド数（0の場合はハードウェアスレッド数）
     */
    void EnableDynamicSizing(bool enabled, uint32_t minThreads = 0, uint32_t maxThreads = 0);
    
    /**
     * @brief 現在のサイジングモードを取得する
     * @return サイジングモード
     */
    SizingMode GetSizingMode() const;
    
    /**
     * @brief ワーカースレッドの数を調整する
     * @param targetThreadCount 目標スレッド数
     * @return 実際に調整された後のスレッド数
     */
    size_t AdjustWorkerThreadCount(size_t targetThreadCount);

private:
    JobSystem();
    ~JobSystem();

    // シングルトンインスタンス用のポインタ
    static JobSystem* s_instance;

    // ワーカースレッドの実行関数
    void WorkerThreadFunction(size_t threadIndex);
    
    void MonitorThreadFunction();

    // 次のタスクを取得（シンプルモード用）
    TaskPtr GetNextTaskSimple();

    // 次のタスクを取得（ワークスチーリング用）
    TaskPtr GetNextTaskWorkStealing(size_t threadIndex);

    // ランダムなスレッドからタスクを盗む
    TaskPtr StealTask(size_t currentThreadIndex);
    
    // 現在の負荷に基づいて適切なスレッド数を計算
    size_t CalculateOptimalThreadCount();
    
    // 新しいスレッドを作成
    void CreateAndStartWorkerThread();

    // ワーカースレッドのプール
    Core::Container::VariableArray<std::unique_ptr<Thread>> m_workerThreads;
    
    // モニタースレッド（動的サイジング用）
    std::unique_ptr<Thread> m_monitorThread;

    // タスクキュー（優先度付きキュー）- シンプルモード用
    using PriorityTaskQueue = std::priority_queue<TaskPtr, Core::Container::VariableArray<TaskPtr>, TaskPriorityCompare>;
    PriorityTaskQueue m_taskQueue;

    // ワーカースレッド用のローカルキュー - ワークスチーリングモード用
    Core::Container::VariableArray<std::unique_ptr<WorkStealingQueue>> m_localQueues;

    // 実行モード
    Atomic<ExecutionMode> m_executionMode;
    
    // サイジングモード
    Atomic<SizingMode> m_sizingMode;
    
    // 最小/最大スレッド数
    Atomic<uint32_t> m_minThreads;
    Atomic<uint32_t> m_maxThreads;

    // スチールされたタスク数
    Atomic<size_t> m_totalStolenTasks;
    
    // 最後のスレッド調整時刻
    std::chrono::steady_clock::time_point m_lastThreadAdjustment;
    
    // スレッド調整間隔（秒）
    static constexpr float THREAD_ADJUSTMENT_INTERVAL = 2.0f;
    
    // ワーカーの稼働率の目標値
    static constexpr float TARGET_WORKER_UTILIZATION = 0.7f;

    // ランダムデバイス（タスクスチーリング用）
    std::mt19937 m_randomGenerator;
    
    // 同期用オブジェクト
    mutable Mutex m_queueMutex;
    mutable Mutex m_workerThreadMutex;
    ConditionVariable m_conditionVar;
    Atomic<bool> m_shutdownRequested;
    Atomic<bool> m_monitorActive;
    
    // スレッド状態追跡
    Atomic<size_t> m_activeThreads;
    
    // キュー内のタスク追跡
    Atomic<size_t> m_queuedTaskCount;
    
    // プロセッサ使用率の履歴
    static constexpr size_t UTILIZATION_HISTORY_SIZE = 10;
    Core::Container::VariableArray<float> m_utilizationHistory;

    // ワーカースレッド統計情報
    mutable Mutex m_statsMutex;
    Core::Container::VariableArray<Atomic<size_t>> m_tasksProcessedPerThread;
};

} // namespace NorvesLib::Thread
