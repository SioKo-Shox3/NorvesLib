#include "Thread/WorkStealingQueue.h"

namespace NorvesLib::Thread
{

    WorkStealingQueue::WorkStealingQueue()
        : m_bottom(0), m_top(0)
    {
    }

    WorkStealingQueue::~WorkStealingQueue()
    {
        // キューの中身をクリア
        ScopedLock lock(m_mutex);
        m_tasks.clear();
    }

    void WorkStealingQueue::Push(TaskPtr task)
    {
        ScopedLock lock(m_mutex);
        const int64_t b = m_bottom.load(std::memory_order_relaxed);

        if (b >= 0 && static_cast<size_t>(b) < m_tasks.size())
        {
            m_tasks[static_cast<size_t>(b)] = task;
        }
        else
        {
            // キューの拡張が必要
            m_tasks.push_back(task);
        }

        // バリア: すべてのプッシュ操作はストアバッファの前に完了する必要がある
        std::atomic_thread_fence(std::memory_order_release);

        // bottomを更新
        m_bottom.store(b + 1, std::memory_order_relaxed);
    }

    TaskPtr WorkStealingQueue::Pop()
    {
        // キューの所有者（プロデューサー）によるPOP操作
        const int64_t b = m_bottom.load(std::memory_order_relaxed) - 1;
        m_bottom.store(b, std::memory_order_relaxed);

        // アカイアバリア: ロードがストア操作の後に確実に行われるようにする
        std::atomic_thread_fence(std::memory_order_seq_cst);

        int64_t t = m_top.load(std::memory_order_relaxed);

        if (t <= b)
        {
            // キューが空でない
            TaskPtr task = nullptr;

            // キューからタスクを取り出すが、まだコミットしない
            {
                ScopedLock lock(m_mutex);
                task = m_tasks[b % m_tasks.size()];
            }

            if (t == b)
            {
                // キューに一つしかタスクがない場合、他のスレッドによるstealと競合する可能性がある
                if (!CompareExchangeTop(t, t + 1))
                {
                    // 他のスレッドが盗んだ
                    task = nullptr;
                }

                // キューを空にする
                m_bottom.store(t + 1, std::memory_order_relaxed);
            }

            return task;
        }
        else
        {
            // キューが空
            m_bottom.store(t, std::memory_order_relaxed);
            return nullptr;
        }
    }

    TaskPtr WorkStealingQueue::Steal()
    {
        // 他のスレッド（コンシューマー）によるsteal操作

        // topを読み込む
        int64_t t = m_top.load(std::memory_order_acquire);

        // アカイアバリア: すべての後続操作がこの前に配置される
        std::atomic_thread_fence(std::memory_order_seq_cst);

        // bottomを読み込む
        int64_t b = m_bottom.load(std::memory_order_acquire);

        if (t < b)
        {
            // キューが空でない
            TaskPtr task;

            {
                ScopedLock lock(m_mutex);
                task = m_tasks[t % m_tasks.size()];
            }

            // topを更新（CASが失敗した場合、他のスレッドがタスクを取得した）
            if (CompareExchangeTop(t, t + 1))
            {
                return task;
            }
        }

        // キューが空または競合により失敗
        return nullptr;
    }

    bool WorkStealingQueue::IsEmpty() const
    {
        int64_t b = m_bottom.load(std::memory_order_relaxed);
        int64_t t = m_top.load(std::memory_order_relaxed);
        return t >= b;
    }

    size_t WorkStealingQueue::Size() const
    {
        int64_t b = m_bottom.load(std::memory_order_relaxed);
        int64_t t = m_top.load(std::memory_order_relaxed);
        return (t < b) ? static_cast<size_t>(b - t) : 0;
    }

    bool WorkStealingQueue::CompareExchangeTop(int64_t expected, int64_t desired)
    {
        return m_top.compare_exchange_strong(expected, desired, std::memory_order_seq_cst);
    }

} // namespace NorvesLib::Thread