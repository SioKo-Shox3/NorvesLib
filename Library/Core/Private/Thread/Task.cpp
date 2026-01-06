#include "Thread/Task.h"
#include <cassert>

namespace NorvesLib::Thread
{

    TaskPtr Task::Create(TaskFunction function, TaskPriority priority)
    {
        return std::make_shared<Task>(std::move(function), priority);
    }

    TaskPtr Task::CreateBatch(const Core::Container::VariableArray<TaskPtr> &tasks, TaskPriority priority)
    {
        // バッチタスクを作成（すべての依存タスクの完了を待つ）
        auto batchTask = std::make_shared<Task>([tasks]()
                                                {
        // すべての依存タスクを待機
        for (const auto& task : tasks)
        {
            task->Wait();
        } }, priority);

        return batchTask;
    }

    Task::Task()
        : m_function(nullptr),
          m_state(State::PENDING),
          m_priority(TaskPriority::NORMAL)
    {
    }

    Task::Task(TaskFunction function, TaskPriority priority)
        : m_function(std::move(function)),
          m_state(State::PENDING),
          m_priority(priority)
    {
    }

    Task::~Task()
    {
        // 終了時に待機中のスレッドがある場合、通知する
        ScopedLock lock(m_mutex);
        m_state = State::COMPLETED;
        m_completionEvent.NotifyAll();
    }

    void Task::Execute()
    {
        // 既に実行中または完了している場合は何もしない
        if (m_state.Load() != State::PENDING)
        {
            return;
        }

        // 状態を実行中に変更
        State expected = State::PENDING;
        if (!m_state.CompareExchangeStrong(expected, State::RUNNING))
        {
            return; // 別スレッドが実行中にした場合
        }

        // 関数を実行
        if (m_function)
        {
            m_function();
        }

        // タスク完了
        m_state = State::COMPLETED;

        // 待機しているスレッドと完了ハンドラに通知
        NotifyCompletion();

        // 子タスクの実行
        Core::Container::VariableArray<TaskPtr> childTasksCopy;

        {
            ScopedLock lock(m_mutex);
            childTasksCopy = m_childTasks;
        }

        // 子タスクをジョブシステムに送信する必要がある場合は、
        // ここでJobSystem::Get().SubmitTask()などを呼び出す
    }

    bool Task::Cancel()
    {
        State expected = State::PENDING;
        if (m_state.CompareExchangeStrong(expected, State::CANCELED))
        {
            // キャンセル成功、待機しているスレッドに通知
            NotifyCompletion();
            return true;
        }

        // すでに実行中または完了している場合はキャンセル不可
        return false;
    }

    void Task::Wait() const
    {
        if (m_state.Load() == State::PENDING || m_state.Load() == State::RUNNING)
        {
            ScopedLock lock(m_mutex);
            m_completionEvent.Wait(m_mutex, [this]
                                   { return m_state.Load() == State::COMPLETED || m_state.Load() == State::CANCELED; });
        }
    }

    TaskPtr Task::OnComplete(TaskCompletionHandler handler)
    {
        ScopedLock lock(m_mutex);

        // 既に完了している場合は直接ハンドラを実行
        if (m_state.Load() == State::COMPLETED)
        {
            // ロックを手動で解除する必要はありません。ScopedLockはスコープを抜けると自動的に解除されます
            handler(shared_from_this());
        }
        else
        {
            // 完了ハンドラをリストに追加
            m_completionHandlers.push_back(std::move(handler));
        }

        return shared_from_this();
    }

    TaskPtr Task::Then(TaskPtr task)
    {
        ScopedLock lock(m_mutex);

        // 子タスクを追加
        m_childTasks.push_back(task);

        return task;
    }

    TaskPtr Task::Then(TaskFunction function, TaskPriority priority)
    {
        // 優先度が指定されていない場合は親タスクの優先度を使用
        if (priority == TaskPriority::NORMAL)
        {
            priority = m_priority;
        }

        // 新しいタスクを作成
        TaskPtr task = Create(std::move(function), priority);

        // 子タスクとして追加
        return Then(task);
    }

    Task::State Task::GetState() const
    {
        return m_state.Load();
    }

    TaskPriority Task::GetPriority() const
    {
        return m_priority;
    }

    void Task::SetPriority(TaskPriority priority)
    {
        // まだ実行キューに入っていないタスクの場合のみ優先度を変更可能
        if (m_state.Load() == State::PENDING)
        {
            m_priority = priority;
        }
    }

    bool Task::IsCompleted() const
    {
        return m_state.Load() == State::COMPLETED;
    }

    bool Task::IsCanceled() const
    {
        return m_state.Load() == State::CANCELED;
    }

    void Task::NotifyCompletion()
    {
        Core::Container::VariableArray<TaskCompletionHandler> handlers;

        {
            ScopedLock lock(m_mutex);

            // 完了イベントの通知
            m_completionEvent.NotifyAll();

            // 完了ハンドラの一時コピー
            handlers = std::move(m_completionHandlers);
        }

        // 完了ハンドラの実行（ロック解除後）
        for (const auto &handler : handlers)
        {
            handler(shared_from_this());
        }
    }

} // namespace NorvesLib::Thread