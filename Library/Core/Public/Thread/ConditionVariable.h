#pragma once

#include <condition_variable>
#include "Mutex.h"
#include "Container/Containers.h"

namespace NorvesLib::Thread
{

/**
 * @brief 条件変数クラス
 * 
 * スレッド間の同期と通知を提供する条件変数の実装です。
 * 特定の条件が満たされるまでスレッドを待機させるために使用します。
 */
class ConditionVariable
{
public:
    /**
     * @brief デフォルトコンストラクタ
     */
    ConditionVariable() = default;
    
    /**
     * @brief コピーコンストラクタ（削除）
     */
    ConditionVariable(const ConditionVariable&) = delete;
    
    /**
     * @brief コピー代入演算子（削除）
     */
    ConditionVariable& operator=(const ConditionVariable&) = delete;
    
    /**
     * @brief デストラクタ
     */
    ~ConditionVariable() = default;
    
    /**
     * @brief 待機中のスレッドの1つを起床させる
     * 
     * 待機しているスレッドがない場合は何も起きません
     */
    void NotifyOne();
    
    /**
     * @brief 待機中のすべてのスレッドを起床させる
     * 
     * 待機しているスレッドがない場合は何も起きません
     */
    void NotifyAll();
    
    /**
     * @brief 条件変数で待機する
     * 
     * @param mutex ロック済みのミューテックス
     * @note ミューテックスは既にロックされている必要があります
     */
    void Wait(Mutex& mutex);
    
    /**
     * @brief 条件が満たされるまで待機する
     * 
     * @param mutex ロック済みのミューテックス
     * @param predicate 条件を評価する述語関数（trueを返すと待機終了）
     * @note ミューテックスは既にロックされている必要があります
     * @tparam Predicate 述語関数の型（bool()を返す関数オブジェクト）
     */
    template<typename Predicate>
    void Wait(Mutex& mutex, Predicate predicate)
    {
        // unique_lockを使用してstd::condition_variableに渡す必要がある
        std::unique_lock<std::mutex> lock(mutex.GetNativeMutex(), std::adopt_lock);
        m_condVar.wait(lock, predicate);
        lock.release(); // ロックを解放せずに所有権を手放す（Mutexクラスが管理するため）
    }
    
    /**
     * @brief 指定時間だけ条件が満たされるのを待機する
     * 
     * @param mutex ロック済みのミューテックス
     * @param relTime 最大待機時間
     * @return タイムアウト前に起床した場合はtrue、タイムアウトした場合はfalse
     * @note ミューテックスは既にロックされている必要があります
     */
    template<typename Rep, typename Period>
    bool WaitFor(Mutex& mutex, const std::chrono::duration<Rep, Period>& relTime)
    {
        std::unique_lock<std::mutex> lock(mutex.GetNativeMutex(), std::adopt_lock);
        auto result = m_condVar.wait_for(lock, relTime) == std::cv_status::no_timeout;
        lock.release();
        return result;
    }
    
    /**
     * @brief 指定時間、または条件が満たされるまで待機する
     * 
     * @param mutex ロック済みのミューテックス
     * @param relTime 最大待機時間
     * @param predicate 条件を評価する述語関数（trueを返すと待機終了）
     * @return 述語がtrueを返した場合はtrue、タイムアウトした場合はfalse
     * @note ミューテックスは既にロックされている必要があります
     * @tparam Predicate 述語関数の型（bool()を返す関数オブジェクト）
     */
    template<typename Rep, typename Period, typename Predicate>
    bool WaitFor(Mutex& mutex, const std::chrono::duration<Rep, Period>& relTime, Predicate predicate)
    {
        std::unique_lock<std::mutex> lock(mutex.GetNativeMutex(), std::adopt_lock);
        auto result = m_condVar.wait_for(lock, relTime, predicate);
        lock.release();
        return result;
    }
    
    /**
     * @brief 指定時刻まで、または条件が満たされるまで待機する
     * 
     * @param mutex ロック済みのミューテックス
     * @param absTime 待機終了時刻
     * @return タイムアウト前に起床した場合はtrue、タイムアウトした場合はfalse
     * @note ミューテックスは既にロックされている必要があります
     */
    template<typename Clock, typename Duration>
    bool WaitUntil(Mutex& mutex, const std::chrono::time_point<Clock, Duration>& absTime)
    {
        std::unique_lock<std::mutex> lock(mutex.GetNativeMutex(), std::adopt_lock);
        auto result = m_condVar.wait_until(lock, absTime) == std::cv_status::no_timeout;
        lock.release();
        return result;
    }
    
    /**
     * @brief 指定時刻まで、または条件が満たされるまで待機する
     * 
     * @param mutex ロック済みのミューテックス
     * @param absTime 待機終了時刻
     * @param predicate 条件を評価する述語関数（trueを返すと待機終了）
     * @return 述語がtrueを返した場合はtrue、タイムアウトした場合はfalse
     * @note ミューテックスは既にロックされている必要があります
     * @tparam Predicate 述語関数の型（bool()を返す関数オブジェクト）
     */
    template<typename Clock, typename Duration, typename Predicate>
    bool WaitUntil(Mutex& mutex, const std::chrono::time_point<Clock, Duration>& absTime, Predicate predicate)
    {
        std::unique_lock<std::mutex> lock(mutex.GetNativeMutex(), std::adopt_lock);
        auto result = m_condVar.wait_until(lock, absTime, predicate);
        lock.release();
        return result;
    }

private:
    std::condition_variable m_condVar;
};

} // namespace NorvesLib::Thread
