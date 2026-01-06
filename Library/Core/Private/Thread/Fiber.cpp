#include "Thread/Fiber.h"
#include <cassert>
#include <stdexcept>

namespace NorvesLib::Thread
{

    // スレッドローカル変数の初期化
    thread_local Fiber *Fiber::s_mainFiber = nullptr;
    thread_local Fiber *Fiber::s_currentFiber = nullptr;

    // デフォルトのスタックサイズ（1MB）
    constexpr size_t DEFAULT_FIBER_STACK_SIZE = 1024 * 1024;

    bool Fiber::InitializeCurrentThread()
    {
        if (s_mainFiber)
        {
            // 既に初期化済みの場合は成功として返す
            return true;
        }

#ifdef _WIN32
        // Windows実装
        void *mainFiberHandle = ConvertThreadToFiber(nullptr);
        if (!mainFiberHandle)
        {
            // すでにファイバー化されている場合は現在のファイバーを取得
            if (GetLastError() == ERROR_ALREADY_FIBER)
            {
                mainFiberHandle = GetCurrentFiber();
            }
            else
            {
                return false;
            }
        }

        // メインファイバーを作成（関数はnullptr、実行されないダミー）
        s_mainFiber = new Fiber([]()
                                {
                                    // ダミー関数
                                },
                                0);
        s_mainFiber->m_fiberHandle = mainFiberHandle;
        s_mainFiber->m_isMainFiber = true;
        s_mainFiber->m_state = State::RUNNING;
        s_currentFiber = s_mainFiber;

        return true;
#else
        // POSIX実装
        try
        {
            // メインファイバーを作成（関数はnullptr、実行されないダミー）
            s_mainFiber = new Fiber([]()
                                    {
                                        // ダミー関数
                                    },
                                    0);
            s_mainFiber->m_isMainFiber = true;
            s_mainFiber->m_state = State::RUNNING;
            s_currentFiber = s_mainFiber;

            // 現在のコンテキストを取得
            if (getcontext(&s_mainFiber->m_context) != 0)
            {
                delete s_mainFiber;
                s_mainFiber = nullptr;
                s_currentFiber = nullptr;
                return false;
            }

            return true;
        }
        catch (...)
        {
            if (s_mainFiber)
            {
                delete s_mainFiber;
                s_mainFiber = nullptr;
            }
            s_currentFiber = nullptr;
            return false;
        }
#endif
    }

    void Fiber::UninitializeCurrentThread()
    {
        if (!s_mainFiber)
        {
            return;
        }

        // メインファイバーへ切り替え
        SwitchToMain();

#ifdef _WIN32
        // メインファイバーがスレッドを作成したものなら、スレッドに戻す
        if (s_mainFiber->m_isMainFiber)
        {
            ConvertFiberToThread();
        }
#endif

        // メインファイバーを解放
        delete s_mainFiber;
        s_mainFiber = nullptr;
        s_currentFiber = nullptr;
    }

    bool Fiber::SwitchToMain()
    {
        if (!s_mainFiber || !s_currentFiber)
        {
            return false;
        }

        if (s_currentFiber == s_mainFiber)
        {
            // すでにメインファイバーで実行中
            return true;
        }

        return s_mainFiber->SwitchTo();
    }

    Fiber *Fiber::GetCurrentFiber()
    {
        return s_currentFiber;
    }

    std::unique_ptr<Fiber> Fiber::Create(FiberFunction function, size_t stackSize)
    {
        if (!function)
        {
            return nullptr;
        }

        try
        {
            return std::make_unique<Fiber>(std::move(function), stackSize);
        }
        catch (const std::exception &)
        {
            return nullptr;
        }
    }

    Fiber::Fiber(FiberFunction function, size_t stackSize)
        : m_function(std::move(function)), m_state(State::CREATED)
#ifdef _WIN32
          ,
          m_fiberHandle(nullptr), m_isMainFiber(false)
#else
          ,
          m_stack(nullptr), m_stackSize(stackSize > 0 ? stackSize : DEFAULT_FIBER_STACK_SIZE), m_isMainFiber(false)
#endif
    {
#ifdef _WIN32
        // Windows実装
        if (stackSize == 0)
        {
            stackSize = DEFAULT_FIBER_STACK_SIZE;
        }

        m_fiberHandle = CreateFiber(
            stackSize,
            [](void *arg)
            { FiberEntryPoint(arg); },
            this);

        if (!m_fiberHandle)
        {
            throw std::runtime_error("Failed to create fiber");
        }
#else
        // POSIX実装
        // コンテキストの初期化
        if (getcontext(&m_context) != 0)
        {
            throw std::runtime_error("Failed to initialize fiber context");
        }

        // スタックの割り当て
        m_stack = new char[m_stackSize];
        m_context.uc_link = nullptr;
        m_context.uc_stack.ss_sp = m_stack;
        m_context.uc_stack.ss_size = m_stackSize;
        m_context.uc_stack.ss_flags = 0;

        // makecontextでエントリポイントを設定
        makecontext(&m_context, reinterpret_cast<void (*)()>(&Fiber::FiberEntryPoint), 1, this);
#endif
    }

    Fiber::Fiber(Fiber &&other) noexcept
        : m_function(std::move(other.m_function)), m_state(other.m_state)
#ifdef _WIN32
          ,
          m_fiberHandle(other.m_fiberHandle), m_isMainFiber(other.m_isMainFiber)
#else
          ,
          m_context(other.m_context), m_stack(other.m_stack), m_stackSize(other.m_stackSize), m_isMainFiber(other.m_isMainFiber)
#endif
    {
#ifdef _WIN32
        other.m_fiberHandle = nullptr;
#else
        other.m_stack = nullptr;
#endif
        other.m_isMainFiber = false;
    }

    Fiber &Fiber::operator=(Fiber &&other) noexcept
    {
        if (this != &other)
        {
            // 既存のファイバーを解放
#ifdef _WIN32
            if (m_fiberHandle && !m_isMainFiber)
            {
                DeleteFiber(m_fiberHandle);
            }
#else
            delete[] m_stack;
#endif

            // 新しいファイバーをムーブ
            m_function = std::move(other.m_function);
            m_state = other.m_state;

#ifdef _WIN32
            m_fiberHandle = other.m_fiberHandle;
            m_isMainFiber = other.m_isMainFiber;
            other.m_fiberHandle = nullptr;
#else
            m_context = other.m_context;
            m_stack = other.m_stack;
            m_stackSize = other.m_stackSize;
            m_isMainFiber = other.m_isMainFiber;
            other.m_stack = nullptr;
#endif
            other.m_isMainFiber = false;
        }
        return *this;
    }

    Fiber::~Fiber()
    {
        if (s_currentFiber == this)
        {
            assert(false && "Cannot destroy a running fiber");
            return;
        }

#ifdef _WIN32
        // Windows実装
        if (m_fiberHandle && !m_isMainFiber)
        {
            DeleteFiber(m_fiberHandle);
        }
#else
        // POSIX実装
        delete[] m_stack;
#endif
    }

    bool Fiber::SwitchTo()
    {
        if (!s_mainFiber)
        {
            // ファイバーシステムが初期化されていない
            return false;
        }

        if (m_state == State::FINISHED)
        {
            // 既に終了したファイバーには切り替えられない
            return false;
        }

        // 現在のファイバーを記録
        Fiber *oldFiber = s_currentFiber;

        // このファイバーへ切り替え
        s_currentFiber = this;

        if (m_state == State::CREATED || m_state == State::SUSPENDED)
        {
            m_state = State::RUNNING;
        }

#ifdef _WIN32
        // Windows実装
        SwitchToFiber(m_fiberHandle);
#else
        // POSIX実装
        if (oldFiber)
        {
            if (swapcontext(&oldFiber->m_context, &m_context) != 0)
            {
                s_currentFiber = oldFiber;
                return false;
            }
        }
        else
        {
            if (setcontext(&m_context) != 0)
            {
                return false;
            }
        }
#endif

        return true;
    }

    bool Fiber::Suspend()
    {
        if (!s_mainFiber || !s_currentFiber)
        {
            return false;
        }

        if (s_currentFiber == s_mainFiber)
        {
            // メインファイバーはSuspendできない
            return false;
        }

        // 状態を一時停止中に変更
        s_currentFiber->m_state = State::SUSPENDED;

        // メインファイバーに切り替え
        return s_mainFiber->SwitchTo();
    }

    Fiber::State Fiber::GetState() const
    {
        return m_state;
    }

    bool Fiber::IsFinished() const
    {
        return m_state == State::FINISHED;
    }

    void Fiber::FiberEntryPoint(void *arg)
    {
        Fiber *fiber = static_cast<Fiber *>(arg);
        if (!fiber)
        {
            return;
        }

        // ファイバー関数を実行
        try
        {
            if (fiber->m_function)
            {
                fiber->m_function();
            }
        }
        catch (...)
        {
            // 例外は無視
        }

        // ファイバーの実行完了を記録
        fiber->m_state = State::FINISHED;

        // メインファイバーに戻る
        if (s_mainFiber)
        {
            s_mainFiber->SwitchTo();
        }

#ifdef _WIN32
        // Windows実装では、ここに到達することはないはずだが、
        // 念のため無限ループで安全に終了を防ぐ
        for (;;)
        {
            SwitchToFiber(s_mainFiber->m_fiberHandle);
        }
#endif
    }

} // namespace NorvesLib::Thread