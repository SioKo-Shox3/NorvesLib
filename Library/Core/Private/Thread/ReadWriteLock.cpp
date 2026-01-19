#include "Thread/ReadWriteLock.h"

namespace NorvesLib::Thread
{

    void ReadWriteLock::LockShared()
    {
        m_sharedMutex.lock_shared();
    }

    bool ReadWriteLock::TryLockShared()
    {
        return m_sharedMutex.try_lock_shared();
    }

    void ReadWriteLock::UnlockShared()
    {
        m_sharedMutex.unlock_shared();
    }

    void ReadWriteLock::LockExclusive()
    {
        m_sharedMutex.lock();
    }

    bool ReadWriteLock::TryLockExclusive()
    {
        return m_sharedMutex.try_lock();
    }

    void ReadWriteLock::UnlockExclusive()
    {
        m_sharedMutex.unlock();
    }

    std::shared_mutex &ReadWriteLock::GetNativeMutex()
    {
        return m_sharedMutex;
    }

    SharedLock::SharedLock(ReadWriteLock &rwLock)
        : m_rwLock(rwLock)
    {
        m_rwLock.LockShared();
    }

    SharedLock::~SharedLock()
    {
        m_rwLock.UnlockShared();
    }

    ExclusiveLock::ExclusiveLock(ReadWriteLock &rwLock)
        : m_rwLock(rwLock)
    {
        m_rwLock.LockExclusive();
    }

    ExclusiveLock::~ExclusiveLock()
    {
        m_rwLock.UnlockExclusive();
    }

} // namespace NorvesLib::Thread
