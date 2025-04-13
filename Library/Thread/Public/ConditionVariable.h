#pragma once

#include <condition_variable>
#include "Mutex.h"
#include "Core/Public/Container/Containers.h"

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
        m_condVar.wait(mutex.GetNativeMutex(), predicate);
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
        return m_condVar.wait_for(mutex.GetNativeMutex(), relTime) == std::cv_status::no_timeout;
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
        return m_condVar.wait_for(mutex.GetNativeMutex(), relTime, predicate);
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
        return m_condVar.wait_until(mutex.GetNativeMutex(), absTime) == std::cv_status::no_timeout;
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
        return m_condVar.wait_until(mutex.GetNativeMutex(), absTime, predicate);
    }

private:
    std::condition_variable m_condVar;
};

} // namespace NorvesLib::Thread