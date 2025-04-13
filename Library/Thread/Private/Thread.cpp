#include "../Public/Thread.h"
#include <cassert>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace NorvesLib::Thread
{

Thread::Thread()
    : m_isRunning(false)
{
}

Thread::Thread(ThreadFunction function)
    : m_isRunning(false)
{
    Start(std::move(function));
}

Thread::Thread(Thread&& other) noexcept
    : m_thread(std::move(other.m_thread)),
      m_isRunning(other.m_isRunning.load()),
      m_name(std::move(other.m_name))
{
    other.m_isRunning = false;
}

Thread& Thread::operator=(Thread&& other) noexcept
{
    if (this != &other)
    {
        if (m_thread.joinable())
        {
            m_thread.join();
        }
        
        m_thread = std::move(other.m_thread);
        m_isRunning.store(other.m_isRunning.load());
        m_name = std::move(other.m_name);
        
        other.m_isRunning = false;
    }
    return *this;
}

Thread::~Thread()
{
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

void Thread::Start(ThreadFunction function)
{
    if (m_thread.joinable())
    {
        m_thread.join();  // 既存のスレッドを終了
    }
    
    m_isRunning = true;
    m_thread = std::thread([this, func = std::move(function)]() {
        func();
        m_isRunning = false;
    });
}

void Thread::Join()
{
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

void Thread::Detach()
{
    if (m_thread.joinable())
    {
        m_thread.detach();
    }
}

bool Thread::Joinable() const
{
    return m_thread.joinable();
}

Thread::ThreadId Thread::GetId() const
{
    return m_thread.get_id();
}

Thread::ThreadId Thread::GetCurrentThreadId()
{
    return std::this_thread::get_id();
}

bool Thread::SetPriority(int priority)
{
#ifdef _WIN32
    // Windowsの場合
    HANDLE handle = reinterpret_cast<HANDLE>(m_thread.native_handle());
    
    // 優先度範囲（0-100）をWindowsの優先度レベルに変換
    int winPriority;
    if (priority < 20)
        winPriority = THREAD_PRIORITY_LOWEST;
    else if (priority < 40)
        winPriority = THREAD_PRIORITY_BELOW_NORMAL;
    else if (priority < 60)
        winPriority = THREAD_PRIORITY_NORMAL;
    else if (priority < 80)
        winPriority = THREAD_PRIORITY_ABOVE_NORMAL;
    else
        winPriority = THREAD_PRIORITY_HIGHEST;
    
    return SetThreadPriority(handle, winPriority) != 0;
#elif defined(__linux__)
    // Linuxの場合
    pthread_t handle = m_thread.native_handle();
    int policy;
    struct sched_param param;
    
    pthread_getschedparam(handle, &policy, &param);
    
    // 優先度範囲（0-100）をLinuxの優先度レベルに変換
    int minPrio = sched_get_priority_min(policy);
    int maxPrio = sched_get_priority_max(policy);
    
    param.sched_priority = minPrio + ((maxPrio - minPrio) * priority) / 100;
    
    return pthread_setschedparam(handle, policy, &param) == 0;
#else
    // 未対応のプラットフォーム
    return false;
#endif
}

void Thread::SetName(const Core::Container::String& name)
{
    m_name = name;
    
#ifdef _WIN32
    // Windowsの場合（Visual Studio デバッガ向け）
    #pragma pack(push, 8)
    struct THREADNAME_INFO {
        DWORD dwType;
        LPCSTR szName;
        DWORD dwThreadID;
        DWORD dwFlags;
    };
    #pragma pack(pop)
    
    THREADNAME_INFO info;
    info.dwType = 0x1000;
    info.szName = m_name.c_str();
    info.dwThreadID = GetThreadId(reinterpret_cast<HANDLE>(m_thread.native_handle()));
    info.dwFlags = 0;
    
    __try {
        constexpr DWORD MS_VC_EXCEPTION = 0x406D1388;
        RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), reinterpret_cast<ULONG_PTR*>(&info));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#elif defined(__linux__)
    // Linuxの場合
    pthread_setname_np(m_thread.native_handle(), m_name.c_str());
#endif
}

bool Thread::SetAffinity(AffinityMask affinityMask)
{
#ifdef _WIN32
    // Windowsの場合
    HANDLE handle = reinterpret_cast<HANDLE>(m_thread.native_handle());
    DWORD_PTR mask = static_cast<DWORD_PTR>(affinityMask);
    return SetThreadAffinityMask(handle, mask) != 0;
#elif defined(__linux__)
    // Linuxの場合
    pthread_t handle = m_thread.native_handle();
    
    cpu_set_t cpuSet;
    CPU_ZERO(&cpuSet);
    
    // マスクのセットビットに対応するCPUコアを設定
    for (int i = 0; i < 64; ++i)
    {
        if ((affinityMask & (1ULL << i)) != 0)
        {
            CPU_SET(i, &cpuSet);
        }
    }
    
    return pthread_setaffinity_np(handle, sizeof(cpu_set_t), &cpuSet) == 0;
#else
    // 未対応のプラットフォーム
    return false;
#endif
}

bool Thread::SetCurrentThreadAffinity(AffinityMask affinityMask)
{
#ifdef _WIN32
    // Windowsの場合
    HANDLE currentThread = GetCurrentThread();
    DWORD_PTR mask = static_cast<DWORD_PTR>(affinityMask);
    return SetThreadAffinityMask(currentThread, mask) != 0;
#elif defined(__linux__)
    // Linuxの場合
    cpu_set_t cpuSet;
    CPU_ZERO(&cpuSet);
    
    // マスクのセットビットに対応するCPUコアを設定
    for (int i = 0; i < 64; ++i)
    {
        if ((affinityMask & (1ULL << i)) != 0)
        {
            CPU_SET(i, &cpuSet);
        }
    }
    
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuSet) == 0;
#else
    // 未対応のプラットフォーム
    return false;
#endif
}

unsigned int Thread::GetHardwareConcurrency()
{
    return std::thread::hardware_concurrency();
}

} // namespace NorvesLib::Thread