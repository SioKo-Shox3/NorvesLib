#pragma once

#include <memory>
#include <functional>
#include <atomic>
#include "Container/Containers.h"
#include "ConditionVariable.h"
#include "Mutex.h"
#include "Atomic.h" // Atomic<T>クラスを明示的にインクルード

namespace NorvesLib::Thread
{

/**
 * @brief 非同期タスクを表す前方宣言クラス
 */
class Task;

/**
 * @brief タスクのスマートポインタ型定義
 */
using TaskPtr = std::shared_ptr<Task>;

/**
 * @brief タスクの完了イベントハンドラ型定義
 */
using TaskCompletionHandler = std::function<void(const TaskPtr&)>;

/**
 * @brief タスク関数の型定義
 */
using TaskFunction = std::function<void()>;

/**
 * @brief タスクの優先度を表す列挙型
 */
enum class TaskPriority : uint8_t
{
    LOW = 0,
    NORMAL = 128,
    HIGH = 192,
    CRITICAL = 255
};

/**
 * @brief 非同期タスクを表すクラス
 * 
 * ジョブシステムで実行される非同期タスク（ジョブ）を表します
 */
class Task : public std::enable_shared_from_this<Task>
{
public:
    /**
     * @brief タスクの状態を表す列挙型
     */
    enum class State
    {
        PENDING,    ///< 実行待ち
        RUNNING,    ///< 実行中
        COMPLETED,  ///< 完了
        CANCELED    ///< キャンセル
    };

    /**
     * @brief 新しいタスクを作成する
     * @param function 実行する関数
     * @param priority タスクの優先度（デフォルトは通常優先度）
     * @return タスクのスマートポインタ
     */
    static TaskPtr Create(TaskFunction function, TaskPriority priority = TaskPriority::NORMAL);

    /**
     * @brief 複数のタスクを一括して待機するタスクを作成する
     * @param tasks 依存タスクのリスト
     * @param priority バッチタスクの優先度（デフォルトは通常優先度）
     * @return 新しいタスク
     */
    static TaskPtr CreateBatch(const Core::Container::VariableArray<TaskPtr>& tasks, TaskPriority priority = TaskPriority::NORMAL);

    /**
     * @brief デフォルトコンストラクタ
     */
    Task();

    /**
     * @brief 関数を指定してタスクを生成するコンストラクタ
     * @param function 実行する関数
     * @param priority タスクの優先度（デフォルトは通常優先度）
     */
    explicit Task(TaskFunction function, TaskPriority priority = TaskPriority::NORMAL);

    /**
     * @brief デストラクタ
     */
    virtual ~Task();

    /**
     * @brief タスクを実行する
     * このメソッドはジョブシステムによって呼び出されます
     */
    void Execute();

    /**
     * @brief タスクをキャンセルする
     * まだ実行されていないタスクをキャンセルします
     * @return キャンセルに成功した場合はtrue
     */
    bool Cancel();

    /**
     * @brief タスクが完了するのを待機する
     * このメソッドは呼び出し元のスレッドをブロックします
     */
    void Wait() const;

    /**
     * @brief 完了イベントを処理するハンドラを登録する
     * @param handler 完了時に呼び出される関数
     * @return 自身への参照（メソッドチェーン用）
     */
    TaskPtr OnComplete(TaskCompletionHandler handler);

    /**
     * @brief 子タスクを追加する
     * @param task 子タスク
     * @return 追加された子タスク
     */
    TaskPtr Then(TaskPtr task);

    /**
     * @brief 子タスクを追加する（内部で新しいタスクを生成）
     * @param function 実行する関数
     * @param priority 子タスクの優先度（デフォルトは親タスクと同じ優先度）
     * @return 追加された子タスク
     */
    TaskPtr Then(TaskFunction function, TaskPriority priority = TaskPriority::NORMAL);

    /**
     * @brief タスクの状態を取得する
     * @return 現在の状態
     */
    State GetState() const;

    /**
     * @brief タスクの優先度を取得する
     * @return 現在の優先度
     */
    TaskPriority GetPriority() const;
    
    /**
     * @brief タスクの優先度を設定する
     * @param priority 新しい優先度
     * @note 既に実行キューに入ったタスクの優先度は変更できません
     */
    void SetPriority(TaskPriority priority);

    /**
     * @brief タスクが完了したかどうかを確認する
     * @return 完了した場合はtrue
     */
    bool IsCompleted() const;

    /**
     * @brief タスクが取り消されたかどうかを確認する
     * @return 取り消された場合はtrue
     */
    bool IsCanceled() const;

private:
    TaskFunction m_function;
    Core::Container::VariableArray<TaskPtr> m_childTasks;
    Core::Container::VariableArray<TaskCompletionHandler> m_completionHandlers;
    Atomic<State> m_state; // Atomic<T>クラスを正しく使用
    mutable ConditionVariable m_completionEvent;
    mutable Mutex m_mutex;
    TaskPriority m_priority;
    
    /**
     * @brief 完了イベントを発行する
     */
    void NotifyCompletion();
};

/**
 * @brief タスク優先度比較用の関数オブジェクト
 * ジョブシステムの優先度付きキュー用
 */
struct TaskPriorityCompare
{
    bool operator()(const TaskPtr& lhs, const TaskPtr& rhs) const
    {
        return static_cast<uint8_t>(lhs->GetPriority()) < static_cast<uint8_t>(rhs->GetPriority());
    }
};

} // namespace NorvesLib::Thread
