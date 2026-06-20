#include "Thread/JobSystem.h"
#include "Thread/Thread.h"
#include <cassert>
#include <algorithm>
#include <random>
#include <chrono>
#include <thread>

namespace NorvesLib::Thread
{

    // シングルトンインスタンスの初期化
    JobSystem *JobSystem::s_instance = nullptr;

    JobSystem &JobSystem::Get()
    {
        if (!s_instance)
        {
            s_instance = new JobSystem();
        }
        return *s_instance;
    }

    JobSystem::JobSystem()
        : m_executionMode(ExecutionMode::EXECUTION_WORK_STEALING),
          m_sizingMode(SizingMode::SIZING_FIXED),
          m_minThreads(0),
          m_maxThreads(0),
          m_totalStolenTasks(0),
          m_shutdownRequested(false),
          m_monitorActive(false),
          m_workerThreadLimit(0),
          m_activeThreads(0),
          m_queuedTaskCount(0),
          m_lastThreadAdjustment(std::chrono::steady_clock::now()),
          m_randomGenerator(std::random_device()())
    {
        m_utilizationHistory.resize(UTILIZATION_HISTORY_SIZE, 0.0f);
    }

    JobSystem::~JobSystem()
    {
        Shutdown();
    }

    void JobSystem::Initialize(uint32_t threadCount, ExecutionMode mode)
    {
        ScopedLock shutdownLock(m_shutdownMutex);
        ScopedLock resizeLock(m_resizeMutex);

        // シャットダウン状態をリセット
        m_shutdownRequested = false;

        // 指定がなければハードウェアの最適なスレッド数を取得
        if (threadCount == 0)
        {
            // メインスレッドを除いたスレッド数を使用（最低1）
            unsigned int concurrentThreads = Thread::GetHardwareConcurrency();
            threadCount = std::max(1u, concurrentThreads > 1 ? concurrentThreads - 1 : concurrentThreads);
        }

        // 実行モードを保存
        m_executionMode = mode;
        m_workerThreadLimit = threadCount;

        // Initializeごとにワーカーと同じ数だけ統計情報とローカルキューを作り直す
        {
            ScopedLock statsLock(m_statsMutex);
            m_tasksProcessedPerThread.clear();
        }

        {
            ScopedLock workerLock(m_workerThreadMutex);
            m_localQueues.clear();
        }

        // スチールカウンターをリセット
        m_totalStolenTasks = 0;

        // 使用率履歴をクリア
        for (auto &util : m_utilizationHistory)
        {
            util = 0.0f;
        }

        // ワーカースレッドを生成
        for (uint32_t i = 0; i < threadCount; ++i)
        {
            CreateAndStartWorkerThread();
        }

        // 動的サイジングがアクティブであればモニタースレッドを起動
        if (m_sizingMode == SizingMode::SIZING_DYNAMIC)
        {
            m_monitorActive = true;
            m_monitorThread = std::make_unique<Thread>([this]
                                                       { MonitorThreadFunction(); });
            m_monitorThread->SetName("JobSystem Monitor");
        }
    }

    void JobSystem::Shutdown()
    {
        ScopedLock shutdownLock(m_shutdownMutex);

        {
            ScopedLock workerLock(m_workerThreadMutex);
            ScopedLock queueLock(m_queueMutex);
            if (m_shutdownRequested)
            {
                return; // 既にシャットダウン処理中
            }

            // シャットダウン要求を設定
            m_shutdownRequested = true;
            m_workerThreadLimit = 0;
        }

        m_monitorActive = false;

        // モニタースレッドが終了するのを待つ
        if (m_monitorThread)
        {
            m_monitorThread.reset();
        }

        ScopedLock resizeLock(m_resizeMutex);
        m_workerThreadLimit = 0;

        // 全ワーカースレッドに作業終了を通知
        m_conditionVar.NotifyAll();

        // 全てのワーカースレッドが終了するのを待つ。
        // Threadの破棄はjoinを行うため、m_workerThreadMutexを解放してから実行する。
        Core::Container::VariableArray<std::unique_ptr<Thread>> workerThreadsToJoin;
        {
            ScopedLock lock(m_workerThreadMutex);
            workerThreadsToJoin.swap(m_workerThreads);
        }
        workerThreadsToJoin.clear();

        // 残りのタスクをクリア
        {
            ScopedLock lock(m_queueMutex);
            while (!m_taskQueue.empty())
            {
                m_taskQueue.pop();
            }
            m_queuedTaskCount = 0;
        }

        // ローカルキューをクリア
        {
            ScopedLock lock(m_workerThreadMutex);
            m_localQueues.clear();
        }
    }

    void JobSystem::SubmitTask(TaskPtr task)
    {
        if (!task)
        {
            return; // nullタスクは追加しない
        }

        const ExecutionMode mode = m_executionMode.Load();

        if (mode == ExecutionMode::EXECUTION_SIMPLE)
        {
            // シンプルモード - グローバルキューに追加
            {
                ScopedLock lock(m_queueMutex);

                // シャットダウン中なら追加しない
                if (m_shutdownRequested)
                {
                    return;
                }

                // キューへタスクを追加（優先度付きキュー）
                m_taskQueue.push(task);
                m_queuedTaskCount++;
            }
        }
        else
        {
            // ワークスチーリングモード - ランダムなワーカーのローカルキューに追加
            ScopedLock lock(m_workerThreadMutex);

            if (m_shutdownRequested)
            {
                return;
            }

            if (m_localQueues.empty())
            {
                // ローカルキューが無い場合はグローバルキューに追加
                ScopedLock queueLock(m_queueMutex);
                m_taskQueue.push(task);
                m_queuedTaskCount++;
            }
            else
            {
                // ランダムなローカルキューに追加
                std::uniform_int_distribution<size_t> dist(0, m_localQueues.size() - 1);
                size_t queueIndex = dist(m_randomGenerator);

                if (queueIndex < m_localQueues.size() && m_localQueues[queueIndex])
                {
                    m_localQueues[queueIndex]->Push(task);
                    m_queuedTaskCount++;
                }
                else
                {
                    // フォールバック：グローバルキューに追加
                    ScopedLock queueLock(m_queueMutex);
                    m_taskQueue.push(task);
                    m_queuedTaskCount++;
                }
            }
        }

        // ワーカースレッドに通知
        m_conditionVar.NotifyOne();
    }

    TaskPtr JobSystem::SubmitTasks(const Core::Container::VariableArray<TaskPtr> &tasks, TaskPriority priority)
    {
        // 全てのタスクを一括で登録するバッチタスクを作成
        TaskPtr batchTask = Task::CreateBatch(tasks, priority);

        // 各タスクをキューに追加
        for (const auto &task : tasks)
        {
            if (task)
            {
                SubmitTask(task);
            }
        }

        // バッチタスク自体もキューに追加
        SubmitTask(batchTask);

        return batchTask;
    }

    void JobSystem::WaitForAll()
    {
        while (true)
        {
            // キューが空になり、アクティブなスレッドがなくなるまで待機
            bool allDone = false;

            if (m_executionMode.Load() == ExecutionMode::EXECUTION_SIMPLE)
            {
                // シンプルモード - グローバルキューのみチェック
                ScopedLock lock(m_queueMutex);
                allDone = m_taskQueue.empty() && m_activeThreads == 0 && m_queuedTaskCount == 0;
            }
            else
            {
                // ワークスチーリングモード - 全ローカルキューを確認
                bool anyQueueHasTasks = false;

                {
                    ScopedLock lock(m_queueMutex);
                    anyQueueHasTasks = !m_taskQueue.empty();
                }

                if (!anyQueueHasTasks)
                {
                    ScopedLock workerLock(m_workerThreadMutex);
                    for (const auto &queue : m_localQueues)
                    {
                        if (queue && !queue->IsEmpty())
                        {
                            anyQueueHasTasks = true;
                            break;
                        }
                    }
                }

                allDone = !anyQueueHasTasks && m_activeThreads == 0 && m_queuedTaskCount == 0;
            }

            if (allDone)
            {
                break;
            }

            // 自身でタスクを処理して待機状態を改善
            TaskPtr task = nullptr;
            if (m_executionMode.Load() == ExecutionMode::EXECUTION_SIMPLE)
            {
                task = GetNextTaskSimple();
            }
            else
            {
                task = GetNextTaskWorkStealing(0); // メインスレッドは0番として扱う
            }

            if (task)
            {
                task->Execute();
                MarkQueuedTaskCompleted();
            }
            else
            {
                // 少し待ってCPUを解放
                std::this_thread::yield();
            }
        }
    }

    size_t JobSystem::GetQueuedTaskCount() const
    {
        return m_queuedTaskCount.Load();
    }

    size_t JobSystem::GetActiveThreadCount() const
    {
        return m_activeThreads.Load();
    }

    size_t JobSystem::GetWorkerThreadCount() const
    {
        ScopedLock lock(m_workerThreadMutex);
        return m_workerThreads.size();
    }

    void JobSystem::AddWorkerThread(std::unique_ptr<Thread> thread)
    {
        ScopedLock resizeLock(m_resizeMutex);

        if (!thread || m_shutdownRequested)
        {
            return;
        }

        // 統計カウンター配列とローカルキューを拡張
        size_t newIndex = 0;
        {
            ScopedLock statsLock(m_statsMutex);
            newIndex = m_tasksProcessedPerThread.size();
            m_tasksProcessedPerThread.push_back(std::make_unique<Atomic<size_t>>(0));
        }

        if (m_workerThreadLimit.Load() < newIndex + 1)
        {
            m_workerThreadLimit = newIndex + 1;
        }

        // ワークスチーリングモードの場合はローカルキューも追加
        if (m_executionMode.Load() == ExecutionMode::EXECUTION_WORK_STEALING)
        {
            ScopedLock workerLock(m_workerThreadMutex);
            if (newIndex >= m_localQueues.size())
            {
                m_localQueues.resize(newIndex + 1);
            }
            m_localQueues[newIndex] = std::make_unique<WorkStealingQueue>();
        }

        // スレッドの実行関数を設定し開始
        thread->Start([this, newIndex]
                      { WorkerThreadFunction(newIndex); });

        // ワーカーリストに追加
        {
            ScopedLock workerLock(m_workerThreadMutex);
            m_workerThreads.push_back(std::move(thread));
        }
    }

    void JobSystem::SetWorkerThreadsAffinity(bool enableAffinityMasks)
    {
        if (!enableAffinityMasks)
        {
            return;
        }

        ScopedLock lock(m_workerThreadMutex);
        const size_t threadCount = m_workerThreads.size();
        if (threadCount == 0)
        {
            return;
        }

        // 各スレッドにCPUコアを割り当て
        unsigned int coreCount = Thread::GetHardwareConcurrency();
        for (size_t i = 0; i < threadCount; ++i)
        {
            // CPUアフィニティを1つのコアに設定
            // メインスレッドを考慮して、ワーカーには1番目以降のコアを使用
            size_t coreIndex = (i + 1) % coreCount;
            AffinityMask mask = 1ULL << coreIndex;

            m_workerThreads[i]->SetAffinity(mask);
        }
    }

    Core::Container::VariableArray<size_t> JobSystem::GetWorkerThreadsStats() const
    {
        ScopedLock lock(m_statsMutex);

        // アトミック型の値を通常の配列にコピー
        Core::Container::VariableArray<size_t> stats;
        stats.reserve(m_tasksProcessedPerThread.size());

        for (const auto &counter : m_tasksProcessedPerThread)
        {
            // unique_ptr経由でAtomicオブジェクトの値を取得
            stats.push_back(counter->Load());
        }

        return stats;
    }

    JobSystem::ExecutionMode JobSystem::GetExecutionMode() const
    {
        return m_executionMode;
    }

    void JobSystem::SetExecutionMode(ExecutionMode mode)
    {
        m_executionMode = mode;
    }

    size_t JobSystem::GetTotalStolenTasks() const
    {
        return m_totalStolenTasks;
    }

    JobSystem::SizingMode JobSystem::GetSizingMode() const
    {
        return m_sizingMode.Load();
    }

    void JobSystem::EnableDynamicSizing(bool enabled, uint32_t minThreads, uint32_t maxThreads)
    {
        ScopedLock shutdownLock(m_shutdownMutex);

        // 現在のモードと同じなら何もしない
        SizingMode newMode = enabled ? SizingMode::SIZING_DYNAMIC : SizingMode::SIZING_FIXED;
        if (newMode == m_sizingMode.Load() &&
            (minThreads == m_minThreads.Load() || minThreads == 0) &&
            (maxThreads == m_maxThreads.Load() || maxThreads == 0))
        {
            return;
        }

        // ハードウェアスレッド数を取得
        unsigned int hardwareConcurrency = Thread::GetHardwareConcurrency();

        // 最小スレッド数（デフォルトはハードウェアスレッド数の1/4、最低1）
        if (minThreads == 0)
        {
            minThreads = std::max(1u, hardwareConcurrency / 4);
        }

        // 最大スレッド数（デフォルトはハードウェアスレッド数）
        if (maxThreads == 0)
        {
            maxThreads = hardwareConcurrency;
        }

        // 最小スレッド数は常に最大スレッド数以下に
        minThreads = std::min(minThreads, maxThreads);

        // 値を設定
        m_minThreads = minThreads;
        m_maxThreads = maxThreads;

        // 前回のモードと新しいモードの切り替え
        SizingMode oldMode = m_sizingMode.Exchange(newMode);

        // SIZING_DYNAMICに切り替える場合は、モニタースレッドを起動
        if (oldMode == SizingMode::SIZING_FIXED && newMode == SizingMode::SIZING_DYNAMIC)
        {
            m_monitorActive = true;
            m_monitorThread = std::make_unique<Thread>([this]
                                                       { MonitorThreadFunction(); });
            m_monitorThread->SetName("JobSystem Monitor");
        }
        // FIXEDに切り替える場合は、モニタースレッドを停止
        else if (oldMode == SizingMode::SIZING_DYNAMIC && newMode == SizingMode::SIZING_FIXED)
        {
            m_monitorActive = false;
            if (m_monitorThread)
            {
                m_monitorThread.reset();
            }
        }
    }

    size_t JobSystem::AdjustWorkerThreadCount(size_t targetThreadCount)
    {
        ScopedLock resizeLock(m_resizeMutex);

        if (m_shutdownRequested)
        {
            return 0;
        }

        // 現在のスレッド数
        size_t currentThreadCount = 0;
        {
            ScopedLock workerLock(m_workerThreadMutex);
            currentThreadCount = m_workerThreads.size();
        }

        // 範囲制限（最小・最大スレッド数）
        size_t minThreads = m_minThreads.Load();
        size_t maxThreads = m_maxThreads.Load();
        targetThreadCount = std::max(minThreads, std::min(maxThreads, targetThreadCount));

        // 現在と目標が同じなら何もしない
        if (targetThreadCount == currentThreadCount)
        {
            return currentThreadCount;
        }

        // スレッド数を増やす場合
        if (targetThreadCount > currentThreadCount)
        {
            m_workerThreadLimit = targetThreadCount;
            size_t threadsToAdd = targetThreadCount - currentThreadCount;
            for (size_t i = 0; i < threadsToAdd; ++i)
            {
                CreateAndStartWorkerThread();
            }

            return GetWorkerThreadCount();
        }
        // スレッド数を減らす場合
        else
        {
            Core::Container::VariableArray<std::unique_ptr<Thread>> workerThreadsToJoin;
            {
                ScopedLock workerLock(m_workerThreadMutex);
                ScopedLock queueLock(m_queueMutex);

                if (m_queuedTaskCount != 0 || m_activeThreads != 0)
                {
                    return currentThreadCount;
                }

                m_workerThreadLimit = targetThreadCount;
                m_conditionVar.NotifyAll();

                while (m_workerThreads.size() > targetThreadCount)
                {
                    workerThreadsToJoin.push_back(std::move(m_workerThreads.back()));
                    m_workerThreads.pop_back();
                }

                if (m_localQueues.size() > targetThreadCount)
                {
                    m_localQueues.resize(targetThreadCount);
                }
            }

            workerThreadsToJoin.clear();

            {
                ScopedLock statsLock(m_statsMutex);
                if (m_tasksProcessedPerThread.size() > targetThreadCount)
                {
                    m_tasksProcessedPerThread.resize(targetThreadCount);
                }
            }

            return GetWorkerThreadCount();
        }
    }

    TaskPtr JobSystem::GetNextTaskSimple()
    {
        TaskPtr task;
        ScopedLock lock(m_queueMutex);

        // キューが空でなければ、タスクを取得
        if (!m_taskQueue.empty())
        {
            task = m_taskQueue.top();
            m_taskQueue.pop();
            return task;
        }

        return nullptr;
    }

    TaskPtr JobSystem::GetNextTaskWorkStealing(size_t threadIndex)
    {
        // まずワーカーミューテックスをロック
        ScopedLock workerLock(m_workerThreadMutex);

        // 自分のローカルキューを確認
        if (threadIndex < m_localQueues.size() && m_localQueues[threadIndex])
        {
            TaskPtr task = m_localQueues[threadIndex]->Pop();
            if (task)
            {
                return task;
            }
        }

        // ローカルキューがなければグローバルキューを確認
        {
            ScopedLock queueLock(m_queueMutex);
            if (!m_taskQueue.empty())
            {
                TaskPtr task = m_taskQueue.top();
                m_taskQueue.pop();
                return task;
            }
        }

        // 他のキューからタスクを盗む
        return StealTask(threadIndex);
    }

    TaskPtr JobSystem::StealTask(size_t currentThreadIndex)
    {
        if (m_localQueues.size() <= 1)
        {
            return nullptr; // 盗めるキューがない
        }

        // ランダムなワーカーを選ぶ
        std::uniform_int_distribution<size_t> dist(0, m_localQueues.size() - 1);
        size_t victimIndex = dist(m_randomGenerator);

        // 自分自身からは盗まない
        if (victimIndex == currentThreadIndex)
        {
            victimIndex = (victimIndex + 1) % m_localQueues.size();
        }

        // 選ばれたワーカーからタスクを盗む
        if (victimIndex < m_localQueues.size() && m_localQueues[victimIndex])
        {
            TaskPtr stolenTask = m_localQueues[victimIndex]->Steal();
            if (stolenTask)
            {
                m_totalStolenTasks++;
                return stolenTask;
            }
        }

        return nullptr;
    }

    void JobSystem::WorkerThreadFunction(size_t threadIndex)
    {
        while (!m_shutdownRequested && threadIndex < m_workerThreadLimit.Load())
        {
            // 現在の実行モードによって、タスク取得方法を切り替え
            TaskPtr task = nullptr;
            const ExecutionMode mode = m_executionMode.Load();
            bool noTasks = false;

            if (mode == ExecutionMode::EXECUTION_SIMPLE)
            {
                // シンプルモード - グローバルキューから取得
                task = GetNextTaskSimple();
                noTasks = !task;
            }
            else
            {
                // ワークスチーリングモード - ローカル/グローバル/スチールから取得
                task = GetNextTaskWorkStealing(threadIndex);
                noTasks = !task;
            }

            // タスクがなければ待機
            if (noTasks)
            {
                ScopedLock lock(m_queueMutex);
                if (!m_shutdownRequested && threadIndex < m_workerThreadLimit.Load() && m_queuedTaskCount == 0)
                {
                    m_conditionVar.Wait(m_queueMutex);
                }
                continue;
            }

            // タスクを実行
            m_activeThreads++;
            task->Execute();
            m_activeThreads--;

            // 統計情報を更新（unique_ptr経由でアクセス）
            {
                ScopedLock statsLock(m_statsMutex);
                if (threadIndex < m_tasksProcessedPerThread.size() && m_tasksProcessedPerThread[threadIndex])
                {
                    (*m_tasksProcessedPerThread[threadIndex])++;
                }
            }

            // キュー内のタスク数を減らす
            MarkQueuedTaskCompleted();
        }
    }

    void JobSystem::MonitorThreadFunction()
    {
        // 前回のアクティブスレッド数
        size_t lastActiveThreads = 0;

        // 監視間隔（ミリ秒）
        constexpr auto monitorInterval = std::chrono::milliseconds(500);

        while (m_monitorActive)
        {
            // 現在のタスク数と実行中スレッド数を取得
            size_t queuedTasks = m_queuedTaskCount.Load();
            size_t activeThreads = m_activeThreads.Load();
            size_t workerThreads = GetWorkerThreadCount();

            // ワーカースレッドの使用率を計算
            float utilization = (workerThreads > 0) ? static_cast<float>(activeThreads) / workerThreads : 0.0f;

            // 使用率履歴を更新
            for (size_t i = 0; i < UTILIZATION_HISTORY_SIZE - 1; ++i)
            {
                m_utilizationHistory[i] = m_utilizationHistory[i + 1];
            }
            m_utilizationHistory[UTILIZATION_HISTORY_SIZE - 1] = utilization;

            // 使用率の平均値を計算
            float avgUtilization = 0.0f;
            for (float u : m_utilizationHistory)
            {
                avgUtilization += u;
            }
            avgUtilization /= UTILIZATION_HISTORY_SIZE;

            // 現在時刻
            auto now = std::chrono::steady_clock::now();

            // 定期的にスレッド数を調整（直前の調整から一定時間経過した場合のみ）
            if (std::chrono::duration<float>(now - m_lastThreadAdjustment).count() >= THREAD_ADJUSTMENT_INTERVAL)
            {
                size_t optimalThreadCount = CalculateOptimalThreadCount();

                // スレッド数を調整
                if (optimalThreadCount != workerThreads)
                {
                    AdjustWorkerThreadCount(optimalThreadCount);
                    m_lastThreadAdjustment = now;
                }
            }

            // 監視間隔だけ待機
            std::this_thread::sleep_for(monitorInterval);
        }
    }

    size_t JobSystem::CalculateOptimalThreadCount()
    {
        // 現在の設定
        size_t minThreads = m_minThreads.Load();
        size_t maxThreads = m_maxThreads.Load();
        size_t currentThreads = GetWorkerThreadCount();

        // 現在のタスク数とアクティブスレッド数
        size_t queuedTasks = m_queuedTaskCount.Load();
        size_t activeThreads = m_activeThreads.Load();

        // 直近の使用率平均
        float avgUtilization = 0.0f;
        for (float u : m_utilizationHistory)
        {
            avgUtilization += u;
        }
        avgUtilization /= UTILIZATION_HISTORY_SIZE;

        size_t optimalThreadCount = currentThreads;

        // 使用率が高い場合はスレッドを増やす
        if (avgUtilization > TARGET_WORKER_UTILIZATION && queuedTasks > 0)
        {
            // 使用率に基づいて必要なスレッド数を計算
            // 目標: activeThreads / newThreadCount = TARGET_WORKER_UTILIZATION
            float targetThreads = activeThreads / TARGET_WORKER_UTILIZATION;

            // 積極的に増やす（一度に最大25%増加）
            size_t newThreads = std::min(
                maxThreads,
                currentThreads + std::max(size_t(1), currentThreads / 4));

            optimalThreadCount = newThreads;
        }
        // 使用率が低く、タスクキューも空の場合はスレッドを減らす
        else if (avgUtilization < TARGET_WORKER_UTILIZATION * 0.5f && queuedTasks == 0)
        {
            // 使用率に基づいて必要なスレッド数を計算
            // 目標: activeThreads / newThreadCount = TARGET_WORKER_UTILIZATION * 0.75
            float targetThreads = activeThreads / (TARGET_WORKER_UTILIZATION * 0.75f);

            // 消極的に減らす（一度に最大20%減少）
            size_t newThreads = std::max(
                minThreads,
                currentThreads - std::min(currentThreads / 5, currentThreads - minThreads));

            optimalThreadCount = newThreads;
        }

        // 範囲内に収める
        return std::max(minThreads, std::min(maxThreads, optimalThreadCount));
    }

    void JobSystem::CreateAndStartWorkerThread()
    {
        size_t threadIndex;

        {
            ScopedLock statsLock(m_statsMutex);
            threadIndex = m_tasksProcessedPerThread.size();
            // std::unique_ptrを使ってAtomicオブジェクトを生成
            m_tasksProcessedPerThread.push_back(std::make_unique<Atomic<size_t>>(0));
        }

        if (m_workerThreadLimit.Load() < threadIndex + 1)
        {
            m_workerThreadLimit = threadIndex + 1;
        }

        // ワークスチーリングモードの場合はローカルキューを作成
        if (m_executionMode.Load() == ExecutionMode::EXECUTION_WORK_STEALING)
        {
            ScopedLock workerLock(m_workerThreadMutex);
            if (threadIndex >= m_localQueues.size())
            {
                m_localQueues.resize(threadIndex + 1);
            }
            m_localQueues[threadIndex] = std::make_unique<WorkStealingQueue>();
        }

        // ワーカースレッドを作成
        auto thread = std::make_unique<Thread>([this, threadIndex]
                                               { WorkerThreadFunction(threadIndex); });

        // スレッドに名前を設定
        Core::Container::String name = "JobSystem Worker ";
        name += std::to_string(threadIndex).c_str();
        thread->SetName(name);

        // リストに追加
        ScopedLock workerLock(m_workerThreadMutex);
        m_workerThreads.push_back(std::move(thread));
    }

    void JobSystem::MarkQueuedTaskCompleted()
    {
        size_t currentCount = m_queuedTaskCount.Load();
        while (currentCount > 0)
        {
            const size_t nextCount = currentCount - 1;
            if (m_queuedTaskCount.CompareExchangeStrong(currentCount, nextCount))
            {
                return;
            }
        }
    }

} // namespace NorvesLib::Thread
