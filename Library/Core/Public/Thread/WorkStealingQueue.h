#pragma once

#include "Task.h"
#include "Mutex.h"
#include "Atomic.h" // Atomic<T>クラスを明示的にインクルード
#include "Container/Containers.h"
#include <atomic>

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
    /**
     * @brief トップインデックスの比較交換操作を行う
     * @param expected 期待値
     * @param desired 変更する値
     * @return 変更に成功した場合はtrue
     */
    bool CompareExchangeTop(int64_t expected, int64_t desired);

    mutable Mutex m_mutex;
    Core::Container::Deque<TaskPtr> m_tasks;
    
    // ロックフリーキュー操作のためのインデックス
    Atomic<int64_t> m_bottom;  // 所有スレッドが操作
    Atomic<int64_t> m_top;     // 他スレッドも操作可能
};

} // namespace NorvesLib::Thread
