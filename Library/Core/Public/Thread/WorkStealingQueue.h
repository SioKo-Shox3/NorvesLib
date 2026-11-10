#pragma once

#include "Task.h"
#include "Mutex.h"
#include "Container/Containers.h"

namespace NorvesLib::Thread
{

/**
 * @brief ワーク・スチーリングを実現するためのローカルタスクキュー
 * 
 * 各ワーカースレッドが保持するローカルタスクキュー。
 * 末尾からの取得と先頭からの盗みを提供する。
 * 任意の提出スレッドと待機を手伝うスレッドが操作するため、全操作をロックで守る。
 */
class WorkStealingQueue
{
public:
    /**
     * @brief コンストラクタ
     */
    WorkStealingQueue();
    
    /**
     * @brief コピーコンストラクタ（削除）
     */
    WorkStealingQueue(const WorkStealingQueue&) = delete;
    
    /**
     * @brief コピー代入演算子（削除）
     */
    WorkStealingQueue& operator=(const WorkStealingQueue&) = delete;
    
    /**
     * @brief デストラクタ
     */
    ~WorkStealingQueue();
    
    /**
     * @brief タスクをキューに追加
     * ロックを取得して末尾に追加する
     * 
     * @param task 追加するタスク
     */
    void Push(TaskPtr task);
    
    /**
     * @brief キューからタスクを取得
     * ロックを取得して末尾から取得する
     * 
     * @return 取り出したタスク、空の場合はnullptr
     */
    TaskPtr Pop();
    
    /**
     * @brief キューからタスクを盗む（他スレッド用）
     * ロックを取得して先頭から取得
     * 
     * @return 盗んだタスク、空の場合はnullptr
     */
    TaskPtr Steal();
    
    /**
     * @brief キューが空かどうかを確認
     * 
     * @return 空の場合はtrue
     */
    bool IsEmpty() const;
    
    /**
     * @brief キュー内のタスク数を取得
     * 
     * @return タスク数
     */
    size_t Size() const;

private:
    mutable Mutex m_mutex;
    Core::Container::Deque<TaskPtr> m_tasks;

};

} // namespace NorvesLib::Thread
