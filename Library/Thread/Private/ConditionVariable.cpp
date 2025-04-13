#include "../Public/ConditionVariable.h"
#include <mutex>

namespace NorvesLib::Thread
{

void ConditionVariable::NotifyOne()
{
    m_condVar.notify_one();
}

void ConditionVariable::NotifyAll()
{
    m_condVar.notify_all();
}

void ConditionVariable::Wait(Mutex& mutex)
{
    // std::condition_variable::wait()はstd::unique_lock<std::mutex>が必要
    std::unique_lock<std::mutex> lock(mutex.GetNativeMutex(), std::adopt_lock);
    m_condVar.wait(lock);
    // ロックは解除しないようにadopt_lockを使用し、lock.release()して所有権を放棄
    lock.release();
}

} // namespace NorvesLib::Thread