#include "../Public/Mutex.h"

namespace NorvesLib::Thread
{

void Mutex::Lock()
{
    m_mutex.lock();
}

bool Mutex::TryLock()
{
    return m_mutex.try_lock();
}

void Mutex::Unlock()
{
    m_mutex.unlock();
}

std::mutex& Mutex::GetNativeMutex()
{
    return m_mutex;
}

ScopedLock::ScopedLock(Mutex& mutex)
    : m_mutex(mutex)
{
    m_mutex.Lock();
}

ScopedLock::~ScopedLock()
{
    m_mutex.Unlock();
}

} // namespace NorvesLib::Thread