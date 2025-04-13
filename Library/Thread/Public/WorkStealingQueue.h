#pragma once

#include <mutex>
#include "Task.h"
#include "Core/Public/Container/Containers.h"

namespace NorvesLib::Thread
{

/**
 * @brief ワーク・スチーリングを実現するためのローカルタスクキュー
 * 
 * 各ワーカースレッドが保持するローカルタスクキュー。
 * 自スレッドはデックの末尾、他のスレッドは先頭からタスクを取得できる。
 */
class WorkStealingQueue
{
public:
    /**
     * @brief デフォルトコンストラクタ
     */
    WorkStealingQueue() = default;
    
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
    ~WorkStealingQueue() = default;
    
    /**
     * @brief タスクをキューに追加（所有スレッド用）
     * ロックなしで末尾に追加できる
     * 
     * @param task 追加するタスク
     */
    void Push(TaskPtr task);
    
    /**
     * @brief キューからタスクを取得（所有スレッド用）
     * ロックなしで末尾から取得できる
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
    mutable std::mutex m_mutex;
    Core::Container::Deque<TaskPtr> m_tasks;
};

} // namespace NorvesLib::Thread