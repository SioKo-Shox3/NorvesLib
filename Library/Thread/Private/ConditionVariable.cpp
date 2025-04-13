#include "../Public/ConditionVariable.h"

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
    m_condVar.wait(mutex.GetNativeMutex());
}

} // namespace NorvesLib::Thread