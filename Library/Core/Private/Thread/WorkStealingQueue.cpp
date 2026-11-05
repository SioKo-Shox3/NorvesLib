#include "Thread/WorkStealingQueue.h"

namespace NorvesLib::Thread
{

    WorkStealingQueue::WorkStealingQueue() = default;

    WorkStealingQueue::~WorkStealingQueue()
    {
        ScopedLock lock(m_mutex);
        m_tasks.clear();
    }

    void WorkStealingQueue::Push(TaskPtr task)
    {
        // 提出元は所有ワーカーに限らないため、追加も取得と同じロックで守る。
        ScopedLock lock(m_mutex);
        m_tasks.push_back(std::move(task));
    }

    TaskPtr WorkStealingQueue::Pop()
    {
        ScopedLock lock(m_mutex);
        if (m_tasks.empty())
        {
            return nullptr;
        }

        TaskPtr task = std::move(m_tasks.back());
        m_tasks.pop_back();
        return task;
    }

    TaskPtr WorkStealingQueue::Steal()
    {
        ScopedLock lock(m_mutex);
        if (m_tasks.empty())
        {
            return nullptr;
        }

        TaskPtr task = std::move(m_tasks.front());
        m_tasks.pop_front();
        return task;
    }

    bool WorkStealingQueue::IsEmpty() const
    {
        ScopedLock lock(m_mutex);
        return m_tasks.empty();
    }

    size_t WorkStealingQueue::Size() const
    {
        ScopedLock lock(m_mutex);
        return m_tasks.size();
    }

} // namespace NorvesLib::Thread
